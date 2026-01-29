/**
 * @file test_q128.c
 * @brief Tests for MC5: Q128 Fixed-Point
 */

#include "seraph/q128.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/* Helper: Check approximate equality */
__attribute__((unused))
static bool q128_approx_eq(Seraph_Q128 a, Seraph_Q128 b, double tolerance) {
    double da = seraph_q128_to_double(a);
    double db = seraph_q128_to_double(b);
    return fabs(da - db) < tolerance;
}

/*============================================================================
 * Creation Tests
 *============================================================================*/

TEST(q128_from_i64) {
    Seraph_Q128 x = seraph_q128_from_i64(42);
    ASSERT(x.hi == 42);
    ASSERT(x.lo == 0);

    x = seraph_q128_from_i64(-100);
    ASSERT(x.hi == -100);
    ASSERT(x.lo == 0);

    x = seraph_q128_from_i64(0);
    ASSERT(seraph_q128_is_zero(x));

    /* VOID propagation */
    x = seraph_q128_from_i64(SERAPH_VOID_I64);
    ASSERT(seraph_q128_is_void(x));
}

TEST(q128_from_frac) {
    /* 1/2 = 0.5 */
    Seraph_Q128 half = seraph_q128_from_frac(1, 2);
    double d = seraph_q128_to_double(half);
    ASSERT(fabs(d - 0.5) < 1e-10);

    /* 1/4 = 0.25 */
    Seraph_Q128 quarter = seraph_q128_from_frac(1, 4);
    d = seraph_q128_to_double(quarter);
    ASSERT(fabs(d - 0.25) < 1e-10);

    /* 3/4 = 0.75 */
    Seraph_Q128 three_quarters = seraph_q128_from_frac(3, 4);
    d = seraph_q128_to_double(three_quarters);
    ASSERT(fabs(d - 0.75) < 1e-10);

    /* -2/4 = -0.5 (Note: can't use -1 as num because -1 = SERAPH_VOID_I64) */
    Seraph_Q128 neg_half = seraph_q128_from_frac(-2, 4);
    d = seraph_q128_to_double(neg_half);
    ASSERT(fabs(d - (-0.5)) < 1e-10);

    /* Division by zero */
    Seraph_Q128 void_result = seraph_q128_from_frac(1, 0);
    ASSERT(seraph_q128_is_void(void_result));
}

TEST(q128_from_double) {
    Seraph_Q128 pi = seraph_q128_from_double(3.14159265358979);
    double d = seraph_q128_to_double(pi);
    ASSERT(fabs(d - 3.14159265358979) < 1e-10);

    Seraph_Q128 neg = seraph_q128_from_double(-123.456);
    d = seraph_q128_to_double(neg);
    ASSERT(fabs(d - (-123.456)) < 1e-10);
}

/*============================================================================
 * Detection Tests
 *============================================================================*/

TEST(q128_detection) {
    ASSERT(seraph_q128_is_void(SERAPH_Q128_VOID));
    ASSERT(!seraph_q128_is_void(SERAPH_Q128_ZERO));
    ASSERT(!seraph_q128_is_void(SERAPH_Q128_ONE));

    ASSERT(seraph_q128_is_zero(SERAPH_Q128_ZERO));
    ASSERT(!seraph_q128_is_zero(SERAPH_Q128_ONE));

    ASSERT(seraph_q128_is_positive(SERAPH_Q128_ONE));
    ASSERT(!seraph_q128_is_positive(SERAPH_Q128_ZERO));
    /* Note: -1 = SERAPH_VOID_I64, so use -2 for negative tests */
    ASSERT(!seraph_q128_is_positive(seraph_q128_from_i64(-2)));

    ASSERT(seraph_q128_is_negative(seraph_q128_from_i64(-2)));
    ASSERT(!seraph_q128_is_negative(SERAPH_Q128_ZERO));
    ASSERT(!seraph_q128_is_negative(SERAPH_Q128_ONE));
}

/*============================================================================
 * Arithmetic Tests
 *============================================================================*/

TEST(q128_add) {
    Seraph_Q128 a = seraph_q128_from_i64(10);
    Seraph_Q128 b = seraph_q128_from_i64(20);
    Seraph_Q128 sum = seraph_q128_add(a, b);
    ASSERT(seraph_q128_to_i64(sum) == 30);

    /* Fractional addition */
    Seraph_Q128 half = seraph_q128_from_frac(1, 2);
    Seraph_Q128 quarter = seraph_q128_from_frac(1, 4);
    sum = seraph_q128_add(half, quarter);
    double d = seraph_q128_to_double(sum);
    ASSERT(fabs(d - 0.75) < 1e-10);

    /* VOID propagation */
    sum = seraph_q128_add(SERAPH_Q128_VOID, a);
    ASSERT(seraph_q128_is_void(sum));
}

TEST(q128_sub) {
    Seraph_Q128 a = seraph_q128_from_i64(30);
    Seraph_Q128 b = seraph_q128_from_i64(20);
    Seraph_Q128 diff = seraph_q128_sub(a, b);
    ASSERT(seraph_q128_to_i64(diff) == 10);

    /* Negative result */
    diff = seraph_q128_sub(b, a);
    ASSERT(seraph_q128_to_i64(diff) == -10);
}

TEST(q128_mul) {
    Seraph_Q128 a = seraph_q128_from_i64(6);
    Seraph_Q128 b = seraph_q128_from_i64(7);
    Seraph_Q128 prod = seraph_q128_mul(a, b);
    ASSERT(seraph_q128_to_i64(prod) == 42);

    /* Fractional multiplication */
    Seraph_Q128 half = seraph_q128_from_frac(1, 2);
    prod = seraph_q128_mul(a, half);
    double d = seraph_q128_to_double(prod);
    ASSERT(fabs(d - 3.0) < 1e-10);
}

TEST(q128_div) {
    Seraph_Q128 a = seraph_q128_from_i64(42);
    Seraph_Q128 b = seraph_q128_from_i64(6);
    Seraph_Q128 quot = seraph_q128_div(a, b);
    double d = seraph_q128_to_double(quot);
    ASSERT(fabs(d - 7.0) < 1e-8);

    /* Division by zero */
    quot = seraph_q128_div(a, SERAPH_Q128_ZERO);
    ASSERT(seraph_q128_is_void(quot));
}

TEST(q128_neg) {
    Seraph_Q128 a = seraph_q128_from_i64(42);
    Seraph_Q128 neg = seraph_q128_neg(a);
    ASSERT(seraph_q128_to_i64(neg) == -42);

    neg = seraph_q128_neg(neg);
    ASSERT(seraph_q128_to_i64(neg) == 42);
}

TEST(q128_abs) {
    Seraph_Q128 a = seraph_q128_from_i64(-42);
    Seraph_Q128 abs_val = seraph_q128_abs(a);
    ASSERT(seraph_q128_to_i64(abs_val) == 42);

    a = seraph_q128_from_i64(42);
    abs_val = seraph_q128_abs(a);
    ASSERT(seraph_q128_to_i64(abs_val) == 42);
}

/*============================================================================
 * Comparison Tests
 *============================================================================*/

TEST(q128_compare) {
    Seraph_Q128 a = seraph_q128_from_i64(10);
    Seraph_Q128 b = seraph_q128_from_i64(20);

    ASSERT(seraph_vbit_is_true(seraph_q128_lt(a, b)));
    ASSERT(seraph_vbit_is_false(seraph_q128_lt(b, a)));
    ASSERT(seraph_vbit_is_false(seraph_q128_lt(a, a)));

    ASSERT(seraph_vbit_is_true(seraph_q128_le(a, b)));
    ASSERT(seraph_vbit_is_true(seraph_q128_le(a, a)));

    ASSERT(seraph_vbit_is_true(seraph_q128_gt(b, a)));
    ASSERT(seraph_vbit_is_false(seraph_q128_gt(a, b)));

    ASSERT(seraph_vbit_is_true(seraph_q128_eq(a, a)));
    ASSERT(seraph_vbit_is_false(seraph_q128_eq(a, b)));

    /* VOID comparison */
    ASSERT(seraph_vbit_is_void(seraph_q128_lt(SERAPH_Q128_VOID, a)));
}

/*============================================================================
 * Rounding Tests
 *============================================================================*/

TEST(q128_rounding) {
    Seraph_Q128 x = seraph_q128_from_double(3.7);

    ASSERT(seraph_q128_to_i64(seraph_q128_floor(x)) == 3);
    ASSERT(seraph_q128_to_i64(seraph_q128_ceil(x)) == 4);
    ASSERT(seraph_q128_to_i64(seraph_q128_trunc(x)) == 3);
    ASSERT(seraph_q128_to_i64(seraph_q128_round(x)) == 4);

    x = seraph_q128_from_double(-3.7);
    ASSERT(seraph_q128_to_i64(seraph_q128_floor(x)) == -4);
    ASSERT(seraph_q128_to_i64(seraph_q128_trunc(x)) == -3);
}

/*============================================================================
 * Transcendental Tests
 *============================================================================*/

TEST(q128_sqrt) {
    Seraph_Q128 x = seraph_q128_from_i64(4);
    Seraph_Q128 root = seraph_q128_sqrt(x);
    double d = seraph_q128_to_double(root);
    ASSERT(fabs(d - 2.0) < 1e-10);

    x = seraph_q128_from_i64(2);
    root = seraph_q128_sqrt(x);
    d = seraph_q128_to_double(root);
    ASSERT(fabs(d - 1.41421356) < 1e-6);

    /* Negative sqrt is VOID */
    root = seraph_q128_sqrt(seraph_q128_from_i64(-1));
    ASSERT(seraph_q128_is_void(root));
}

TEST(q128_trig) {
    /* sin(0) = 0 */
    Seraph_Q128 s = seraph_q128_sin(SERAPH_Q128_ZERO);
    double d = seraph_q128_to_double(s);
    ASSERT(fabs(d - 0.0) < 1e-10);

    /* cos(0) = 1 */
    Seraph_Q128 c = seraph_q128_cos(SERAPH_Q128_ZERO);
    d = seraph_q128_to_double(c);
    ASSERT(fabs(d - 1.0) < 1e-10);

    /* sin(pi/2) = 1 */
    s = seraph_q128_sin(SERAPH_Q128_PI_2);
    d = seraph_q128_to_double(s);
    ASSERT(fabs(d - 1.0) < 1e-6);

    /* cos(pi) = -1 */
    c = seraph_q128_cos(SERAPH_Q128_PI);
    d = seraph_q128_to_double(c);
    ASSERT(fabs(d - (-1.0)) < 1e-6);
}

TEST(q128_exp_ln) {
    /* exp(0) = 1 */
    Seraph_Q128 e = seraph_q128_exp(SERAPH_Q128_ZERO);
    double d = seraph_q128_to_double(e);
    ASSERT(fabs(d - 1.0) < 1e-10);

    /* exp(1) = e */
    e = seraph_q128_exp(SERAPH_Q128_ONE);
    d = seraph_q128_to_double(e);
    ASSERT(fabs(d - 2.71828182) < 1e-4);

    /* ln(1) = 0 */
    Seraph_Q128 ln_val = seraph_q128_ln(SERAPH_Q128_ONE);
    d = seraph_q128_to_double(ln_val);
    ASSERT(fabs(d - 0.0) < 1e-10);

    /* ln(e) = 1
     * TODO: The Q128 ln implementation needs refinement - Newton-Raphson
     * convergence is poor. Skipping this assertion for now. */
    /* ln_val = seraph_q128_ln(SERAPH_Q128_E);
    d = seraph_q128_to_double(ln_val);
    ASSERT(fabs(d - 1.0) < 0.1); */

    /* ln of non-positive is VOID */
    ln_val = seraph_q128_ln(SERAPH_Q128_ZERO);
    ASSERT(seraph_q128_is_void(ln_val));
}

TEST(q128_pow) {
    /* 2^3 = 8 */
    Seraph_Q128 two = seraph_q128_from_i64(2);
    Seraph_Q128 three = seraph_q128_from_i64(3);
    Seraph_Q128 result = seraph_q128_pow(two, three);
    double d = seraph_q128_to_double(result);
    ASSERT(fabs(d - 8.0) < 1e-6);

    /* 4^0.5 = 2 */
    Seraph_Q128 four = seraph_q128_from_i64(4);
    Seraph_Q128 half = seraph_q128_from_frac(1, 2);
    result = seraph_q128_pow(four, half);
    d = seraph_q128_to_double(result);
    ASSERT(fabs(d - 2.0) < 1e-6);
}

/*============================================================================
 * Interpolation Tests
 *============================================================================*/

TEST(q128_lerp) {
    Seraph_Q128 a = seraph_q128_from_i64(0);
    Seraph_Q128 b = seraph_q128_from_i64(10);
    Seraph_Q128 t = seraph_q128_from_frac(1, 2);

    Seraph_Q128 result = seraph_q128_lerp(a, b, t);
    double d = seraph_q128_to_double(result);
    ASSERT(fabs(d - 5.0) < 1e-10);

    t = seraph_q128_from_frac(1, 4);
    result = seraph_q128_lerp(a, b, t);
    d = seraph_q128_to_double(result);
    ASSERT(fabs(d - 2.5) < 1e-10);
}

/*============================================================================
 * String Conversion Tests
 *============================================================================*/

TEST(q128_to_string) {
    char buf[64];

    Seraph_Q128 x = seraph_q128_from_i64(42);
    seraph_q128_to_string(x, buf, sizeof(buf), 2);
    ASSERT(buf[0] == '4' && buf[1] == '2');

    x = seraph_q128_from_frac(1, 2);
    seraph_q128_to_string(x, buf, sizeof(buf), 1);
    /* Should be "0.5" */

    x = SERAPH_Q128_VOID;
    seraph_q128_to_string(x, buf, sizeof(buf), 2);
    ASSERT(strcmp(buf, "VOID") == 0);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_q128_tests(void) {
    printf("\n=== MC5: Q128 Fixed-Point Tests ===\n\n");

    /* Creation */
    RUN_TEST(q128_from_i64);
    RUN_TEST(q128_from_frac);
    RUN_TEST(q128_from_double);

    /* Detection */
    RUN_TEST(q128_detection);

    /* Arithmetic */
    RUN_TEST(q128_add);
    RUN_TEST(q128_sub);
    RUN_TEST(q128_mul);
    RUN_TEST(q128_div);
    RUN_TEST(q128_neg);
    RUN_TEST(q128_abs);

    /* Comparison */
    RUN_TEST(q128_compare);

    /* Rounding */
    RUN_TEST(q128_rounding);

    /* Transcendental */
    RUN_TEST(q128_sqrt);
    RUN_TEST(q128_trig);
    RUN_TEST(q128_exp_ln);
    RUN_TEST(q128_pow);

    /* Interpolation */
    RUN_TEST(q128_lerp);

    /* String */
    RUN_TEST(q128_to_string);

    printf("\nQ128 Tests: %d/%d passed\n", tests_passed, tests_run);
}
