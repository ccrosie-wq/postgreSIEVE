# Benchmarking Tools

Scripts for launching the DB and collecting data using `pgbench`.

Prerequisites:
- You have already followed the README in the root directory, built and installed postgres, and created the postgres user on your system
- `postgres` user has read/write/execute permissions on the postgres data directory
- You have updated `collection_scripts/settings.env` to match your installation. This should be correct by default if you followed the official tutorial.

To run any of the scripts outlined in the next section, run the following **as postgres user**:

```bash
$ cd collection_scripts
$ ./<script_name>.sh
```

Each script will output the results of each individual experiment in a separate file (these files will be reported when the script completes). The format of output is the `pgbench` output format with the cache hit ratio added.

# Collection Scripts Summary

The main script used for benchmarking is `bench.sh`:

```bash
Usage:
    -r read_weight (default 1)
    -u update_weight (default 1)
    -t time (s) (default 10)
    -a alpha (default 1.5, only for zipfian distribution)
    -b buffer_size (MB) (default 128)
    -d distribution (default zipfian)
    -c clients (default 12)
    -w working_set_ratio (default 1, only for uniform distribution)
    -p threads (default 2)
```

All the experiments invoke different configurations of this script to collect results. Our experiment scripts are these:
- `ace_fig_c.sh`: Collect data for figure 10C of ACEing the Bufferpool.
- `ace_fig_e.sh`: Collect data for figure 10E of ACEing the Bufferpool.
- `ace_fig_e_uniform.sh`
- `sieve_thruput_bench.sh`: create figure 6 from the SIEVE paper
- `make_mem_charts.sh`: Collect data on every (given) permutation of memory sizes, r/w ratios, and query distributions.

## Other Benchmarking Scripts

These scripts are used by `bench.sh` to edit database configuration.

- `init.sh`: initialize pgbench
- `launch_db.sh`: launch database (if not already running) using the DB location "/usr/local/pgsql/data"
- `stop_db.sh`: stop database using the DB location "/usr/local/pgsql/data"
- `set_buffersize.sh`: set the size of the buffercache and restart the DB

## Transaction Scripts (stored in pgscripts dir)

These are the scripts that are used by `bench.sh` to perform queries and collect statistics.

- `zipfian_select.sql/zipfian_update.sql` - simple select and update transactions that draw data from a skewed zipfian distribution
- `uniform_select.sql/uniform_.sql` - simple select and update transactions that draw data from a uniform distribution. configurable size of working set.
- `disable_autovacuum.sql/vacuum.sql` - these scripts disable postgres auto-vacuuming for the pgbench standard tables, and manually vacuum said tables respectively
- `read_hits.sql/read_total.sql` - Read the number of buffercache hits and total buffercache accesses respectively. Used in tandem to calculate the hit/miss ratio for a given setup.
- `reset_stats.sql` - clear the pgstats table used for reading hit ratio

## Chart Generation Scripts

These are the scripts used to turn raw output into bar charts. Not necessary for reproducing results.

- `generate_plot.py`: given the output from a pgbench run, generate plots comparing the throughput for different read/write ratios

    ```bash
    usage: generate_plot.py [-h] input output title

    positional arguments:
      input       location of input files
      output      directory to output files to
      title       chart title

    options:
      -h, --help  show this help message and exit
    ```

# Miscellaneous Settings

For some experiments in our project, we did not script required the changes because we did not need to do them often. These changes are:
- Disable all disk accesses during benchmark: change `fsync` to "off" in your `postgres.conf` file and restart the database.
