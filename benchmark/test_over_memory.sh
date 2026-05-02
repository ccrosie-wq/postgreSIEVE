#!/bin/bash
MEM_SIZES=(900)
# MEM_SIZES=(450 1350 2250 2700 3150)
CLIENTS=12 # two clients
THREADS=6


for size in "${MEM_SIZES[@]}"; do
    ./bench.sh -r 1 -u 3 -t 180 -b "$size" -d uniform -c "$CLIENTS" -p $THREADS
    ./bench.sh -r 3 -u 1 -t 180 -b "$size" -d uniform -c "$CLIENTS" -p $THREADS
    mkdir -p "outputs/clock/uni_$size"
    mv outputs/*.txt "outputs/clock/uni_$size"

    ./bench.sh -r 1 -u 3 -t 180 -b "$size" -d zipfian -a 1.01 -c "$CLIENTS" -p $THREADS
    ./bench.sh -r 3 -u 1 -t 180 -b "$size" -d zipfian -a 1.01 -c "$CLIENTS" -p $THREADS
    mkdir -p "outputs/clock/zipf_$size"
    mv outputs/*.txt "outputs/clock/zipf_$size"
done