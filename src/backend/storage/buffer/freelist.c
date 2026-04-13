/*-------------------------------------------------------------------------
 *
 * freelist.c
 *	  routines for managing the buffer pool's replacement strategy.
 *
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/buffer/freelist.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgstat.h"
#include "port/atomics.h"
#include "storage/buf_internals.h"
#include "storage/bufmgr.h"
#include "storage/proc.h"

#define INT_ACCESS_ONCE(var)	((int)(*((volatile int *)&(var))))

typedef enum EvictionPolicy
{
    EVICTION_CLOCKSWEEP = 0,   // default
    EVICTION_SIEVE      = 1,   
	EVICTION_LFU		= 2,   
} EvictionPolicy;


/*
 * The shared freelist control information.
 */
typedef struct
{
	/* Spinlock: protects the values below */
	slock_t		buffer_strategy_lock;

	EvictionPolicy    active_policy;          // define policy
    uint32            completePasses;         //-- bgwriter: full traversal count
    pg_atomic_uint32  numBufferAllocs;        /* Buffers allocated since last reset */
    
	/*
	 * Bgworker process to be notified upon activity or -1 if none. See
	 * StrategyNotifyBgWriter.
	 */
	int               bgwprocno;             //-- bgwriter latch (-1 = none)
} BufferStrategyCommon;

/* Pointers to shared state */
static BufferStrategyCommon *StrategyControl = NULL;


//addition for clocksweep
typedef struct { 
	pg_atomic_uint32 nextVictimBuffer; 
} ClockSweepState;

static ClockSweepState *ClockSweepCtl = NULL;

typedef struct
{
    int32  sieve_hand;    // curr eviction hand (buf_id, NBuff == invalid/empty)
    // int32  sieve_head;    // head of 'list' - unused
	int32  bgw_sync_seq; //track sync pos for bgwriter
	// Next[] and Prev[] arrs follow
} SieveState;
static SieveState *SieveCtl  = NULL;
static int32      *SieveNext = NULL;
static int32      *SievePrev = NULL;


typedef struct 
{
	int32	lfu_hand;
	int32 	bgw_sync_seq; //track sync pos for bgwriter

	 // LFUFreq[] follow in shmem
} LFUState;

static LFUState *LFUCtl = NULL;
static int32    *LFUFreq = NULL;

// Need to make two more states one for LRU and one for LFU.

//hold passthrough refs for policy methods
typedef struct EvictionVtable
{
    BufferDesc *(*get_buffer)(BufferAccessStrategy, uint64 *);
    void        (*notify_hit)(BufferDesc *);        // cache hit
    void        (*notify_insert)(BufferDesc *);     // new page load
    void        (*notify_invalidate)(BufferDesc *); // inavalidate
    Size        (*shmem_size)(int n_buffers);
    void        (*initialize)(bool found);
} EvictionVtable;


//declare cswp funcs
static Size ClockSweepShmemSize(int n_buffers);
static void ClockSweepInitialize(bool found);
static BufferDesc *ClockSweepGetBuffer(BufferAccessStrategy strategy, uint64 *buf_state);

//declare sieve funcs
static Size SieveShmemSize(int n_buffers);
static void SieveInitialize(bool found);
static BufferDesc *SieveGetBuffer(BufferAccessStrategy strategy, uint64 *buf_state);
static void SieveNotifyInsert(BufferDesc *buf);
static void SieveNotifyInvalidate(BufferDesc *buf);

// declare lru funcs
// delcare lfu funcs

static Size LeastFrequentlyUsedShmemSize(int n_buffers);
static void LeastFrequentlyUsedInitialize(bool found);
static BufferDesc *LeastFrequentlyUsedGetBuffer(BufferAccessStrategy strategy, uint64 *buf_state);
static void LeastFrequentlyUsedInsert(BufferDesc *buf);
static void LeastFrequentlyUsedUpdate(BufferDesc *buf);
static void LeastFrequentlyUsedDelete(BufferDesc *buf);


//cswp specific refs
static const EvictionVtable ClockSweepVtable = {
    .get_buffer        = ClockSweepGetBuffer,
    .notify_hit        = NULL,        //no
    .notify_insert     = NULL,        //no
    .notify_invalidate = NULL,        //no
    .shmem_size        = ClockSweepShmemSize,
    .initialize        = ClockSweepInitialize,
};

//sieve refs
static const EvictionVtable SieveVtable = {
	.get_buffer        = SieveGetBuffer,
	.notify_hit        = NULL,           /* PinBuffer CAS bumps usage_count → visited=1 */
	.notify_insert     = SieveNotifyInsert,
	.notify_invalidate = SieveNotifyInvalidate,
	.shmem_size        = SieveShmemSize,
	.initialize        = SieveInitialize,
};

// LFU refs
static const EvictionVtable LFUVtable = {
	.get_buffer        = LeastFrequentlyUsedGetBuffer,      
	.notify_hit        = LeastFrequentlyUsedUpdate,
	.notify_insert     = LeastFrequentlyUsedInsert,
	.notify_invalidate = LeastFrequentlyUsedDelete,
	.shmem_size        = LeastFrequentlyUsedShmemSize,
	.initialize        = LeastFrequentlyUsedInitialize,
};

static const EvictionVtable *ActiveEviction = NULL; //set at strat init

/*
 * Private (non-shared) state for managing a ring of shared buffers to re-use.
 * This is currently the only kind of BufferAccessStrategy object, but someday
 * we might have more kinds.
 */
typedef struct BufferAccessStrategyData
{
	/* Overall strategy type */
	BufferAccessStrategyType btype;
	/* Number of elements in buffers[] array */
	int			nbuffers;

	/*
	 * Index of the "current" slot in the ring, ie, the one most recently
	 * returned by GetBufferFromRing.
	 */
	int			current;

	/*
	 * Array of buffer numbers.  InvalidBuffer (that is, zero) indicates we
	 * have not yet selected a buffer for this ring slot.  For allocation
	 * simplicity this is palloc'd together with the fixed fields of the
	 * struct.
	 */
	Buffer		buffers[FLEXIBLE_ARRAY_MEMBER];
}			BufferAccessStrategyData;


/* Prototypes for internal functions */
static BufferDesc *GetBufferFromRing(BufferAccessStrategy strategy,
									 uint64 *buf_state);
static void AddBufferToRing(BufferAccessStrategy strategy,
							BufferDesc *buf);



////////////////////////////////////////////////////////////
////////////CLOCK SWEEP FUNCS            ///////////////////
////////////////////////////////////////////////////////////


/*
 * ClockSweepTick - Helper routine for StrategyGetBuffer()
 * now for ClockSweepGetBuffer() <-
 * Move the clock hand one buffer ahead of its current position and return the
 * id of the buffer now under the hand.
 */
static inline uint32
ClockSweepTick(void)
{
	uint32		victim;

	/*
	 * Atomically move hand ahead one buffer - if there's several processes
	 * doing this, this can lead to buffers being returned slightly out of
	 * apparent order.
	 */
	victim = pg_atomic_fetch_add_u32(&ClockSweepCtl->nextVictimBuffer, 1);

	if (victim >= NBuffers)
	{
		uint32		originalVictim = victim;

		/* always wrap what we look up in BufferDescriptors */
		victim = victim % NBuffers;

		/*
		 * If we're the one that just caused a wraparound, force
		 * completePasses to be incremented while holding the spinlock. We
		 * need the spinlock so StrategySyncStart() can return a consistent
		 * value consisting of nextVictimBuffer and completePasses.
		 */
		if (victim == 0)
		{
			uint32		expected;
			uint32		wrapped;
			bool		success = false;

			expected = originalVictim + 1;

			while (!success)
			{
				/*
				 * Acquire the spinlock while increasing completePasses. That
				 * allows other readers to read nextVictimBuffer and
				 * completePasses in a consistent manner which is required for
				 * StrategySyncStart().  In theory delaying the increment
				 * could lead to an overflow of nextVictimBuffers, but that's
				 * highly unlikely and wouldn't be particularly harmful.
				 */
				SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

				wrapped = expected % NBuffers;

				success = pg_atomic_compare_exchange_u32(&ClockSweepCtl->nextVictimBuffer,
														 &expected, wrapped);
				if (success)
					StrategyControl->completePasses++;
				SpinLockRelease(&StrategyControl->buffer_strategy_lock);
			}
		}
	}
	return victim;
}


static Size //r
ClockSweepShmemSize(int n_buffers)
{
    return sizeof(ClockSweepState);
}

static void
ClockSweepInitialize(bool found)
{
	//ClockSweepCTL set in StrategyInitialize
    if (!found)
        pg_atomic_init_u32(&ClockSweepCtl->nextVictimBuffer, 0); //fancy =0
}


//select next victim, replaces StrategyGetBuffer() to target clock sweep
//most of the function remains unchanged, the top bit remains in the OG StratGetbuff
static BufferDesc *
ClockSweepGetBuffer(BufferAccessStrategy strategy, uint64 *buf_state)
{
    BufferDesc *buf;
    int         trycounter = NBuffers;

    for (;;)
    {
        uint64  old_buf_state;
        uint64  local_buf_state;

        buf = GetBufferDescriptor(ClockSweepTick());

        old_buf_state = pg_atomic_read_u64(&buf->state);
        for (;;)
        {
            local_buf_state = old_buf_state;

            if (BUF_STATE_GET_REFCOUNT(local_buf_state) != 0)
            {
                if (--trycounter == 0) 
				{
					/*
					 * We've scanned all the buffers without making any state
					 * changes, so all the buffers are pinned (or were when we
					 * looked at them). We could hope that someone will free
					 * one eventually, but it's probably better to fail than
					 * to risk getting stuck in an infinite loop.
					 */

                    elog(ERROR, "no unpinned buffers available");
				}
                break;
            }

            if (unlikely(local_buf_state & BM_LOCKED))
            {
                old_buf_state = WaitBufHdrUnlocked(buf);
                continue;
            }

            if (BUF_STATE_GET_USAGECOUNT(local_buf_state) != 0)
            {
                local_buf_state -= BUF_USAGECOUNT_ONE;
                if (pg_atomic_compare_exchange_u64(&buf->state,
												   &old_buf_state,
                                                   local_buf_state))
                {
                    trycounter = NBuffers;
                    break;
                }
            }
            else
            {
                local_buf_state += BUF_REFCOUNT_ONE;
                if (pg_atomic_compare_exchange_u64(&buf->state,
                                                   &old_buf_state,
                                                   local_buf_state))
                {
                    if (strategy != NULL)
                        AddBufferToRing(strategy, buf);
                    *buf_state = local_buf_state;
                    TrackNewBufferPin(BufferDescriptorGetBuffer(buf));
                    return buf;
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////
/////////   SIEVE FUNCS               //////////////////////
////////////////////////////////////////////////////////////


static Size
SieveShmemSize(int n_buffers)
{
	return sizeof(SieveState) + 2 * (n_buffers + 1) * sizeof(int32);
} //alloc for next/prev arrs


static void
SieveInitialize(bool found)
{
	/* SieveCtl already points into shmem (set by StrategyInitialize) */
	SieveNext = (int32 *) (SieveCtl + 1); //point into emtpy slots left my shmem dec.
	SievePrev = SieveNext + (NBuffers + 1);

	if (!found)
	{
		int			i;

		//set to avoid err
		SieveCtl->sieve_hand = 0;
		// SieveCtl->sieve_head = NBuffers - 1;	//see above
		SieveCtl->bgw_sync_seq = 0;

		//loop around - empty
		//Next[] points towards newer items & viseversa
		SieveNext[NBuffers] = NBuffers - 1; //Next[NBuff] points towards absolute tail
		SievePrev[NBuffers] = 0;

		// link list in order
		for (i = 0; i < NBuffers; i++)
		{
			SieveNext[i] = (i < NBuffers - 1) ? i + 1 : NBuffers;
			SievePrev[i] = (i > 0) ? i - 1 : NBuffers;
		}
	}
}

//advance list hand
static inline void
SieveAdvanceHand(void)
{
	int32		cur  = SieveCtl->sieve_hand; //get curr hand pos
	int32		next = SieveNext[cur];	// get nxt-> of curr

	if (next == NBuffers)
	{ //curr was at head, wrap around to tail
		next = SievePrev[NBuffers];
		// StrategyControl->completePasses++; // increment to signal loop, fix bug
	}	
	SieveCtl->sieve_hand = next;
}

//drop buf_id from list, advance hand if it was already pointing at buf_id
static void
SieveUnlinkAndAdvance(int32 buf_id)
{
	int32		newer = SieveNext[buf_id]; //get neighbors
	int32		older = SievePrev[buf_id];

	//check for hand condition
	if (SieveCtl->sieve_hand == buf_id)
	{
		int32		new_hand = newer; //step the hand forward

		if (new_hand == NBuffers) //true if buf_id @ head
			new_hand = older; // move head to prev instead
		SieveCtl->sieve_hand = new_hand;
	}

	//stitch the gap
	if (newer == NBuffers) //buf_id was head
		SieveNext[NBuffers] = older; //prev(older) is now head
	else
		SievePrev[newer] = older; // newer points back to old

	if (older == NBuffers) //buf_id was tail
		SievePrev[NBuffers] = newer; //forward is new tail
	else
		SieveNext[older] = newer; //old points fwd to new

	// clear the data for bufid
	SieveNext[buf_id] = NBuffers;
	SievePrev[buf_id] = NBuffers;
}

//call on load, insert buf at head
static void
SieveNotifyInsert(BufferDesc *buf)
{
	int32		buf_id   = buf->buf_id;
	int32		old_head;

	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

	//check if buf already present, if so, unlink and continue
	if (SieveNext[buf_id] != NBuffers || SievePrev[buf_id] != NBuffers || SieveNext[NBuffers] == buf_id)
          SieveUnlinkAndAdvance(buf_id);

	old_head = SieveNext[NBuffers]; //get global head

	SieveNext[buf_id] = NBuffers; //set new buf as head
	SievePrev[buf_id] = old_head; // point new to oldhead

	if (old_head != NBuffers) //dont set if empty
		SieveNext[old_head] = buf_id; // new <- old
	else
		SievePrev[NBuffers] = buf_id; // tail is also new buf

	SieveNext[NBuffers] = buf_id; //global head is newbuf

	//set hand if it was empty
	if (SieveCtl->sieve_hand == NBuffers)
		SieveCtl->sieve_hand = buf_id;

	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}

//drop buf from list
static void
SieveNotifyInvalidate(BufferDesc *buf)
{
	// int32		buf_id = buf->buf_id;

	// SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	// SieveUnlinkAndAdvance(buf_id); //call under lock
	// SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}

/// use sieve pass policy to find victim buff
static BufferDesc *
SieveGetBuffer(BufferAccessStrategy strategy, uint64 *buf_state)
{
	int			trycounter = NBuffers;

	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

	for (;;)
	{
		int32		candidate;
		BufferDesc *buf;
		uint64		old_buf_state;
		uint64		new_buf_state;

		if (SieveCtl->sieve_hand == NBuffers)
		{
			SpinLockRelease(&StrategyControl->buffer_strategy_lock);
			elog(ERROR, "no buffers in SIEVE eviction queue");
		}

		candidate     = SieveCtl->sieve_hand;
		buf           = GetBufferDescriptor(candidate);
		old_buf_state = pg_atomic_read_u64(&buf->state);

		//skip
		if (unlikely(old_buf_state & BM_LOCKED))
		{
			SieveAdvanceHand();
			continue;
		}

		//skip
		if (BUF_STATE_GET_REFCOUNT(old_buf_state) != 0)
		{
			SieveAdvanceHand();
			if (--trycounter == 0)
			{
				SpinLockRelease(&StrategyControl->buffer_strategy_lock);
				elog(ERROR, "no unpinned buffers available");
			}
			continue;
		}

		//reset visit
		if (BUF_STATE_GET_USAGECOUNT(old_buf_state) != 0)
		{
			new_buf_state = old_buf_state & ~((uint64) BUF_USAGECOUNT_MASK);
			(void) pg_atomic_compare_exchange_u64(&buf->state,
												  &old_buf_state,
												  new_buf_state);
			SieveAdvanceHand();
			trycounter = NBuffers; //reset attempts
			continue;
		}

		// not visited, not claimed
		new_buf_state = old_buf_state + BUF_REFCOUNT_ONE; //claim
		if (pg_atomic_compare_exchange_u64(&buf->state, &old_buf_state, new_buf_state))
		{ ///evict worked
			SieveUnlinkAndAdvance(candidate);

			if (strategy != NULL)
				AddBufferToRing(strategy, buf);

			*buf_state = new_buf_state;
			TrackNewBufferPin(BufferDescriptorGetBuffer(buf));
			SpinLockRelease(&StrategyControl->buffer_strategy_lock);
			return buf;
		}

		//retry
		trycounter = NBuffers;
	}
}


////////////////////////////////////////////////////////////
/////////////////	LRU FUNCS						////////
////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////
/////////////////	LFU FUNCS						////////
////////////////////////////////////////////////////////////

static Size
LeastFrequentlyUsedShmemSize(int n_buffers)
{
	return sizeof(LFUState) + n_buffers * sizeof(int32);
}

static void
LeastFrequentlyUsedInitilize(bool found)
{
	LFUFreq = (int32 *) (LFUCtl + 1); 

	if (!found) {
		int i;
		LFUCtl ->lfu_hand = 0;
		LFUCtl ->bgw_sync_seq = 0;

		for(i = 0; i < NBuffers; i++) {
			LFUFreq[i] = 0;
		}
	}
}

static void 
LeastFrequentlyUsedInsert(BufferDesc *buf)
{
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	LFUFreq[buf->buf_id] = 1; //set to 1
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}

static void
LeastFrequentlyUsedUpdate(BufferDesc *buf)
{
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	LFUFreq[buf->buf_id] ++; //increment on hit
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}

static void
LeastFrequentlyUsedDelete(BufferDesc *buf)
{
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	LFUFreq[buf->buf_id] = 0; //reset on invalidate
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}

/*
 * LeastFrequentlyUsedGetBuffer
 *
 * Scan all buffers starting from lfu_hand, find the unpinned buffer
 * with the lowest frequency count, and evict it.  O(n) per eviction
 * which matches ClockSweep's worst case.
 */
static BufferDesc *
LeastFrequentlyUsedGetBuffer(BufferAccessStrategy strategy, uint64 *buf_state)
{
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

	for (;;)
	{
		int32		min_freq   = INT32_MAX;
		int32		min_buf_id = -1;
		int			i;
		int32		scan_start = LFUCtl->lfu_hand;

		/* Phase 1: full scan to find minimum frequency among evictable bufs */
		for (i = 0; i < NBuffers; i++)
		{
			int32		candidate = (scan_start + i) % NBuffers;
			BufferDesc *buf;
			uint64		local_state;

			buf         = GetBufferDescriptor(candidate);
			local_state = pg_atomic_read_u64(&buf->state);

			/* skip pinned or locked buffers */
			if (BUF_STATE_GET_REFCOUNT(local_state) != 0)
				continue;
			if (unlikely(local_state & BM_LOCKED))
				continue;
			
			if (LFUFreq[candidate] < min_freq)
			{
				min_freq   = LFUFreq[candidate];
				min_buf_id = candidate;
			}
		}

		if (min_buf_id == -1)
		{
			SpinLockRelease(&StrategyControl->buffer_strategy_lock);
			elog(ERROR, "no unpinned buffers available");
		}

		/* Phase 2: try to claim the victim via CAS */
		{
			BufferDesc *buf       = GetBufferDescriptor(min_buf_id);
			uint64		old_state = pg_atomic_read_u64(&buf->state);
			uint64		new_state;

			/* re-check: may have been pinned/locked between scan and claim */
			if (BUF_STATE_GET_REFCOUNT(old_state) != 0 ||
				unlikely(old_state & BM_LOCKED))
				continue;   /* retry full scan */

			new_state = old_state + BUF_REFCOUNT_ONE;
			if (pg_atomic_compare_exchange_u64(&buf->state, &old_state,
											   new_state))
			{ 
				/* evict: reset freq, advance hand past victim */
				LFUFreq[min_buf_id] = 0;
				LFUCtl->lfu_hand = (min_buf_id + 1) % NBuffers;

				if (strategy != NULL)
					AddBufferToRing(strategy, buf);

				*buf_state = new_state;
				TrackNewBufferPin(BufferDescriptorGetBuffer(buf));
				SpinLockRelease(&StrategyControl->buffer_strategy_lock);
				return buf;
			}
		}
		/* CAS failed, retry full scan */
	}
}


/*
 * StrategyGetBuffer
 *
 *	Called by the bufmgr to get the next candidate buffer to use in
 *	GetVictimBuffer(). The only hard requirement GetVictimBuffer() has is that
 *	the selected buffer must not currently be pinned by anyone.
 *
 *	strategy is a BufferAccessStrategy object, or NULL for default strategy.
 *
 *	It is the callers responsibility to ensure the buffer ownership can be
 *	tracked via TrackNewBufferPin().
 *
 *	The buffer is pinned and marked as owned, using TrackNewBufferPin(),
 *	before returning.
 */
BufferDesc *
StrategyGetBuffer(BufferAccessStrategy strategy, uint64 *buf_state, bool *from_ring)
{
	BufferDesc *buf;
	int			bgwprocno;
	// int			trycounter;

	*from_ring = false;

	/*
	 * If given a strategy object, see whether it can select a buffer. We
	 * assume strategy objects don't need buffer_strategy_lock.
	 */
	if (strategy != NULL)
	{
		buf = GetBufferFromRing(strategy, buf_state);
		if (buf != NULL)
		{
			*from_ring = true;
			return buf;
		}
	}

	/*
	 * If asked, we need to waken the bgwriter. Since we don't want to rely on
	 * a spinlock for this we force a read from shared memory once, and then
	 * set the latch based on that value. We need to go through that length
	 * because otherwise bgwprocno might be reset while/after we check because
	 * the compiler might just reread from memory.
	 *
	 * This can possibly set the latch of the wrong process if the bgwriter
	 * dies in the wrong moment. But since PGPROC->procLatch is never
	 * deallocated the worst consequence of that is that we set the latch of
	 * some arbitrary process.
	 */
	bgwprocno = INT_ACCESS_ONCE(StrategyControl->bgwprocno);
	if (bgwprocno != -1)
	{
		/* reset bgwprocno first, before setting the latch */
		StrategyControl->bgwprocno = -1;

		/*
		 * Not acquiring ProcArrayLock here which is slightly icky. It's
		 * actually fine because procLatch isn't ever freed, so we just can
		 * potentially set the wrong process' (or no process') latch.
		 */
		SetLatch(&GetPGProcByNumber(bgwprocno)->procLatch);
	}

	/*
	 * We count buffer allocation requests so that the bgwriter can estimate
	 * the rate of buffer consumption.  Note that buffers recycled by a
	 * strategy object are intentionally not counted here.
	 */
	pg_atomic_fetch_add_u32(&StrategyControl->numBufferAllocs, 1);

	return ActiveEviction->get_buffer(strategy, buf_state); //ref to et func
}

/*
 * StrategySyncStart -- tell BgBufferSync where to start syncing
 *
 * The result is the buffer index of the best buffer to sync first.
 * BgBufferSync() will proceed circularly around the buffer array from there.
 *
 * In addition, we return the completed-pass count (which is effectively
 * the higher-order bits of nextVictimBuffer) and the count of recent buffer
 * allocs if non-NULL pointers are passed.  The alloc count is reset after
 * being read.
 */
int
StrategySyncStart(uint32 *complete_passes, uint32 *num_buf_alloc)
{
	uint32		nextVictimBuffer;
	int			result;

	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	nextVictimBuffer = pg_atomic_read_u32(&ClockSweepCtl->nextVictimBuffer);
	result = nextVictimBuffer % NBuffers;

	if (ActiveEviction == &SieveVtable)
	{
		// *complete_passes = StrategyControl->completePasses;
		uint32 seq = (uint32) ++SieveCtl->bgw_sync_seq;
		result = seq % NBuffers;
		
		if (complete_passes)
          *complete_passes = seq / NBuffers;
	}
	else 
	{
		nextVictimBuffer = pg_atomic_read_u32(&ClockSweepCtl->nextVictimBuffer);
		result = nextVictimBuffer % NBuffers;

		if (complete_passes)
		{
			*complete_passes = StrategyControl->completePasses;

			/*
			 * Additionally add the number of wraparounds that happened before
			 * completePasses could be incremented. C.f. ClockSweepTick().
			 */
			*complete_passes += nextVictimBuffer / NBuffers;
		}
	}

	if (num_buf_alloc)
	{
		*num_buf_alloc = pg_atomic_exchange_u32(&StrategyControl->numBufferAllocs, 0);
	}
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
	return result;
}

/*
 * StrategyNotifyBgWriter -- set or clear allocation notification latch
 *
 * If bgwprocno isn't -1, the next invocation of StrategyGetBuffer will
 * set that latch.  Pass -1 to clear the pending notification before it
 * happens.  This feature is used by the bgwriter process to wake itself up
 * from hibernation, and is not meant for anybody else to use.
 */
void
StrategyNotifyBgWriter(int bgwprocno)
{
	/*
	 * We acquire buffer_strategy_lock just to ensure that the store appears
	 * atomic to StrategyGetBuffer.  The bgwriter should call this rather
	 * infrequently, so there's no performance penalty from being safe.
	 */
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	StrategyControl->bgwprocno = bgwprocno;
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}

/*
 * Strategy hooks for targeted funcs via vtab- cache hit, load page, invalidate page
 * defs in bufinternals.h
 * bufmgr wont directly interact with ActiveEviction logic native to freelist
 */
void
StrategyNotifyHit(BufferDesc *buf)
{
	if (ActiveEviction && ActiveEviction->notify_hit)
		ActiveEviction->notify_hit(buf);
}

void
StrategyNotifyInsert(BufferDesc *buf)
{
	if (ActiveEviction && ActiveEviction->notify_insert)
		ActiveEviction->notify_insert(buf);
}

void
StrategyNotifyInvalidate(BufferDesc *buf)
{
	if (ActiveEviction && ActiveEviction->notify_invalidate)
		ActiveEviction->notify_invalidate(buf);
}


/*
 * StrategyShmemSize
 *
 * estimate the size of shared memory used by the freelist-related structures.
 *
 * Note: for somewhat historical reasons, the buffer lookup hashtable size
 * is also determined here.
 */
Size
StrategyShmemSize(void)
{
	Size		size = 0;

	/* size of lookup hash table ... see comment in StrategyInitialize */
	size = add_size(size, BufTableShmemSize(NBuffers + NUM_BUFFER_PARTITIONS));

	/* size of the shared replacement strategy control block */
	//size the new common struct + target struct
	// size = add_size(size, MAXALIGN(sizeof(BufferStrategyCommon) + sizeof(ClockSweepState)));
	size = add_size(size, MAXALIGN(sizeof(BufferStrategyCommon) +
		Max(Max(sizeof(ClockSweepState),
				sizeof(SieveState) + 2 * (NBuffers + 1) * sizeof(int32)),
			sizeof(LFUState) + NBuffers * sizeof(int32))));
	return size;
}

/*
 * StrategyInitialize -- initialize the buffer cache replacement
 *		strategy.
 *
 * Assumes: All of the buffers are already built into a linked list.
 *		Only called by postmaster and only during initialization.
 */
void
StrategyInitialize(bool init)
{
	bool		found;
	bool 		atomic_found;

	/*
	 * Initialize the shared buffer lookup hashtable.
	 *
	 * Since we can't tolerate running out of lookup table entries, we must be
	 * sure to specify an adequate table size here.  The maximum steady-state
	 * usage is of course NBuffers entries, but BufferAlloc() tries to insert
	 * a new entry before deleting the old.  In principle this could be
	 * happening in each partition concurrently, so we could need as many as
	 * NBuffers + NUM_BUFFER_PARTITIONS entries.
	 */
	InitBufTable(NBuffers + NUM_BUFFER_PARTITIONS);

	/*
	 * Get or create the shared strategy control block
	 */
	// ActiveEviction = &ClockSweepVtable;
	ActiveEviction = &SieveVtable;
	// ActiveEviction = &LFUVtable;

	StrategyControl = (BufferStrategyCommon *)
		ShmemInitStruct("Buffer Strategy Status",
						MAXALIGN(sizeof(BufferStrategyCommon) +
								 Max(sizeof(ClockSweepState),
									 sizeof(SieveState) +
									 2 * (NBuffers + 1) * sizeof(int32))),
						&found);

	
	ClockSweepCtl = (ClockSweepState *)((char *) StrategyControl + sizeof(BufferStrategyCommon));
	SieveCtl      = (SieveState *)     ((char *) StrategyControl + sizeof(BufferStrategyCommon));
	LFUCtl        = (LFUState *)       ((char *) StrategyControl + sizeof(BufferStrategyCommon));

	if (!found)
	{
		/*
		 * Only done once, usually in postmaster
		 */
		Assert(init);

		SpinLockInit(&StrategyControl->buffer_strategy_lock);

		/* Clear statistics */
		StrategyControl->completePasses = 0;
		pg_atomic_init_u32(&StrategyControl->numBufferAllocs, 0);

		/* No pending notification */
		StrategyControl->bgwprocno = -1;
	}
	else
		Assert(!init);

	ActiveEviction->initialize(found);
}


/* ----------------------------------------------------------------
 *				Backend-private buffer ring management
 * ----------------------------------------------------------------
 */


/*
 * GetAccessStrategy -- create a BufferAccessStrategy object
 *
 * The object is allocated in the current memory context.
 */
BufferAccessStrategy
GetAccessStrategy(BufferAccessStrategyType btype)
{
	int			ring_size_kb;

	/*
	 * Select ring size to use.  See buffer/README for rationales.
	 *
	 * Note: if you change the ring size for BAS_BULKREAD, see also
	 * SYNC_SCAN_REPORT_INTERVAL in access/heap/syncscan.c.
	 */
	switch (btype)
	{
		case BAS_NORMAL:
			/* if someone asks for NORMAL, just give 'em a "default" object */
			return NULL;

		case BAS_BULKREAD:
			{
				int			ring_max_kb;

				/*
				 * The ring always needs to be large enough to allow some
				 * separation in time between providing a buffer to the user
				 * of the strategy and that buffer being reused. Otherwise the
				 * user's pin will prevent reuse of the buffer, even without
				 * concurrent activity.
				 *
				 * We also need to ensure the ring always is large enough for
				 * SYNC_SCAN_REPORT_INTERVAL, as noted above.
				 *
				 * Thus we start out a minimal size and increase the size
				 * further if appropriate.
				 */
				ring_size_kb = 256;

				/*
				 * There's no point in a larger ring if we won't be allowed to
				 * pin sufficiently many buffers.  But we never limit to less
				 * than the minimal size above.
				 */
				ring_max_kb = GetPinLimit() * (BLCKSZ / 1024);
				ring_max_kb = Max(ring_size_kb, ring_max_kb);

				/*
				 * We would like the ring to additionally have space for the
				 * configured degree of IO concurrency. While being read in,
				 * buffers can obviously not yet be reused.
				 *
				 * Each IO can be up to io_combine_limit blocks large, and we
				 * want to start up to effective_io_concurrency IOs.
				 *
				 * Note that effective_io_concurrency may be 0, which disables
				 * AIO.
				 */
				ring_size_kb += (BLCKSZ / 1024) *
					io_combine_limit * effective_io_concurrency;

				if (ring_size_kb > ring_max_kb)
					ring_size_kb = ring_max_kb;
				break;
			}
		case BAS_BULKWRITE:
			ring_size_kb = 16 * 1024;
			break;
		case BAS_VACUUM:
			ring_size_kb = 2048;
			break;

		default:
			elog(ERROR, "unrecognized buffer access strategy: %d",
				 (int) btype);
			return NULL;		/* keep compiler quiet */
	}

	return GetAccessStrategyWithSize(btype, ring_size_kb);
}

/*
 * GetAccessStrategyWithSize -- create a BufferAccessStrategy object with a
 *		number of buffers equivalent to the passed in size.
 *
 * If the given ring size is 0, no BufferAccessStrategy will be created and
 * the function will return NULL.  ring_size_kb must not be negative.
 */
BufferAccessStrategy
GetAccessStrategyWithSize(BufferAccessStrategyType btype, int ring_size_kb)
{
	int			ring_buffers;
	BufferAccessStrategy strategy;

	Assert(ring_size_kb >= 0);

	/* Figure out how many buffers ring_size_kb is */
	ring_buffers = ring_size_kb / (BLCKSZ / 1024);

	/* 0 means unlimited, so no BufferAccessStrategy required */
	if (ring_buffers == 0)
		return NULL;

	/* Cap to 1/8th of shared_buffers */
	ring_buffers = Min(NBuffers / 8, ring_buffers);

	/* NBuffers should never be less than 16, so this shouldn't happen */
	Assert(ring_buffers > 0);

	/* Allocate the object and initialize all elements to zeroes */
	strategy = (BufferAccessStrategy)
		palloc0(offsetof(BufferAccessStrategyData, buffers) +
				ring_buffers * sizeof(Buffer));

	/* Set fields that don't start out zero */
	strategy->btype = btype;
	strategy->nbuffers = ring_buffers;

	return strategy;
}

/*
 * GetAccessStrategyBufferCount -- an accessor for the number of buffers in
 *		the ring
 *
 * Returns 0 on NULL input to match behavior of GetAccessStrategyWithSize()
 * returning NULL with 0 size.
 */
int
GetAccessStrategyBufferCount(BufferAccessStrategy strategy)
{
	if (strategy == NULL)
		return 0;

	return strategy->nbuffers;
}

/*
 * GetAccessStrategyPinLimit -- get cap of number of buffers that should be pinned
 *
 * When pinning extra buffers to look ahead, users of a ring-based strategy are
 * in danger of pinning too much of the ring at once while performing look-ahead.
 * For some strategies, that means "escaping" from the ring, and in others it
 * means forcing dirty data to disk very frequently with associated WAL
 * flushing.  Since external code has no insight into any of that, allow
 * individual strategy types to expose a clamp that should be applied when
 * deciding on a maximum number of buffers to pin at once.
 *
 * Callers should combine this number with other relevant limits and take the
 * minimum.
 */
int
GetAccessStrategyPinLimit(BufferAccessStrategy strategy)
{
	if (strategy == NULL)
		return NBuffers;

	switch (strategy->btype)
	{
		case BAS_BULKREAD:

			/*
			 * Since BAS_BULKREAD uses StrategyRejectBuffer(), dirty buffers
			 * shouldn't be a problem and the caller is free to pin up to the
			 * entire ring at once.
			 */
			return strategy->nbuffers;

		default:

			/*
			 * Tell caller not to pin more than half the buffers in the ring.
			 * This is a trade-off between look ahead distance and deferring
			 * writeback and associated WAL traffic.
			 */
			return strategy->nbuffers / 2;
	}
}

/*
 * FreeAccessStrategy -- release a BufferAccessStrategy object
 *
 * A simple pfree would do at the moment, but we would prefer that callers
 * don't assume that much about the representation of BufferAccessStrategy.
 */
void
FreeAccessStrategy(BufferAccessStrategy strategy)
{
	/* don't crash if called on a "default" strategy */
	if (strategy != NULL)
		pfree(strategy);
}

/*
 * GetBufferFromRing -- returns a buffer from the ring, or NULL if the
 *		ring is empty / not usable.
 *
 * The buffer is pinned and marked as owned, using TrackNewBufferPin(), before
 * returning.
 */
static BufferDesc *
GetBufferFromRing(BufferAccessStrategy strategy, uint64 *buf_state)
{
	BufferDesc *buf;
	Buffer		bufnum;
	uint64		old_buf_state;
	uint64		local_buf_state;	/* to avoid repeated (de-)referencing */


	/* Advance to next ring slot */
	if (++strategy->current >= strategy->nbuffers)
		strategy->current = 0;

	/*
	 * If the slot hasn't been filled yet, tell the caller to allocate a new
	 * buffer with the normal allocation strategy.  He will then fill this
	 * slot by calling AddBufferToRing with the new buffer.
	 */
	bufnum = strategy->buffers[strategy->current];
	if (bufnum == InvalidBuffer)
		return NULL;

	buf = GetBufferDescriptor(bufnum - 1);

	/*
	 * Check whether the buffer can be used and pin it if so. Do this using a
	 * CAS loop, to avoid having to lock the buffer header.
	 */
	old_buf_state = pg_atomic_read_u64(&buf->state);
	for (;;)
	{
		local_buf_state = old_buf_state;

		/*
		 * If the buffer is pinned we cannot use it under any circumstances.
		 *
		 * If usage_count is 0 or 1 then the buffer is fair game (we expect 1,
		 * since our own previous usage of the ring element would have left it
		 * there, but it might've been decremented by clock-sweep since then).
		 * A higher usage_count indicates someone else has touched the buffer,
		 * so we shouldn't re-use it.
		 */
		if (BUF_STATE_GET_REFCOUNT(local_buf_state) != 0
			|| BUF_STATE_GET_USAGECOUNT(local_buf_state) > 1)
			break;

		/* See equivalent code in PinBuffer() */
		if (unlikely(local_buf_state & BM_LOCKED))
		{
			old_buf_state = WaitBufHdrUnlocked(buf);
			continue;
		}

		/* pin the buffer if the CAS succeeds */
		local_buf_state += BUF_REFCOUNT_ONE;

		if (pg_atomic_compare_exchange_u64(&buf->state, &old_buf_state,
										   local_buf_state))
		{
			*buf_state = local_buf_state;

			TrackNewBufferPin(BufferDescriptorGetBuffer(buf));
			return buf;
		}
	}

	/*
	 * Tell caller to allocate a new buffer with the normal allocation
	 * strategy.  He'll then replace this ring element via AddBufferToRing.
	 */
	return NULL;
}

/*
 * AddBufferToRing -- add a buffer to the buffer ring
 *
 * Caller must hold the buffer header spinlock on the buffer.  Since this
 * is called with the spinlock held, it had better be quite cheap.
 */
static void
AddBufferToRing(BufferAccessStrategy strategy, BufferDesc *buf)
{
	strategy->buffers[strategy->current] = BufferDescriptorGetBuffer(buf);
}

/*
 * Utility function returning the IOContext of a given BufferAccessStrategy's
 * strategy ring.
 */
IOContext
IOContextForStrategy(BufferAccessStrategy strategy)
{
	if (!strategy)
		return IOCONTEXT_NORMAL;

	switch (strategy->btype)
	{
		case BAS_NORMAL:

			/*
			 * Currently, GetAccessStrategy() returns NULL for
			 * BufferAccessStrategyType BAS_NORMAL, so this case is
			 * unreachable.
			 */
			pg_unreachable();
			return IOCONTEXT_NORMAL;
		case BAS_BULKREAD:
			return IOCONTEXT_BULKREAD;
		case BAS_BULKWRITE:
			return IOCONTEXT_BULKWRITE;
		case BAS_VACUUM:
			return IOCONTEXT_VACUUM;
	}

	elog(ERROR, "unrecognized BufferAccessStrategyType: %d", strategy->btype);
	pg_unreachable();
}

/*
 * StrategyRejectBuffer -- consider rejecting a dirty buffer
 *
 * When a nondefault strategy is used, the buffer manager calls this function
 * when it turns out that the buffer selected by StrategyGetBuffer needs to
 * be written out and doing so would require flushing WAL too.  This gives us
 * a chance to choose a different victim.
 *
 * Returns true if buffer manager should ask for a new victim, and false
 * if this buffer should be written and re-used.
 */
bool
StrategyRejectBuffer(BufferAccessStrategy strategy, BufferDesc *buf, bool from_ring)
{
	/* We only do this in bulkread mode */
	if (strategy->btype != BAS_BULKREAD)
		return false;

	/* Don't muck with behavior of normal buffer-replacement strategy */
	if (!from_ring ||
		strategy->buffers[strategy->current] != BufferDescriptorGetBuffer(buf))
		return false;

	/*
	 * Remove the dirty buffer from the ring; necessary to prevent infinite
	 * loop if all ring members are dirty.
	 */
	strategy->buffers[strategy->current] = InvalidBuffer;

	return true;
}
