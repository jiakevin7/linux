#!/bin/bash

set -e
rm -f combined.txt perf_output.txt

# Events to measure (use "perf list" to confirm exact names on your system)
PERF_EVENTS="dtlb_misses.walk_active,cycles"

echo "Running Control (test_control)" >> perf_output.txt
echo "------------------------------" >> perf_output.txt

# --- CONTROL GROUP ---
sum=0
for i in {1..1000}; do
    BENCH=./test_control
    OUTDIR=results0
    mkdir -p "$OUTDIR"

    # Use perf stat to collect hardware counters
    # The output from perf stat is redirected to perf_output.txt
    # The benchmark's cycle output is still captured via tee
    perf stat -e "$PERF_EVENTS" -r 1 "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH
    
    cat "$OUTDIR"/$BENCH >> combined.txt
    # Extract the single cycle count printed by the C program
    value=$(tail -n 1 "$OUTDIR/$BENCH")
    sum=$(( sum + value ))
    
done
echo "total cycles for control: $sum" >> combined.txt

# --- PT PREFETCHER GROUP ---
echo "" >> perf_output.txt
echo "Running Page-Table Prefetcher (test_pt)" >> perf_output.txt
echo "---------------------------------------" >> perf_output.txt
sum=0
for i in {1..1000}; do
    BENCH=./test_pt
    OUTDIR=results0
    mkdir -p "$OUTDIR"

    perf stat -e "$PERF_EVENTS" -r 1 "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH

    cat "$OUTDIR"/$BENCH >> combined.txt
    value=$(tail -n 1 "$OUTDIR/$BENCH")
    sum=$(( sum + value ))

done
echo "total cycles for pt: $sum" >> combined.txt

# --- PTE WARMER GROUP ---
echo "" >> perf_output.txt
echo "Running PTE Warmer (test_warm)" >> perf_output.txt
echo "------------------------------" >> perf_output.txt
sum=0
for i in {1..1000}; do
    BENCH=./test_warm
    OUTDIR=results0
    mkdir -p "$OUTDIR"

    perf stat -e "$PERF_EVENTS" -r 1 "$BENCH" 2>&1 | tee "$OUTDIR"/$BENCH
    
    cat "$OUTDIR"/$BENCH >> combined.txt
    value=$(tail -n 1 "$OUTDIR/$BENCH")
    sum=$(( sum + value ))

done
echo "total cycles for warm: $sum" >> combined.txt