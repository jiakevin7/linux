#!/bin/bash

set -e

for i in {0..10}; do
	BENCH=./test_pt
	OUTDIR=results2
	mkdir -p "$OUTDIR"

	echo "=== Running benchmark $BENCH"
	sudo taskset -c 2 perf stat -e cycles,instructions,cache-misses,branches,branch-misses \
	  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH

	echo "Results saved to $OUTDIR/$BENCH.txt"
done
