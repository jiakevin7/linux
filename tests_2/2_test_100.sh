#!/bin/bash

set -e
rm -f combined.txt
OUTDIR=results0

TEST=test

sum=0
for i in {1..1000}; do
	BENCH=control
	mkdir -p "$OUTDIR"

	PT_MODE=$BENCH ./$TEST 2>&1 | tee "$OUTDIR"/$BENCH

	cat "$OUTDIR/$BENCH" >> "combined.txt"
	value=$(cat "$OUTDIR/$BENCH")
	sum=$(( sum + value ))

done
echo "total cycles for control: $sum" >> combined.txt

sum=0
for i in {1..1000}; do
	BENCH=prefetch
	mkdir -p "$OUTDIR"

	PT_MODE=$BENCH ./$TEST 2>&1 | tee "$OUTDIR"/$BENCH

	cat "$OUTDIR/$BENCH" >> "combined.txt"
	value=$(cat "$OUTDIR/$BENCH")
	sum=$(( sum + value ))

done
echo "total cycles for pt: $sum" >> combined.txt
