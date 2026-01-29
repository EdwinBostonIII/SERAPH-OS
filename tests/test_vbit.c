/**
 * @file test_vbit.c
 * @brief Tests for MC1: VBIT Three-Valued Logic
 */

#include "seraph/vbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
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
 * VBIT Detection Tests
 *============================================================================*/

TEST(vbit_constants) {
    ASSERT(SERAPH_VBIT_FALSE == 0x00);
    ASSERT(SERAPH_VBIT_TRUE == 0x01);
    ASSERT(SERAPH_VBIT_VOID == 0xFF);
}

TEST(vbit_detection) {
    ASSERT(seraph_vbit_is_false(SERAPH_VBIT_FALSE) == true);
    ASSERT(seraph_vbit_is_false(SERAPH_VBIT_TRUE) == false);
    ASSERT(seraph_vbit_is_false(SERAPH_VBIT_VOID) == false);

    ASSERT(seraph_vbit_is_true(SERAPH_VBIT_TRUE) == true);
    ASSERT(seraph_vbit_is_true(SERAPH_VBIT_FALSE) == false);
    ASSERT(seraph_vbit_is_true(SERAPH_VBIT_VOID) == false);

    ASSERT(seraph_vbit_is_void(SERAPH_VBIT_VOID) == true);
    ASSERT(seraph_vbit_is_void(SERAPH_VBIT_FALSE) == false);
    ASSERT(seraph_vbit_is_void(SERAPH_VBIT_TRUE) == false);

    ASSERT(seraph_vbit_is_valid(SERAPH_VBIT_FALSE) == true);
    ASSERT(seraph_vbit_is_valid(SERAPH_VBIT_TRUE) == true);
    ASSERT(seraph_vbit_is_valid(SERAPH_VBIT_VOID) == false);
}

/*============================================================================
 * VBIT NOT Tests
 *============================================================================*/

TEST(vbit_not) {
    ASSERT(seraph_vbit_not(SERAPH_VBIT_FALSE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_not(SERAPH_VBIT_TRUE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_not(SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT AND Tests (Kleene)
 *============================================================================*/

TEST(vbit_and) {
    /* FALSE dominates */
    ASSERT(seraph_vbit_and(SERAPH_VBIT_FALSE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_and(SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_and(SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_and(SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_and(SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);

    /* TRUE AND TRUE = TRUE */
    ASSERT(seraph_vbit_and(SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);

    /* VOID propagates when not dominated by FALSE */
    ASSERT(seraph_vbit_and(SERAPH_VBIT_TRUE, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_and(SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_and(SERAPH_VBIT_VOID, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT OR Tests (Kleene)
 *============================================================================*/

TEST(vbit_or) {
    /* TRUE dominates */
    ASSERT(seraph_vbit_or(SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_or(SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_or(SERAPH_VBIT_TRUE, SERAPH_VBIT_VOID) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_or(SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_or(SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);

    /* FALSE OR FALSE = FALSE */
    ASSERT(seraph_vbit_or(SERAPH_VBIT_FALSE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);

    /* VOID propagates when not dominated by TRUE */
    ASSERT(seraph_vbit_or(SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_or(SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_or(SERAPH_VBIT_VOID, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT XOR Tests
 *============================================================================*/

TEST(vbit_xor) {
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_FALSE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_FALSE);

    /* VOID always propagates in XOR */
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_TRUE, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_xor(SERAPH_VBIT_VOID, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT IMPLIES Tests
 *============================================================================*/

TEST(vbit_implies) {
    /* FALSE implies anything is TRUE */
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_FALSE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID) == SERAPH_VBIT_TRUE);

    /* TRUE implies FALSE is FALSE */
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);
    /* TRUE implies TRUE is TRUE */
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);
    /* TRUE implies VOID is VOID */
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_TRUE, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);

    /* VOID implies TRUE is TRUE (TRUE dominates in OR) */
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);
    /* VOID implies FALSE is VOID */
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE) == SERAPH_VBIT_VOID);
    /* VOID implies VOID is VOID */
    ASSERT(seraph_vbit_implies(SERAPH_VBIT_VOID, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT IFF Tests
 *============================================================================*/

TEST(vbit_iff) {
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_FALSE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);

    /* VOID propagates */
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_TRUE, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_iff(SERAPH_VBIT_VOID, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT Comparison Tests
 *============================================================================*/

TEST(vbit_comparison) {
    /* Equality */
    ASSERT(seraph_vbit_eq_u64(5, 5) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_eq_u64(5, 6) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_eq_u64(SERAPH_VOID_U64, 5) == SERAPH_VBIT_VOID);
    ASSERT(seraph_vbit_eq_u64(5, SERAPH_VOID_U64) == SERAPH_VBIT_VOID);

    /* Less than */
    ASSERT(seraph_vbit_lt_u64(3, 5) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_lt_u64(5, 3) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_lt_u64(5, 5) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_lt_u64(SERAPH_VOID_U64, 5) == SERAPH_VBIT_VOID);

    /* Less than or equal */
    ASSERT(seraph_vbit_le_u64(3, 5) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_le_u64(5, 5) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_le_u64(6, 5) == SERAPH_VBIT_FALSE);

    /* Greater than */
    ASSERT(seraph_vbit_gt_u64(5, 3) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_gt_u64(3, 5) == SERAPH_VBIT_FALSE);

    /* Greater than or equal */
    ASSERT(seraph_vbit_ge_u64(5, 3) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_ge_u64(5, 5) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_ge_u64(3, 5) == SERAPH_VBIT_FALSE);
}

/*============================================================================
 * VBIT Conversion Tests
 *============================================================================*/

TEST(vbit_conversion) {
    ASSERT(seraph_vbit_to_bool(SERAPH_VBIT_TRUE, false) == true);
    ASSERT(seraph_vbit_to_bool(SERAPH_VBIT_FALSE, true) == false);
    ASSERT(seraph_vbit_to_bool(SERAPH_VBIT_VOID, true) == true);
    ASSERT(seraph_vbit_to_bool(SERAPH_VBIT_VOID, false) == false);

    ASSERT(seraph_vbit_from_bool(true) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_from_bool(false) == SERAPH_VBIT_FALSE);

    ASSERT(seraph_vbit_from_u64(0) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_from_u64(1) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_from_u64(42) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_from_u64(SERAPH_VOID_U64) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT Array Tests
 *============================================================================*/

TEST(vbit_all_true) {
    Seraph_Vbit all_true[] = {SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE};
    ASSERT(seraph_vbit_all_true(all_true, 3) == SERAPH_VBIT_TRUE);

    Seraph_Vbit has_false[] = {SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE};
    ASSERT(seraph_vbit_all_true(has_false, 3) == SERAPH_VBIT_FALSE);

    Seraph_Vbit has_void[] = {SERAPH_VBIT_TRUE, SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE};
    ASSERT(seraph_vbit_all_true(has_void, 3) == SERAPH_VBIT_VOID);

    Seraph_Vbit void_and_false[] = {SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE};
    ASSERT(seraph_vbit_all_true(void_and_false, 3) == SERAPH_VBIT_FALSE);  /* FALSE dominates */
}

TEST(vbit_any_true) {
    Seraph_Vbit all_false[] = {SERAPH_VBIT_FALSE, SERAPH_VBIT_FALSE, SERAPH_VBIT_FALSE};
    ASSERT(seraph_vbit_any_true(all_false, 3) == SERAPH_VBIT_FALSE);

    Seraph_Vbit has_true[] = {SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE};
    ASSERT(seraph_vbit_any_true(has_true, 3) == SERAPH_VBIT_TRUE);

    Seraph_Vbit has_void[] = {SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE};
    ASSERT(seraph_vbit_any_true(has_void, 3) == SERAPH_VBIT_VOID);

    Seraph_Vbit void_and_true[] = {SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE};
    ASSERT(seraph_vbit_any_true(void_and_true, 3) == SERAPH_VBIT_TRUE);  /* TRUE dominates */
}

TEST(vbit_counts) {
    Seraph_Vbit mixed[] = {
        SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID,
        SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE
    };

    ASSERT(seraph_vbit_count_true(mixed, 6) == 3);
    ASSERT(seraph_vbit_count_false(mixed, 6) == 2);
    ASSERT(seraph_vbit_count_void(mixed, 6) == 1);
}

TEST(vbit_array_ops) {
    Seraph_Vbit a[] = {SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE, SERAPH_VBIT_VOID};
    Seraph_Vbit b[] = {SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE, SERAPH_VBIT_TRUE};
    Seraph_Vbit result[3];

    seraph_vbit_and_array(a, b, result, 3);
    ASSERT(result[0] == SERAPH_VBIT_FALSE);
    ASSERT(result[1] == SERAPH_VBIT_FALSE);
    ASSERT(result[2] == SERAPH_VBIT_VOID);

    seraph_vbit_or_array(a, b, result, 3);
    ASSERT(result[0] == SERAPH_VBIT_TRUE);
    ASSERT(result[1] == SERAPH_VBIT_TRUE);
    ASSERT(result[2] == SERAPH_VBIT_TRUE);

    seraph_vbit_not_array(a, result, 3);
    ASSERT(result[0] == SERAPH_VBIT_FALSE);
    ASSERT(result[1] == SERAPH_VBIT_TRUE);
    ASSERT(result[2] == SERAPH_VBIT_VOID);
}

/*============================================================================
 * VBIT Select Tests
 *============================================================================*/

TEST(vbit_select) {
    ASSERT(seraph_vbit_select_u64(SERAPH_VBIT_TRUE, 10, 20) == 10);
    ASSERT(seraph_vbit_select_u64(SERAPH_VBIT_FALSE, 10, 20) == 20);
    ASSERT(SERAPH_IS_VOID_U64(seraph_vbit_select_u64(SERAPH_VBIT_VOID, 10, 20)));
}

TEST(vbit_coalesce) {
    ASSERT(seraph_vbit_coalesce(SERAPH_VBIT_TRUE, SERAPH_VBIT_FALSE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_coalesce(SERAPH_VBIT_FALSE, SERAPH_VBIT_TRUE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_coalesce(SERAPH_VBIT_VOID, SERAPH_VBIT_TRUE) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_vbit_coalesce(SERAPH_VBIT_VOID, SERAPH_VBIT_FALSE) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_vbit_coalesce(SERAPH_VBIT_VOID, SERAPH_VBIT_VOID) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_vbit_tests(void) {
    printf("\n=== MC1: VBIT Three-Valued Logic Tests ===\n\n");

    /* Constants & Detection */
    RUN_TEST(vbit_constants);
    RUN_TEST(vbit_detection);

    /* Logic Operations */
    RUN_TEST(vbit_not);
    RUN_TEST(vbit_and);
    RUN_TEST(vbit_or);
    RUN_TEST(vbit_xor);
    RUN_TEST(vbit_implies);
    RUN_TEST(vbit_iff);

    /* Comparisons */
    RUN_TEST(vbit_comparison);

    /* Conversions */
    RUN_TEST(vbit_conversion);

    /* Array Operations */
    RUN_TEST(vbit_all_true);
    RUN_TEST(vbit_any_true);
    RUN_TEST(vbit_counts);
    RUN_TEST(vbit_array_ops);

    /* Selection */
    RUN_TEST(vbit_select);
    RUN_TEST(vbit_coalesce);

    printf("\nVBIT Tests: %d/%d passed\n", tests_passed, tests_run);
}
