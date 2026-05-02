#!/bin/bash
source "settings.env"
set -e # exit if failed

# start db
if [[ $("${PG_BIN}"/pg_isready) != 0 ]]; then
    echo "PostgreSQL not running, attempting to start..."
    "${PG_BIN}/pg_ctl" -D "$PG_DATA_HOME" -l "$PG_LOGFILE" start
fi

echo "PostgreSQL Started"
