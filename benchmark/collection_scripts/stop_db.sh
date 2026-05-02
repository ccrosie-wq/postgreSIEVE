#!/bin/bash
source "settings.env"

pg_ctl -D "$PG_DATA_HOME" -l "$PG_LOGFILE" stop
