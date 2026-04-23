#!/bin/bash
MEM_UNIT=900
THREADS=(1 2 4 8 16)
CLIENTS_PER_THREAD=2

for threads in "${THREADS[@]}"; do
	# size of buffer cache scales w/ number of clients and working set
	size=$(echo "$threads * $MEM_UNIT" | bc)
	echo "$size mb"

	# working set ratio should scale w/ # clients
	client=$(echo "$CLIENTS_PER_THREAD * $threads" | bc)
	w=0$(echo "0.1 * $threads" | bc)

	./bench.sh -r 1 -u 1 -t 180 -b "$size" -d uniform -c "$client" -w "$w" -p $threads

	mkdir -p "outputs/sieve_fig/sieve/${client}_clients_1_1"
	mv outputs/*.txt "outputs/sieve_fig/sieve/${client}_clients_1_1"
done

