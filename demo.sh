#!/bin/bash

echo "================="
echo "PostgreSIEVE Demo"
echo "================="

echo "Enter to view freelist.c..."
read
less $(find . -name "freelist.c")

echo "Enter to view bufmgr.c..."
read
less $(find . -name "bufmgr.c")

cd benchmark

echo "Enter to start the DB..."
read
./launch_db.sh

echo "Enter to run a pgbench example..."
read
./uni_bench.sh

echo "Enter to stop the DB..."
read
./stop_db.sh
