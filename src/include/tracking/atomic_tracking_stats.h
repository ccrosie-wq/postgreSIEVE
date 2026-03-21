/*------------------------------------------------------------------

atomic_tracking_stats.h 

   - Type defintions for tracking the stats for SIEVE Project for when we do not want the overhead of the locks utilizing atomics

src/backend/storage/tracking/atomic_tracking_stats.h
---------------------------------------------------------------------*

*/

#ifndef AtomicStats_H
#define AtomicStats_H

#include "postgres.h"
#include "c.h"
#include "port/atomics.h"



typedef struct AtomicStats
{
    pg_atomic_uint64        atomic_cache_hits;
    pg_atomic_uint64        atomic_cache_misses;       

    
}AtomicStats;


extern AtomicStats *AtomicStatsPointer;

/*
* APIs to be called by the internals
*/

// extern void RecordAtomicCacheHitRatio(void);
extern void RecordAtomicCacheHit(void);
extern void RecordAtomicCacheMiss(uint64 count);

extern double GenAtomicStats(void);



#endif



