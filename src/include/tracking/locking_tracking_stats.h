/*------------------------------------------------------------------

locking_tracking_stats.h 

   - Type defintions for tracking the stats for SIEVE Project for using locks

src/backend/storage/tracking/locking_stats_tracking.h
---------------------------------------------------------------------*
*/


#ifndef LockingStats_H
#define LockingStats_H



// need to include the locks needed below
#include "postgres.h"
#include "c.h"
#include "storage/lwlock.h"


typedef struct LockingStats
{
   // LW lock
   LWLock      *metricLock; // This is the lightweight lock
   uint64      lock_cache_hits;
   uint64      lock_cache_misses;
   uint64      lock_throughput; // number of transactions
   uint64      lock_latency; // culumlative latency;



} LockingStats;

// global pointer to the locking stats

extern LockingStats *LockingStatsPointer;

/*
* APIs to be called by the internals
*/
extern void RecordCacheHitRatio(uint64 hits, uint64 misses);
extern void RecordLatency(uint64 latency);


/*
* APIs to be called by the SQL queries to provide the final output
*/
extern void GetNumOfTranscations(uint64 throughput);
extern void GetLockingMetrics(uint64 cache_ratio, uint64 throughput, uint64 latency);



#endif