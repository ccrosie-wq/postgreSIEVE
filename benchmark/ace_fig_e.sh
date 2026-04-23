#!/bin/bash
DB_SIZE=15000
SIZES=(300 600 900 1200 1500 3000 7500 15000)

client=12
thread=6

for size in "${SIZES[@]}"; do
    ./bench.sh -r 1 -u 1 -t 180 -b "$size" -d zipfian -c "$client" -a 1.01 -p $thread
    mkdir -p "outputs/ace_fig_e/clock/percent_$size"
    mv outputs/*.txt "outputs/ace_fig_e/clock/bp_$size"
done

