/*-------------------------------------------------------------------------
 *
 * sieve_stats.c
 * 
 *	  Implementation for C functions that sieve_stats.h that will be called by the sql query
 *
 * IDENTIFICATION
 *	  contrib/sieve_stats/sieve_stats.c
 *
 *-------------------------------------------------------------------------
 */

 #include "postgres.h"
 #include "fmgr.h"
 #include "storage/shmem.h"
 #include "storage/ipc.h"
 #include "port/atomics.h"

PG_MODULE_MAGIC;

typedef struct AtomicStats
{
    pg_atomic_uint64    atomic_cache_hits;
    pg_atomic_uint64    atomic_cache_misses;
} AtomicStats;

static AtomicStats      *stats_ptr = NULL;
static shmem_startup_hook_type  prev_shmem_startup_hook = NULL;

static void
sieve_stats_shmem_startup(void)
{
    bool found;

    if(prev_shmem_startup_hook)
        prev_shmem_startup_hook();
    stats_ptr = (AtomicStats *)
        ShmemInitStruct("Atomic Tracking Stats",
        sizeof(AtomicStats),
    &found);
}

void _PG_init(void);
void
_PG_init(void)
{
    prev_shmem_startup_hook = shmem_startup_hook;
    shmem_startup_hook = sieve_stats_shmem_startup;
}

PG_FUNCTION_INFO_V1(get_atomic_hit_ratio);
Datum
get_atomic_hit_ratio(PG_FUNCTION_ARGS)
{
    uint64  hits;
    uint64  misses;
    uint64  total;

    if (stats_ptr == NULL)
          elog(ERROR, "sieve_stats: not in shared_preload_libraries");

      hits   = pg_atomic_read_u64(&stats_ptr->atomic_cache_hits);
      misses = pg_atomic_read_u64(&stats_ptr->atomic_cache_misses);
      total  = hits + misses;

      if (total == 0)
          PG_RETURN_FLOAT8(0.0);

      PG_RETURN_FLOAT8((double)hits / (double)total);
}
