#!/bin/bash

# 128 mb testing
./uni_bench.sh -r 1 -u 1 -t 180 -b 128
./uni_bench.sh -r 1 -u 9 -t 180 -b 128
./uni_bench.sh -r 9 -u 1 -t 180 -b 128
mkdir outputs/uni_128
mv outputs/*.txt outputs/uni_128/

./zipf_bench.sh -r 1 -u 1 -t 180 -b 128
./zipf_bench.sh -r 1 -u 9 -t 180 -b 128
./zipf_bench.sh -r 9 -u 1 -t 180 -b 128
mkdir outputs/zipf_128
mv outputs/*.txt outputs/zipf_128/

# 8 mb testing
./uni_bench.sh -r 1 -u 1 -t 180 -b 8
./uni_bench.sh -r 1 -u 9 -t 180 -b 8
./uni_bench.sh -r 9 -u 1 -t 180 -b 8
mkdir outputs/uni_8
mv outputs/*.txt outputs/uni_8/

./zipf_bench.sh -r 1 -u 1 -t 180 -b 8
./zipf_bench.sh -r 1 -u 9 -t 180 -b 8
./zipf_bench.sh -r 9 -u 1 -t 180 -b 8
mkdir outputs/zipf_8
mv outputs/*.txt outputs/zipf_8/
