#include <linux/pt_prefetch.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>
#include <linux/printk.h>

static inline struct pt_prefetch_state *alloc_pt_prefetch_state(void)
{
	struct pt_prefetch_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	hash_init(state->table);
	state->count = 0;
	state->clock_hand = 0;
	spin_lock_init(&state->lock);

	return state;
}



static inline void free_pt_prefetch_state(struct pt_prefetch_state *state)
{
	struct pt_prefetch_entry *entry;
	struct hlist_node *tmp;
	int bkt;

	if (!state)
		return;

	hash_for_each_safe(state->table, bkt, tmp, entry, node) {
		hash_del(&entry->node);
		kfree(entry);
	}
	kfree(state);
}



static inline struct pt_prefetch_state *ensure_pt_prefetch_state(struct task_struct *tsk)
{
	struct pt_prefetch_state *state = tsk->pt_prefetch;

	if (likely(state))
		return state;

	/* First fault - allocate now */
	state = alloc_pt_prefetch_state();
	if (!state)
		return NULL;

	/* Race with concurrent fault? Check again */
	if (cmpxchg(&tsk->pt_prefetch, NULL, state) != NULL) {
		/* Someone else allocated, use theirs */
		free_pt_prefetch_state(state);
		state = tsk->pt_prefetch;
	}

	return state;
}

static inline void record_pt_walk_kvas(struct task_struct *tsk, unsigned long address, pgd_t *pgd, p4d_t *p4d, pud_t *pud, pmd_t *pmd, pte_t *pte)
{
	struct pt_prefetch_state *state;
	struct pt_prefetch_entry *entry, *victim;
	unsigned long va_page = address & PAGE_MASK;
	unsigned long hash_key;
	int slot;

	/* Ensure state exists */
	state = ensure_pt_prefetch_state(tsk);
	if (!state) {
		return;
	}

	spin_lock(&state->lock);

	/* Check if entry already exists */
	hash_key = hash_long(va_page, PT_PREFETCH_HASH_BITS);
	hash_for_each_possible(state->table, entry, node, hash_key) {
		if (entry->va == va_page) {
			/* Update existing entry and set reference bit */
			entry->pgd_kva = (unsigned long)pgd;
			entry->p4d_kva = (unsigned long)p4d;
			entry->pud_kva = (unsigned long)pud;
			entry->pmd_kva = (unsigned long)pmd;
			entry->pte_kva = (unsigned long)pte;
			entry->referenced = true;  // Mark as referenced
			spin_unlock(&state->lock);
			return;
		}
	}

	/* Need to add new entry - check if we need to evict */
	if (state->count >= PT_PREFETCH_MAX_ENTRIES) {
		victim = evict_one_entry_clock(state);
		kfree(victim);
	}

	/* Find empty slot in entries array */
	slot = find_empty_slot(state);

	/* Allocate and insert new entry */
	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry) {
		entry->va = va_page;
		entry->pgd_kva = (unsigned long)pgd;
		entry->p4d_kva = (unsigned long)p4d;
		entry->pud_kva = (unsigned long)pud;
		entry->pmd_kva = (unsigned long)pmd;
		entry->pte_kva = (unsigned long)pmd;
		entry->referenced = true;  // New entry starts as referenced

		/* Add to hash table */
		hash_add(state->table, &entry->node, hash_key);

		/* Add to entries array at the found slot */
		state->entries[slot] = entry;
		++state->count;

		pr_debug("pt_prefetch: recorded VA=%lx at slot=%d, PTE_KVA=%lx\n", 
					 va_page, slot, (unsigned long)pte);
	}

	spin_unlock(&state->lock);
}


/* Find victim using clock algorithm and evict it */
static inline struct pt_prefetch_entry *evict_one_entry_clock(struct pt_prefetch_state *state)
{
	unsigned int start_hand = state->clock_hand;
	struct pt_prefetch_entry *victim = NULL;

	pr_debug("evicting with %ui entries and %ui max_entries\n", 
					state->count,	 PT_PREFETCH_MAX_ENTRIES);

	/* Clock sweep - look for unreferenced entry */
	do {
		victim = state->entries[state->clock_hand];

		if (!victim->referenced) {
			/* Found victim with reference bit = 0 */
			break;
		}

		/* Give it a second chance - clear reference bit */
		victim->referenced = false;
		state->clock_hand = (state->clock_hand + 1) % PT_PREFETCH_MAX_ENTRIES;

	} while (state->clock_hand != start_hand);

	/* victim now points to entry to evict */
	if (victim) {
		/* Remove from hash table */
		hash_del(&victim->node);

		state->entries[state->clock_hand] = NULL;
		state->clock_hand = (state->clock_hand + 1) % PT_PREFETCH_MAX_ENTRIES;
		--state->count;
	}

	return victim;
}



/* Find first empty slot in entries array */
static inline int find_empty_slot(struct pt_prefetch_state *state)
{
	int i;
	for (i = 0; i < PT_PREFETCH_MAX_ENTRIES; ++i) {
		if (state->entries[i] == NULL)
			return i;
	}
	pr_debug("pt_prefetch: no empty slot found despite count=%u\n", state->count);
	return -1;  /* Should never happen if count is accurate */
}




/*
 * Prefetch page table entries for the next task.
 * Called right before context switch to warm up PTEs on target CPU.
 */
static inline void prefetch_task_page_tables(struct task_struct *next)
{
	struct pt_prefetch_state *state;
	struct pt_prefetch_entry *entry;
	int i;
	int prefetch_count = 0;

	/* Only prefetch if we have state */
	state = next->pt_prefetch;
	if (!state)
		return;

	spin_lock(&state->lock);

	for (i = 0; i < PT_PREFETCH_MAX_ENTRIES; ++i) {
		entry = state->entries[i];
		if (!entry)
			continue;
		
		if (likely(entry->pgd_kva))
			prefetch((void *)entry->pgd_kva);

		if (likely(entry->p4d_kva))
			prefetch((void *)entry->p4d_kva);

		if (likely(entry->pud_kva))
			prefetch((void *)entry->pud_kva);

		if (entry->pmd_kva)
			prefetch((void *)entry->pmd_kva);

		if (entry->pte_kva)
			prefetch((void *)entry->pte_kva);

		++prefetch_count;
	}

	spin_unlock(&state->lock);

	pr_debug("pt_prefetch: prefetched %d PT entries for task %d\n",
					prefetch_count, next->pid);
}
