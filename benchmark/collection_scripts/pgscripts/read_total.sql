SELECT 
    sum(heap_blks_read) + sum(heap_blks_hit) AS read_total
FROM 
    pg_statio_user_tables;
