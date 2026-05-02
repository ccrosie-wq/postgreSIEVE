PostgreSIEVE
============

*Authors: Ryan Crosier, Will Garlington, Jackson Gilstrap*

This codebase is a fork of Postgres implementing several bufferpool eviction policies.

To get started, follow the [official installation instructions](https://www.postgresql.org/docs/devel/installation.html)

    IMPORTANT: To run benchmarks with the scripts as given, YOU MUST have a postgres user as described in the installation instructions.
    
For our experiments, we set up the WAL on a separate device than the actual data. Follow [this post](https://postgresqldba-am.blogspot.com/2024/08/putting-pgwal-on-separate-device.html) to do the same on your system if you have multiple drives available.

## Changing Policies

The main contribution in this codebase is the ability to change bufferpool eviction policies. Changing policies requires recompiling the codebase.

To change the policy, you must:

- Stop Postgres (use the `benchmark/collection_scripts/stop_db.sh`) script
- Change the active eviction policy in the `src/backend/storage/buffer/freelist.c` file. Go to line 1282, comment out the currently selected eviction policy (called `ActiveEviction`), and uncomment the policy you'd like to use. 
- Rebuild using `make` and `sudo make install` in the root directory.
- Start Postgres (use the `benchmark/collection_scripts/launch_db.sh`) script

## Running Experiments and Benchmarks

See the `benchmark/README.md` file. The contents of the benchmark folder are used to run all experiments.


PostgreSQL Database Management System
=====================================

This directory contains the source code distribution of the PostgreSQL
database management system.

PostgreSQL is an advanced object-relational database management system
that supports an extended subset of the SQL standard, including
transactions, foreign keys, subqueries, triggers, user-defined types
and functions.  This distribution also contains C language bindings.

Copyright and license information can be found in the file COPYRIGHT.

General documentation about this version of PostgreSQL can be found at
<https://www.postgresql.org/docs/devel/>.  In particular, information
about building PostgreSQL from the source code can be found at
<https://www.postgresql.org/docs/devel/installation.html>.

The latest version of this software, and related software, may be
obtained at <https://www.postgresql.org/download/>.  For more information
look at our web site located at <https://www.postgresql.org/>.
