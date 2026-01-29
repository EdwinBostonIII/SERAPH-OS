/**
 * @file q16_trig.c
 * @brief SERAPH Q16.16 Zero-Table Trigonometry Implementation
 *
 * MC26: SERAPH Performance Revolution - Pillar 1
 *
 * Implements the complex functions declared in q16_trig.h.
 * All operations are pure integer - ZERO FPU usage.
 */

#include "seraph/q16_trig.h"
#include "seraph/bmi2_intrin.h"

/*============================================================================
 * atan2 Implementation
 *
 * Uses polynomial approximation of atan for the base range [0, 1],
 * then extends using identities:
 *   atan(x) = π/2 - atan(1/x)     for |x| > 1
 *   atan(-x) = -atan(x)
 *============================================================================*/

/* atan(x) polynomial coefficients for x in [0, 1] */
/* atan(x) ≈ x - x³/3 + x⁵/5 - x⁷/7 + ... */
/* Optimized minimax coefficients in Q16.16 */
#define Q16_ATAN_C1  ((Q16)0x0000FFDC)   /* ≈ 0.9998 */
#define Q16_ATAN_C3  ((Q16)0xFFFF5578)   /* ≈ -0.3320 */
#define Q16_ATAN_C5  ((Q16)0x00001556)   /* ≈ 0.0830 */
#define Q16_ATAN_C7  ((Q16)0xFFFFFC00)   /* ≈ -0.0156 */

/**
 * @brief Compute atan(x) for x in [0, 1]
 */
static Q16 q16_atan_unit(Q16 x) {
    if (x <= 0) return 0;
    if (x >= Q16_ONE) return Q16_PI_2 / 2;  /* atan(1) = π/4 */

    Q16 x2 = q16_mul(x, x);

    /* Horner's method */
    Q16 result = Q16_ATAN_C7;
    result = q16_mul(result, x2) + Q16_ATAN_C5;
    result = q16_mul(result, x2) + Q16_ATAN_C3;
    result = q16_mul(result, x2) + Q16_ATAN_C1;
    result = q16_mul(result, x);

    return result;
}

Q16 q16_atan2(Q16 y, Q16 x) {
    /* Handle special cases */
    if (x == 0 && y == 0) {
        return 0;  /* Undefined, return 0 */
    }

    if (x == 0) {
        return (y > 0) ? Q16_PI_2 : -Q16_PI_2;
    }

    if (y == 0) {
        return (x > 0) ? 0 : Q16_PI;
    }

    /* Get absolute values */
    Q16 ax = (x < 0) ? -x : x;
    Q16 ay = (y < 0) ? -y : y;

    Q16 angle;

    if (ay <= ax) {
        /* |y/x| <= 1, use atan directly */
        Q16 ratio = q16_div(ay, ax);
        angle = q16_atan_unit(ratio);
    } else {
        /* |y/x| > 1, use identity: atan(y/x) = π/2 - atan(x/y) */
        Q16 ratio = q16_div(ax, ay);
        angle = Q16_PI_2 - q16_atan_unit(ratio);
    }

    /* Adjust for quadrant */
    if (x < 0) {
        angle = Q16_PI - angle;
    }
    if (y < 0) {
        angle = -angle;
    }

    return angle;
}

/*============================================================================
 * sqrt Implementation
 *
 * Uses Newton-Raphson iteration:
 *   x_{n+1} = (x_n + S/x_n) / 2
 *
 * Starting with a good initial guess based on leading bits.
 *============================================================================*/

Q16 q16_sqrt(Q16 x) {
    if (x <= 0) return 0;

    /* Initial guess: use bit manipulation for fast approximation
     * sqrt(x) ≈ 2^(log2(x)/2) */
    uint32_t ux = (uint32_t)x;

    /* Find highest set bit */
    int msb = 31;
    while (msb > 0 && !(ux & (1u << msb))) msb--;

    /* Initial guess: 2^((msb+16)/2) accounting for Q16.16 format */
    /* For Q16.16, the implicit binary point is at bit 16 */
    int shift = (msb + 16) / 2;
    Q16 guess = (Q16)(1u << shift);

    /* Newton-Raphson iterations */
    for (int i = 0; i < 6; i++) {
        if (guess == 0) break;
        Q16 div_result = q16_div(x, guess);
        guess = (guess + div_result) >> 1;
    }

    return guess;
}

/*============================================================================
 * hypot Implementation
 *
 * Computes sqrt(x² + y²) with overflow protection.
 *
 * Method: Scale to avoid overflow, then scale result back.
 *   Let m = max(|x|, |y|)
 *   hypot = m * sqrt(1 + (min/max)²)
 *============================================================================*/

Q16 q16_hypot(Q16 x, Q16 y) {
    /* Get absolute values */
    if (x < 0) x = -x;
    if (y < 0) y = -y;

    /* Handle trivial cases */
    if (x == 0) return y;
    if (y == 0) return x;

    /* Ensure x >= y */
    if (y > x) {
        Q16 temp = x;
        x = y;
        y = temp;
    }

    /* Now x >= y > 0 */
    /* hypot = x * sqrt(1 + (y/x)²) */

    Q16 ratio = q16_div(y, x);
    Q16 ratio_sq = q16_mul(ratio, ratio);
    Q16 inner = Q16_ONE + ratio_sq;
    Q16 root = q16_sqrt(inner);

    return q16_mul(x, root);
}

/*============================================================================
 * Additional Utility Functions
 *============================================================================*/

/**
 * @brief Integer sine lookup - 256 entry table for sin[0..63]
 *
 * This is NOT used in the zero-table implementation but provided
 * as reference for validation.
 */
#ifdef Q16_ENABLE_TABLE_VALIDATION
static const int16_t sin_table_256[64] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757
};

/**
 * @brief Validate polynomial against table
 *
 * Returns max error in ULPs.
 */
int32_t q16_validate_sin_table(void) {
    int32_t max_error = 0;

    for (int i = 0; i < 64; i++) {
        /* Angle for table index i: (i/64) * (π/2) */
        Q16 angle = (Q16)((int64_t)Q16_PI_2 * i / 64);
        Q16 computed = q16_sin(angle);

        /* Table value scaled to Q16 */
        Q16 table_val = (Q16)sin_table_256[i] << 1;  /* Table is Q15 approx */

        int32_t error = computed - table_val;
        if (error < 0) error = -error;
        if (error > max_error) max_error = error;
    }

    return max_error;
}
#endif

/**
 * @brief Convert degrees to Q16 radians
 */
Q16 q16_deg_to_rad(int32_t degrees) {
    /* radians = degrees * π / 180 */
    /* Use fixed-point multiply: (degrees << 16) * π / 180 */
    int64_t rad = ((int64_t)degrees << 16) * Q16_PI / 180;
    return (Q16)(rad >> 16);
}

/**
 * @brief Convert Q16 radians to degrees
 */
int32_t q16_rad_to_deg(Q16 radians) {
    /* degrees = radians * 180 / π */
    int64_t deg = ((int64_t)radians * 180) / Q16_PI;
    return (int32_t)deg;
}

/**
 * @brief Compute asin(x) using Newton's method
 *
 * asin(x) is the inverse of sin: find θ such that sin(θ) = x
 *
 * @param x Input in Q16.16 range [-1, 1]
 * @return asin(x) in Q16.16 radians
 */
Q16 q16_asin(Q16 x) {
    /* Handle bounds */
    if (x >= Q16_ONE) return Q16_PI_2;
    if (x <= Q16_NEG_ONE) return -Q16_PI_2;

    /* For small x, asin(x) ≈ x */
    if (x > -0x1000 && x < 0x1000) {
        return x;
    }

    /* Use identity: asin(x) = atan(x / sqrt(1 - x²)) */
    Q16 x2 = q16_mul(x, x);
    Q16 denom = q16_sqrt(Q16_ONE - x2);

    if (denom == 0) {
        return (x > 0) ? Q16_PI_2 : -Q16_PI_2;
    }

    return q16_atan2(x, denom);
}

/**
 * @brief Compute acos(x) = π/2 - asin(x)
 *
 * @param x Input in Q16.16 range [-1, 1]
 * @return acos(x) in Q16.16 radians
 */
Q16 q16_acos(Q16 x) {
    return Q16_PI_2 - q16_asin(x);
}
