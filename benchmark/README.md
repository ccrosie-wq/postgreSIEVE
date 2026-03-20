# Benchmarking Tools

Scripts for launching the DB and collecting data using `pgbench`.

## Benchmarking Scripts

- `launch_db.sh`: launch database (if not already running) using the DB location "/usr/local/pgsql/data"
- `stop_db.sh`: stop database using the DB location "/usr/local/pgsql/data"
- `zipf_bench.sh` run pgbench using the `zipfian` scripts under the "pgscripts" directory. These scripts draw from a zipfian (skewed) distribution that is parameterized by an alpha, positively correlated with skewedness. The script takes a read weight, write weight, and alpha parameter.
- `set_buffersize.sh`: Use the `-b` option in zipf_bench instead!

## Data Aggregation

- `generate_plot.py`: given the output from a pgbench run, generate plots comparing the throughput for different read/write ratios

## Scripts for pgbench

- `zipfian_select.sql/zipfian_update.sql` - simple select and update transactions that draw data from a skewed zipfian distribution
