/**
 * @file integers.c
 * @brief MC4: Entropic Arithmetic Implementation
 */

#include "seraph/integers.h"

/*============================================================================
 * Power Implementation
 *============================================================================*/

uint64_t seraph_pow_u64(uint64_t base, uint32_t exp, Seraph_ArithMode mode) {
    /* Early exit for special cases (branchless-friendly) */
    uint64_t void_mask = seraph_void_mask_u64(base);
    if (exp == 0) return seraph_select_u64(SERAPH_VOID_U64, 1, void_mask);
    if (base == 0) return 0;
    if (base == 1) return 1;

    uint64_t result = 1;

    /* Binary exponentiation - NO checking inside loop (deferred) */
    while (exp > 0) {
        if (exp & 1) {
            result = seraph_mul_u64(result, base, mode);
        }
        exp >>= 1;
        if (exp > 0) {
            base = seraph_mul_u64(base, base, mode);
        }
    }

    /* VOID propagates automatically through mul operations */
    return result;
}

/*============================================================================
 * Array Operations
 *============================================================================*/

/**
 * @brief Add arrays element-wise
 */
void seraph_add_array_u64(const uint64_t* a, const uint64_t* b, uint64_t* dst,
                          size_t count, Seraph_ArithMode mode) {
    if (!a || !b || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_add_u64(a[i], b[i], mode);
    }
}

/**
 * @brief Subtract arrays element-wise
 */
void seraph_sub_array_u64(const uint64_t* a, const uint64_t* b, uint64_t* dst,
                          size_t count, Seraph_ArithMode mode) {
    if (!a || !b || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_sub_u64(a[i], b[i], mode);
    }
}

/**
 * @brief Multiply arrays element-wise
 */
void seraph_mul_array_u64(const uint64_t* a, const uint64_t* b, uint64_t* dst,
                          size_t count, Seraph_ArithMode mode) {
    if (!a || !b || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_mul_u64(a[i], b[i], mode);
    }
}

/**
 * @brief Scale array by constant
 */
void seraph_scale_array_u64(const uint64_t* a, uint64_t scale, uint64_t* dst,
                            size_t count, Seraph_ArithMode mode) {
    if (!a || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_mul_u64(a[i], scale, mode);
    }
}

/**
 * @brief Sum all elements in array (deferred VOID checking)
 *
 * VOID propagates automatically through the branchless add operations.
 * No checking inside the loop - raw speed with full safety.
 */
uint64_t seraph_sum_array_u64(const uint64_t* a, size_t count, Seraph_ArithMode mode) {
    if (!a || count == 0) return 0;

    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum = seraph_add_u64(sum, a[i], mode);
        /* NO CHECK HERE - VOID propagates automatically */
    }
    return sum;  /* Will be VOID if any element was VOID or overflow occurred */
}

/**
 * @brief Product of all elements in array (deferred VOID checking)
 */
uint64_t seraph_product_array_u64(const uint64_t* a, size_t count, Seraph_ArithMode mode) {
    if (!a || count == 0) return 1;

    uint64_t product = 1;
    for (size_t i = 0; i < count; i++) {
        product = seraph_mul_u64(product, a[i], mode);
        /* NO CHECK HERE - VOID propagates automatically */
    }
    return product;  /* Will be VOID if any element was VOID or overflow occurred */
}

/*============================================================================
 * Dot Product
 *============================================================================*/

/**
 * @brief Dot product of two arrays (deferred VOID checking)
 */
uint64_t seraph_dot_u64(const uint64_t* a, const uint64_t* b, size_t count,
                        Seraph_ArithMode mode) {
    if (!a || !b || count == 0) return 0;

    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        uint64_t product = seraph_mul_u64(a[i], b[i], mode);
        sum = seraph_add_u64(sum, product, mode);
        /* NO CHECK HERE - VOID propagates automatically */
    }
    return sum;  /* Will be VOID if any operation resulted in VOID */
}

/*============================================================================
 * GCD / LCM
 *============================================================================*/

/**
 * @brief Greatest Common Divisor (Euclidean algorithm, branchless VOID check at entry only)
 */
uint64_t seraph_gcd_u64(uint64_t a, uint64_t b) {
    uint64_t void_mask = seraph_void_mask2_u64(a, b);
    if (void_mask) return SERAPH_VOID_U64;  /* Single check at entry */

    while (b != 0) {
        uint64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/**
 * @brief Least Common Multiple (branchless VOID check at entry)
 */
uint64_t seraph_lcm_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode) {
    uint64_t void_mask = seraph_void_mask2_u64(a, b);
    if (void_mask) return SERAPH_VOID_U64;  /* Single check at entry */

    if (a == 0 || b == 0) return 0;

    uint64_t gcd = seraph_gcd_u64(a, b);
    /* LCM = (a / gcd) * b - VOID propagates through mul */
    return seraph_mul_u64(a / gcd, b, mode);
}

/*============================================================================
 * Factorial
 *============================================================================*/

/**
 * @brief Factorial with overflow control (deferred VOID checking)
 *
 * Returns VOID for n > 20 in VOID mode (20! is last to fit in uint64_t)
 */
uint64_t seraph_factorial_u64(uint32_t n, Seraph_ArithMode mode) {
    if (n <= 1) return 1;

    /* 21! > UINT64_MAX - early exit for known overflow */
    if (n > 20 && mode == SERAPH_ARITH_VOID) {
        return SERAPH_VOID_U64;
    }

    uint64_t result = 1;
    for (uint32_t i = 2; i <= n; i++) {
        result = seraph_mul_u64(result, i, mode);
        /* NO CHECK HERE - VOID propagates automatically through mul */
    }
    return result;  /* Will be VOID if overflow occurred in VOID mode */
}

/*============================================================================
 * Integer Square Root
 *============================================================================*/

/**
 * @brief Integer square root (floor) - single VOID check at entry
 */
uint64_t seraph_isqrt_u64(uint64_t n) {
    uint64_t void_mask = seraph_void_mask_u64(n);
    if (void_mask) return SERAPH_VOID_U64;
    if (n == 0) return 0;

    /* Newton's method - no checks inside loop */
    uint64_t x = n;
    uint64_t y = (x + 1) >> 1;

    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }

    return x;
}

/**
 * @brief Integer cube root (floor) - single VOID check at entry
 */
uint64_t seraph_icbrt_u64(uint64_t n) {
    uint64_t void_mask = seraph_void_mask_u64(n);
    if (void_mask) return SERAPH_VOID_U64;
    if (n == 0) return 0;

    /* Binary search - no checks inside loop */
    uint64_t lo = 1;
    uint64_t hi = 2642245;  /* cbrt(2^64) ≈ 2642245 */

    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1) / 2;
        if (mid <= 2642245 && mid * mid * mid <= n) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    return lo;
}

/*============================================================================
 * Widening Operations
 *============================================================================*/

/**
 * @brief Full 128-bit multiply result (single VOID check at entry)
 */
void seraph_mul_u64_wide(uint64_t a, uint64_t b, uint64_t* lo, uint64_t* hi) {
    if (!lo || !hi) return;

    uint64_t void_mask = seraph_void_mask2_u64(a, b);
    if (void_mask) {
        *lo = SERAPH_VOID_U64;
        *hi = SERAPH_VOID_U64;
        return;
    }

#if defined(__SIZEOF_INT128__)
    __uint128_t result = (__uint128_t)a * (__uint128_t)b;
    *lo = (uint64_t)result;
    *hi = (uint64_t)(result >> 64);
#else
    /* Decompose into 32-bit parts */
    uint64_t a_lo = a & 0xFFFFFFFF;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFF;
    uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t carry = (p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF);

    *lo = (p0 & 0xFFFFFFFF) | (carry << 32);
    *hi = p3 + (p1 >> 32) + (p2 >> 32) + (carry >> 32);
#endif
}
