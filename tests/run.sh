#!/bin/bash

set -e
TEMP=temp.txt
TEST=test_first_touch

for BENCH in control pt_warm pte_prefetch pte_prefetch_load
do
	sum=0
	> "$BENCH.txt"
	for i in {1..1000}; do
		PT_MODE=$BENCH ./$TEST 2>&1 | tee "$TEMP"

		cat "$TEMP" >> "$BENCH.txt"
		value=$(cat "$TEMP")
		sum=$(( sum + value ))

	done
	echo "total cycles for $BENCH: $sum" >> "$BENCH.txt"
done

rm -rf temp.txt
