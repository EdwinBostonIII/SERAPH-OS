/**
 * @file test_integers.c
 * @brief Tests for MC4: Entropic Arithmetic
 */

#include "seraph/integers.h"
#include <stdio.h>
#include <stdlib.h>

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
 * Addition Tests
 *============================================================================*/

TEST(add_u64_normal) {
    /* Normal addition */
    ASSERT(seraph_add_u64(10, 20, SERAPH_ARITH_VOID) == 30);
    ASSERT(seraph_add_u64(0, 0, SERAPH_ARITH_VOID) == 0);
    ASSERT(seraph_add_u64(UINT64_MAX - 1, 0, SERAPH_ARITH_VOID) == UINT64_MAX - 1);
}

TEST(add_u64_void_mode) {
    /* Overflow in VOID mode */
    ASSERT(SERAPH_IS_VOID_U64(seraph_add_u64(UINT64_MAX - 1, 10, SERAPH_ARITH_VOID)));

    /* VOID propagation */
    ASSERT(SERAPH_IS_VOID_U64(seraph_add_u64(SERAPH_VOID_U64, 10, SERAPH_ARITH_VOID)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_add_u64(10, SERAPH_VOID_U64, SERAPH_ARITH_VOID)));
}

TEST(add_u64_wrap_mode) {
    /* Note: UINT64_MAX = SERAPH_VOID_U64 in SERAPH, so we can't use it */
    /* Use UINT64_MAX - 1 (0xFFFFFFFFFFFFFFFE) as max non-VOID value */
    uint64_t max_valid = UINT64_MAX - 1;  /* 0xFFFFFFFFFFFFFFFE */

    /* Wrap around: max_valid + 2 = 0 (wraps past VOID) */
    uint64_t result = seraph_add_u64(max_valid, 2, SERAPH_ARITH_WRAP);
    ASSERT(result == 0);

    /* Another wrap test */
    result = seraph_add_u64(max_valid - 5, 10, SERAPH_ARITH_WRAP);
    ASSERT(result == 3);
}

TEST(add_u64_saturate_mode) {
    uint64_t result = seraph_add_u64(UINT64_MAX - 1, 10, SERAPH_ARITH_SATURATE);
    ASSERT(result == SERAPH_SAT_MAX_U64);
}

TEST(add_i64_normal) {
    ASSERT(seraph_add_i64(10, 20, SERAPH_ARITH_VOID) == 30);
    ASSERT(seraph_add_i64(-10, -20, SERAPH_ARITH_VOID) == -30);
    ASSERT(seraph_add_i64(-10, 20, SERAPH_ARITH_VOID) == 10);
}

TEST(add_i64_overflow) {
    /* Positive overflow */
    ASSERT(SERAPH_IS_VOID_I64(seraph_add_i64(INT64_MAX, 1, SERAPH_ARITH_VOID)));

    /* Negative overflow */
    ASSERT(SERAPH_IS_VOID_I64(seraph_add_i64(INT64_MIN, -1, SERAPH_ARITH_VOID)));

    /* Saturation */
    ASSERT(seraph_add_i64(INT64_MAX - 1, 100, SERAPH_ARITH_SATURATE) == SERAPH_SAT_MAX_I64);
    ASSERT(seraph_add_i64(INT64_MIN + 1, -100, SERAPH_ARITH_SATURATE) == SERAPH_SAT_MIN_I64);
}

/*============================================================================
 * Subtraction Tests
 *============================================================================*/

TEST(sub_u64_normal) {
    ASSERT(seraph_sub_u64(30, 20, SERAPH_ARITH_VOID) == 10);
    ASSERT(seraph_sub_u64(100, 100, SERAPH_ARITH_VOID) == 0);
}

TEST(sub_u64_underflow) {
    /* Underflow in VOID mode */
    ASSERT(SERAPH_IS_VOID_U64(seraph_sub_u64(10, 20, SERAPH_ARITH_VOID)));

    /* Wrap mode */
    uint64_t result = seraph_sub_u64(10, 20, SERAPH_ARITH_WRAP);
    ASSERT(result == UINT64_MAX - 9);  /* Wraps to large positive */

    /* Saturate mode */
    ASSERT(seraph_sub_u64(10, 20, SERAPH_ARITH_SATURATE) == 0);
}

TEST(sub_i64_overflow) {
    /* Subtraction overflow: large_positive - large_negative */
    ASSERT(SERAPH_IS_VOID_I64(seraph_sub_i64(INT64_MAX, -1, SERAPH_ARITH_VOID)));

    /* Subtraction underflow: large_negative - large_positive */
    ASSERT(SERAPH_IS_VOID_I64(seraph_sub_i64(INT64_MIN, 1, SERAPH_ARITH_VOID)));
}

/*============================================================================
 * Multiplication Tests
 *============================================================================*/

TEST(mul_u64_normal) {
    ASSERT(seraph_mul_u64(10, 20, SERAPH_ARITH_VOID) == 200);
    ASSERT(seraph_mul_u64(0, 1000, SERAPH_ARITH_VOID) == 0);
    ASSERT(seraph_mul_u64(1, UINT64_MAX - 1, SERAPH_ARITH_VOID) == UINT64_MAX - 1);
}

TEST(mul_u64_overflow) {
    /* VOID mode */
    ASSERT(SERAPH_IS_VOID_U64(seraph_mul_u64(UINT64_MAX / 2, 3, SERAPH_ARITH_VOID)));

    /* Saturate mode */
    ASSERT(seraph_mul_u64(UINT64_MAX / 2, 3, SERAPH_ARITH_SATURATE) == SERAPH_SAT_MAX_U64);
}

TEST(mul_u32_overflow) {
    /* Normal */
    ASSERT(seraph_mul_u32(1000, 1000, SERAPH_ARITH_VOID) == 1000000);

    /* Overflow */
    ASSERT(SERAPH_IS_VOID_U32(seraph_mul_u32(UINT32_MAX, 2, SERAPH_ARITH_VOID)));
}

/*============================================================================
 * Division Tests
 *============================================================================*/

TEST(div_u64_normal) {
    ASSERT(seraph_div_u64(100, 10, SERAPH_ARITH_VOID) == 10);
    ASSERT(seraph_div_u64(100, 7, SERAPH_ARITH_VOID) == 14);  /* Floor division */
    ASSERT(seraph_div_u64(0, 10, SERAPH_ARITH_VOID) == 0);
}

TEST(div_by_zero) {
    /* Always returns VOID regardless of mode */
    ASSERT(SERAPH_IS_VOID_U64(seraph_div_u64(100, 0, SERAPH_ARITH_VOID)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_div_u64(100, 0, SERAPH_ARITH_WRAP)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_div_u64(100, 0, SERAPH_ARITH_SATURATE)));

    ASSERT(SERAPH_IS_VOID_I64(seraph_div_i64(100, 0, SERAPH_ARITH_VOID)));
}

TEST(div_i64_special) {
    /* Note: In SERAPH, -1 = SERAPH_VOID_I64 for signed types.
     * So INT64_MIN / -1 is really INT64_MIN / VOID, which returns VOID.
     * This is different from C semantics where INT64_MIN / -1 overflows.
     *
     * Test that dividing by VOID (-1) returns VOID regardless of mode: */
    ASSERT(SERAPH_IS_VOID_I64(seraph_div_i64(INT64_MIN, -1, SERAPH_ARITH_VOID)));
    ASSERT(SERAPH_IS_VOID_I64(seraph_div_i64(INT64_MIN, -1, SERAPH_ARITH_SATURATE)));
    ASSERT(SERAPH_IS_VOID_I64(seraph_div_i64(INT64_MIN, -1, SERAPH_ARITH_WRAP)));

    /* Test normal division with valid divisors */
    ASSERT(seraph_div_i64(INT64_MIN, 2, SERAPH_ARITH_VOID) == INT64_MIN / 2);
    ASSERT(seraph_div_i64(-100, 10, SERAPH_ARITH_VOID) == -10);
}

/*============================================================================
 * Modulo Tests
 *============================================================================*/

TEST(mod_u64_normal) {
    ASSERT(seraph_mod_u64(100, 7, SERAPH_ARITH_VOID) == 2);
    ASSERT(seraph_mod_u64(100, 10, SERAPH_ARITH_VOID) == 0);

    /* Mod by zero */
    ASSERT(SERAPH_IS_VOID_U64(seraph_mod_u64(100, 0, SERAPH_ARITH_VOID)));
}

/*============================================================================
 * Negation Tests
 *============================================================================*/

TEST(neg_i64_normal) {
    ASSERT(seraph_neg_i64(10, SERAPH_ARITH_VOID) == -10);
    ASSERT(seraph_neg_i64(-10, SERAPH_ARITH_VOID) == 10);
    ASSERT(seraph_neg_i64(0, SERAPH_ARITH_VOID) == 0);
}

TEST(neg_i64_overflow) {
    /* INT64_MIN has no positive equivalent */
    ASSERT(SERAPH_IS_VOID_I64(seraph_neg_i64(INT64_MIN, SERAPH_ARITH_VOID)));
    ASSERT(seraph_neg_i64(INT64_MIN, SERAPH_ARITH_SATURATE) == SERAPH_SAT_MAX_I64);
}

/*============================================================================
 * Absolute Value Tests
 *============================================================================*/

TEST(abs_i64_normal) {
    ASSERT(seraph_abs_i64(10, SERAPH_ARITH_VOID) == 10);
    ASSERT(seraph_abs_i64(-10, SERAPH_ARITH_VOID) == 10);
    ASSERT(seraph_abs_i64(0, SERAPH_ARITH_VOID) == 0);
}

TEST(abs_i64_min) {
    /* abs(INT64_MIN) = 2^63 which fits in uint64_t */
    uint64_t result = seraph_abs_i64(INT64_MIN, SERAPH_ARITH_VOID);
    ASSERT(result == (uint64_t)INT64_MAX + 1);
}

/*============================================================================
 * Checked Operations Tests
 *============================================================================*/

TEST(add_u64_checked) {
    bool overflow;

    uint64_t result = seraph_add_u64_checked(10, 20, &overflow);
    ASSERT(result == 30);
    ASSERT(overflow == false);

    result = seraph_add_u64_checked(UINT64_MAX, 1, &overflow);
    ASSERT(overflow == true);
}

TEST(mul_u64_checked) {
    bool overflow;

    uint64_t result = seraph_mul_u64_checked(10, 20, &overflow);
    ASSERT(result == 200);
    ASSERT(overflow == false);

    result = seraph_mul_u64_checked(UINT64_MAX, 2, &overflow);
    ASSERT(overflow == true);
}

/*============================================================================
 * Min/Max Tests
 *============================================================================*/

TEST(min_max_u64) {
    ASSERT(seraph_min_u64(10, 20) == 10);
    ASSERT(seraph_min_u64(20, 10) == 10);
    ASSERT(seraph_max_u64(10, 20) == 20);
    ASSERT(seraph_max_u64(20, 10) == 20);

    /* VOID handling */
    ASSERT(seraph_min_u64(SERAPH_VOID_U64, 10) == 10);
    ASSERT(seraph_min_u64(10, SERAPH_VOID_U64) == 10);
    ASSERT(seraph_max_u64(SERAPH_VOID_U64, 10) == 10);
}

TEST(min_max_i64) {
    ASSERT(seraph_min_i64(-10, 10) == -10);
    ASSERT(seraph_max_i64(-10, 10) == 10);

    ASSERT(seraph_min_i64(SERAPH_VOID_I64, -100) == -100);
}

/*============================================================================
 * Clamp Tests
 *============================================================================*/

TEST(clamp_u64) {
    ASSERT(seraph_clamp_u64(50, 10, 100) == 50);
    ASSERT(seraph_clamp_u64(5, 10, 100) == 10);
    ASSERT(seraph_clamp_u64(150, 10, 100) == 100);

    /* VOID propagation */
    ASSERT(SERAPH_IS_VOID_U64(seraph_clamp_u64(SERAPH_VOID_U64, 10, 100)));
}

/*============================================================================
 * Power Tests
 *============================================================================*/

TEST(pow_u64) {
    ASSERT(seraph_pow_u64(2, 0, SERAPH_ARITH_VOID) == 1);
    ASSERT(seraph_pow_u64(2, 1, SERAPH_ARITH_VOID) == 2);
    ASSERT(seraph_pow_u64(2, 10, SERAPH_ARITH_VOID) == 1024);
    ASSERT(seraph_pow_u64(3, 5, SERAPH_ARITH_VOID) == 243);

    /* Overflow */
    ASSERT(SERAPH_IS_VOID_U64(seraph_pow_u64(2, 64, SERAPH_ARITH_VOID)));
}

/*============================================================================
 * GCD/LCM Tests (TODO: implement seraph_gcd_u64, seraph_lcm_u64)
 *============================================================================*/

/* TEST(gcd_lcm) {
    ASSERT(seraph_gcd_u64(12, 8) == 4);
    ASSERT(seraph_gcd_u64(17, 13) == 1);
    ASSERT(seraph_gcd_u64(100, 25) == 25);

    ASSERT(seraph_lcm_u64(4, 6, SERAPH_ARITH_VOID) == 12);
    ASSERT(seraph_lcm_u64(3, 7, SERAPH_ARITH_VOID) == 21);
} */

/*============================================================================
 * Square Root Tests (TODO: implement seraph_isqrt_u64)
 *============================================================================*/

/* TEST(isqrt) {
    ASSERT(seraph_isqrt_u64(0) == 0);
    ASSERT(seraph_isqrt_u64(1) == 1);
    ASSERT(seraph_isqrt_u64(4) == 2);
    ASSERT(seraph_isqrt_u64(9) == 3);
    ASSERT(seraph_isqrt_u64(10) == 3);
    ASSERT(seraph_isqrt_u64(100) == 10);
    ASSERT(seraph_isqrt_u64(1000000) == 1000);

    ASSERT(SERAPH_IS_VOID_U64(seraph_isqrt_u64(SERAPH_VOID_U64)));
} */

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_integer_tests(void) {
    printf("\n=== MC4: Entropic Arithmetic Tests ===\n\n");

    /* Addition */
    RUN_TEST(add_u64_normal);
    RUN_TEST(add_u64_void_mode);
    RUN_TEST(add_u64_wrap_mode);
    RUN_TEST(add_u64_saturate_mode);
    RUN_TEST(add_i64_normal);
    RUN_TEST(add_i64_overflow);

    /* Subtraction */
    RUN_TEST(sub_u64_normal);
    RUN_TEST(sub_u64_underflow);
    RUN_TEST(sub_i64_overflow);

    /* Multiplication */
    RUN_TEST(mul_u64_normal);
    RUN_TEST(mul_u64_overflow);
    RUN_TEST(mul_u32_overflow);

    /* Division */
    RUN_TEST(div_u64_normal);
    RUN_TEST(div_by_zero);
    RUN_TEST(div_i64_special);

    /* Modulo */
    RUN_TEST(mod_u64_normal);

    /* Negation */
    RUN_TEST(neg_i64_normal);
    RUN_TEST(neg_i64_overflow);

    /* Absolute Value */
    RUN_TEST(abs_i64_normal);
    RUN_TEST(abs_i64_min);

    /* Checked Operations */
    RUN_TEST(add_u64_checked);
    RUN_TEST(mul_u64_checked);

    /* Min/Max */
    RUN_TEST(min_max_u64);
    RUN_TEST(min_max_i64);

    /* Clamp */
    RUN_TEST(clamp_u64);

    /* Power */
    RUN_TEST(pow_u64);

    /* GCD/LCM - TODO: implement */
    /* RUN_TEST(gcd_lcm); */

    /* Square Root - TODO: implement */
    /* RUN_TEST(isqrt); */

    printf("\nInteger Tests: %d/%d passed\n", tests_passed, tests_run);
}
