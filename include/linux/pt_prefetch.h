/* include/linux/pt_prefetch.h */
#ifndef _LINUX_PT_PREFETCH_H
#define _LINUX_PT_PREFETCH_H

#include <linux/types.h>
#include <linux/hashtable.h>
#include <linux/spinlock.h>

#define PT_PREFETCH_HASH_BITS 2  /* 4 buckets */
#define PT_PREFETCH_MAX_ENTRIES 16

struct pt_prefetch_entry {
	unsigned long va;           /* Key */
	unsigned long pgd_kva;
	unsigned long p4d_kva;
	unsigned long pud_kva;
	unsigned long pmd_kva;
	unsigned long pte_kva;
	
	bool valid;				/* Indicates if the entry is valid */
	bool referenced;				/* For eviction policy, fault should set referenced to 1 */
	struct hlist_node hash_node;	/* Hash table node */
};

struct pt_prefetch_state {
	DECLARE_HASHTABLE(table, PT_PREFETCH_HASH_BITS);
	struct pt_prefetch_entry entries[PT_PREFETCH_MAX_ENTRIES];
	u8 count;
	u8 clock_hand; /* For clock eviction */
	spinlock_t lock;
};

/* Helpers */
static inline struct pt_prefetch_state *alloc_pt_prefetch_state(void);
static inline void free_pt_prefetch_state(struct pt_prefetch_state *state);
static inline void record_pt_walk_kvas(struct task_struct *tsk, unsigned long address, pgd_t *pgd, p4d_t *p4d, pud_t *pud, pmd_t *pmd, pte_t *pte);
static inline void prefetch_task_page_tables(struct task_struct *next);
static inline struct pt_prefetch_state *ensure_pt_prefetch_state(struct task_struct *tsk);
static inline struct pt_prefetch_entry *evict_one_entry_clock(struct pt_prefetch_state *state);

#endif
