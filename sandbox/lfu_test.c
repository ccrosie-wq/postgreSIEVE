/*
 * lfu_test.c - standalone unit tests for the LFU eviction policy
 *
 * Build:  make  (in this dir)
 * Run:    ./lfu_test
 *
 * Mirrors freelist.c LFU logic exactly (direct port, no PostgreSQL deps).
 * Key behavioral facts tested here:
 *
 *   1. All N_BUFS slots pre-link into bucket[1] at freq=1 on init,
 *      matching LFUInitialize() in production.
 *
 *   2. lfu_get_victim() checks only the LRU TAIL of each bucket. If that
 *      buffer is pinned, next_candidate = LFUPrev[tail] = sentinel, so the
 *      inner while exits and the outer loop skips to freq+1. A pinned LRU
 *      tail causes the entire bucket to be skipped, not just that one entry.
 *
 *   3. min_freq is a conservative lower bound — it advances only when the
 *      bucket it points to empties (during a hit or lazy scan in get_victim).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- configuration ---- */
#define N_BUFS        8
#define LFU_MAX_FREQ  16   /* reduced from production 1024 for test clarity */

/* ---- sentinel layout (mirrors freelist.c macros) ---- */
#define LFU_SENTINEL(f)      (N_BUFS + (f) - 1)   /* f is 1-indexed */
#define LFU_BUCKET_EMPTY(f)  (LFUNext[LFU_SENTINEL(f)] == LFU_SENTINEL(f))

/* ---- state (mirrors freelist.c globals) ---- */
static int LFUFreq[N_BUFS];                        /* 0 = evicted / not cached */
static int LFUNext[N_BUFS + LFU_MAX_FREQ];         /* toward MRU */
static int LFUPrev[N_BUFS + LFU_MAX_FREQ];         /* toward LRU */
static int min_freq;

typedef struct { int buf_id; int refcount; } Buf;
static Buf buffers[N_BUFS];

static Buf *get_buf(int id) { return &buffers[id]; }

/* ---- debug helpers ---- */

static void print_bucket(int f)
{
    int s = LFU_SENTINEL(f);
    printf("  bucket[%d] (LRU->MRU): ", f);
    int cur = LFUPrev[s]; /* start at LRU tail */
    if (cur == s) { printf("(empty)\n"); return; }
    while (cur != s) {
        printf("%d(f=%d)", cur, LFUFreq[cur]);
        cur = LFUNext[cur]; /* walk toward MRU */
        if (cur != s) printf(" -> ");
    }
    printf("\n");
}

static void print_state(const char *label)
{
    printf("  [%s] min_freq=%d\n", label, min_freq);
    for (int f = 1; f <= LFU_MAX_FREQ; f++)
        if (!LFU_BUCKET_EMPTY(f))
            print_bucket(f);
}

/* ---- LFU algorithm: direct port of freelist.c ---- */

static void lfu_bucket_unlink(int buf_id)
{
    int s    = LFU_SENTINEL(LFUFreq[buf_id]);
    int next = LFUNext[buf_id]; /* toward MRU */
    int prev = LFUPrev[buf_id]; /* toward LRU */

    if (next == s)  LFUNext[s]    = prev; /* buf_id was MRU head */
    else            LFUPrev[next] = prev;

    if (prev == s)  LFUPrev[s]    = next; /* buf_id was LRU tail */
    else            LFUNext[prev] = next;

    LFUNext[buf_id] = N_BUFS;
    LFUPrev[buf_id] = N_BUFS;
}

static void lfu_bucket_insert_mru(int buf_id, int freq)
{
    int s        = LFU_SENTINEL(freq);
    int old_head = LFUNext[s]; /* current MRU item */

    LFUNext[buf_id] = s;
    LFUPrev[buf_id] = old_head;

    if (old_head == s)  LFUPrev[s]        = buf_id; /* bucket was empty */
    else                LFUNext[old_head] = buf_id;

    LFUNext[s] = buf_id;
}

static void lfu_init(void)
{
    int i, s;

    min_freq = 1;

    for (i = 1; i <= LFU_MAX_FREQ; i++) {
        s = LFU_SENTINEL(i);
        LFUNext[s] = s;
        LFUPrev[s] = s;
    }

    /* pre-link all buffers into bucket[1]: buf 0 = LRU tail, N_BUFS-1 = MRU head */
    s = LFU_SENTINEL(1);
    LFUNext[s] = N_BUFS - 1;
    LFUPrev[s] = 0;

    for (i = 0; i < N_BUFS; i++) {
        LFUFreq[i] = 1;
        LFUNext[i] = (i < N_BUFS - 1) ? i + 1 : s;
        LFUPrev[i] = (i > 0)           ? i - 1 : s;
        buffers[i].buf_id   = i;
        buffers[i].refcount = 0;
    }
}

static void lfu_notify_insert(int buf_id)
{
    if (LFUFreq[buf_id] != 0)
        lfu_bucket_unlink(buf_id);

    LFUFreq[buf_id] = 1;
    lfu_bucket_insert_mru(buf_id, 1);
    min_freq = 1;
}

static void lfu_notify_hit(int buf_id)
{
    int f = LFUFreq[buf_id];
    if (f == 0) return;

    int new_f = (f < LFU_MAX_FREQ) ? f + 1 : LFU_MAX_FREQ; /* saturating */

    lfu_bucket_unlink(buf_id);

    if (f == min_freq && LFU_BUCKET_EMPTY(f))
        min_freq = new_f;

    LFUFreq[buf_id] = new_f;
    lfu_bucket_insert_mru(buf_id, new_f);
}

static void lfu_notify_invalidate(int buf_id)
{
    if (LFUFreq[buf_id] != 0) {
        lfu_bucket_unlink(buf_id);
        LFUFreq[buf_id] = 0;
        /* leave min_freq as-is: next insert resets to 1 */
    }
}

/*
 * lfu_get_victim - mirrors LFUGetBuffer().
 *
 * Scans freq buckets from min_freq upward. Within each bucket, checks
 * only the LRU tail (LFUPrev[sentinel]).  next_candidate = LFUPrev[tail]
 * = sentinel in the normal case, so a pinned tail causes the inner while
 * to exit immediately and the outer loop advances to freq+1.
 *
 * On success: unlinks the victim, sets LFUFreq[victim]=0, sets
 * refcount=1 to simulate a pin, and returns the buf_id.
 * Returns -1 if all buffers are pinned.
 */
static int lfu_get_victim(void)
{
    int f;
    for (f = min_freq; f <= LFU_MAX_FREQ; f++) {
        int s         = LFU_SENTINEL(f);
        int candidate = LFUPrev[s]; /* LRU tail */

        while (candidate != s) {
            int next_candidate = LFUPrev[candidate]; /* mirrors LFUGetBuffer exactly */

            if (get_buf(candidate)->refcount == 0) {
                lfu_bucket_unlink(candidate);
                LFUFreq[candidate] = 0;
                if (LFU_BUCKET_EMPTY(f))
                    min_freq = f; /* conservative lower bound, same as production */
                get_buf(candidate)->refcount = 1;
                return candidate;
            }
            candidate = next_candidate;
        }

        if (LFU_BUCKET_EMPTY(f))
            min_freq = f + 1;
    }
    return -1;
}

/* ---- test infrastructure ---- */

static int pass_count = 0;
static int fail_count = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); pass_count++; } \
    else       { printf("  FAIL: %s  (line %d)\n", msg, __LINE__); fail_count++; } \
} while(0)

/* ---- tests ---- */

/*
 * Test 1: initial list structure after lfu_init
 *
 * All N_BUFS buffers start in bucket[1] with freq=1.
 * buf 0 = LRU tail (evicted first), buf N_BUFS-1 = MRU head.
 * All other buckets are empty. min_freq = 1.
 */
static void test_init_state(void)
{
    printf("\n=== Test 1: initial list structure ===\n");
    lfu_init();
    print_state("after lfu_init");

    CHECK(min_freq == 1,                                    "min_freq initialized to 1");
    CHECK(LFUNext[LFU_SENTINEL(1)] == N_BUFS - 1,          "bucket[1] MRU head is N_BUFS-1");
    CHECK(LFUPrev[LFU_SENTINEL(1)] == 0,                   "bucket[1] LRU tail is 0");

    for (int i = 0; i < N_BUFS; i++)
        CHECK(LFUFreq[i] == 1, "all buffers initialized at freq=1");

    for (int f = 2; f <= LFU_MAX_FREQ; f++)
        CHECK(LFU_BUCKET_EMPTY(f), "all buckets above 1 are empty");
}

/*
 * Test 2: eviction order with no hits
 *
 * With no hits, all buffers stay at freq=1 and eviction follows LRU order.
 * Expected: 0, 1, 2, ..., N_BUFS-1 (buf 0 is LRU tail at init).
 * After all are evicted, get_victim returns -1.
 */
static void test_evict_order_no_hits(void)
{
    printf("\n=== Test 2: eviction order, no hits ===\n");
    lfu_init();

    for (int i = 0; i < N_BUFS; i++) {
        int victim = lfu_get_victim();
        printf("  evicted: %d (expected %d)\n", victim, i);
        CHECK(victim == i, "correct LRU-within-bucket eviction order");
        get_buf(victim)->refcount = 0;
    }
    CHECK(lfu_get_victim() == -1, "returns -1 when all buffers evicted");
}

/*
 * Test 3: hit moves buffer to higher frequency bucket
 *
 * After hitting the LRU tail (buf 0), it moves to bucket[2].
 * The next eviction should pick buf 1 (new LRU tail of bucket[1]),
 * not buf 0 which is now at a higher frequency.
 */
static void test_hit_moves_to_higher_bucket(void)
{
    printf("\n=== Test 3: hit moves buffer to higher freq bucket ===\n");
    lfu_init();

    lfu_notify_hit(0); /* buf 0: freq 1->2, moves to bucket[2] */
    print_state("after hit on buf 0");

    CHECK(LFUFreq[0] == 2,                         "buf 0 freq incremented to 2");
    CHECK(!LFU_BUCKET_EMPTY(2),                    "bucket[2] now contains buf 0");
    CHECK(LFUPrev[LFU_SENTINEL(1)] == 1,           "buf 1 is new LRU tail of bucket[1]");

    int victim = lfu_get_victim();
    printf("  evicted: %d (expected 1)\n", victim);
    CHECK(victim == 1, "buf 1 evicted (lowest freq, LRU tail), not buf 0 at freq=2");
    get_buf(victim)->refcount = 0;

    CHECK(LFUFreq[0] == 2, "buf 0 still at freq=2 after other buffer evicted");
}

/*
 * Test 4: pinned LRU tail causes entire bucket to be skipped
 *
 * lfu_get_victim() uses next_candidate = LFUPrev[candidate].  For the LRU
 * tail, LFUPrev[tail] = sentinel, so a pinned tail exits the inner while
 * immediately and the outer loop advances to the next frequency bucket.
 * This can cause a higher-frequency buffer to be evicted over lower-freq
 * pinned buffers — correct production behavior.
 *
 * Setup:  buf 7 hit twice (freq=3); buf 0 pinned (LRU tail of bucket[1]).
 * Expected: eviction picks buf 7 (freq=3) from bucket[3], skipping bucket[1].
 */
static void test_pinned_tail_skips_bucket(void)
{
    printf("\n=== Test 4: pinned LRU tail causes skip to next freq bucket ===\n");
    lfu_init();

    lfu_notify_hit(7); /* freq 1->2 */
    lfu_notify_hit(7); /* freq 2->3 */
    print_state("after 2 hits on buf 7");

    buffers[0].refcount = 1; /* pin buf 0 (LRU tail of bucket[1]) */
    printf("  pinned buf 0 (LRU tail of bucket[1])\n");

    int victim = lfu_get_victim();
    printf("  evicted: %d (expected 7)\n", victim);
    CHECK(victim == 7,
          "pinned LRU tail in bucket[1] skips bucket; buf 7 (freq=3) evicted");

    buffers[0].refcount = 0;
    get_buf(victim)->refcount = 0;
}

/*
 * Test 5: re-insert after invalidation resets frequency to 1
 *
 * When a buffer is invalidated then re-inserted (new page loaded into the
 * slot), frequency resets to 1 and the buffer lands at the MRU head of
 * bucket[1]. min_freq is set to 1.
 */
static void test_reinsert_resets_freq(void)
{
    printf("\n=== Test 5: re-insert resets frequency to 1 ===\n");
    lfu_init();

    lfu_notify_hit(0);
    lfu_notify_hit(0); /* buf 0 now at freq=3 */
    CHECK(LFUFreq[0] == 3, "buf 0 freq is 3 before invalidation");

    lfu_notify_invalidate(0);
    CHECK(LFUFreq[0] == 0, "invalidated buf 0 has freq=0");

    lfu_notify_insert(0); /* new page loaded into slot 0 */
    print_state("after re-inserting buf 0");

    CHECK(LFUFreq[0] == 1,                       "re-insert resets freq to 1");
    CHECK(min_freq == 1,                          "min_freq reset to 1 on insert");
    CHECK(LFUNext[LFU_SENTINEL(1)] == 0,         "buf 0 is MRU head of bucket[1]");
}

/*
 * Test 6: frequency saturation at LFU_MAX_FREQ
 *
 * The counter must not exceed LFU_MAX_FREQ regardless of hit count.
 * When f == LFU_MAX_FREQ, re-insertion into the same bucket refreshes
 * recency (MRU position) without changing the frequency value.
 */
static void test_freq_saturation(void)
{
    printf("\n=== Test 6: frequency saturation at LFU_MAX_FREQ=%d ===\n", LFU_MAX_FREQ);
    lfu_init();

    for (int i = 0; i < LFU_MAX_FREQ + 10; i++)
        lfu_notify_hit(0);

    printf("  buf 0 freq: %d (expected %d)\n", LFUFreq[0], LFU_MAX_FREQ);
    CHECK(LFUFreq[0] == LFU_MAX_FREQ,       "frequency saturates at LFU_MAX_FREQ");
    CHECK(!LFU_BUCKET_EMPTY(LFU_MAX_FREQ),  "buf 0 is in the max-freq bucket");
}

/*
 * Test 7: invalidated buffer is never returned as a victim
 *
 * lfu_notify_invalidate() sets freq=0 and unlinks the buffer from its
 * bucket. A freq=0 buffer is not in any bucket and must never be selected
 * by lfu_get_victim().
 */
static void test_invalidation(void)
{
    printf("\n=== Test 7: invalidated buffer never returned as victim ===\n");
    lfu_init();

    lfu_notify_hit(0);
    lfu_notify_hit(0); /* buf 0 at freq=3 */

    lfu_notify_invalidate(0);
    print_state("after invalidating buf 0");

    CHECK(LFUFreq[0] == 0, "invalidated buf 0 has freq=0");

    int saw_zero = 0;
    for (int i = 0; i < N_BUFS - 1; i++) {
        int victim = lfu_get_victim();
        printf("  evicted: %d\n", victim);
        if (victim == 0) saw_zero = 1;
        if (victim >= 0) get_buf(victim)->refcount = 0;
    }
    CHECK(!saw_zero, "invalidated buf 0 never returned as victim");
}

int main(void)
{
    printf("lfu_test: N_BUFS=%d, LFU_MAX_FREQ=%d, SENTINEL base=%d\n",
           N_BUFS, LFU_MAX_FREQ, LFU_SENTINEL(1));

    test_init_state();
    test_evict_order_no_hits();
    test_hit_moves_to_higher_bucket();
    test_pinned_tail_skips_bucket();
    test_reinsert_resets_freq();
    test_freq_saturation();
    test_invalidation();

    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
