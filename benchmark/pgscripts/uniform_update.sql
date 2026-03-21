-- Update data drawn from uniform distribution

-- Constants (depends on values found in src/bin/pgbench/pgbench.c)
\set nbranches	1
\set ntellers	10
\set naccounts	100000

-- Transaction Variables
\set aid random(1, :naccounts * :scale)
\set bid random(1, :nbranches * :scale)
\set tid random(1, :ntellers * :scale)
\set delta random(-5000, 5000)

-- Execute Transaction (same as the built-in simple-update)
BEGIN;
UPDATE pgbench_accounts SET abalance = abalance + :delta WHERE aid = :aid;
SELECT abalance FROM pgbench_accounts WHERE aid = :aid;
INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES (:tid, :bid, :aid, :delta, CURRENT_TIMESTAMP);
END;
