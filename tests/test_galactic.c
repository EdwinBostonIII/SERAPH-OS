/**
 * @file test_galactic.c
 * @brief Tests for MC5+: Galactic Numbers (Automatic Differentiation)
 */

#include "seraph/galactic.h"
#include <stdio.h>
#include <stdlib.h>
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
static bool approx_eq(double a, double b, double tolerance) {
    return fabs(a - b) < tolerance;
}

/*============================================================================
 * Creation Tests
 *============================================================================*/

TEST(galactic_variable) {
    /* Variable x with tangent = 1 */
    Seraph_Galactic x = seraph_galactic_variable_d(3.0);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(x), 3.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(x), 1.0, 1e-10));
}

TEST(galactic_constant) {
    /* Constant c with tangent = 0 */
    Seraph_Galactic c = seraph_galactic_constant_d(5.0);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(c), 5.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(c), 0.0, 1e-10));
}

TEST(galactic_detection) {
    ASSERT(seraph_galactic_is_void(SERAPH_GALACTIC_VOID));
    ASSERT(!seraph_galactic_is_void(SERAPH_GALACTIC_ZERO));
    ASSERT(!seraph_galactic_is_void(SERAPH_GALACTIC_ONE));

    ASSERT(seraph_galactic_exists(SERAPH_GALACTIC_ONE));
    ASSERT(!seraph_galactic_exists(SERAPH_GALACTIC_VOID));
}

/*============================================================================
 * Basic Arithmetic Tests
 *============================================================================*/

TEST(galactic_add) {
    Seraph_Galactic x = seraph_galactic_variable_d(3.0);  // x = 3, dx = 1
    Seraph_Galactic c = seraph_galactic_constant_d(5.0);  // c = 5, dc = 0

    /* f(x) = x + 5 */
    Seraph_Galactic f = seraph_galactic_add(x, c);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 8.0, 1e-10));  // 3 + 5
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 1.0, 1e-10)); // df/dx = 1

    /* f(x) = x + x */
    f = seraph_galactic_add(x, x);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 6.0, 1e-10));  // 3 + 3
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 2.0, 1e-10)); // df/dx = 2
}

TEST(galactic_sub) {
    Seraph_Galactic x = seraph_galactic_variable_d(10.0);
    Seraph_Galactic c = seraph_galactic_constant_d(3.0);

    /* f(x) = x - 3 */
    Seraph_Galactic f = seraph_galactic_sub(x, c);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 7.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 1.0, 1e-10));
}

TEST(galactic_mul_product_rule) {
    Seraph_Galactic x = seraph_galactic_variable_d(3.0);

    /* f(x) = x² (x × x) */
    /* f'(x) = 2x */
    Seraph_Galactic f = seraph_galactic_mul(x, x);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 9.0, 1e-10));   // 3²
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 6.0, 1e-10));  // 2×3

    /* f(x) = 5x */
    /* f'(x) = 5 */
    Seraph_Galactic five = seraph_galactic_constant_d(5.0);
    f = seraph_galactic_mul(five, x);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 15.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 5.0, 1e-10));
}

TEST(galactic_div_quotient_rule) {
    Seraph_Galactic x = seraph_galactic_variable_d(4.0);
    Seraph_Galactic c = seraph_galactic_constant_d(2.0);

    /* f(x) = x / 2 */
    /* f'(x) = 1/2 */
    Seraph_Galactic f = seraph_galactic_div(x, c);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 2.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 0.5, 1e-10));

    /* f(x) = 1/x */
    /* f'(x) = -1/x² */
    Seraph_Galactic one = seraph_galactic_constant_d(1.0);
    f = seraph_galactic_div(one, x);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 0.25, 1e-10));  // 1/4
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), -0.0625, 1e-8)); // -1/16
}

/*============================================================================
 * Polynomial Tests
 *============================================================================*/

TEST(galactic_polynomial) {
    /* f(x) = x² + 2x + 1 at x = 3 */
    /* f(3) = 9 + 6 + 1 = 16 */
    /* f'(x) = 2x + 2 */
    /* f'(3) = 6 + 2 = 8 */

    Seraph_Galactic x = seraph_galactic_variable_d(3.0);
    Seraph_Galactic two = seraph_galactic_constant_d(2.0);
    Seraph_Galactic one = seraph_galactic_constant_d(1.0);

    Seraph_Galactic x2 = seraph_galactic_mul(x, x);          // x²
    Seraph_Galactic two_x = seraph_galactic_mul(two, x);     // 2x
    Seraph_Galactic sum1 = seraph_galactic_add(x2, two_x);   // x² + 2x
    Seraph_Galactic f = seraph_galactic_add(sum1, one);      // x² + 2x + 1

    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 16.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 8.0, 1e-10));
}

/*============================================================================
 * Transcendental Tests
 *============================================================================*/

TEST(galactic_sqrt) {
    /* f(x) = sqrt(x) at x = 4 */
    /* f(4) = 2 */
    /* f'(x) = 1/(2×sqrt(x)) */
    /* f'(4) = 1/4 = 0.25 */

    Seraph_Galactic x = seraph_galactic_variable_d(4.0);
    Seraph_Galactic f = seraph_galactic_sqrt(x);

    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 2.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 0.25, 1e-10));

    /* Negative sqrt is VOID */
    x = seraph_galactic_variable_d(-1.0);
    f = seraph_galactic_sqrt(x);
    ASSERT(seraph_galactic_is_void(f));
}

TEST(galactic_sin_cos) {
    /* f(x) = sin(x) at x = 0 */
    /* f(0) = 0, f'(0) = cos(0) = 1 */
    Seraph_Galactic x = seraph_galactic_variable_d(0.0);
    Seraph_Galactic s = seraph_galactic_sin(x);

    ASSERT(approx_eq(seraph_galactic_primal_to_double(s), 0.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(s), 1.0, 1e-10));

    /* f(x) = cos(x) at x = 0 */
    /* f(0) = 1, f'(0) = -sin(0) = 0 */
    Seraph_Galactic c = seraph_galactic_cos(x);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(c), 1.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(c), 0.0, 1e-10));
}

TEST(galactic_exp) {
    /* f(x) = e^x at x = 0 */
    /* f(0) = 1, f'(0) = e^0 = 1 */
    Seraph_Galactic x = seraph_galactic_variable_d(0.0);
    Seraph_Galactic f = seraph_galactic_exp(x);

    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 1.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 1.0, 1e-10));

    /* f(x) = e^x at x = 1 */
    /* f(1) = e, f'(1) = e */
    x = seraph_galactic_variable_d(1.0);
    f = seraph_galactic_exp(x);

    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 2.71828, 1e-4));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 2.71828, 1e-4));
}

TEST(galactic_ln) {
    /* f(x) = ln(x) at x = e */
    /* f(e) = 1, f'(e) = 1/e */
    Seraph_Galactic x = seraph_galactic_variable_d(2.71828182845904);
    Seraph_Galactic f = seraph_galactic_ln(x);

    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 1.0, 1e-4));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 1.0/2.71828, 1e-4));

    /* ln of non-positive is VOID */
    x = seraph_galactic_variable_d(0.0);
    f = seraph_galactic_ln(x);
    ASSERT(seraph_galactic_is_void(f));
}

/*============================================================================
 * Chain Rule Test (Composition)
 *============================================================================*/

TEST(galactic_chain_rule) {
    /* f(x) = sin(x²) at x = 1 */
    /* f(1) = sin(1) */
    /* f'(x) = cos(x²) × 2x (chain rule) */
    /* f'(1) = cos(1) × 2 */

    Seraph_Galactic x = seraph_galactic_variable_d(1.0);
    Seraph_Galactic x2 = seraph_galactic_mul(x, x);       // x² with derivative 2x
    Seraph_Galactic f = seraph_galactic_sin(x2);          // sin(x²) with chain rule

    double expected_value = sin(1.0);          // sin(1)
    double expected_deriv = cos(1.0) * 2.0;    // cos(1) × 2

    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), expected_value, 1e-6));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), expected_deriv, 1e-6));
}

/*============================================================================
 * VOID Propagation Tests
 *============================================================================*/

TEST(galactic_void_propagation) {
    Seraph_Galactic x = seraph_galactic_variable_d(3.0);

    /* VOID propagates through operations */
    Seraph_Galactic sum = seraph_galactic_add(x, SERAPH_GALACTIC_VOID);
    ASSERT(seraph_galactic_is_void(sum));

    Seraph_Galactic prod = seraph_galactic_mul(SERAPH_GALACTIC_VOID, x);
    ASSERT(seraph_galactic_is_void(prod));

    /* Division by zero */
    Seraph_Galactic zero = seraph_galactic_constant_d(0.0);
    Seraph_Galactic div_result = seraph_galactic_div(x, zero);
    ASSERT(seraph_galactic_is_void(div_result));
}

/*============================================================================
 * Utility Tests
 *============================================================================*/

TEST(galactic_lerp) {
    /* lerp(0, 10, 0.5) = 5 */
    Seraph_Galactic a = seraph_galactic_constant_d(0.0);
    Seraph_Galactic b = seraph_galactic_constant_d(10.0);
    Seraph_Galactic t = seraph_galactic_variable_d(0.5);  // t is variable

    Seraph_Galactic f = seraph_galactic_lerp(a, b, t);

    /* f(t) = 0 + t × (10 - 0) = 10t */
    /* f(0.5) = 5 */
    /* f'(t) = 10 */
    ASSERT(approx_eq(seraph_galactic_primal_to_double(f), 5.0, 1e-10));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(f), 10.0, 1e-10));
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_galactic_tests(void) {
    printf("\n=== MC5+: Galactic Numbers Tests ===\n\n");

    /* Creation */
    RUN_TEST(galactic_variable);
    RUN_TEST(galactic_constant);
    RUN_TEST(galactic_detection);

    /* Arithmetic */
    RUN_TEST(galactic_add);
    RUN_TEST(galactic_sub);
    RUN_TEST(galactic_mul_product_rule);
    RUN_TEST(galactic_div_quotient_rule);

    /* Polynomial */
    RUN_TEST(galactic_polynomial);

    /* Transcendental */
    RUN_TEST(galactic_sqrt);
    RUN_TEST(galactic_sin_cos);
    RUN_TEST(galactic_exp);
    RUN_TEST(galactic_ln);

    /* Chain Rule */
    RUN_TEST(galactic_chain_rule);

    /* VOID */
    RUN_TEST(galactic_void_propagation);

    /* Utility */
    RUN_TEST(galactic_lerp);

    printf("\nGalactic Tests: %d/%d passed\n", tests_passed, tests_run);
}
