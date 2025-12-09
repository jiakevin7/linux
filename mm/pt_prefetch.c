#include <linux/pt_prefetch.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>
#include <linux/printk.h>

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

	struct pt_prefetch_state *state;
	struct pt_prefetch_entry *entry;
	unsigned long va_page = address & PAGE_MASK;
	unsigned long hash_key;

	pr_debug("pt_prefetch: recording a page fault for va_page %lx. tsk->pt_prefetch_enabled = %d\n",
					va_page, tsk->pt_prefetch_enabled);

	if (!tsk->pt_prefetch_enabled)
		return;

	/* Ensure state exists */
	state = ensure_pt_prefetch_state(tsk);
	if (!state) {
		return;
	}

	spin_lock(&state->lock);

	/* Check if entry already exists */
	hash_key = hash_long(va_page, PT_PREFETCH_HASH_BITS);
	hash_for_each_possible(state->table, entry, hash_node, hash_key) {
		if (entry->va == va_page) {

			pr_debug("pt_prefetch: va already exists. updating entry.\n");

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
	if (state->count == PT_PREFETCH_MAX_ENTRIES)
		entry = evict_one_entry_clock(state); // evict deletes the hash_node from the linked list of the old key, and decrements count
	else
		entry = &state->entries[state->count];  /* Next free slot */

	entry->va = va_page;
	entry->valid = true;

	entry->pgd_kva = (unsigned long)pgd;
	entry->p4d_kva = (unsigned long)p4d;
	entry->pud_kva = (unsigned long)pud;
	entry->pmd_kva = (unsigned long)pmd;
	entry->pte_kva = (unsigned long)pte;
	entry->referenced = true;  // New entry starts as referenced

	hash_add(state->table, &entry->hash_node, hash_key);
	++state->count;
	pr_debug("pt_prefetch: recorded VA=%lx at slot=%lx, PTE_KVA=%lx\n", 
					va_page, entry - state->entries, (unsigned long)pte);

	spin_unlock(&state->lock);
}


/* Find victim using clock algorithm and evict it */
struct pt_prefetch_entry *evict_one_entry_clock(struct pt_prefetch_state *state)
{
	struct pt_prefetch_entry *victim;

	pr_debug("pt_prefetch: evicting with %ui entries and %ui max_entries\n", 
					state->count,	 PT_PREFETCH_MAX_ENTRIES);

	/* Clock sweep - look for unreferenced entry */
	do {
		victim = &state->entries[state->clock_hand];

		pr_debug("pt_prefetch: clock_hand %d on va %lx\n", state->clock_hand, victim->va);

		if (!victim->referenced) {
			/* Found victim with reference bit = 0 */
			break;
		}

		/* Give it a second chance - clear reference bit */
		victim->referenced = false;
		state->clock_hand = (state->clock_hand + 1) % PT_PREFETCH_MAX_ENTRIES;

	} while (1);

	pr_debug("pt_prefetch: evicting va %lx\n", victim->va);

	/* Remove from hash table */
	hash_del(&victim->hash_node);
	victim->valid = false;
	state->clock_hand = (state->clock_hand + 1) % PT_PREFETCH_MAX_ENTRIES;
	--state->count;

	return victim;
}


/*
 * Prefetch page table entries for the next task.
 * Called right before context switch to warm up PTEs on target CPU.
 */
void prefetch_task_page_tables(struct task_struct *next)
{

	struct pt_prefetch_state *state;
	struct pt_prefetch_entry *entry;
	int i;
	int prefetch_count = 0;

	pr_debug("pt_prefetch: inside of prefetch_task_page_tables for task %d. next->pt_prefetch_enabled = %d\n",
					next->pid, next->pt_prefetch_enabled);

	if (!next->pt_prefetch_enabled)
		return;

	/* Only prefetch if we have state */
	state = next->pt_prefetch;
	if (!state)
		return;

	spin_lock(&state->lock);

	for (i = 0; i < PT_PREFETCH_MAX_ENTRIES; ++i) {
		entry = state->entries+i;

		if (!entry->valid) continue;

		pr_debug("pt_prefetch: prefetching entry %d\n", i);
		
		if (likely(entry->pgd_kva))
		{
			pr_debug("pt_prefetch: prefetching pgd_kva %lx\n", entry->pgd_kva);
			prefetch((void *)entry->pgd_kva);
		}

		if (likely(entry->p4d_kva)) 
		{
			pr_debug("pt_prefetch: prefetching p4d_kva %lx\n", entry->p4d_kva);
			prefetch((void *)entry->p4d_kva);
		}

		if (likely(entry->pud_kva)) 
		{
			pr_debug("pt_prefetch: prefetching pud_kva %lx\n", entry->pud_kva);
			prefetch((void *)entry->pud_kva);
		}

		if (entry->pmd_kva) 
		{
			pr_debug("pt_prefetch: prefetching pmd_kva %lx\n", entry->pmd_kva);
			prefetch((void *)entry->pmd_kva);
		}

		if (entry->pte_kva) 
		{
			pr_debug("pt_prefetch: prefetching pte_kva %lx\n", entry->pte_kva);
			prefetch((void *)entry->pte_kva);
		}

		++prefetch_count;
	}

	spin_unlock(&state->lock);

	pr_debug("pt_prefetch: prefetched %d PT entries for task %d\n",
					prefetch_count, next->pid);
}
