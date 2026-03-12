/*------------------------------------------------------------------

locking_tracking_stats.h 

   - Type defintions for tracking the stats for SIEVE Project for using locks

src/backend/storage/tracking/locking_stats_tracking.h
---------------------------------------------------------------------*
*/


#ifndef LockingStats_H
#define LockingStats_H



// need to include the locks needed below
#include "c.h";
#include "storage/lwlock.h";


typedef struct LockingStats
{
   // LW lock
   LWLock      *metricLock;
   uint64_t    lock_cache_hits;
   uint64_t    lock_cache_misses;
   uint64_t    lock_throughput;
   uint64_t    lock_latency;



} LockingStats;

// global pointer to the locking stats

extern LockingStats *LockingStatsPointer;

extern void GenerateCacheHitRatio(void);
extern void GenerateThroughputNum(void);
extern void GenerateLatencyNum(void);
extern void GenerateLockingStats(void);




#endif