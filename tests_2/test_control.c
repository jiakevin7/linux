// test.c
// First-K cold-access probe for page-table prefetcher correctness.
// Builds with:  gcc -O2 -Wall test.c -lnuma -o test
// Run:          ./test
// Env tweaks:   K=64 MIB=1024 VERIFY=1 ./test

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <numa.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PR_SET_PT_PREFETCH		65
#define PR_GET_PT_PREFETCH		66

#define PR_SET_PTE_WARM		67
#define PR_GET_PTE_WARM		68

// ----------------------------- utils ---------------------------------

static void die(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static void warnx(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr);
}

static unsigned long xorshift32(unsigned long x) {
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

static void shuffle(size_t *a, size_t n, unsigned seed) {
	// Fisher-Yates using a simple xorshift RNG for reproducibility
	unsigned long s = seed ? seed : 0xdeadbeefUL;
	for (size_t i = n - 1; i > 0; --i) {
		s = xorshift32(s);
		size_t j = (size_t)(s % (i + 1));
		size_t t = a[i]; a[i] = a[j]; a[j] = t;
	}
}

// cycle timer with fences to bound reordering
static inline uint64_t rdtscp_barrier(void) {
	unsigned aux;
	uint32_t lo, hi;
	asm volatile("lfence\n\t"
		"rdtscp\n\t"
		: "=a"(lo), "=d"(hi), "=c"(aux)
		:
		: "memory");
	asm volatile("lfence" ::: "memory");
	return ((uint64_t)hi << 32) | lo;
}

// best-effort sysfs write helper
static void try_sysfs_write(const char *path, const char *val) {
	FILE *f = fopen(path, "w");
	if (!f) return;
	fwrite(val, 1, strlen(val), f);
	fclose(f);
}

// pin current thread to any CPU belonging to `node`
static int pin_to_any_cpu_on_node(int node) {
	struct bitmask *cpus = numa_allocate_cpumask();
	if (!cpus) return -1;
	if (numa_node_to_cpus(node, cpus) != 0) {
		numa_free_cpumask(cpus);
		return -1;
	}
	cpu_set_t set; CPU_ZERO(&set);
	int ok = -1;
	for (int i = 0; i < (int)cpus->size; ++i) {
		if (numa_bitmask_isbitset(cpus, i)) {
			CPU_SET(i, &set);
			ok = 0;
			break;
		}
	}
	numa_free_cpumask(cpus);
	if (ok) return -1;
	return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

// pick two distinct online NUMA nodes with memory
static void pick_two_nodes(int *src, int *dst) {
	int maxn = numa_max_node();
	if (maxn < 1) die("Only one NUMA node detected (0..%d)", maxn);
	int first = -1, second = -1;
	for (int n = 0; n <= maxn; ++n) {
		long sz = numa_node_size64(n, NULL); // bytes; -1 on error
		if (sz <= 0) continue;
		if (first == -1) first = n;
		else { second = n; break; }
	}
	if (first == -1 || second == -1) die("Need at least two NUMA nodes with memory");
	*src = first; *dst = second;
}

// optional: sample residency of some pages using move_pages
static void sample_residency(void *base, size_t len, size_t page_sz, int expect_node,
														 size_t samples) {
	size_t pages = len / page_sz;
	if (samples > pages) samples = pages;
	void **addrs = malloc(samples * sizeof(void*));
	int *status = (int *)malloc(samples * sizeof(int));
	if (!addrs || !status) { free(addrs); free(status); return; }

	// uniform-ish sample across the buffer
	for (size_t i = 0; i < samples; ++i) {
		size_t idx = (i * pages) / samples;
		addrs[i] = (char*)base + idx * page_sz;
	}
	// move_pages with nodes=NULL queries current node(s)
	long rc = syscall(SYS_move_pages, 0 /* self */, (int)samples, addrs,
									 NULL, status, 0);
	if (rc != 0) {
		warnx("move_pages query failed (errno=%d), skipping residency sample", errno);
		goto out;
	}
	size_t on_expect = 0;
	for (size_t i = 0; i < samples; ++i) if (status[i] == expect_node) ++on_expect;
	fprintf(stderr, "Residency sample: %zu/%zu pages on node %d (%.1f%%)\n",
				 on_expect, samples, expect_node, 100.0 * on_expect / samples);
out:
	free(addrs); free(status);
}

// ----------------------------- main ----------------------------------

int main(void) {
	if (numa_available() < 0) die("NUMA not available on this system");

	int pt = prctl(PR_GET_PT_PREFETCH, 0, 0, 0, 0);
	int warm = prctl(PR_GET_PTE_WARM, 0, 0, 0, 0);

	printf("PR_GET_PT_PREFETCH set to %d\n", pt);
	printf("PR_GET_PTE_WARM set to %d\n", warm);

	// Best-effort: reduce confounders (requires privileges; ignore failures)
	try_sysfs_write("/proc/sys/kernel/numa_balancing", "0\n");
	try_sysfs_write("/sys/kernel/mm/transparent_hugepage/enabled", "never\n");

	// Parameters via env (defaults: 1024 MiB total, K=64 first touches)
	size_t mib_total = getenv("MIB") ? strtoull(getenv("MIB"), NULL, 10) : 32;
	size_t K = getenv("K") ? strtoull(getenv("K"), NULL, 10) : 16;
	unsigned seed = getenv("SEED") ? (unsigned)strtoul(getenv("SEED"), NULL, 10) : 12345u;
	bool verify = getenv("VERIFY") && atoi(getenv("VERIFY")) != 0;

	const size_t PAGE = 4096ULL;
	const size_t REGION = 2ULL * 1024 * 1024; // 2 MiB per PTE-page coverage
	const size_t len = mib_total * 1024ULL * 1024ULL;

	int src_node, dst_node;
	pick_two_nodes(&src_node, &dst_node);
	fprintf(stderr, "Auto-selected nodes: src=%d dst=%d | MIB=%zu K=%zu\n",
				 src_node, dst_node, mib_total, K);

	// Map anonymous memory
	void *buf = mmap(NULL, len, PROT_READ | PROT_WRITE,
									MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) die("mmap failed: %s", strerror(errno));

	// Strictly bind to src and move pages there
	unsigned long nodemask[1024 / sizeof(unsigned long)] = {0};
	// assumes node id < 64; for very large node ids, extend nodemask logic
	if (src_node >= (int)(8 * sizeof(nodemask[0])))
		die("src_node too large for nodemask in this demo");
	nodemask[0] = (1UL << src_node);
	if (mbind(buf, len, MPOL_BIND, nodemask,
					 8 * sizeof(nodemask[0]),
					 MPOL_MF_MOVE | MPOL_MF_STRICT) != 0) {
		die("mbind src failed: %s", strerror(errno));
	}

	// First-touch on src
	if (pin_to_any_cpu_on_node(src_node) != 0) die("pin to src failed");
	volatile char *cbuf = (volatile char*)buf;
	for (size_t off = 0; off < len; off += PAGE) cbuf[off] = (char)(off >> 12);

	// Lock to avoid paging noise
	if (mlock(buf, len) != 0) warnx("mlock failed (non-fatal): %s", strerror(errno));

	if (verify) sample_residency(buf, len, PAGE, src_node, /*samples=*/1024);

	// Build candidate offsets: one 4 KiB page inside each 2 MiB region
	size_t regions = len / REGION;
	if (regions < K) die("Not enough 2MiB regions (%zu) for K=%zu", regions, K);
	size_t *cand = malloc(regions * sizeof(size_t));
	if (!cand) die("oom for cand");
	for (size_t r = 0; r < regions; ++r) {
		// choose a page offset within the region to avoid adjacent-line effects
		// use a simple LCG-ish pattern keyed by r
		size_t within_pages = (REGION / PAGE);
		size_t within = ((r * 1315423911u) % within_pages) * PAGE;
		cand[r] = r * REGION + within;
	}
	shuffle(cand, regions, seed);

	// Choose K probe offsets
	size_t *probe = malloc(K * sizeof(size_t));
	if (!probe) die("oom for probe");
	memcpy(probe, cand, K * sizeof(size_t));
	free(cand);

	volatile unsigned warm_acc = 1;
	for (size_t i = 0; i < K; ++i) {
		size_t off = probe[i] ^ (size_t)(warm_acc & (REGION - 1));
		warm_acc += cbuf[off];
	}
	// warm_acc is only to keep the loop "live"
	fprintf(stderr, "warm_acc=%u\n", warm_acc);

	// Migrate to dst (TLB/PWC cold on this CPU)
	if (pin_to_any_cpu_on_node(dst_node) != 0) die("pin to dst failed");
	for (volatile int i = 0; i < 100000; ++i) {} // tiny settle

	// Measure first K one-byte loads with dependent addressing
	// Output CSV: idx,cycles
	printf("idx,cycles\n");
	volatile unsigned acc = 1;
	unsigned total_cyc_count = 0;
	for (size_t i = 0; i < K; ++i) {
		// data-dependent index to defeat hoisting and prefetching
		size_t off = probe[i] ^ (size_t)(acc & (REGION - 1));
		uint64_t t0 = rdtscp_barrier();
		acc += cbuf[off];
		uint64_t t1 = rdtscp_barrier();
		uint64_t cyc = t1 - t0;
		total_cyc_count += cyc;
		printf("%zu,%" PRIu64 "\n", i, cyc);
	}
	fprintf(stderr, "acc=%u, total_cyc_count=%u\n", acc, total_cyc_count); // keep the loop "live"

	// Cleanup
	munlock(buf, len);
	munmap(buf, len);
	free(probe);
	return 0;
}
