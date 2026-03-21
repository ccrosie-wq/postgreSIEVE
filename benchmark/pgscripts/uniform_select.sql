-- Select uniform data

-- Constants (depends on values found in src/bin/pgbench/pgbench.c)
\set nbranches	1
\set ntellers	10
\set naccounts	100000

-- Transaction Variables
\set aid random(1, :naccounts * :scale)

-- Execute Transaction (same as built-in simple-update)
BEGIN;
SELECT abalance FROM pgbench_accounts WHERE aid = :aid;
END;
