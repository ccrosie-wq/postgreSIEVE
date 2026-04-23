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
- `uniform_select.sql/uniform_.sql` - simple select and update transactions that draw data from a uniform distribution. configurable size of working set.

# Figure Generation Methodologies

- ACE Figure 10C: Set BP Size = 900 MB which should be about 6 percent of 15 GB. Set alpha = 1.01 in zipfian distribution to approximate a 90/10 data locality, such that the working set is only slightly larger than the BP (1.5GB vs 0.9 GB). Vary the r/w ratio by using pgbench weights. Run each test for 3 minutes on each implementation.

- ACE Figure 10E:

- SIEVE Figure 6: Set BP Size = working set size = unit set size * number of clients. In this case, we use a 90/10 locality w/ a 9/1 r/w ratio.
