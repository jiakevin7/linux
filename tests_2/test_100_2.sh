#!/bin/bash

set -e
rm -f combined.txt

sum=0
for i in {1..1000}; do
	BENCH=./test_control
	OUTDIR=results0
	mkdir -p "$OUTDIR"

	  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH
	
	cat "$OUTDIR"/$BENCH >> combined.txt
	value=$(cat "$OUTDIR/$BENCH")
	sum=$(( sum + value ))
	
done
echo "total cycles for control: $sum" >> combined.txt
sum=0
for i in {1..1000}; do
	BENCH=./test_pt
	mkdir -p "$OUTDIR"

	  "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH

	cat "$OUTDIR"/$BENCH >> combined.txt
	value=$(cat "$OUTDIR/$BENCH")
	sum=$(( sum + value ))

done
echo "total cycles for pt: $sum" >> combined.txt
