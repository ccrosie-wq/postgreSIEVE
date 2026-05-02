#!/bin/bash

source "settings.env"

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
sed "s/^\(shared_buffers =\).*/\1 ${m}MB/" "$PG_CONF" > "$tmp"
mv "$tmp" "$PG_CONF"

if pg_isready; then
    echo "Postgres running, reloading conf..."
    ./stop_db.sh
    ./launch_db.sh
fi
