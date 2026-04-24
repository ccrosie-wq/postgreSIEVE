/*
 * lru_test.c - tests for LRU
 *
 * Build:  make  (in this dir)
 * Run:    ./lru_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define N_BUFS 8
#define SENTINEL N_BUFS

//state

typedef struct {
    int buf_id;
    int refcount;
} Buf;

/* LRUNext[i] = toward head (MRU); LRUPrev[i] = toward tail (LRU) */
static int LRUNext[N_BUFS + 1];
static int LRUPrev[N_BUFS + 1];
static Buf buffers[N_BUFS];

//helpers
static Buf *get_buf(int id) { return &buffers[id]; }

static void print_list(const char *label)
{
    printf("  [%s] list tail->head: ", label);
    int tail = LRUPrev[SENTINEL];
    if (tail == SENTINEL) { printf("(empty)\n"); return; }
    int cur = tail;
    while (cur != SENTINEL) {
        printf("%d", cur);
        int nxt = LRUNext[cur];
        if (nxt != SENTINEL) printf(" -> ");
        cur = nxt;
    }
    printf("\n");
}

/*
 * Mirrors LRUInitialize(): pre-constructs the list with all buffers.
 *   tail = 0  (LRUPrev[SENTINEL] = 0)
 *   head = N_BUFS-1  (LRUNext[SENTINEL] = N_BUFS-1)
 */
static void lru_init(void)
{
    LRUNext[SENTINEL] = N_BUFS - 1;
    LRUPrev[SENTINEL] = 0;

    for (int i = 0; i < N_BUFS; i++) {
        LRUNext[i] = (i < N_BUFS - 1) ? i + 1 : SENTINEL;
        LRUPrev[i] = (i > 0)          ? i - 1 : SENTINEL;
        buffers[i].buf_id   = i;
        buffers[i].refcount = 0;
    }
}

/* Remove buf_id from the list */
static void lru_unlink(int buf_id)
{
    int mru_nb = LRUNext[buf_id]; /* toward head */
    int lru_nb = LRUPrev[buf_id]; /* toward tail */

    if (mru_nb == SENTINEL)
        LRUNext[SENTINEL] = lru_nb; /* buf_id was head */
    else
        LRUPrev[mru_nb] = lru_nb;

    if (lru_nb == SENTINEL)
        LRUPrev[SENTINEL] = mru_nb; /* buf_id was tail */
    else
        LRUNext[lru_nb] = mru_nb;

    LRUNext[buf_id] = SENTINEL;
    LRUPrev[buf_id] = SENTINEL;
}

/* Insert buf_id at the head (MRU position) */
static void lru_insert_mru(int buf_id)
{
    int old_head = LRUNext[SENTINEL];

    LRUNext[buf_id] = SENTINEL;
    LRUPrev[buf_id] = old_head;

    if (old_head == SENTINEL)
        LRUPrev[SENTINEL] = buf_id; /* list was empty */
    else
        LRUNext[old_head] = buf_id;

    LRUNext[SENTINEL] = buf_id;
}

#define LRU_IN_LIST(id) \
    (LRUNext[id] != SENTINEL || LRUPrev[id] != SENTINEL || LRUNext[SENTINEL] == (id))

/* Cache hit: promote buf_id to MRU head */
static void lru_notify_hit(int buf_id)
{
    if (LRU_IN_LIST(buf_id)) {
        lru_unlink(buf_id);
        lru_insert_mru(buf_id);
    }
}

/* Page load: insert buf_id at MRU head (unlink first if stale) */
static void lru_notify_insert(int buf_id)
{
    if (LRU_IN_LIST(buf_id))
        lru_unlink(buf_id);
    lru_insert_mru(buf_id);
}

/*
 * Evict the LRU (tail) buffer.  If the tail is pinned, scan toward head
 * and wrap back to tail if needed.  Returns buf_id or -1 if none available.
 */
static int lru_get_victim(void)
{
    int candidate = LRUPrev[SENTINEL]; /* start at tail */
    int trycounter = N_BUFS;

    for (;;) {
        if (candidate == SENTINEL)
            return -1; /* empty */

        Buf *buf = get_buf(candidate);

        if (buf->refcount != 0) { /* pinned – skip toward head */
            candidate = LRUNext[candidate];
            if (candidate == SENTINEL)
                candidate = LRUPrev[SENTINEL]; /* wrap to tail */
            if (--trycounter == 0)
                return -1;
            continue;
        }

        lru_unlink(candidate);
        buf->refcount = 1; /* "pin" */
        return candidate;
    }
}

//test helps

static int pass_count = 0;
static int fail_count = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); pass_count++; } \
    else       { printf("  FAIL: %s  (line %d)\n", msg, __LINE__); fail_count++; } \
} while(0)

//tests

/*
 * Test 1: initial list structure after lru_init
 *
 * lru_init() pre-constructs the list with all N_BUFS buffers.
 * expected: tail=0, head=N_BUFS-1, order (0,1,..,N_BUFS-1)
 */
static void test_init_state(void)
{
    printf("\n=== Test 1: initial list structure ===\n");
    lru_init();
    print_list("after lru_init");

    CHECK(LRUNext[SENTINEL] == N_BUFS - 1, "head is N_BUFS-1 (MRU)");
    CHECK(LRUPrev[SENTINEL] == 0, "tail is 0 (LRU)");

    int cur = LRUPrev[SENTINEL]; /* tail */
    for (int i = 0; i < N_BUFS; i++) {
        CHECK(cur == i, "list node in expected order");
        cur = LRUNext[cur];
    }
    CHECK(cur == SENTINEL, "list terminates at sentinel");
}

/*
 * Test 2: eviction order with no hits
 *
 * with no hits, list order is unchanged
 * expected order (0,1,2,..,N_BUFS-1) tail first
 */
static void test_evict_no_hits(void)
{
    printf("\n=== Test 2: eviction order, no hits ===\n");
    lru_init();

    for (int i = 0; i < N_BUFS; i++) {
        int victim = lru_get_victim();
        printf("  evicted: %d (expected %d)\n", victim, i);
        CHECK(victim == i, "correct eviction order");
        get_buf(victim)->refcount = 0;
    }
}

/*
 * Test 3: cache hit promotes buffer to head
 *
 * hit buf 0 (tail), moves to head
 * expected order (1,2,...,N_BUFS-1 ,0) hit buf 0 is now last
 */
static void test_hit_promotes_to_head(void)
{
    printf("\n=== Test 3: cache hit promotes to head ===\n");
    lru_init();
    lru_notify_hit(0);
    print_list("after hit on buf 0");

    CHECK(LRUNext[SENTINEL] == 0, "buf 0 is now at head");
    CHECK(LRUPrev[SENTINEL] == 1, "buf 1 is now at tail");

    for (int i = 0; i < N_BUFS; i++) {
        int expected = (i < N_BUFS - 1) ? i + 1 : 0;
        int victim   = lru_get_victim();
        printf("  evicted: %d (expected %d)\n", victim, expected);
        CHECK(victim == expected, "correct eviction order after hit");
        get_buf(victim)->refcount = 0;
    }
}

/*
 * Test 4: multiple hits produce correct MRU ordering
 *
 * hit buf 1, then buf 3 (last hit = head)
 * list becomes: head 3,1,7,6,5,4,2,0 tail
 * expected order (0,2,4,5,6,7,1,3)
 */
static void test_multiple_hits(void)
{
    printf("\n=== Test 4: multiple hits ===\n");
    lru_init();
    lru_notify_hit(1);
    lru_notify_hit(3);
    print_list("after hits on 1 then 3");

    CHECK(LRUNext[SENTINEL] == 3, "buf 3 (last hit) is at head");
    CHECK(LRUPrev[SENTINEL] == 0, "buf 0 (never hit) remains at tail");

    int expected[] = {0, 2, 4, 5, 6, 7, 1, 3};
    for (int i = 0; i < N_BUFS; i++) {
        int victim = lru_get_victim();
        printf("  evicted: %d (expected %d)\n", victim, expected[i]);
        CHECK(victim == expected[i], "correct eviction order with multiple hits");
        get_buf(victim)->refcount = 0;
    }
}

/*
 * Test 5: re-insertion after eviction goes to head
 *
 * evict buf 0 (tail), then reinsert (mimic page reload)
 * expected order same as test 3 (1,2,..,7,0)
 */
static void test_reinsert_goes_to_head(void)
{
    printf("\n=== Test 5: re-insertion goes to head ===\n");
    lru_init();

    int victim = lru_get_victim();
    CHECK(victim == 0, "first eviction is buf 0 (tail)");
    get_buf(victim)->refcount = 0;

    lru_notify_insert(0);
    print_list("after re-inserting buf 0");
    CHECK(LRUNext[SENTINEL] == 0, "buf 0 is now at head after re-insert");

    for (int i = 0; i < N_BUFS; i++) {
        int expected = (i < N_BUFS - 1) ? i + 1 : 0;
        int v = lru_get_victim();
        printf("  evicted: %d (expected %d)\n", v, expected);
        CHECK(v == expected, "correct eviction order after re-insert");
        get_buf(v)->refcount = 0;
    }
}

/*
 * Test 6: pinned buffer is skipped
 *
 * pin buf 0 (tail), should be skipped in favor of buf 1
 * after unpin, buf 0 becomes evictable again
 */
static void test_pinned_skipped(void)
{
    printf("\n=== Test 6: pinned buffer is skipped ===\n");
    lru_init();
    buffers[0].refcount = 1;

    int victim = lru_get_victim();
    printf("  evicted: %d (expected 1)\n", victim);
    CHECK(victim == 1, "pinned buf 0 skipped; buf 1 evicted instead");

    buffers[0].refcount = 0;
    get_buf(victim)->refcount = 0;
    victim = lru_get_victim();
    printf("  evicted: %d (expected 0)\n", victim);
    CHECK(victim == 0, "buf 0 now evicted after unpin");
}


int main(void)
{
    printf("lru_test: N_BUFS=%d, SENTINEL=%d\n", N_BUFS, SENTINEL);

    test_init_state();
    test_evict_no_hits();
    test_hit_promotes_to_head();
    test_multiple_hits();
    test_reinsert_goes_to_head();
    test_pinned_skipped();

    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
