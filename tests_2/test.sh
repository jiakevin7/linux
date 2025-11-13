#!/bin/bash
# run_prefetch_test_no_flag.sh
# Benchmark using perf without runtime toggle

set -e

BENCH=./test
OUTDIR=results
mkdir -p "$OUTDIR"

echo "=== Running benchmark with current kernel (prefetcher always-on/off build) ==="
sudo taskset -c 2 perf stat -e \
  dTLB-load-misses.walk_duration,\
  dTLB-store-misses.walk_duration,\
  cycles \
  "$BENCH" 2>&1 | tee "$OUTDIR"/perf_run.txt

echo "Results saved to $OUTDIR/perf_run.txt"
