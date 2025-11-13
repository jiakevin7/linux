#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <numa.h>
#include <numaif.h>

#define PR_SET_PT_PREFETCH		65
#define PR_GET_PT_PREFETCH		66

#define PR_SET_PTE_WARM		67
#define PR_GET_PTE_WARM		68

/* Simple helper: nanoseconds difference */
static inline uint64_t nsec_diff(const struct timespec *start,
																 const struct timespec *end) {
	uint64_t s = (uint64_t)start->tv_sec * 1000000000ull + start->tv_nsec;
	uint64_t e = (uint64_t)end->tv_sec   * 1000000000ull + end->tv_nsec;
	return e - s;
}

/* Bind current thread to the given NUMA node */
static int bind_to_node(int node) {
	if (numa_run_on_node(node) != 0) {
		perror("numa_run_on_node");
		return -1;
	}
	return 0;
}

int main(int argc, char **argv) {
	if (numa_available() < 0) {
		fprintf(stderr, "NUMA not available on this system\n");
		return 1;
	}

	int max_node = numa_max_node();
	if (max_node < 1) {
		fprintf(stderr, "Need at least 2 NUMA nodes (found %d)\n", max_node + 1);
		return 1;
	}

	prctl(PR_SET_PTE_WARM, 1, 0, 0, 0);

	int pt = prctl(PR_GET_PT_PREFETCH, 0, 0, 0, 0);
	int warm = prctl(PR_GET_PTE_WARM, 0, 0, 0, 0);

	printf("PR_GET_PT_PREFETCH set to %d\n", pt);
	printf("PR_GET_PTE_WARM set to %d\n", warm);

	int src_node = 0;
	int dst_node = 1;

	size_t pages = 256 * 1024;              /* 256K pages = 1 GB if 4 KB pages */
	size_t len   = pages * 4096ull;

	printf("Using src_node=%d, dst_node=%d, len=%zu bytes\n",
				src_node, dst_node, len);

	/* Allocate memory explicitly on src_node */
	printf("Allocating buffer on node %d...\n", src_node);
	void *buf = numa_alloc_onnode(len, src_node);
	if (!buf) {
		fprintf(stderr, "numa_alloc_onnode failed\n");
		return 1;
	}

	/* Bind to src_node and fault in pages there */
	printf("Binding to node %d and touching pages...\n", src_node);
	if (bind_to_node(src_node) != 0) {
		fprintf(stderr, "bind_to_node(%d) failed\n", src_node);
		return 1;
	}

	volatile char *cbuf = (volatile char *)buf;
	for (size_t i = 0; i < len; i += 4096) {
		cbuf[i] = (char)(i & 0xff);
	}
	printf("First touch on node %d completed.\n", src_node);

	/* Optional: thrash caches a bit */
	size_t thrash_len = 64 * 1024 * 1024ull;   /* 64 MB */
	char *thrash = static_cast<char *>(malloc(thrash_len));
	if (thrash) {
		for (size_t i = 0; i < thrash_len; i += 64) {
			thrash[i] ^= 1;
		}
		free(thrash);
	}

	/* Now bind to dst_node (different NUMA node) */
	printf("Binding to node %d for measurement...\n", dst_node);
	if (bind_to_node(dst_node) != 0) {
		fprintf(stderr, "bind_to_node(%d) failed\n", dst_node);
		return 1;
	}

	sleep(1);  /* let things settle */

	/* Measure access time from dst_node */
	struct timespec t_start, t_end;
	volatile uint64_t acc = 0;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) != 0) {
		perror("clock_gettime start");
		return 1;
	}

	for (size_t i = 0; i < len; i += 4096) {
		acc += cbuf[i];
	}

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_end) != 0) {
		perror("clock_gettime end");
		return 1;
	}

	uint64_t dt_ns = nsec_diff(&t_start, &t_end);
	double dt_per_page = (double)dt_ns / (double)pages;

	printf("Cross-node access time: %lu ns total, ~%.1f ns per page (acc=%llu)\n",
				(unsigned long)dt_ns, dt_per_page,
				(unsigned long long)acc);

	numa_free(buf, len);
	return 0;
}
