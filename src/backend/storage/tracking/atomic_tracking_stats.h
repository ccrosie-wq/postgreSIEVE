/*------------------------------------------------------------------

atomic_tracking_stats.h 

   - Type defintions for tracking the stats for SIEVE Project for when we do not want the overhead of the locks utilizing atomics

src/backend/storage/tracking/atomic_tracking_stats.h
---------------------------------------------------------------------*

*/

#ifndef AtomicStats_H;
#define AtomicStats_H;


#include "c.h";
#include "port/atomics.h";


typedef struct AtomicStats
{
    pg_atomic_uint64        atomic_cache_hits;
    pg_atomic_uint64        atomic_cache_misses;       
    pg_atomic_uint64        atomic_throughput;
    pg_atomic_uint64        atomic_latency;

    
}AtomicStats;


extern AtomicStats *AtomicStatsPointer;

extern void GenerateAtomicCacheHitRatio(void);
extern void GenerateAtomicThroughputNum(void);
extern void GenerateAtomicLatencyNum(void);
extern void GenerateAtomicStats(void);



#endif



