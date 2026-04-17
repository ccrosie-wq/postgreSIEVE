#!/bin/bash
MEM_SIZES=(2 4 8 16 32 64 128)

for size in "${MEM_SIZES[@]}"; do
    ./bench.sh -r 1 -u 1 -t 180 -b "$size" -d uniform
    ./bench.sh -r 1 -u 9 -t 180 -b "$size" -d uniform
    ./bench.sh -r 9 -u 1 -t 180 -b "$size" -d uniform
    mkdir "outputs/uni_$size"
    mv outputs/*.txt "outputs/uni_$size"

    ./bench.sh -r 1 -u 1 -t 180 -b "$size" -d zipfian
    ./bench.sh -r 1 -u 9 -t 180 -b "$size" -d zipfian
    ./bench.sh -r 9 -u 1 -t 180 -b "$size" -d zipfian
    mkdir "outputs/zipf_$size"
    mv outputs/*.txt "outputs/zipf_$size"
done

