#ifndef _LINUX_PT_PREFETCH_H
#define _LINUX_PT_PREFETCH_H

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/kthread.h>

#define PTEWARM_N 16  /* power of two is not required here */

struct ptewarm_slot {
	unsigned long va;  /* page-aligned user VA */
	u8 ref;            /* CLOCK reference bit: set on fault, cleared by scan */
	u8 valid;          /* 0 = empty slot */
	u16 pad;           /* keep 4B aligned */
};

struct ptewarm_clock {
	/* Producer-only state (fault path) */
	u8 clock_hand;     /* victim hand for insertion/eviction */
	u8 _pad0[63];

	/* Consumer-only state (warmer thread) */
	u8 scan_hand;      /* where the warmer scans from */
	u8 _pad1[63];

	/* Shared slots (benign data races on ref/valid are OK for hints) */
	struct ptewarm_slot slots[PTEWARM_N];
};


void ptewarm_clock_init(struct ptewarm_clock *cw);
void ptewarm_clock_record(struct ptewarm_clock *cw, unsigned long addr);
unsigned ptewarm_clock_scan(struct task_struct *t, unsigned budget);

void ptewarm_maybe_init(struct task_struct *t);
void ptewarm_clock_free(struct task_struct *t);

#endif /* _LINUX_PT_PREFETCH_H */
