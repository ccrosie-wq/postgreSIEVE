#!/bin/bash
set -e # exit if failed

# user check
if [[ $(whoami) != "postgres" ]]; then
    echo "Must be postgres user to run!"
    exit 1
fi

# start db
if [[ $(/usr/local/pgsql/bin/pg_isready) != 0 ]]; then
    echo "PostgreSQL not running, attempting to start..."
    /usr/local/pgsql/bin/pg_ctl -D /usr/local/pgsql/data -l logfile start
fi

echo "PostgreSQL Started"
