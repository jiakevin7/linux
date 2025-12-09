/* include/linux/pt_prefetch.h */
#ifndef _LINUX_PTE_WARM_H
#define _LINUX_PTE_WARM_H

#include <linux/types.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/syscalls.h>   // for SYSCALL_DEFINE*
#include <linux/hugetlb.h>

#include <asm/pgtable.h>

#define PT_PREFETCH_HASH_BITS 4  /* 16 buckets */
#define PT_PREFETCH_MAX_ENTRIES 64

struct pt_prefetch_entry {
	bool valid;				/* Indicates if the entry is valid */
	bool referenced;				/* For eviction policy, fault should set referenced to 1 */
	unsigned long kva;           /* Key */
	struct hlist_node hash_node;	/* Hash table node */
};

struct pt_prefetch_state {
	u8 count;
	u8 clock_hand; /* For clock eviction */
	DECLARE_HASHTABLE(table, PT_PREFETCH_HASH_BITS);
	struct pt_prefetch_entry entries[PT_PREFETCH_MAX_ENTRIES];
	spinlock_t lock;
};

/* Helpers */

static inline unsigned long pt_key(unsigned long kva);
struct pt_prefetch_state *alloc_pt_prefetch_state(void);
void free_pt_prefetch_state(struct pt_prefetch_state *state);
void record_pt_walk_kvas(struct task_struct *tsk, unsigned long address, pgd_t *pgd, p4d_t *p4d, pud_t *pud, pmd_t *pmd, pte_t *pte);
void prefetch_task_page_tables(struct task_struct *next);
struct pt_prefetch_state *ensure_pt_prefetch_state(struct task_struct *tsk);
struct pt_prefetch_entry *evict_one_entry_clock(struct pt_prefetch_state *state);
int record_pt_addr(unsigned long addr);

#endif
