#!/bin/bash
source "./settings.env"

./launch_db.sh
pgbench -i -s 1000
psql -f pgscripts/disable_autovacuum.sql
