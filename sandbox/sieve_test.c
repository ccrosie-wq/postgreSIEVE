/*
 * sieve_test.c - test for sieve to make sure it works
 *
 * Build:  make  (in this dir)
 * Run:    ./sieve_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define N_BUFS 8

/* sentinel index – the "virtual" anchor node used by the linked list */
#define SENTINEL N_BUFS

//simple buf state
typedef struct {
    int buf_id;
    int refcount;   /* 0 = evictable */
    int visited;    /* 1 = recently used; maps to usage_count != 0 */
} Buf;

//state
static int sieve_hand;
static int SieveNext[N_BUFS + 1]; /* toward head (newer) */
static int SievePrev[N_BUFS + 1]; /* toward tail (older) */
static Buf buffers[N_BUFS];

//helpers

static Buf *get_buf(int id) { return &buffers[id]; }

/* Pretty-print the list from tail to head */
static void print_list(const char *label)
{
    printf("  [%s] hand=%d | list tail->head: ", label,
           sieve_hand == SENTINEL ? -1 : sieve_hand);
    int tail = SievePrev[SENTINEL];
    if (tail == SENTINEL) {
        printf("(empty)\n");
        return;
    }
    int cur = tail;
    while (cur != SENTINEL) {
        printf("%d(v=%d)", cur, buffers[cur].visited);
        int nxt = SieveNext[cur];
        if (nxt != SENTINEL) printf(" -> ");
        cur = nxt;
    }
    printf("\n");
}


//core logic from file

/*
 * Mirrors SieveInitialize() in freelist.c:
 * pre-constructs the full doubly-linked list with buf ids 0..N_BUFS-1.
 *   tail = 0  (SievePrev[SENTINEL] = 0)
 *   head = N_BUFS-1  (SieveNext[SENTINEL] = N_BUFS-1)
 *   hand = 0  (oldest buffer, same as the real init)
 */
static void sieve_init(void)
{
    int i;

    sieve_hand = 0;

    SieveNext[SENTINEL] = N_BUFS - 1;   /* head */
    SievePrev[SENTINEL] = 0;             /* tail */

    for (i = 0; i < N_BUFS; i++) {
        SieveNext[i] = (i < N_BUFS - 1) ? i + 1 : SENTINEL;
        SievePrev[i] = (i > 0)          ? i - 1 : SENTINEL;
        buffers[i].buf_id   = i;
        buffers[i].refcount = 0;
        buffers[i].visited  = 0;
    }
}

/* advance hand one step toward head; wrap at head back to tail */
static void sieve_advance_hand(void)
{
    int cur  = sieve_hand;
    int next = SieveNext[cur];
    if (next == SENTINEL)           /* cur was the head - wrap to tail */
        next = SievePrev[SENTINEL];
    sieve_hand = next;
}

/*
 * Remove buf_id from the list; if the hand was pointing at it, step
 * the hand forward (or backward if buf_id was the head).
 */
static void sieve_unlink_and_advance(int buf_id)
{
    int newer = SieveNext[buf_id]; /* toward head */
    int older = SievePrev[buf_id]; /* toward tail */

    /* move hand away before we destroy the links */
    if (sieve_hand == buf_id) {
        int new_hand = newer;
        if (new_hand == SENTINEL)  /* buf_id was the head — wrap to tail */
            new_hand = SievePrev[SENTINEL];
        sieve_hand = new_hand;
    }

    /* stitch the gap */
    if (newer == SENTINEL)         /* buf_id was head */
        SieveNext[SENTINEL] = older;
    else
        SievePrev[newer] = older;

    if (older == SENTINEL)         /* buf_id was tail */
        SievePrev[SENTINEL] = newer;
    else
        SieveNext[older] = newer;

    /* mark as detached */
    SieveNext[buf_id] = SENTINEL;
    SievePrev[buf_id] = SENTINEL;
}

/*
 * Insert buf_id at the head of the list (most-recently-used position).
 * If buf_id is already in the list it is re-linked at the head.
 */
static void sieve_notify_insert(int buf_id)
{
    /* already in list? unlink first */
    if (SieveNext[buf_id] != SENTINEL ||
        SievePrev[buf_id] != SENTINEL ||
        SieveNext[SENTINEL] == buf_id)
        sieve_unlink_and_advance(buf_id);

    int old_head = SieveNext[SENTINEL];

    SieveNext[buf_id] = SENTINEL;  /* new node is the head */
    SievePrev[buf_id] = old_head;  /* old head is now one step older */

    if (old_head != SENTINEL)
        SieveNext[old_head] = buf_id; /* old head's newer neighbour = new node */
    else
        SievePrev[SENTINEL] = buf_id; /* list was empty; new node is also tail */

    SieveNext[SENTINEL] = buf_id;  /* global head pointer */

    /* if hand was empty, point it at the first inserted buffer */
    if (sieve_hand == SENTINEL)
        sieve_hand = buf_id;
}

/*
 * Return the id of the next victim buffer, or -1 if none available.
 * Mirrors the inner loop of SieveGetBuffer in freelist.c.
 */
static int sieve_get_victim(void)
{
    for (int tries = 0; tries <= N_BUFS * 2; tries++) {
        if (sieve_hand == SENTINEL)
            return -1;  /* empty list */

        int  candidate = sieve_hand;
        Buf *buf       = get_buf(candidate);

        if (buf->refcount != 0) {        /* pinned – skip */
            sieve_advance_hand();
            continue;
        }

        if (buf->visited != 0) {         /* visited – give a second chance */
            buf->visited = 0;
            sieve_advance_hand();
            continue;
        }

        /* unvisited and unpinned – evict */
        sieve_unlink_and_advance(candidate);
        buf->refcount = 1;               /* "pin" to simulate caller holding it */
        return candidate;
    }
    return -1;  /* all buffers pinned or stuck */
}

//test helpers
static int pass_count = 0;
static int fail_count = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); pass_count++; } \
    else       { printf("  FAIL: %s  (line %d)\n", msg, __LINE__); fail_count++; } \
} while(0)

/*
 * Test 1: initial list structure after sieve_init
 *
 * sieve_init() pre-constructs the list with all N_BUFS buffers.
 * Expected: tail=0, head=N_BUFS-1, hand=0, order 0,1..N_BUFS-1.
 */
static void test_init_state(void)
{
    printf("\n=== Test 1: initial list structure ===\n");
    sieve_init();
    print_list("after sieve_init");

    CHECK(SieveNext[SENTINEL] == N_BUFS - 1, "head is N_BUFS-1");
    CHECK(SievePrev[SENTINEL] == 0,           "tail is 0");
    CHECK(sieve_hand == 0,                    "hand starts at tail (0)");

    /* walk tail-head and confirm all N_BUFS nodes are present in order */
    int cur = SievePrev[SENTINEL]; /* tail */
    for (int i = 0; i < N_BUFS; i++) {
        CHECK(cur == i, "list node in expected order");
        cur = SieveNext[cur];
    }
    CHECK(cur == SENTINEL, "list terminates at sentinel");
}

/*
 * Test 2: eviction order with no visited flags
 *
 * with no visits, sieve should behave like LRU
 * expected order (0,1,2,3,4,5,6,7) reverse of initial state
 */
static void test_evict_no_visits(void)
{
    printf("\n=== Test 2: eviction order, no visits ===\n");
    sieve_init();

    for (int i = 0; i < N_BUFS; i++) {
        int victim = sieve_get_victim();
        printf("  evicted: %d (expected %d)\n", victim, i);
        CHECK(victim == i, "correct eviction order");
        get_buf(victim)->refcount = 0;
    }
}

/*
 * Test 3: visited buffer gets a second chance
 *
 * Mark buf 0 (the tail / hand start) as visited.
 * Expected eviction order: 1, 2, ..., N_BUFS-1, 0.
 * Buf 0 survives its first encounter; its visit flag is cleared and the
 * hand continues to 1.  After N_BUFS-1 is evicted the hand wraps and
 * finds 0 unvisited.
 * mark 0 (tail) as visited, should get skipped by hand
 * expected order (1,2,3,4,5,6,7,0) visited is now last.
 */
static void test_visited_second_chance(void)
{
    printf("\n=== Test 3: visited buffer gets second chance ===\n");
    sieve_init();
    buffers[0].visited = 1;
    print_list("before eviction (buf 0 visited)");

    for (int i = 0; i < N_BUFS; i++) {
        int expected = (i < N_BUFS - 1) ? i + 1 : 0;
        int victim   = sieve_get_victim();
        printf("  evicted: %d (expected %d)\n", victim, expected);
        CHECK(victim == expected, "correct eviction order with visited buf");
        get_buf(victim)->refcount = 0;
    }
}

/*
 * Test 4: multiple visited buffers
 *
 * initial state is standard (7..0)
 * set visited=1 on even-indexed buffers {0, 2, 4, 6}.
 *
 * sieve victims in order should be (1,3,5,7,0,2,4,6)
 */
static void test_multiple_visited(void)
{
    printf("\n=== Test 4: multiple visited buffers ===\n");
    sieve_init();

    for (int i = 0; i < N_BUFS; i += 2)
        buffers[i].visited = 1;
    print_list("initial state (even bufs visited)");

    int expected[] = {1, 3, 5, 7, 0, 2, 4, 6};
    for (int i = 0; i < N_BUFS; i++) {
        int victim = sieve_get_victim();
        printf("  evicted: %d (expected %d)\n", victim, expected[i]);
        CHECK(victim == expected[i], "correct eviction order with multiple visited");
        get_buf(victim)->refcount = 0;
    }
}

/*
 * Test 5: re-insertion moves a buffer to the head
 *
 * evict buf0 at tail, then reinsert (mimic reinserts in code)
 * order should be (1..7, 0)
 */
static void test_reinsert_goes_to_head(void)
{
    printf("\n=== Test 5: re-insertion goes to head ===\n");
    sieve_init();

    int victim = sieve_get_victim();
    CHECK(victim == 0, "first eviction is buf 0 (tail)");
    get_buf(victim)->refcount = 0;

    sieve_notify_insert(0);
    print_list("after re-inserting buf 0");
    CHECK(SieveNext[SENTINEL] == 0, "buf 0 is now at head after re-insert");

    for (int i = 0; i < N_BUFS; i++) {
        int expected = (i < N_BUFS - 1) ? i + 1 : 0;
        int v = sieve_get_victim();
        printf("  evicted: %d (expected %d)\n", v, expected);
        CHECK(v == expected, "correct eviction order after re-insert");
        get_buf(v)->refcount = 0;
    }
}

/*
 * Test 6: pinned buffers are skipped
 *
 * Pin buf 0 (the tail / hand start).  First eviction should skip 0 and
 * return 1.  After unpinning 0 it becomes eligible again.
 */
static void test_pinned_skipped(void)
{
    printf("\n=== Test 6: pinned buffers are skipped ===\n");
    sieve_init();
    buffers[0].refcount = 1;

    int victim = sieve_get_victim();
    printf("  evicted: %d (expected 1)\n", victim);
    CHECK(victim == 1, "pinned buf 0 skipped; buf 1 evicted instead");

    buffers[0].refcount = 0;
    get_buf(victim)->refcount = 0;
    victim = sieve_get_victim();
    printf("  evicted: %d (expected 2)\n", victim);
    CHECK(victim == 2, "eviction continues normally after unpin");
}


int main(void)
{
    printf("sieve_test: N_BUFS=%d, SENTINEL=%d\n", N_BUFS, SENTINEL);

    test_init_state();
    test_evict_no_visits();
    test_visited_second_chance();
    test_multiple_visited();
    test_reinsert_goes_to_head();
    test_pinned_skipped();

    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
