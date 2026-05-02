#!/bin/bash
WORKING_SET_SIZE=(1000) # about 6 percent of data size (900=15*.06)
CLIENTS=(12) # two clients
THREADS=6

mkdir -p outputs/ace_fig_c/sieve

for size in "${WORKING_SET_SIZE[@]}"; do
    for client in "${CLIENTS[@]}"; do
        ./bench.sh -r 0 -u 10 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 1 -u 9 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 2 -u 8 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 3 -u 7 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 4 -u 6 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 5 -u 5 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 6 -u 4 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 7 -u 3 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 8 -u 2 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 9 -u 1 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        ./bench.sh -r 10 -u 0 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $THREADS
        mv outputs/*.txt "outputs/ace_fig_c/sieve"
    done
done

