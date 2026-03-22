#!/bin/bash

PGCONF="/usr/local/pgsql/data/postgresql.conf"

m="128"

while getopts ":m:" opt; do
  case $opt in
    m) m="$OPTARG" ;;
    \?) echo "Invalid option: -$OPTARG" >&2
        echo "Usage:"
        echo "    -m megabytes (default 128)"
        exit 1
        ;;
  esac
done

# replace buffer size in postgres configuration
tmp=$(mktemp)
sed "s/^\(shared_buffers =\).*/\1 ${m}MB/" $PGCONF > "$tmp"
mv "$tmp" $PGCONF

pg_isready
if pg_isready; then
    echo "Postgres running, reloading conf..."
    /usr/local/pgsql/bin/pg_ctl -D /usr/local/pgsql/data reload
fi
