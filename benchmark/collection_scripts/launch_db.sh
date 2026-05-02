#!/bin/bash
set -e # exit if failed
PG_DATA_HOME=/mnt/Storage/pgsql/data
# start db
if [[ $(/usr/local/pgsql/bin/pg_isready) != 0 ]]; then
    echo "PostgreSQL not running, attempting to start..."
    /usr/local/pgsql/bin/pg_ctl -D $PG_DATA_HOME -l logfile start
fi

echo "PostgreSQL Started"
