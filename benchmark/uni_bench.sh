#!/bin/bash
# Accept outputs of pgbench and aggregate into CSV that can be formatted into graphs and tables

SCALE_FACTOR="1000"
CLIENTS="12"

read_weight=1
update_weight=1
time=10

while getopts ":r:u:t:" opt; do
  case $opt in
    u) update_weight="$OPTARG" ;;
    r) read_weight="$OPTARG" ;;
    t) time="$OPTARG" ;;
    \?) echo "Invalid option: -$OPTARG" >&2
        echo "Usage:"
        echo "    -r read_weight (default 1)"
        echo "    -u update_weight (default 1)"
        echo "    -t time (s) (default 10)"
        exit 1
        ;;
  esac
done

# run the benchmark
bench_file=outputs/$(date -Iseconds)_${read_weight}_${update_weight}.txt
pgbench -c ${CLIENTS} -s ${SCALE_FACTOR} -T "${time}" -f pgscripts/uniform_select.sql@"${read_weight}" -f pgscripts/uniform_update.sql@"${update_weight}" >> "$bench_file"
