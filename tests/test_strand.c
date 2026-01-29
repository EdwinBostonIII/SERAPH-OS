/**
 * @file test_strand.c
 * @brief Tests for MC13: Strand - Capability-Isolated Temporal Threading
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seraph/strand.h"

static int tests_run = 0;
static int tests_passed = 0;
static int test_failed_flag = 0;

/*
 * Static storage for Seraph_Strand structures to avoid stack overflow.
 * Each Seraph_Strand is 11KB - too large for stack allocation in tests.
 */
static Seraph_Strand s_strand1;
static Seraph_Strand s_strand2;
static Seraph_Strand s_strand3;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  " #name "... "); \
    tests_run++; \
    test_failed_flag = 0; \
    name(); \
    if (!test_failed_flag) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        test_failed_flag = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED at line %d: %s != %s (got %lld vs %lld)\n", \
               __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        test_failed_flag = 1; \
        return; \
    } \
} while(0)

/*============================================================================
 * Test Entry Points
 *============================================================================*/

static volatile int test_entry_called = 0;
static volatile uint64_t test_entry_arg = 0;

static void test_entry_simple(void* arg) {
    test_entry_called = 1;
    test_entry_arg = (uint64_t)(uintptr_t)arg;
}

static void test_entry_yield(void* arg) {
    (void)arg;
    seraph_strand_yield();
    test_entry_called = 1;
}

static void test_entry_exit(void* arg) {
    uint64_t code = (uint64_t)(uintptr_t)arg;
    seraph_strand_exit(code);
}

/* NOTE: These functions are available for future tests
static void test_entry_tick(void* arg) {
    (void)arg;
    for (int i = 0; i < 100; i++) {
        seraph_strand_tick();
    }
    test_entry_called = 1;
}

static void test_entry_local_alloc(void* arg) {
    (void)arg;
    void* ptr = seraph_strand_local_alloc(1024);
    test_entry_called = (ptr != NULL) ? 1 : 0;
}
*/

/*============================================================================
 * State Machine Tests
 *============================================================================*/

TEST(test_strand_initial_state) {
    Seraph_Strand* strand = &s_strand1;
    Seraph_Strand_Error err = seraph_strand_create(strand, test_entry_simple, NULL, 0);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT_EQ(strand->state, SERAPH_STRAND_NASCENT);
    seraph_strand_destroy(strand);
}

TEST(test_strand_start_transition) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    ASSERT_EQ(strand->state, SERAPH_STRAND_NASCENT);

    Seraph_Strand_Error err = seraph_strand_start(strand);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT_EQ(strand->state, SERAPH_STRAND_READY);

    seraph_strand_destroy(strand);
}

TEST(test_strand_invalid_start) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_start(strand);
    ASSERT_EQ(strand->state, SERAPH_STRAND_READY);

    /* Starting again should fail */
    Seraph_Strand_Error err = seraph_strand_start(strand);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_STATE);

    seraph_strand_destroy(strand);
}

TEST(test_strand_terminated_state) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_start(strand);

    /* Run to completion */
    test_entry_called = 0;
    seraph_strand_run_quantum(strand);
    ASSERT_EQ(strand->state, SERAPH_STRAND_TERMINATED);
    ASSERT_EQ(test_entry_called, 1);

    seraph_strand_destroy(strand);
}

TEST(test_strand_state_string) {
    ASSERT(strcmp(seraph_strand_state_string(SERAPH_STRAND_NASCENT), "NASCENT") == 0);
    ASSERT(strcmp(seraph_strand_state_string(SERAPH_STRAND_READY), "READY") == 0);
    ASSERT(strcmp(seraph_strand_state_string(SERAPH_STRAND_RUNNING), "RUNNING") == 0);
    ASSERT(strcmp(seraph_strand_state_string(SERAPH_STRAND_BLOCKED), "BLOCKED") == 0);
    ASSERT(strcmp(seraph_strand_state_string(SERAPH_STRAND_WAITING), "WAITING") == 0);
    ASSERT(strcmp(seraph_strand_state_string(SERAPH_STRAND_TERMINATED), "TERMINATED") == 0);
}

/*============================================================================
 * Creation Tests
 *============================================================================*/

TEST(test_strand_create_basic) {
    Seraph_Strand* strand = &s_strand1;
    Seraph_Strand_Error err = seraph_strand_create(strand, test_entry_simple, NULL, 0);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT(strand->strand_id > 0);
    ASSERT(strand->stack_base != NULL);
    ASSERT_EQ(strand->stack_size, SERAPH_STRAND_DEFAULT_STACK_SIZE);
    seraph_strand_destroy(strand);
}

TEST(test_strand_create_with_arg) {
    Seraph_Strand* strand = &s_strand1;
    void* arg = (void*)0x12345678;
    seraph_strand_create(strand, test_entry_simple, arg, 0);
    ASSERT(strand->entry_arg == arg);
    seraph_strand_destroy(strand);
}

TEST(test_strand_create_custom_stack) {
    Seraph_Strand* strand = &s_strand1;
    size_t custom_size = 128 * 1024;  /* 128KB */
    Seraph_Strand_Error err = seraph_strand_create(strand, test_entry_simple, NULL, custom_size);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT_EQ(strand->stack_size, custom_size);
    seraph_strand_destroy(strand);
}

TEST(test_strand_create_null_entry) {
    Seraph_Strand* strand = &s_strand1;
    Seraph_Strand_Error err = seraph_strand_create(strand, NULL, NULL, 0);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_INVALID);
}

TEST(test_strand_create_null_strand) {
    Seraph_Strand_Error err = seraph_strand_create(NULL, test_entry_simple, NULL, 0);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_NULL);
}

TEST(test_strand_unique_ids) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    Seraph_Strand* s3 = &s_strand3;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);
    seraph_strand_create(s3, test_entry_simple, NULL, 0);

    ASSERT(s1->strand_id != s2->strand_id);
    ASSERT(s2->strand_id != s3->strand_id);
    ASSERT(s1->strand_id != s3->strand_id);

    seraph_strand_destroy(s3);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

/*============================================================================
 * Capability Table Tests
 *============================================================================*/

TEST(test_strand_cap_table_init) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);

    /* All slots should be VOID */
    ASSERT_EQ(strand->cap_count, 0);
    for (uint32_t i = 0; i < SERAPH_STRAND_CAP_TABLE_SIZE; i++) {
        ASSERT_EQ(strand->cap_table[i].status, SERAPH_CAP_STATUS_VOID);
        ASSERT(seraph_cap_is_void(strand->cap_table[i].cap));
    }

    seraph_strand_destroy(strand);
}

TEST(test_strand_cap_store_get) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);

    /* Create a test capability */
    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);

    /* Store it */
    Seraph_Strand_Error err = seraph_strand_cap_store(strand, 5, cap);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT_EQ(strand->cap_count, 1);

    /* Get it back */
    Seraph_Capability retrieved = seraph_strand_cap_get(strand, 5);
    ASSERT(seraph_cap_same_region(cap, retrieved));

    seraph_strand_destroy(strand);
}

TEST(test_strand_cap_find_slot) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);

    /* First empty slot should be 0 */
    uint32_t slot = seraph_strand_cap_find_slot(strand);
    ASSERT_EQ(slot, 0);

    /* Fill slot 0 */
    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);
    seraph_strand_cap_store(strand, 0, cap);

    /* Next empty slot should be 1 */
    slot = seraph_strand_cap_find_slot(strand);
    ASSERT_EQ(slot, 1);

    seraph_strand_destroy(strand);
}

TEST(test_strand_cap_clear) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);

    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);
    seraph_strand_cap_store(strand, 10, cap);
    ASSERT_EQ(strand->cap_count, 1);

    /* Clear it */
    Seraph_Strand_Error err = seraph_strand_cap_clear(strand, 10);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT_EQ(strand->cap_count, 0);

    /* Should be VOID now */
    Seraph_Capability cleared = seraph_strand_cap_get(strand, 10);
    ASSERT(seraph_cap_is_void(cleared));

    seraph_strand_destroy(strand);
}

/*============================================================================
 * Capability Grant Tests
 *============================================================================*/

TEST(test_strand_grant_basic) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    /* Set s1 as current */
    seraph_strand_set_current(s1);

    /* Store a capability in s1 */
    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);
    seraph_strand_cap_store(s1, 0, cap);
    ASSERT_EQ(s1->cap_count, 1);

    /* Grant to s2 */
    Seraph_Strand_Error err = seraph_strand_grant(s2, 0, 0);
    ASSERT_EQ(err, SERAPH_STRAND_OK);

    /* s1's cap should be VOID now */
    ASSERT_EQ(s1->cap_count, 0);
    Seraph_Capability s1_cap = seraph_strand_cap_get(s1, 0);
    ASSERT(seraph_cap_is_void(s1_cap));

    /* s2 should have the cap */
    ASSERT_EQ(s2->cap_count, 1);
    Seraph_Capability s2_cap = seraph_strand_cap_get(s2, 0);
    ASSERT(!seraph_cap_is_void(s2_cap));
    ASSERT(seraph_cap_same_region(cap, s2_cap));

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

/*============================================================================
 * Capability Lend Tests
 *============================================================================*/

TEST(test_strand_lend_basic) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    seraph_strand_set_current(s1);

    /* Store a capability in s1 */
    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);
    seraph_strand_cap_store(s1, 0, cap);

    /* Lend to s2 with timeout */
    Seraph_Strand_Error err = seraph_strand_lend(s2, 0, 0, 1000);
    ASSERT_EQ(err, SERAPH_STRAND_OK);

    /* s1's cap should be LENT */
    ASSERT_EQ(s1->cap_table[0].status, SERAPH_CAP_STATUS_LENT);

    /* s2 should have BORROWED cap */
    ASSERT_EQ(s2->cap_table[0].status, SERAPH_CAP_STATUS_BORROWED);
    ASSERT_EQ(s2->cap_table[0].timeout, (Seraph_Chronon)1000);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

TEST(test_strand_lend_timeout_expiry) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    seraph_strand_set_current(s1);

    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);
    seraph_strand_cap_store(s1, 0, cap);

    /* Lend with short timeout */
    seraph_strand_lend(s2, 0, 0, 50);

    /* s2's chronon is past timeout */
    s2->chronon = 100;

    /* Process lends - should expire */
    seraph_strand_process_lends(s2);

    /* s2's cap should now be VOID */
    ASSERT_EQ(s2->cap_table[0].status, SERAPH_CAP_STATUS_VOID);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

TEST(test_strand_revoke) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    seraph_strand_set_current(s1);

    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);
    seraph_strand_cap_store(s1, 0, cap);
    seraph_strand_lend(s2, 0, 0, 10000);

    /* Revoke early */
    Seraph_Strand_Error err = seraph_strand_revoke(0);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT_EQ(s1->cap_table[0].status, SERAPH_CAP_STATUS_OWNED);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

TEST(test_strand_return) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    seraph_strand_set_current(s1);

    char buffer[64];
    Seraph_Capability cap = seraph_cap_create(buffer, 64, 1, SERAPH_CAP_RW);
    seraph_strand_cap_store(s1, 0, cap);
    seraph_strand_lend(s2, 0, 0, 10000);

    /* s2 returns early */
    seraph_strand_set_current(s2);
    Seraph_Strand_Error err = seraph_strand_return(0);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT_EQ(s2->cap_table[0].status, SERAPH_CAP_STATUS_VOID);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

/*============================================================================
 * Mutex Tests
 *============================================================================*/

TEST(test_mutex_init) {
    Seraph_Strand_Mutex mutex;
    Seraph_Strand_Error err = seraph_strand_mutex_init(&mutex);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT(mutex.holder == NULL);
    ASSERT(mutex.wait_queue == NULL);
    ASSERT_EQ(mutex.acquisitions, (uint64_t)0);
    seraph_strand_mutex_destroy(&mutex);
}

TEST(test_mutex_acquire_uncontended) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_set_current(strand);

    Seraph_Strand_Mutex mutex;
    seraph_strand_mutex_init(&mutex);

    Seraph_Capability cap = seraph_strand_mutex_acquire(&mutex, 0);
    ASSERT(!seraph_cap_is_void(cap));
    ASSERT(mutex.holder == strand);
    ASSERT_EQ(mutex.acquisitions, (uint64_t)1);

    seraph_strand_mutex_destroy(&mutex);
    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

TEST(test_mutex_release) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_set_current(strand);

    Seraph_Strand_Mutex mutex;
    seraph_strand_mutex_init(&mutex);

    Seraph_Capability cap = seraph_strand_mutex_acquire(&mutex, 0);
    ASSERT(mutex.holder == strand);

    Seraph_Strand_Error err = seraph_strand_mutex_release(&mutex, cap);
    ASSERT_EQ(err, SERAPH_STRAND_OK);
    ASSERT(mutex.holder == NULL);

    seraph_strand_mutex_destroy(&mutex);
    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

TEST(test_mutex_try_acquire_success) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_set_current(strand);

    Seraph_Strand_Mutex mutex;
    seraph_strand_mutex_init(&mutex);

    Seraph_Capability cap = seraph_strand_mutex_try_acquire(&mutex, 0);
    ASSERT(!seraph_cap_is_void(cap));
    ASSERT(mutex.holder == strand);

    seraph_strand_mutex_destroy(&mutex);
    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

TEST(test_mutex_try_acquire_fail) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    Seraph_Strand_Mutex mutex;
    seraph_strand_mutex_init(&mutex);

    /* s1 acquires */
    seraph_strand_set_current(s1);
    Seraph_Capability cap1 = seraph_strand_mutex_try_acquire(&mutex, 0);
    ASSERT(!seraph_cap_is_void(cap1));

    /* s2 tries to acquire - should fail */
    seraph_strand_set_current(s2);
    Seraph_Capability cap2 = seraph_strand_mutex_try_acquire(&mutex, 0);
    ASSERT(seraph_cap_is_void(cap2));

    seraph_strand_mutex_destroy(&mutex);
    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

TEST(test_mutex_holder_only_release) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    Seraph_Strand_Mutex mutex;
    seraph_strand_mutex_init(&mutex);

    /* s1 acquires */
    seraph_strand_set_current(s1);
    Seraph_Capability cap = seraph_strand_mutex_acquire(&mutex, 0);
    ASSERT(!seraph_cap_is_void(cap));

    /* s2 tries to release - should fail */
    seraph_strand_set_current(s2);
    Seraph_Strand_Error err = seraph_strand_mutex_release(&mutex, cap);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_PERM);
    ASSERT(mutex.holder == s1);  /* Still held by s1 */

    seraph_strand_mutex_destroy(&mutex);
    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

/*============================================================================
 * Chronon Tests
 *============================================================================*/

TEST(test_strand_chronon_init_zero) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    ASSERT_EQ(strand->chronon, SERAPH_CHRONON_ZERO);
    seraph_strand_destroy(strand);
}

TEST(test_strand_chronon_tick) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_set_current(strand);

    Seraph_Chronon t1 = seraph_strand_chronon();
    ASSERT_EQ(t1, SERAPH_CHRONON_ZERO);

    Seraph_Chronon t2 = seraph_strand_tick();
    ASSERT_EQ(t2, (Seraph_Chronon)1);

    Seraph_Chronon t3 = seraph_strand_tick();
    ASSERT_EQ(t3, (Seraph_Chronon)2);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

TEST(test_strand_chronon_independent) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    /* Tick s1 */
    seraph_strand_set_current(s1);
    seraph_strand_tick();
    seraph_strand_tick();
    ASSERT_EQ(s1->chronon, (Seraph_Chronon)2);

    /* s2 should still be at 0 */
    ASSERT_EQ(s2->chronon, SERAPH_CHRONON_ZERO);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

/*============================================================================
 * Strand-Local Storage Tests
 *============================================================================*/

TEST(test_strand_local_alloc) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_set_current(strand);

    void* ptr = seraph_strand_local_alloc(1024);
    ASSERT(ptr != NULL);

    /* Can write to it */
    memset(ptr, 0x42, 1024);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

TEST(test_strand_local_calloc) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_set_current(strand);

    uint8_t* ptr = (uint8_t*)seraph_strand_local_calloc(256);
    ASSERT(ptr != NULL);

    /* Should be zero-initialized */
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(ptr[i], 0);
    }

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

TEST(test_strand_local_remaining) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    seraph_strand_set_current(strand);

    size_t before = seraph_strand_local_remaining();
    seraph_strand_local_alloc(4096);
    size_t after = seraph_strand_local_remaining();

    ASSERT(after < before);
    ASSERT(before - after >= 4096);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

/*============================================================================
 * Join/Exit Tests
 *============================================================================*/

TEST(test_strand_join_terminated) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    /* Run s2 to completion */
    seraph_strand_start(s2);
    seraph_strand_run_quantum(s2);
    ASSERT_EQ(s2->state, SERAPH_STRAND_TERMINATED);

    /* s1 joins s2 - should return immediately */
    seraph_strand_start(s1);
    seraph_strand_set_current(s1);
    s1->state = SERAPH_STRAND_RUNNING;

    uint64_t exit_code = 0;
    Seraph_Strand_Error err = seraph_strand_join(s2, &exit_code);
    ASSERT_EQ(err, SERAPH_STRAND_OK);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

TEST(test_strand_exit_code) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_exit, (void*)42, 0);
    seraph_strand_start(strand);
    seraph_strand_run_quantum(strand);

    ASSERT_EQ(strand->state, SERAPH_STRAND_TERMINATED);
    ASSERT_EQ(strand->exit_code, (uint64_t)42);

    seraph_strand_destroy(strand);
}

/*============================================================================
 * Yield Tests
 *============================================================================*/

TEST(test_strand_yield_state) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_yield, NULL, 0);
    seraph_strand_start(strand);

    seraph_strand_set_current(strand);
    strand->state = SERAPH_STRAND_RUNNING;

    seraph_strand_yield();
    ASSERT_EQ(strand->state, SERAPH_STRAND_READY);
    ASSERT_EQ(strand->yield_count, (uint64_t)1);

    seraph_strand_set_current(NULL);
    seraph_strand_destroy(strand);
}

/*============================================================================
 * Deadlock Detection Tests
 *============================================================================*/

TEST(test_deadlock_detection_simple) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    /* s2 is waiting on s1 */
    s2->waiting_on = s1;
    s2->state = SERAPH_STRAND_WAITING;

    /* s1 joining s2 would create a cycle */
    Seraph_Vbit would_deadlock = seraph_strand_would_deadlock(s1, s2);
    ASSERT_EQ(would_deadlock, SERAPH_VBIT_TRUE);

    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

TEST(test_deadlock_detection_no_cycle) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);

    /* No waiting relationships */
    Seraph_Vbit would_deadlock = seraph_strand_would_deadlock(s1, s2);
    ASSERT_EQ(would_deadlock, SERAPH_VBIT_FALSE);

    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

/*============================================================================
 * Scheduler Tests
 *============================================================================*/

TEST(test_strand_schedule_single) {
    test_entry_called = 0;
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, (void*)123, 0);
    seraph_strand_start(strand);

    Seraph_Strand* strands[] = { strand };
    seraph_strand_schedule(strands, 1);

    ASSERT_EQ(strand->state, SERAPH_STRAND_TERMINATED);
    ASSERT_EQ(test_entry_called, 1);
    ASSERT_EQ(test_entry_arg, (uint64_t)123);

    seraph_strand_destroy(strand);
}

TEST(test_strand_schedule_multiple) {
    Seraph_Strand* s1 = &s_strand1;
    Seraph_Strand* s2 = &s_strand2;
    Seraph_Strand* s3 = &s_strand3;
    seraph_strand_create(s1, test_entry_simple, NULL, 0);
    seraph_strand_create(s2, test_entry_simple, NULL, 0);
    seraph_strand_create(s3, test_entry_simple, NULL, 0);
    seraph_strand_start(s1);
    seraph_strand_start(s2);
    seraph_strand_start(s3);

    Seraph_Strand* strands[] = { s1, s2, s3 };
    seraph_strand_schedule(strands, 3);

    ASSERT_EQ(s1->state, SERAPH_STRAND_TERMINATED);
    ASSERT_EQ(s2->state, SERAPH_STRAND_TERMINATED);
    ASSERT_EQ(s3->state, SERAPH_STRAND_TERMINATED);

    seraph_strand_destroy(s3);
    seraph_strand_destroy(s2);
    seraph_strand_destroy(s1);
}

/*============================================================================
 * VOID Propagation Tests
 *============================================================================*/

TEST(test_strand_void_operations) {
    /* NULL strand operations */
    Seraph_Strand_Error err = seraph_strand_start(NULL);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_NULL);

    err = seraph_strand_cap_store(NULL, 0, SERAPH_CAP_VOID);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_NULL);

    Seraph_Capability cap = seraph_strand_cap_get(NULL, 0);
    ASSERT(seraph_cap_is_void(cap));

    uint32_t slot = seraph_strand_cap_find_slot(NULL);
    ASSERT_EQ(slot, SERAPH_VOID_U32);
}

TEST(test_strand_null_params) {
    Seraph_Strand_Error err = seraph_strand_join(NULL, NULL);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_NULL);

    err = seraph_strand_mutex_init(NULL);
    ASSERT_EQ(err, SERAPH_STRAND_ERR_NULL);

    Seraph_Capability cap = seraph_strand_mutex_acquire(NULL, 0);
    ASSERT(seraph_cap_is_void(cap));
}

/*============================================================================
 * Utility Tests
 *============================================================================*/

TEST(test_strand_is_valid) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    ASSERT(seraph_strand_is_valid(strand));
    ASSERT(!seraph_strand_is_valid(NULL));

    seraph_strand_destroy(strand);
    /* After destroy, should not be valid */
    ASSERT(!seraph_strand_is_valid(strand));
}

TEST(test_strand_is_runnable) {
    Seraph_Strand* strand = &s_strand1;
    seraph_strand_create(strand, test_entry_simple, NULL, 0);
    ASSERT(!seraph_strand_is_runnable(strand));  /* NASCENT */

    seraph_strand_start(strand);
    ASSERT(seraph_strand_is_runnable(strand));  /* READY */

    strand->state = SERAPH_STRAND_TERMINATED;
    ASSERT(!seraph_strand_is_runnable(strand));  /* TERMINATED */

    seraph_strand_destroy(strand);
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void test_strand(void) {
    printf("\n=== MC13: Strand Tests ===\n");

    /* State machine tests */
    RUN_TEST(test_strand_initial_state);
    RUN_TEST(test_strand_start_transition);
    RUN_TEST(test_strand_invalid_start);
    RUN_TEST(test_strand_terminated_state);
    RUN_TEST(test_strand_state_string);

    /* Creation tests */
    RUN_TEST(test_strand_create_basic);
    RUN_TEST(test_strand_create_with_arg);
    RUN_TEST(test_strand_create_custom_stack);
    RUN_TEST(test_strand_create_null_entry);
    RUN_TEST(test_strand_create_null_strand);
    RUN_TEST(test_strand_unique_ids);

    /* Capability table tests */
    RUN_TEST(test_strand_cap_table_init);
    RUN_TEST(test_strand_cap_store_get);
    RUN_TEST(test_strand_cap_find_slot);
    RUN_TEST(test_strand_cap_clear);

    /* Capability grant tests */
    RUN_TEST(test_strand_grant_basic);

    /* Capability lend tests */
    RUN_TEST(test_strand_lend_basic);
    RUN_TEST(test_strand_lend_timeout_expiry);
    RUN_TEST(test_strand_revoke);
    RUN_TEST(test_strand_return);

    /* Mutex tests */
    RUN_TEST(test_mutex_init);
    RUN_TEST(test_mutex_acquire_uncontended);
    RUN_TEST(test_mutex_release);
    RUN_TEST(test_mutex_try_acquire_success);
    RUN_TEST(test_mutex_try_acquire_fail);
    RUN_TEST(test_mutex_holder_only_release);

    /* Chronon tests */
    RUN_TEST(test_strand_chronon_init_zero);
    RUN_TEST(test_strand_chronon_tick);
    RUN_TEST(test_strand_chronon_independent);

    /* Strand-local storage tests */
    RUN_TEST(test_strand_local_alloc);
    RUN_TEST(test_strand_local_calloc);
    RUN_TEST(test_strand_local_remaining);

    /* Join/Exit tests */
    RUN_TEST(test_strand_join_terminated);
    RUN_TEST(test_strand_exit_code);

    /* Yield tests */
    RUN_TEST(test_strand_yield_state);

    /* Deadlock detection tests */
    RUN_TEST(test_deadlock_detection_simple);
    RUN_TEST(test_deadlock_detection_no_cycle);

    /* Scheduler tests */
    RUN_TEST(test_strand_schedule_single);
    RUN_TEST(test_strand_schedule_multiple);

    /* VOID propagation tests */
    RUN_TEST(test_strand_void_operations);
    RUN_TEST(test_strand_null_params);

    /* Utility tests */
    RUN_TEST(test_strand_is_valid);
    RUN_TEST(test_strand_is_runnable);

    printf("\nStrand tests: %d/%d passed\n", tests_passed, tests_run);
}

/* Global counters for test_main.c */
int strand_tests_run(void) { return tests_run; }
int strand_tests_passed(void) { return tests_passed; }
