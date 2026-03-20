/*-------------------------------------------------------------------------
 *
 * locking_tracking_stats.c
 * 
 *	  Implementation for functions that sre defined in locking_tracking_stats.h
 *
 * IDENTIFICATION
 *	  src/backend/storage/tracking/locking_tracking_stats.c
 *
 *-------------------------------------------------------------------------
 */


#include "locking_tracking_stats.h"
#include "postgres.h"

LockingStats *LockingStatsPointer = NULL;


void
GenerateLockingCacheRatio(void)
{

    // Implement the logic for calculating the cache ratio
    // First idea of how to achieve this
    // Get lock
    // Call hook into the local  memory BufferUsage to get the hit number
    // Calculate ratio (somewhere in here save to a table )
    // Release lock
    // Return number
}


void
GenerateLockingThroughput(void)
{
    // Implement the logic for calculating the throughput
    // Get lock
    // calculte throughput number(somewhere in here save to a table )
    // release lock 
    // return 
}

void 
GenerateLockingLatency(void)
{
    // Implement the logic for calculating the latency
    // Get lock
    // Calculate latency (somewhere in here save to a table )
    // Release lock
    // return 
}

void
GenerateLockingStats(void)
{
    // Implement the logic that calculates each metric for cache ratio, latency, and throughput
    // Write a small sql query that returns a table of the metrics 
}
