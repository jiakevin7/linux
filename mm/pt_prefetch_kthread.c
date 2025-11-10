#ifndef _LINUX_PTEWARM_H
#include <linux/pt_prefetch_kthread.h>
#include <linux/slab.h>
#include <linux/sched.h>

void ptewarm_clock_init(struct ptewarm_clock *cw)
{
	int i;
	cw->clock_hand = 0;
	cw->scan_hand  = 0;
	for (i = 0; i < PTEWARM_N; i++) {
		cw->slots[i].valid = 0;
		cw->slots[i].ref   = 0;
	}
}

/* Fast “reference” on fault. Inserts with CLOCK if miss. */
void ptewarm_clock_record(struct ptewarm_clock *cw, unsigned long addr)
{
	unsigned long va = addr & PAGE_MASK;
	int i;

	if (!current->ptewarm_enabled)
		return;

	if (unlikely(!cw))
		return;

	/*
	 * First: quick hit check with a short linear probe.
	 * For N=16 this is cheap and tends to hit early.
	 */
	for (i = 0; i < PTEWARM_N; i++) {
		struct ptewarm_slot *s = &cw->slots[i];
		if (READ_ONCE(s->valid) && READ_ONCE(s->va) == va) {
			WRITE_ONCE(s->ref, 1);
			return;
		}
	}

	/* Miss: CLOCK insertion */
	for (i = 0; i < PTEWARM_N; i++) {
		u8 h = cw->clock_hand;
		struct ptewarm_slot *s = &cw->slots[h];

		if (!READ_ONCE(s->valid)) {
			/* empty slot: insert */
			WRITE_ONCE(s->va, va);
			WRITE_ONCE(s->ref, 1);
			WRITE_ONCE(s->valid, 1);
			cw->clock_hand = (h + 1) % PTEWARM_N;
			return;
		}

		if (READ_ONCE(s->ref)) {
			/* give a second chance */
			WRITE_ONCE(s->ref, 0);
		} else {
			/* evict */
			WRITE_ONCE(s->va, va);
			WRITE_ONCE(s->ref, 1);
			/* valid already 1 */
			cw->clock_hand = (h + 1) % PTEWARM_N;
			return;
		}
		cw->clock_hand = (h + 1) % PTEWARM_N;
	}

	/* All had ref=1 on this pass; we cleared them. Insert at current hand. */
	{
		u8 h = cw->clock_hand;
		struct ptewarm_slot *s = &cw->slots[h];
		WRITE_ONCE(s->va, va);
		WRITE_ONCE(s->ref, 1);
		WRITE_ONCE(s->valid, 1);
		cw->clock_hand = (h + 1) % PTEWARM_N;
	}
}

/*
 * Consumer: warm up to `budget` entries.
 * Clears ref so unreferenced items get evicted later.
 * Returns how many were attempted.
 */
unsigned ptewarm_clock_scan(struct task_struct t, unsigned budget)
{
	struct ptewarm_clock *cw = t->ptewarm;
	struct mm_struct *mm = t->mm;
	unsigned done = 0;

	if (!t->ptewarm_enabled)
		return 0;

	if (!cw || !mm || !budget) return 0;

	/*
		 * Bind to the task's mm so the user VA translates in this context.
		 * (If you call from the scheduler path directly, use use_mm/unuse_mm;
		 * for a dedicated worker, do kthread_use_mm() once at thread start.)
		 */
	kthread_use_mm(mm);
	while (budget--) {
		u8 h = cw->scan_hand;
		struct ptewarm_slot *s = &cw->slots[h];

		if (READ_ONCE(s->valid)) {
			unsigned long va = READ_ONCE(s->va);      /* page-aligned VA */
			prefetch((const void __force *)va);       /* non-faulting hint */
			WRITE_ONCE(s->ref, 0);                    /* CLOCK second-chance cleared */
		}
		cw->scan_hand = (h + 1) % PTEWARM_N;
		done++;
	}
	kthread_unuse_mm(mm);
	return done;
}


struct ptewarm_clock *ptewarm_clock_alloc(gfp_t gfp)
{
	struct ptewarm_clock *cw = kzalloc(sizeof(*cw), gfp);
	if (cw) ptewarm_clock_init(cw);
	return cw;
}

void ptewarm_maybe_init(struct task_struct *t)
{
	if (!t->ptewarm_enabled)
		return;

	if (unlikely(!READ_ONCE(t->ptewarm))) {
		struct ptewarm_clock *cw = ptewarm_clock_alloc(GFP_ATOMIC);
		if (cw) WRITE_ONCE(t->ptewarm, cw);
	}
}

void ptewarm_clock_free(struct task_struct *t)
{
	struct ptewarm_clock *cw = xchg(&t->ptewarm, NULL);
	kfree(cw);
}

#endif /* _LINUX_PTEWARM_H */

