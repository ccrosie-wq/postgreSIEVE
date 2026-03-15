-- Select skewed data drawn from zipfian distribution (https://en.wikipedia.org/wiki/Zipf%27s_law)

-- Constants (depends on values found in src/bin/pgbench/pgbench.c)
\set nbranches	1
\set ntellers	10
\set naccounts	100000
\set alpha 1.5

-- Transaction Variables
\set aid random_zipfian(1, :naccounts * :scale, :alpha)

-- Execute Transaction (same as built-in simple-update)
BEGIN;
SELECT abalance FROM pgbench_accounts WHERE aid = :aid;
END;
