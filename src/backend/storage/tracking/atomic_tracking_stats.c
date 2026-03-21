/*-------------------------------------------------------------------------
 *
 * atomic_tracking_stats.c
 * 
 *	  Implementation for functions that sre defined in atomic_tracking_stats.h
 *
 * IDENTIFICATION
 *	  src/backend/storage/tracking/atomic_tracking_stats.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "tracking/atomic_tracking_stats.h"

AtomicStats *AtomicStatsPointer = NULL;


void
RecordAtomicCacheHit(void)
{
    if (AtomicStatsPointer == NULL) return;
    pg_atomic_fetch_add_u64(&AtomicStatsPointer->atomic_cache_hits, 1);
    
};

void
RecordAtomicCacheMiss(uint64 count)
{
    if (AtomicStatsPointer == NULL) return;
    pg_atomic_fetch_add_u64(&AtomicStatsPointer->atomic_cache_misses, count);
};


double 
GenAtomicStats(void)
{

    // Implement the logic that calculates each metric for cache ratio, latency, and throughput
    //this is being hooking into a c-function to be called by sql 

    uint64 hits;
    uint64 misses;

    uint64 total;

    hits = pg_atomic_read_u64(&AtomicStatsPointer->atomic_cache_hits);
    misses = pg_atomic_read_u64(&AtomicStatsPointer->atomic_cache_misses);
    // guard against dividing by zero
    total = hits + misses;
    if (total == 0) return 0.0;
    return (double)hits / (double)total;
    

};

