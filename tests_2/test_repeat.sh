#!/bin/bash

set -e

for i in {0..10}; do
	BENCH=./test_control
	OUTDIR=results0
	mkdir -p "$OUTDIR"

	echo "=== Running benchmark $BENCH"
	  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH
	
	cat "$OUTDIR"/$BENCH >> combined.txt

	echo "Results saved to $OUTDIR/$BENCH.txt"

	BENCH=./test_pt
	mkdir -p "$OUTDIR"

	echo "=== Running benchmark $BENCH"
	  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH

	cat "$OUTDIR"/$BENCH >> combined.txt

	echo "Results saved to $OUTDIR/$BENCH.txt"

	BENCH=./test_warm
	mkdir -p "$OUTDIR"

	echo "=== Running benchmark $BENCH"
	  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH
	
	cat "$OUTDIR"/$BENCH >> combined.txt

	echo "Results saved to $OUTDIR/$BENCH.txt"
done
