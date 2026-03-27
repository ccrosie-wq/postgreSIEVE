#!/bin/bash
# Accept outputs of pgbench and aggregate into CSV that can be formatted into graphs and tables

SCALE_FACTOR="1000"
THREADS="2"
CLIENTS="12"

read_weight=1
update_weight=1
time=10
buffer_size=128
alpha=1.5

while getopts ":r:u:t:a:b:" opt; do
  case $opt in
    u) update_weight="$OPTARG" ;;
    r) read_weight="$OPTARG" ;;
    t) time="$OPTARG" ;;
    a) alpha="$OPTARG" ;;
    b) buffer_size="$OPTARG" ;;
    \?) echo "Invalid option: -$OPTARG" >&2
        echo "Usage:"
        echo "    -r read_weight (default 1)"
        echo "    -u update_weight (default 1)"
        echo "    -t time (s) (default 10)"
        echo "    -a alpha (default 1.5)"
        echo "    -b buffer_size (MB) (default 128)"
        exit 1
        ;;
  esac
done

# set buffer size
./set_buffersize.sh -m "$buffer_size"

# apply alpha
tmp=$(mktemp)
alp=$alpha awk '/\\set alpha/ {$0="\\set alpha "ENVIRON["alp"]""; print; next} {print}' pgscripts/zipfian_select.sql > "$tmp" && mv "$tmp" pgscripts/zipfian_select.sql
alp=$alpha awk '/\\set alpha/ {$0="\\set alpha "ENVIRON["alp"]""; print; next} {print}' pgscripts/zipfian_update.sql > "$tmp" && mv "$tmp" pgscripts/zipfian_update.sql
chmod -R g+rwx pgscripts/* # ensure permissions are retained so users can still read the file

# Get Initial Misses + Hits
hits_init=$(psql --csv -f pgscripts/read_hits.sql postgres | awk 'NR==2')
total_init=$(psql --csv -f pgscripts/read_total.sql postgres | awk 'NR==2')

# run the benchmark
bench_file=outputs/$(date -Iseconds)_${read_weight}_${update_weight}_${alpha}.txt
pgbench -c ${CLIENTS} -j ${THREADS} -s ${SCALE_FACTOR} -T "${time}" -f pgscripts/zipfian_select.sql@"${read_weight}" -f pgscripts/zipfian_update.sql@"${update_weight}" postgres >> "$bench_file"

# Get Ending Misses + Hits
hits_after=$(psql --csv -f pgscripts/read_hits.sql postgres | awk 'NR==2')
total_after=$(psql --csv -f pgscripts/read_total.sql postgres | awk 'NR==2')

hits_delta=$( echo "$hits_after-$hits_init"| bc)
total_delta=$( echo "$total_after-$total_init"| bc)
ratio=$(python3 -c "print($hits_delta/$total_delta)" | bc)
echo "ratio: $ratio" >> "$bench_file"
