#!/bin/bash
# run_prefetch_test_no_flag.sh
# Benchmark using perf without runtime toggle

set -e

BENCH=./test_control
OUTDIR=results4
mkdir -p "$OUTDIR"

echo "=== Running benchmark $BENCH"
sudo taskset -c 2 perf stat -e dTLB-load-misses,dTLB-store-misses,dTLB-loads,dTLB-stores,iTLB-load-misses,iTLB-loads,cycles,instructions,cache-misses,branches,branch-misses \
  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH

echo "Results saved to $OUTDIR/$BENCH.txt"

BENCH=./test_pt
mkdir -p "$OUTDIR"

echo "=== Running benchmark $BENCH"
sudo taskset -c 2 perf stat -e dTLB-load-misses,dTLB-store-misses,dTLB-loads,dTLB-stores,iTLB-load-misses,iTLB-loads,cycles,instructions,cache-misses,branches,branch-misses \
  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH

echo "Results saved to $OUTDIR/$BENCH.txt"

BENCH=./test_warm
mkdir -p "$OUTDIR"

echo "=== Running benchmark $BENCH"
sudo taskset -c 2 perf stat -e dTLB-load-misses,dTLB-store-misses,dTLB-loads,dTLB-stores,iTLB-load-misses,iTLB-loads,cycles,instructions,cache-misses,branches,branch-misses \
  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH

echo "Results saved to $OUTDIR/$BENCH.txt"
