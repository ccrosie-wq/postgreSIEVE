#!/bin/bash
# Baseline Benchmarks
alphas=(
	"1.5"
	"3"
	"6"
)
for a in "${alphas[@]}"; do
	./zipf_bench.sh -r 1 -u 1 -t 180 -a $a
	./zipf_bench.sh -r 1 -u 9 -t 180 -a $a
	./zipf_bench.sh -r 9 -u 1 -t 180 -a $a
done

