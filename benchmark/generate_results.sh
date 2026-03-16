#!/bin/bash
# Generate line charts from pgbench results.
# Author: Ryan Crosier

INPUT_DIR='./outputs'
OUTPUT_DIR='./results'

# ensure output dir exists
mkdir -p $OUTPUT_DIR

# retrieve clients, latency, and transaction thruput
for file in $(ls $INPUT_DIR); do
	echo $file
	tmp=$(mktemp)
	# awk '/(clients)|(latency)|(tps)/' $file > $tmp

	# format as table and write to file
	table_out="table_$file"

	# use python to generate plots
	plot_out="plot_${file%.txt}.png"
	# python generate_plot.py $table_out $plot_out

	# clean up temp file
	rm $tmp
done

