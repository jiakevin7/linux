#include <linux/pt_prefetch.h>

#define L1_CACHE_MASK	(~(L1_CACHE_BYTES - 1))

static inline unsigned long pt_key(unsigned long kva)
{
	return kva >> L1_CACHE_SHIFT;   /* cache line number */
}

static struct pt_prefetch_entry *
pt_lookup(struct pt_prefetch_state *s, unsigned long kva)
{
	struct pt_prefetch_entry *e;

	hash_for_each_possible(s->table, e, hash_node, pt_key(kva)) {
		if (e->valid && e->kva == kva)
			return e;
	}

	return NULL;
}

static struct pt_prefetch_entry *
pt_insert_or_touch(struct pt_prefetch_state *s, unsigned long kva)
{

	struct pt_prefetch_entry *e;

	e = pt_lookup(s, kva);
	if (e) {
		pr_debug("pt_prefetch: touched KVA=%lx at slot=%lx\n", kva, e - s->entries);
		e->referenced = true;     /* touched again */
		return e;
	}

	/* Need a free slot (or evict) */
	if (s->count < PT_PREFETCH_MAX_ENTRIES) {
		int i;
		for (i = 0; i < PT_PREFETCH_MAX_ENTRIES; i++) {
			if (!s->entries[i].valid) {
				e = &s->entries[i];
				break;
			}
		}
	} else {
		e = evict_one_entry_clock(s);
	}

	/* Fill and add to hashset */
	e->valid = true;
	e->referenced = true;
	e->kva = kva;

	hash_add(s->table, &e->hash_node, pt_key(kva));
	s->count++;

	pr_debug("pt_prefetch: inserted KVA=%lx at slot=%lx\n", 
					kva, e - s->entries);

	return e;
}

struct pt_prefetch_state *alloc_pt_prefetch_state(void)
{
	struct pt_prefetch_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	hash_init(state->table);
	state->count = 0;
	state->clock_hand = 0;

	for (int i = 0; i < PT_PREFETCH_MAX_ENTRIES; ++i)
		state->entries[i].valid = false;

	spin_lock_init(&state->lock);

	return state;
}

void free_pt_prefetch_state(struct pt_prefetch_state *state)
{
	kfree(state);
}


struct pt_prefetch_state *ensure_pt_prefetch_state(struct task_struct *tsk)
{
	struct pt_prefetch_state *state = tsk->pt_prefetch;

	pr_debug("pt_prefetch: ensure_pt_prefetch_state called on thread id %i\n", tsk->pid);

	if (likely(state)) return state;

	/* First fault - allocate now */
	pr_debug("pt_prefetch: allocating state\n");
	state = alloc_pt_prefetch_state();

	if (!state) return NULL;

	tsk->pt_prefetch = state;
	pr_debug("pt_prefetch: state successfully allocated\n");

	return state;
}

void record_pt_walk_kvas(struct task_struct *tsk, unsigned long address, pgd_t *pgd, p4d_t *p4d, pud_t *pud, pmd_t *pmd, pte_t *pte)
{

	struct pt_prefetch_state *s;

	if (!tsk->pt_prefetch_enabled)
		return;

	pr_debug("pt_prefetch: recording address %lx\n", address);

	/* Ensure state exists */
	s = ensure_pt_prefetch_state(tsk);
	if (!s)
		return;

	spin_lock(&s->lock);

	/* dedupe at cache line granularity */
	unsigned long pgd_page = (unsigned long)pgd & L1_CACHE_MASK;
	unsigned long p4d_page = (unsigned long)p4d & L1_CACHE_MASK;
	unsigned long pud_page = (unsigned long)pud & L1_CACHE_MASK;
	unsigned long pmd_page = (unsigned long)pmd & L1_CACHE_MASK;
	unsigned long pte_page = (unsigned long)pte & L1_CACHE_MASK;

	if (likely(pgd_page)) pt_insert_or_touch(s, pgd_page);
	if (likely(p4d_page)) pt_insert_or_touch(s, p4d_page);
	if (likely(pud_page)) pt_insert_or_touch(s, pud_page);
	if (likely(pmd_page)) pt_insert_or_touch(s, pmd_page);
	if (likely(pte_page)) pt_insert_or_touch(s, pte_page);

	spin_unlock(&s->lock);
}


/* Find victim using clock algorithm and evict it */
struct pt_prefetch_entry *evict_one_entry_clock(struct pt_prefetch_state *s)
{
	struct pt_prefetch_entry *victim;

	pr_debug("pt_prefetch: evicting with %u entries and %u max_entries\n", 
					s->count,	 PT_PREFETCH_MAX_ENTRIES);

	/* Clock sweep - look for unreferenced entry */
	do {
		victim = &s->entries[s->clock_hand];

		pr_debug("pt_prefetch: clock_hand %d on va %lx\n", s->clock_hand, victim->kva);

		if (!victim->referenced) {
			/* Found victim with reference bit = 0 */
			break;
		}

		/* Give it a second chance - clear reference bit */
		victim->referenced = false;
		s->clock_hand = (s->clock_hand + 1) % PT_PREFETCH_MAX_ENTRIES;

	} while (1);

	pr_debug("pt_prefetch: evicting va %lx\n", victim->kva);

	/* Remove from hash table */
	hash_del(&victim->hash_node);
	victim->valid = false;
	s->clock_hand = (s->clock_hand + 1) % PT_PREFETCH_MAX_ENTRIES;
	--s->count;

	return victim;
}


/*
 * Prefetch page table entries for the next task.
 * Called right before context switch to warm up PTEs on target CPU.
 */
void prefetch_task_page_tables(struct task_struct *next)
{

	struct pt_prefetch_state *s;
	struct pt_prefetch_entry *e;
	int i;
	int prefetch_count = 0;

	pr_debug("pt_prefetch: inside of prefetch_task_page_tables for task %d. next->pt_prefetch_enabled = %d\n",
					next->pid, next->pt_prefetch_enabled);

	if (!next->pt_prefetch_enabled)
		return;

	/* Only prefetch if we have state */
	s = next->pt_prefetch;
	if (!s)
		return;

	spin_lock(&s->lock);

	for (i = 0; i < PT_PREFETCH_MAX_ENTRIES; i++)
	{
		e = &s->entries[i];

		if (!e->valid) continue;

		pr_debug("pt_prefetch: prefetched pte %lx\n", e->kva);
		prefetch((void *)e->kva);
		++prefetch_count;
	}

	spin_unlock(&s->lock);

	pr_debug("pt_prefetch: prefetched %d PT entries for task %d\n",
					prefetch_count, next->pid);
}

SYSCALL_DEFINE1(record_pt_walk, unsigned long, addr)
{
    struct task_struct *tsk = current;
    struct mm_struct *mm = tsk->mm;
    struct vm_area_struct *vma;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    int ret = 0;

    if (!mm)
        return -EINVAL;   /* e.g. kernel thread */

    /* Make sure the address is valid in this mm */
    down_read(&mm->mmap_lock);
    vma = find_vma(mm, addr);
    if (!vma || addr < vma->vm_start) {
        ret = -EFAULT;
        goto out_unlock;
    }

    /* Walk the page tables for this address */
    pgd = pgd_offset(mm, addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        ret = -EFAULT;
        goto out_unlock;
    }

    p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        ret = -EFAULT;
        goto out_unlock;
    }

    pud = pud_offset(p4d, addr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        ret = -EFAULT;
        goto out_unlock;
    }

    pmd = pmd_offset(pud, addr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        ret = -EFAULT;
        goto out_unlock;
    }

    if (pmd_huge(*pmd) || pmd_large(*pmd)) {
        /* 2M hugepage case – you might want a different handler here */
        ret = -EFAULT;
        goto out_unlock;
    }

    pte = pte_offset_map(pmd, addr);
    if (!pte) {
        ret = -EFAULT;
        goto out_unlock;
    }

    /* Now call your recorder with all the pieces */
    record_pt_walk_kvas(tsk, addr, pgd, p4d, pud, pmd, pte);

    pte_unmap(pte);
out_unlock:
    up_read(&mm->mmap_lock);
    return ret;
}
