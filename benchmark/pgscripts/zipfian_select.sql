-- Select skewed data drawn from zipfian distribution (https://en.wikipedia.org/wiki/Zipf%27s_law)

-- Constants (depends on values found in src/bin/pgbench/pgbench.c)
\set nbranches	1
\set ntellers	10
\set naccounts	100000

-- Transaction Variables
\set aid random_zipfian(1, :naccounts * :scale, 1.5) -- last parameter is alpha in zipfian distribution
\set bid random_zipfian(1, :nbranches * :scale, 1.5)
\set tid random_zipfian(1, :ntellers * :scale, 1.5)
\set delta random(-5000, 5000)

-- Execute Transaction (same as built-in simple-update)
BEGIN;
SELECT abalance FROM pgbench_accounts WHERE aid = :aid;
END;
