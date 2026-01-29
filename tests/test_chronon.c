/**
 * @file test_chronon.c
 * @brief Test suite for MC7: Chronon - Causal Time
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seraph/chronon.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_TRUE(x) ASSERT((x) == true)
#define ASSERT_FALSE(x) ASSERT((x) == false)

/*============================================================================
 * Local Clock Tests
 *============================================================================*/

TEST(localclock_init) {
    Seraph_LocalClock clock;
    Seraph_Vbit result = seraph_localclock_init(&clock, 42);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_EQ(clock.current, SERAPH_CHRONON_ZERO);
    ASSERT_EQ(clock.id, 42);
}

TEST(localclock_init_null) {
    Seraph_Vbit result = seraph_localclock_init(NULL, 1);
    ASSERT_EQ(result, SERAPH_VBIT_FALSE);
}

TEST(localclock_init_void_id) {
    Seraph_LocalClock clock;
    Seraph_Vbit result = seraph_localclock_init(&clock, SERAPH_VOID_U32);
    ASSERT_EQ(result, SERAPH_VBIT_FALSE);
}

TEST(localclock_tick) {
    Seraph_LocalClock clock;
    seraph_localclock_init(&clock, 1);

    Seraph_Chronon t1 = seraph_localclock_tick(&clock);
    ASSERT_EQ(t1, 1);

    Seraph_Chronon t2 = seraph_localclock_tick(&clock);
    ASSERT_EQ(t2, 2);

    Seraph_Chronon t3 = seraph_localclock_tick(&clock);
    ASSERT_EQ(t3, 3);
}

TEST(localclock_tick_null) {
    Seraph_Chronon t = seraph_localclock_tick(NULL);
    ASSERT_TRUE(seraph_chronon_is_void(t));
}

TEST(localclock_read) {
    Seraph_LocalClock clock;
    seraph_localclock_init(&clock, 1);

    ASSERT_EQ(seraph_localclock_read(&clock), 0);

    seraph_localclock_tick(&clock);
    ASSERT_EQ(seraph_localclock_read(&clock), 1);

    seraph_localclock_tick(&clock);
    seraph_localclock_tick(&clock);
    ASSERT_EQ(seraph_localclock_read(&clock), 3);
}

TEST(localclock_merge) {
    Seraph_LocalClock clock;
    seraph_localclock_init(&clock, 1);

    /* Start at 0, tick to 1 */
    seraph_localclock_tick(&clock);
    ASSERT_EQ(clock.current, 1);

    /* Merge with 5: max(1, 5) + 1 = 6 */
    Seraph_Chronon result = seraph_localclock_merge(&clock, 5);
    ASSERT_EQ(result, 6);
    ASSERT_EQ(clock.current, 6);

    /* Merge with 3: max(6, 3) + 1 = 7 */
    result = seraph_localclock_merge(&clock, 3);
    ASSERT_EQ(result, 7);
    ASSERT_EQ(clock.current, 7);
}

TEST(localclock_merge_void) {
    Seraph_LocalClock clock;
    seraph_localclock_init(&clock, 1);

    Seraph_Chronon result = seraph_localclock_merge(&clock, SERAPH_CHRONON_VOID);
    ASSERT_TRUE(seraph_chronon_is_void(result));
}

/*============================================================================
 * Scalar Chronon Tests
 *============================================================================*/

TEST(chronon_void_detection) {
    ASSERT_TRUE(seraph_chronon_is_void(SERAPH_CHRONON_VOID));
    ASSERT_FALSE(seraph_chronon_is_void(0));
    ASSERT_FALSE(seraph_chronon_is_void(1));
    ASSERT_FALSE(seraph_chronon_is_void(SERAPH_CHRONON_MAX));
}

TEST(chronon_exists) {
    ASSERT_FALSE(seraph_chronon_exists(SERAPH_CHRONON_VOID));
    ASSERT_TRUE(seraph_chronon_exists(0));
    ASSERT_TRUE(seraph_chronon_exists(12345));
}

TEST(chronon_compare) {
    ASSERT_EQ(seraph_chronon_compare(1, 2), SERAPH_CAUSAL_BEFORE);
    ASSERT_EQ(seraph_chronon_compare(5, 5), SERAPH_CAUSAL_EQUAL);
    ASSERT_EQ(seraph_chronon_compare(10, 3), SERAPH_CAUSAL_AFTER);
    ASSERT_EQ(seraph_chronon_compare(SERAPH_CHRONON_VOID, 1), SERAPH_CAUSAL_VOID);
    ASSERT_EQ(seraph_chronon_compare(1, SERAPH_CHRONON_VOID), SERAPH_CAUSAL_VOID);
}

TEST(chronon_max) {
    ASSERT_EQ(seraph_chronon_max(3, 7), 7);
    ASSERT_EQ(seraph_chronon_max(10, 5), 10);
    ASSERT_EQ(seraph_chronon_max(5, 5), 5);
    ASSERT_TRUE(seraph_chronon_is_void(seraph_chronon_max(SERAPH_CHRONON_VOID, 5)));
    ASSERT_TRUE(seraph_chronon_is_void(seraph_chronon_max(5, SERAPH_CHRONON_VOID)));
}

TEST(chronon_min) {
    ASSERT_EQ(seraph_chronon_min(3, 7), 3);
    ASSERT_EQ(seraph_chronon_min(10, 5), 5);
    ASSERT_EQ(seraph_chronon_min(5, 5), 5);
    ASSERT_TRUE(seraph_chronon_is_void(seraph_chronon_min(SERAPH_CHRONON_VOID, 5)));
}

TEST(chronon_add) {
    ASSERT_EQ(seraph_chronon_add(5, 3), 8);
    ASSERT_EQ(seraph_chronon_add(0, 100), 100);
    ASSERT_TRUE(seraph_chronon_is_void(seraph_chronon_add(SERAPH_CHRONON_VOID, 1)));
    /* Near-overflow: CHRONON_MAX + 1 should VOID */
    ASSERT_TRUE(seraph_chronon_is_void(seraph_chronon_add(SERAPH_CHRONON_MAX, 1)));
}

TEST(chronon_void_mask) {
    uint64_t mask_void = seraph_chronon_void_mask(SERAPH_CHRONON_VOID);
    ASSERT_EQ(mask_void, ~0ULL);

    uint64_t mask_valid = seraph_chronon_void_mask(42);
    ASSERT_EQ(mask_valid, 0ULL);
}

/*============================================================================
 * Event Tests
 *============================================================================*/

TEST(event_create) {
    Seraph_Event e = seraph_event_create(100, 1, 1, 0xABCD);
    ASSERT_FALSE(seraph_event_is_void(e));
    ASSERT_EQ(e.timestamp, 100);
    ASSERT_EQ(e.source_id, 1);
    ASSERT_EQ(e.sequence, 1);
    ASSERT_EQ(e.payload_hash, 0xABCD);
    ASSERT_EQ(e.predecessor, 0);  /* Genesis event */
}

TEST(event_create_void_timestamp) {
    Seraph_Event e = seraph_event_create(SERAPH_CHRONON_VOID, 1, 1, 0);
    ASSERT_TRUE(seraph_event_is_void(e));
}

TEST(event_is_genesis) {
    Seraph_Event e = seraph_event_create(1, 0, 0, 0);
    ASSERT_TRUE(seraph_event_is_genesis(e));

    Seraph_Event chained = seraph_event_chain(e, 2, 0, 1, 0);
    ASSERT_FALSE(seraph_event_is_genesis(chained));
}

TEST(event_chain) {
    Seraph_Event e1 = seraph_event_create(1, 0, 0, 0x1111);
    ASSERT_FALSE(seraph_event_is_void(e1));

    Seraph_Event e2 = seraph_event_chain(e1, 2, 0, 1, 0x2222);
    ASSERT_FALSE(seraph_event_is_void(e2));
    ASSERT_EQ(e2.timestamp, 2);
    ASSERT_NE(e2.predecessor, 0);  /* Should have predecessor hash */
    ASSERT_EQ(e2.predecessor, seraph_event_hash(e1));
}

TEST(event_chain_invalid_timestamp) {
    Seraph_Event e1 = seraph_event_create(5, 0, 0, 0);

    /* Can't chain with earlier timestamp */
    Seraph_Event e2 = seraph_event_chain(e1, 3, 0, 1, 0);
    ASSERT_TRUE(seraph_event_is_void(e2));

    /* Can't chain with same timestamp */
    Seraph_Event e3 = seraph_event_chain(e1, 5, 0, 1, 0);
    ASSERT_TRUE(seraph_event_is_void(e3));
}

TEST(event_hash_consistency) {
    Seraph_Event e = seraph_event_create(123, 45, 67, 0xDEADBEEF);

    uint64_t hash1 = seraph_event_hash(e);
    uint64_t hash2 = seraph_event_hash(e);

    ASSERT_EQ(hash1, hash2);  /* Same event = same hash */
    ASSERT_NE(hash1, SERAPH_VOID_U64);  /* Should not be VOID */
}

TEST(event_hash_different_events) {
    Seraph_Event e1 = seraph_event_create(1, 0, 0, 0);
    Seraph_Event e2 = seraph_event_create(2, 0, 0, 0);

    uint64_t hash1 = seraph_event_hash(e1);
    uint64_t hash2 = seraph_event_hash(e2);

    ASSERT_NE(hash1, hash2);  /* Different events = different hashes */
}

TEST(event_compare) {
    Seraph_Event e1 = seraph_event_create(1, 0, 0, 0);
    Seraph_Event e2 = seraph_event_create(2, 0, 1, 0);

    ASSERT_EQ(seraph_event_compare(e1, e2), SERAPH_CAUSAL_BEFORE);
    ASSERT_EQ(seraph_event_compare(e2, e1), SERAPH_CAUSAL_AFTER);
    ASSERT_EQ(seraph_event_compare(e1, e1), SERAPH_CAUSAL_EQUAL);
}

/*============================================================================
 * Vector Clock Tests
 *============================================================================*/

TEST(vclock_init) {
    Seraph_VectorClock vclock;
    Seraph_Vbit result = seraph_vclock_init(&vclock, 3, 0);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_EQ(vclock.node_count, 3);
    ASSERT_EQ(vclock.self_id, 0);
    ASSERT_TRUE(seraph_vclock_is_valid(&vclock));

    /* All components should be 0 */
    ASSERT_EQ(seraph_vclock_get(&vclock, 0), 0);
    ASSERT_EQ(seraph_vclock_get(&vclock, 1), 0);
    ASSERT_EQ(seraph_vclock_get(&vclock, 2), 0);

    seraph_vclock_destroy(&vclock);
}

TEST(vclock_init_invalid) {
    Seraph_VectorClock vclock;

    /* NULL pointer */
    ASSERT_EQ(seraph_vclock_init(NULL, 3, 0), SERAPH_VBIT_VOID);

    /* Zero nodes */
    ASSERT_EQ(seraph_vclock_init(&vclock, 0, 0), SERAPH_VBIT_FALSE);

    /* self_id >= node_count */
    ASSERT_EQ(seraph_vclock_init(&vclock, 3, 5), SERAPH_VBIT_FALSE);
}

TEST(vclock_tick) {
    Seraph_VectorClock vclock;
    seraph_vclock_init(&vclock, 3, 1);  /* Node 1 */

    Seraph_Chronon t1 = seraph_vclock_tick(&vclock);
    ASSERT_EQ(t1, 1);
    ASSERT_EQ(seraph_vclock_get(&vclock, 1), 1);
    ASSERT_EQ(seraph_vclock_get(&vclock, 0), 0);  /* Other nodes unchanged */
    ASSERT_EQ(seraph_vclock_get(&vclock, 2), 0);

    Seraph_Chronon t2 = seraph_vclock_tick(&vclock);
    ASSERT_EQ(t2, 2);
    ASSERT_EQ(seraph_vclock_get(&vclock, 1), 2);

    seraph_vclock_destroy(&vclock);
}

TEST(vclock_snapshot) {
    Seraph_VectorClock vclock;
    seraph_vclock_init(&vclock, 3, 0);

    seraph_vclock_tick(&vclock);  /* [1, 0, 0] */

    Seraph_Chronon buffer[3];
    uint32_t count = seraph_vclock_snapshot(&vclock, buffer, 3);

    ASSERT_EQ(count, 3);
    ASSERT_EQ(buffer[0], 1);
    ASSERT_EQ(buffer[1], 0);
    ASSERT_EQ(buffer[2], 0);

    seraph_vclock_destroy(&vclock);
}

TEST(vclock_receive) {
    Seraph_VectorClock vclock;
    seraph_vclock_init(&vclock, 3, 0);  /* Node 0 */

    /* Start: [0, 0, 0], tick: [1, 0, 0] */
    seraph_vclock_tick(&vclock);

    /* Receive [0, 5, 3] */
    Seraph_Chronon received[] = {0, 5, 3};
    Seraph_Vbit result = seraph_vclock_receive(&vclock, received, 3);

    ASSERT_EQ(result, SERAPH_VBIT_TRUE);

    /* After receive: max([1,0,0], [0,5,3]) + tick = [2, 5, 3] */
    ASSERT_EQ(seraph_vclock_get(&vclock, 0), 2);
    ASSERT_EQ(seraph_vclock_get(&vclock, 1), 5);
    ASSERT_EQ(seraph_vclock_get(&vclock, 2), 3);

    seraph_vclock_destroy(&vclock);
}

TEST(vclock_compare_before) {
    Seraph_VectorClock a, b;
    seraph_vclock_init(&a, 3, 0);
    seraph_vclock_init(&b, 3, 1);

    /* a = [1, 0, 0] */
    seraph_vclock_tick(&a);

    /* b = [1, 1, 0] */
    Seraph_Chronon a_snap[] = {1, 0, 0};
    seraph_vclock_receive(&b, a_snap, 3);  /* b becomes [1, 1, 0] */

    /* a → b (a happens-before b) */
    Seraph_CausalOrder order = seraph_vclock_compare(&a, &b);
    ASSERT_EQ(order, SERAPH_CAUSAL_BEFORE);

    Seraph_Vbit hb = seraph_vclock_happens_before(&a, &b);
    ASSERT_EQ(hb, SERAPH_VBIT_TRUE);

    seraph_vclock_destroy(&a);
    seraph_vclock_destroy(&b);
}

TEST(vclock_compare_concurrent) {
    Seraph_VectorClock a, b;
    seraph_vclock_init(&a, 3, 0);
    seraph_vclock_init(&b, 3, 1);

    /* a = [1, 0, 0] (node 0 ticks independently) */
    seraph_vclock_tick(&a);

    /* b = [0, 1, 0] (node 1 ticks independently) */
    seraph_vclock_tick(&b);

    /* a || b (concurrent - neither ordered) */
    Seraph_CausalOrder order = seraph_vclock_compare(&a, &b);
    ASSERT_EQ(order, SERAPH_CAUSAL_CONCURRENT);

    Seraph_Vbit conc = seraph_vclock_is_concurrent(&a, &b);
    ASSERT_EQ(conc, SERAPH_VBIT_TRUE);

    Seraph_Vbit hb = seraph_vclock_happens_before(&a, &b);
    ASSERT_EQ(hb, SERAPH_VBIT_FALSE);

    seraph_vclock_destroy(&a);
    seraph_vclock_destroy(&b);
}

TEST(vclock_compare_equal) {
    Seraph_VectorClock a, b;
    seraph_vclock_init(&a, 3, 0);
    seraph_vclock_init(&b, 3, 0);

    /* Both at [0, 0, 0] */
    ASSERT_EQ(seraph_vclock_compare(&a, &b), SERAPH_CAUSAL_EQUAL);

    seraph_vclock_tick(&a);
    seraph_vclock_tick(&b);

    /* Both at [1, 0, 0] */
    ASSERT_EQ(seraph_vclock_compare(&a, &b), SERAPH_CAUSAL_EQUAL);

    seraph_vclock_destroy(&a);
    seraph_vclock_destroy(&b);
}

TEST(vclock_copy) {
    Seraph_VectorClock a, b;
    seraph_vclock_init(&a, 3, 0);
    seraph_vclock_init(&b, 3, 1);

    seraph_vclock_tick(&a);
    seraph_vclock_tick(&a);  /* a = [2, 0, 0] */

    Seraph_Vbit result = seraph_vclock_copy(&b, &a);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);

    ASSERT_EQ(seraph_vclock_get(&b, 0), 2);
    ASSERT_EQ(seraph_vclock_get(&b, 1), 0);
    ASSERT_EQ(seraph_vclock_get(&b, 2), 0);

    seraph_vclock_destroy(&a);
    seraph_vclock_destroy(&b);
}

TEST(vclock_merge) {
    Seraph_VectorClock a, b;
    seraph_vclock_init(&a, 3, 0);
    seraph_vclock_init(&b, 3, 1);

    /* a = [3, 0, 0] */
    seraph_vclock_tick(&a);
    seraph_vclock_tick(&a);
    seraph_vclock_tick(&a);

    /* b = [0, 2, 0] */
    seraph_vclock_tick(&b);
    seraph_vclock_tick(&b);

    /* Merge into a: [max(3,0), max(0,2), max(0,0)] = [3, 2, 0] */
    Seraph_Vbit result = seraph_vclock_merge(&a, &b);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);

    ASSERT_EQ(seraph_vclock_get(&a, 0), 3);
    ASSERT_EQ(seraph_vclock_get(&a, 1), 2);
    ASSERT_EQ(seraph_vclock_get(&a, 2), 0);

    seraph_vclock_destroy(&a);
    seraph_vclock_destroy(&b);
}

TEST(vclock_size_mismatch) {
    Seraph_VectorClock a, b;
    seraph_vclock_init(&a, 3, 0);
    seraph_vclock_init(&b, 5, 0);  /* Different size */

    ASSERT_EQ(seraph_vclock_compare(&a, &b), SERAPH_CAUSAL_VOID);
    ASSERT_EQ(seraph_vclock_copy(&a, &b), SERAPH_VBIT_FALSE);
    ASSERT_EQ(seraph_vclock_merge(&a, &b), SERAPH_VBIT_FALSE);

    seraph_vclock_destroy(&a);
    seraph_vclock_destroy(&b);
}

/*============================================================================
 * Integration Tests
 *============================================================================*/

TEST(distributed_scenario) {
    /*
     * Simulate a 3-node distributed system:
     *   Node 0: Process A
     *   Node 1: Process B
     *   Node 2: Process C
     *
     * Timeline:
     *   1. A does local work
     *   2. A sends message to B
     *   3. B does local work
     *   4. C does local work (concurrent with B)
     *   5. B sends message to C
     */

    Seraph_VectorClock a, b, c;
    seraph_vclock_init(&a, 3, 0);
    seraph_vclock_init(&b, 3, 1);
    seraph_vclock_init(&c, 3, 2);

    /* 1. A does local work: A = [1, 0, 0] */
    seraph_vclock_tick(&a);

    /* 2. A sends to B: B receives [1, 0, 0], becomes [1, 1, 0] */
    Seraph_Chronon a_snap1[3];
    seraph_vclock_snapshot(&a, a_snap1, 3);
    seraph_vclock_receive(&b, a_snap1, 3);

    /* Verify: A → B */
    ASSERT_EQ(seraph_vclock_happens_before(&a, &b), SERAPH_VBIT_TRUE);

    /* 3. B does local work: B = [1, 2, 0] */
    seraph_vclock_tick(&b);

    /* 4. C does local work (independently): C = [0, 0, 1] */
    seraph_vclock_tick(&c);

    /* Verify: B || C (concurrent) */
    ASSERT_EQ(seraph_vclock_is_concurrent(&b, &c), SERAPH_VBIT_TRUE);

    /* 5. B sends to C: C receives [1, 2, 0], becomes [1, 2, 2] */
    Seraph_Chronon b_snap[3];
    seraph_vclock_snapshot(&b, b_snap, 3);
    seraph_vclock_receive(&c, b_snap, 3);

    /* Verify: B → C now */
    ASSERT_EQ(seraph_vclock_happens_before(&b, &c), SERAPH_VBIT_TRUE);

    /* Verify: A → C (transitivity) */
    ASSERT_EQ(seraph_vclock_happens_before(&a, &c), SERAPH_VBIT_TRUE);

    seraph_vclock_destroy(&a);
    seraph_vclock_destroy(&b);
    seraph_vclock_destroy(&c);
}

TEST(event_chain_scenario) {
    /*
     * Create a chain of events representing a transaction:
     *   Genesis → Prepare → Commit → Finalize
     */

    Seraph_LocalClock clock;
    seraph_localclock_init(&clock, 0);

    /* Genesis event */
    Seraph_Chronon t1 = seraph_localclock_tick(&clock);
    Seraph_Event genesis = seraph_event_create(t1, 0, 0, 0);
    ASSERT_TRUE(seraph_event_is_genesis(genesis));

    /* Prepare event */
    Seraph_Chronon t2 = seraph_localclock_tick(&clock);
    Seraph_Event prepare = seraph_event_chain(genesis, t2, 0, 1, 0xAAAA);
    ASSERT_FALSE(seraph_event_is_void(prepare));
    ASSERT_EQ(prepare.predecessor, seraph_event_hash(genesis));

    /* Commit event */
    Seraph_Chronon t3 = seraph_localclock_tick(&clock);
    Seraph_Event commit = seraph_event_chain(prepare, t3, 0, 2, 0xBBBB);
    ASSERT_FALSE(seraph_event_is_void(commit));
    ASSERT_EQ(commit.predecessor, seraph_event_hash(prepare));

    /* Finalize event */
    Seraph_Chronon t4 = seraph_localclock_tick(&clock);
    Seraph_Event finalize = seraph_event_chain(commit, t4, 0, 3, 0xCCCC);
    ASSERT_FALSE(seraph_event_is_void(finalize));

    /* Verify causal ordering */
    ASSERT_EQ(seraph_event_compare(genesis, prepare), SERAPH_CAUSAL_BEFORE);
    ASSERT_EQ(seraph_event_compare(prepare, commit), SERAPH_CAUSAL_BEFORE);
    ASSERT_EQ(seraph_event_compare(commit, finalize), SERAPH_CAUSAL_BEFORE);
    ASSERT_EQ(seraph_event_compare(genesis, finalize), SERAPH_CAUSAL_BEFORE);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_chronon_tests(void) {
    printf("\n=== MC7: Chronon Tests ===\n\n");

    printf("Local Clock Tests:\n");
    RUN_TEST(localclock_init);
    RUN_TEST(localclock_init_null);
    RUN_TEST(localclock_init_void_id);
    RUN_TEST(localclock_tick);
    RUN_TEST(localclock_tick_null);
    RUN_TEST(localclock_read);
    RUN_TEST(localclock_merge);
    RUN_TEST(localclock_merge_void);

    printf("\nScalar Chronon Tests:\n");
    RUN_TEST(chronon_void_detection);
    RUN_TEST(chronon_exists);
    RUN_TEST(chronon_compare);
    RUN_TEST(chronon_max);
    RUN_TEST(chronon_min);
    RUN_TEST(chronon_add);
    RUN_TEST(chronon_void_mask);

    printf("\nEvent Tests:\n");
    RUN_TEST(event_create);
    RUN_TEST(event_create_void_timestamp);
    RUN_TEST(event_is_genesis);
    RUN_TEST(event_chain);
    RUN_TEST(event_chain_invalid_timestamp);
    RUN_TEST(event_hash_consistency);
    RUN_TEST(event_hash_different_events);
    RUN_TEST(event_compare);

    printf("\nVector Clock Tests:\n");
    RUN_TEST(vclock_init);
    RUN_TEST(vclock_init_invalid);
    RUN_TEST(vclock_tick);
    RUN_TEST(vclock_snapshot);
    RUN_TEST(vclock_receive);
    RUN_TEST(vclock_compare_before);
    RUN_TEST(vclock_compare_concurrent);
    RUN_TEST(vclock_compare_equal);
    RUN_TEST(vclock_copy);
    RUN_TEST(vclock_merge);
    RUN_TEST(vclock_size_mismatch);

    printf("\nIntegration Tests:\n");
    RUN_TEST(distributed_scenario);
    RUN_TEST(event_chain_scenario);

    printf("\nChronon Tests: %d/%d passed\n", tests_passed, tests_run);
}
