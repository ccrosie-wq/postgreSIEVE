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

#include "atomic_tracking_stats.h"
#include "postgres.h"

AtomicStats *AtomicStatsPointer = NULL;

void 
GenerateAtomicCacheHitRatio(void)
{
    // Implement the logic to calculate and update the cache hit ratio using atomic_cache_hits and atomic_cache_misses
};

void 
GenerateAtomicThroughputNum(void)
{
    // Implement the logic to calculate and update the throughput number using atomic_throughput
};

void 
GenerateAtomicLatencyNum(void)
{
    // Implement the logic to calculate and update the latency number using atomic_latency
};

void 
GenerateAtomicStats(void)
{
    // Implement the logic that calculates each metric for cache ratio, latency, and throughput
    // Write a small sql query that returns a table of the metrics 
};

