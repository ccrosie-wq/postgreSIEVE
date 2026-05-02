#!/bin/bash
PG_DATA_HOME=/mnt/Storage/pgsql/data
rm -r $PG_DATA_HOME
initdb -D $PG_DATA_HOME
./launch_db.sh
pgbench -i -s 1000
psql -f pgscripts/disable_autovacuum.sql

