#!/bin/bash

input_path="outputs"
output_path="charts"
dists=( "uni" "zipf" )
mems=( 900 1800 3600 )

for size in "${mems[@]}"; do
    for dist in "${dists[@]}"; do
        python generate_plot.py "$input_path/sieve/${dist}_$size $input_path/clock/${dist}_$size $input_path/lru/${dist}_$size $input_path/lfu/${dist}_$size" "$output_path/${dist}_$size" "$dist with $size MB BP"
    done
done
