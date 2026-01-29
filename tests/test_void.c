/**
 * @file test_void.c
 * @brief Tests for MC0: VOID Semantics
 */

#include "seraph/void.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) __attribute__((unused)) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s... ", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

/*============================================================================
 * VOID Constant Tests
 *============================================================================*/

TEST(void_constants) {
    /* Test that VOID constants have all bits set */
    ASSERT(SERAPH_VOID_U8  == 0xFF);
    ASSERT(SERAPH_VOID_U16 == 0xFFFF);
    ASSERT(SERAPH_VOID_U32 == 0xFFFFFFFFUL);
    ASSERT(SERAPH_VOID_U64 == 0xFFFFFFFFFFFFFFFFULL);

    /* Signed VOID should be -1 */
    ASSERT(SERAPH_VOID_I8  == -1);
    ASSERT(SERAPH_VOID_I16 == -1);
    ASSERT(SERAPH_VOID_I32 == -1);
    ASSERT(SERAPH_VOID_I64 == -1);
}

/*============================================================================
 * VOID Detection Tests
 *============================================================================*/

TEST(void_detection_u8) {
    ASSERT(SERAPH_IS_VOID_U8(SERAPH_VOID_U8) == 1);
    ASSERT(SERAPH_IS_VOID_U8(0) == 0);
    ASSERT(SERAPH_IS_VOID_U8(1) == 0);
    ASSERT(SERAPH_IS_VOID_U8(254) == 0);
}

TEST(void_detection_u16) {
    ASSERT(SERAPH_IS_VOID_U16(SERAPH_VOID_U16) == 1);
    ASSERT(SERAPH_IS_VOID_U16(0) == 0);
    ASSERT(SERAPH_IS_VOID_U16(0xFF) == 0);  /* Not VOID - too small */
    ASSERT(SERAPH_IS_VOID_U16(0xFFFE) == 0);
}

TEST(void_detection_u32) {
    ASSERT(SERAPH_IS_VOID_U32(SERAPH_VOID_U32) == 1);
    ASSERT(SERAPH_IS_VOID_U32(0) == 0);
    ASSERT(SERAPH_IS_VOID_U32(0xFFFFFFFF - 1) == 0);
}

TEST(void_detection_u64) {
    ASSERT(SERAPH_IS_VOID_U64(SERAPH_VOID_U64) == 1);
    ASSERT(SERAPH_IS_VOID_U64(0) == 0);
    ASSERT(SERAPH_IS_VOID_U64(0xFFFFFFFFFFFFFFFEULL) == 0);
}

TEST(void_detection_signed) {
    ASSERT(SERAPH_IS_VOID_I8(SERAPH_VOID_I8) == 1);
    ASSERT(SERAPH_IS_VOID_I8(0) == 0);
    ASSERT(SERAPH_IS_VOID_I8(-2) == 0);

    ASSERT(SERAPH_IS_VOID_I32(SERAPH_VOID_I32) == 1);
    ASSERT(SERAPH_IS_VOID_I32(0) == 0);
    ASSERT(SERAPH_IS_VOID_I32(-2) == 0);

    ASSERT(SERAPH_IS_VOID_I64(SERAPH_VOID_I64) == 1);
    ASSERT(SERAPH_IS_VOID_I64(0) == 0);
}

TEST(exists_macro) {
    ASSERT(SERAPH_EXISTS((uint64_t)0) == 1);
    ASSERT(SERAPH_EXISTS((uint64_t)42) == 1);
    ASSERT(SERAPH_EXISTS(SERAPH_VOID_U64) == 0);
}

TEST(unwrap_or) {
    uint64_t val = SERAPH_UNWRAP_OR(SERAPH_VOID_U64, 42);
    ASSERT(val == 42);

    val = SERAPH_UNWRAP_OR((uint64_t)100, 42);
    ASSERT(val == 100);
}

/*============================================================================
 * VOID Propagation Tests
 *============================================================================*/

static uint64_t double_value(uint64_t x) { return x * 2; }
static uint64_t add_values(uint64_t a, uint64_t b) { return a + b; }

TEST(void_unary_propagation) {
    /* Normal value passes through */
    uint64_t result = seraph_void_unary_u64(5, double_value);
    ASSERT(result == 10);

    /* VOID propagates */
    result = seraph_void_unary_u64(SERAPH_VOID_U64, double_value);
    ASSERT(SERAPH_IS_VOID_U64(result));
}

TEST(void_binary_propagation) {
    /* Normal values pass through */
    uint64_t result = seraph_void_binary_u64(3, 4, add_values);
    ASSERT(result == 7);

    /* VOID in first operand propagates */
    result = seraph_void_binary_u64(SERAPH_VOID_U64, 4, add_values);
    ASSERT(SERAPH_IS_VOID_U64(result));

    /* VOID in second operand propagates */
    result = seraph_void_binary_u64(3, SERAPH_VOID_U64, add_values);
    ASSERT(SERAPH_IS_VOID_U64(result));

    /* VOID in both operands propagates */
    result = seraph_void_binary_u64(SERAPH_VOID_U64, SERAPH_VOID_U64, add_values);
    ASSERT(SERAPH_IS_VOID_U64(result));
}

/*============================================================================
 * Safe Arithmetic Tests
 *============================================================================*/

TEST(safe_division) {
    /* Normal division works */
    ASSERT(seraph_safe_div_u64(10, 2) == 5);
    ASSERT(seraph_safe_div_u64(7, 3) == 2);

    /* Division by zero returns VOID */
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_div_u64(10, 0)));

    /* VOID operands propagate */
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_div_u64(SERAPH_VOID_U64, 2)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_div_u64(10, SERAPH_VOID_U64)));
}

TEST(safe_division_signed) {
    /* Normal division works */
    ASSERT(seraph_safe_div_i64(10, 2) == 5);
    ASSERT(seraph_safe_div_i64(-10, 2) == -5);
    ASSERT(seraph_safe_div_i64(INT64_MIN, 2) == INT64_MIN / 2);

    /* Division by zero returns VOID */
    ASSERT(SERAPH_IS_VOID_I64(seraph_safe_div_i64(10, 0)));

    /* Division by -1 returns VOID because -1 = SERAPH_VOID_I64 in SERAPH.
     * This means INT64_MIN / -1 is not a special overflow case in SERAPH,
     * it's just division by VOID which always returns VOID. */
    ASSERT(SERAPH_IS_VOID_I64(seraph_safe_div_i64(INT64_MIN, -1)));
    ASSERT(SERAPH_IS_VOID_I64(seraph_safe_div_i64(100, -1)));  /* Any division by VOID */
}

TEST(safe_modulo) {
    ASSERT(seraph_safe_mod_u64(10, 3) == 1);
    ASSERT(seraph_safe_mod_u64(9, 3) == 0);

    /* Modulo by zero returns VOID */
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_mod_u64(10, 0)));
}

TEST(safe_shift) {
    /* Normal shift works */
    ASSERT(seraph_safe_shl_u64(1, 4) == 16);
    ASSERT(seraph_safe_shr_u64(16, 2) == 4);

    /* Shift by 64+ bits returns VOID */
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_shl_u64(1, 64)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_shl_u64(1, 100)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_shr_u64(1, 64)));

    /* VOID operand propagates */
    ASSERT(SERAPH_IS_VOID_U64(seraph_safe_shl_u64(SERAPH_VOID_U64, 4)));
}

/*============================================================================
 * SIMD Batch Check Tests
 *============================================================================*/

TEST(batch_check_4x64) {
    uint64_t values1[4] = {1, 2, 3, 4};
    ASSERT(seraph_void_check_4x64(values1) == 0);

    uint64_t values2[4] = {SERAPH_VOID_U64, 2, 3, 4};
    ASSERT(seraph_void_check_4x64(values2) == 1);  /* Bit 0 set */

    uint64_t values3[4] = {1, SERAPH_VOID_U64, 3, 4};
    ASSERT(seraph_void_check_4x64(values3) == 2);  /* Bit 1 set */

    uint64_t values4[4] = {1, 2, SERAPH_VOID_U64, SERAPH_VOID_U64};
    ASSERT(seraph_void_check_4x64(values4) == 12);  /* Bits 2,3 set */

    uint64_t values5[4] = {SERAPH_VOID_U64, SERAPH_VOID_U64, SERAPH_VOID_U64, SERAPH_VOID_U64};
    ASSERT(seraph_void_check_4x64(values5) == 15);  /* All bits set */
}

TEST(batch_check_2x64) {
    uint64_t values1[2] = {1, 2};
    ASSERT(seraph_void_check_2x64(values1) == 0);

    uint64_t values2[2] = {SERAPH_VOID_U64, 2};
    ASSERT(seraph_void_check_2x64(values2) == 1);

    uint64_t values3[2] = {1, SERAPH_VOID_U64};
    ASSERT(seraph_void_check_2x64(values3) == 2);

    uint64_t values4[2] = {SERAPH_VOID_U64, SERAPH_VOID_U64};
    ASSERT(seraph_void_check_2x64(values4) == 3);
}

/*============================================================================
 * Array Operation Tests
 *============================================================================*/

TEST(void_count) {
    uint64_t values[8] = {1, SERAPH_VOID_U64, 3, 4, SERAPH_VOID_U64, SERAPH_VOID_U64, 7, 8};
    ASSERT(seraph_void_count_u64(values, 8) == 3);
    ASSERT(seraph_void_count_u64(values, 0) == 0);
    ASSERT(seraph_void_count_u64(NULL, 8) == 0);
}

TEST(void_find_first) {
    uint64_t values1[8] = {1, 2, 3, SERAPH_VOID_U64, 5, 6, 7, 8};
    ASSERT(seraph_void_find_first_u64(values1, 8) == 3);

    uint64_t values2[4] = {1, 2, 3, 4};
    ASSERT(seraph_void_find_first_u64(values2, 4) == SIZE_MAX);

    uint64_t values3[4] = {SERAPH_VOID_U64, 2, 3, 4};
    ASSERT(seraph_void_find_first_u64(values3, 4) == 0);
}

TEST(void_any_all) {
    uint64_t values1[4] = {1, 2, 3, 4};
    ASSERT(seraph_void_any_u64(values1, 4) == false);
    ASSERT(seraph_void_all_u64(values1, 4) == false);

    uint64_t values2[4] = {1, SERAPH_VOID_U64, 3, 4};
    ASSERT(seraph_void_any_u64(values2, 4) == true);
    ASSERT(seraph_void_all_u64(values2, 4) == false);

    uint64_t values3[4] = {SERAPH_VOID_U64, SERAPH_VOID_U64, SERAPH_VOID_U64, SERAPH_VOID_U64};
    ASSERT(seraph_void_any_u64(values3, 4) == true);
    ASSERT(seraph_void_all_u64(values3, 4) == true);
}

TEST(void_replace) {
    uint64_t values[4] = {1, SERAPH_VOID_U64, 3, SERAPH_VOID_U64};
    size_t replaced = seraph_void_replace_u64(values, 4, 42);
    ASSERT(replaced == 2);
    ASSERT(values[0] == 1);
    ASSERT(values[1] == 42);
    ASSERT(values[2] == 3);
    ASSERT(values[3] == 42);
}

/*============================================================================
 * Void Archaeology Tests (Causality Tracking)
 *============================================================================*/

TEST(void_tracking_init) {
    /* Initialize tracking */
    seraph_void_tracking_init();
    ASSERT(seraph_void_tracking_enabled() == true);

    /* Can disable tracking */
    seraph_void_tracking_set_enabled(false);
    ASSERT(seraph_void_tracking_enabled() == false);

    /* Re-enable for other tests */
    seraph_void_tracking_set_enabled(true);
}

TEST(void_record_basic) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(true);

    /* Record a VOID with reason */
    uint64_t id = seraph_void_record(SERAPH_VOID_REASON_DIV_ZERO, 0, 10, 0,
                                      __FILE__, __func__, __LINE__, "test div zero");
    ASSERT(id > 0);

    /* Look it up */
    Seraph_VoidContext ctx = seraph_void_lookup(id);
    ASSERT(ctx.void_id == id);
    ASSERT(ctx.reason == SERAPH_VOID_REASON_DIV_ZERO);
    ASSERT(ctx.input_a == 10);
    ASSERT(ctx.input_b == 0);
    ASSERT(strcmp(ctx.message, "test div zero") == 0);
}

TEST(void_record_macro) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(true);

    /* Use the convenience macro */
    uint64_t id = SERAPH_VOID_RECORD(SERAPH_VOID_REASON_OVERFLOW, 0, 100, 200, "overflow test");
    ASSERT(id > 0);

    Seraph_VoidContext ctx = seraph_void_lookup(id);
    ASSERT(ctx.reason == SERAPH_VOID_REASON_OVERFLOW);
    ASSERT(ctx.input_a == 100);
    ASSERT(ctx.input_b == 200);
}

/* Callback for void chain walk test */
static int g_walk_count = 0;

static void walk_chain_callback(const Seraph_VoidContext* ctx, void* ud) {
    (void)ctx; (void)ud;
    g_walk_count++;
}

TEST(void_causality_chain) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(true);

    /* Create a chain: first VOID creates second */
    uint64_t id1 = SERAPH_VOID_RECORD(SERAPH_VOID_REASON_DIV_ZERO, 0, 5, 0, "original error");
    uint64_t id2 = SERAPH_VOID_RECORD(SERAPH_VOID_REASON_PROPAGATED, id1, 0, 0, "propagated from div");

    /* Lookup the chain */
    Seraph_VoidContext ctx2 = seraph_void_lookup(id2);
    ASSERT(ctx2.predecessor == id1);

    /* Walk the chain */
    g_walk_count = 0;
    seraph_void_walk_chain(id2, walk_chain_callback, NULL);

    /* Should have visited 2 nodes (id1, id2) */
    ASSERT(g_walk_count == 2);
}

TEST(void_last_context) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(true);

    /* Record something */
    uint64_t id = SERAPH_VOID_RECORD(SERAPH_VOID_REASON_NULL_PTR, 0, 0, 0, "null ptr");

    /* Last should return it */
    Seraph_VoidContext ctx = seraph_void_last();
    ASSERT(ctx.void_id == id);
    ASSERT(ctx.reason == SERAPH_VOID_REASON_NULL_PTR);
}

TEST(void_reason_strings) {
    ASSERT(strcmp(seraph_void_reason_str(SERAPH_VOID_REASON_UNKNOWN), "unknown") == 0);
    ASSERT(strcmp(seraph_void_reason_str(SERAPH_VOID_REASON_DIV_ZERO), "divide-by-zero") == 0);
    ASSERT(strcmp(seraph_void_reason_str(SERAPH_VOID_REASON_OVERFLOW), "overflow") == 0);
    ASSERT(strcmp(seraph_void_reason_str(SERAPH_VOID_REASON_NULL_PTR), "null-pointer") == 0);
    ASSERT(strcmp(seraph_void_reason_str(SERAPH_VOID_REASON_OUT_OF_BOUNDS), "out-of-bounds") == 0);
}

TEST(void_tracked_div) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(true);

    /* Normal division works */
    uint64_t result = seraph_tracked_div_u64(10, 2);
    ASSERT(result == 5);

    /* Division by zero returns VOID and records context */
    result = seraph_tracked_div_u64(10, 0);
    ASSERT(SERAPH_IS_VOID_U64(result));

    /* Check that context was recorded */
    Seraph_VoidContext ctx = seraph_void_last();
    ASSERT(ctx.reason == SERAPH_VOID_REASON_DIV_ZERO);
    ASSERT(ctx.input_a == 10);
    ASSERT(ctx.input_b == 0);
}

TEST(void_tracked_mod) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(true);

    /* Normal modulo works */
    uint64_t result = seraph_tracked_mod_u64(10, 3);
    ASSERT(result == 1);

    /* Modulo by zero returns VOID and records context */
    result = seraph_tracked_mod_u64(10, 0);
    ASSERT(SERAPH_IS_VOID_U64(result));

    Seraph_VoidContext ctx = seraph_void_last();
    ASSERT(ctx.reason == SERAPH_VOID_REASON_DIV_ZERO);
}

TEST(void_tracking_disabled) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(false);

    /* Should return 0 when disabled */
    uint64_t id = SERAPH_VOID_RECORD(SERAPH_VOID_REASON_TIMEOUT, 0, 0, 0, "timeout");
    ASSERT(id == 0);

    /* Re-enable */
    seraph_void_tracking_set_enabled(true);
}

TEST(void_clear) {
    seraph_void_tracking_init();
    seraph_void_tracking_set_enabled(true);

    /* Record something */
    uint64_t id = SERAPH_VOID_RECORD(SERAPH_VOID_REASON_IO, 0, 0, 0, "io error");
    ASSERT(id > 0);

    /* Clear the table */
    seraph_void_clear();

    /* Lookup should return NONE */
    Seraph_VoidContext ctx = seraph_void_lookup(id);
    ASSERT(ctx.void_id == 0);  /* SERAPH_VOID_CONTEXT_NONE */
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_void_tests(void) {
    printf("\n=== MC0: VOID Semantics Tests ===\n\n");

    /* Constants */
    RUN_TEST(void_constants);

    /* Detection */
    RUN_TEST(void_detection_u8);
    RUN_TEST(void_detection_u16);
    RUN_TEST(void_detection_u32);
    RUN_TEST(void_detection_u64);
    RUN_TEST(void_detection_signed);
    RUN_TEST(exists_macro);
    RUN_TEST(unwrap_or);

    /* Propagation */
    RUN_TEST(void_unary_propagation);
    RUN_TEST(void_binary_propagation);

    /* Safe Arithmetic */
    RUN_TEST(safe_division);
    RUN_TEST(safe_division_signed);
    RUN_TEST(safe_modulo);
    RUN_TEST(safe_shift);

    /* SIMD Batch */
    RUN_TEST(batch_check_4x64);
    RUN_TEST(batch_check_2x64);

    /* Array Operations */
    RUN_TEST(void_count);
    RUN_TEST(void_find_first);
    RUN_TEST(void_any_all);
    RUN_TEST(void_replace);

    /* Void Archaeology (Causality Tracking) */
    printf("\n  --- Void Archaeology ---\n");
    RUN_TEST(void_tracking_init);
    RUN_TEST(void_record_basic);
    RUN_TEST(void_record_macro);
    RUN_TEST(void_causality_chain);
    RUN_TEST(void_last_context);
    RUN_TEST(void_reason_strings);
    RUN_TEST(void_tracked_div);
    RUN_TEST(void_tracked_mod);
    RUN_TEST(void_tracking_disabled);
    RUN_TEST(void_clear);

    printf("\nVOID Tests: %d/%d passed\n", tests_passed, tests_run);
}
