# Benchmarking Tools

Scripts for launching the DB and collecting data using `pgbench`.

## Scripts
- `launch_db.sh`: launch database (if not already running) using the DB location "/usr/local/pgsql/data"
- `stop_db.sh`: stop database using the DB location "/usr/local/pgsql/data"
- `zipf_bench.sh` run pgbench using the `zipfian` scripts under the "pgscripts" directory. These scripts draw from a zipfian (skewed) distribution that is parameterized by an alpha, positively correlated with skewedness. The script takes a read weight, write weight, and alpha parameter.

