SELECT 
    sum(heap_blks_hit) AS read_hits
FROM 
    pg_statio_user_tables;
