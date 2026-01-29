/**
 * @file q128.c
 * @brief MC5: Q128 Fixed-Point Implementation
 */

#include "seraph/q128.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/* Pi = 3.14159265358979323846... */
/* hi = 3, lo = 0x243F6A8885A308D3 (0.14159... * 2^64) */
const Seraph_Q128 SERAPH_Q128_PI = { 3, 0x243F6A8885A308D3ULL };

/* Pi/2 */
const Seraph_Q128 SERAPH_Q128_PI_2 = { 1, 0x921FB54442D18469ULL };

/* 2*Pi */
const Seraph_Q128 SERAPH_Q128_2PI = { 6, 0x487ED5110B4611A6ULL };

/* e = 2.71828182845904523536... */
const Seraph_Q128 SERAPH_Q128_E = { 2, 0xB7E151628AED2A6AULL };

/* ln(2) = 0.69314718055994530941... */
const Seraph_Q128 SERAPH_Q128_LN2 = { 0, 0xB17217F7D1CF79ABULL };

/* sqrt(2) = 1.41421356237309504880... */
const Seraph_Q128 SERAPH_Q128_SQRT2 = { 1, 0x6A09E667F3BCC908ULL };

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/* Unsigned 128-bit type for intermediate calculations */
typedef struct {
    uint64_t lo;
    uint64_t hi;
} U128;

/* Add two U128 */
static U128 u128_add(U128 a, U128 b) {
    U128 result;
    result.lo = a.lo + b.lo;
    result.hi = a.hi + b.hi + (result.lo < a.lo ? 1 : 0);
    return result;
}

/* Subtract U128: a - b */
__attribute__((unused))
static U128 u128_sub(U128 a, U128 b) {
    U128 result;
    result.lo = a.lo - b.lo;
    result.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0);
    return result;
}

/* Compare U128: returns -1, 0, +1 */
__attribute__((unused))
static int u128_compare(U128 a, U128 b) {
    if (a.hi != b.hi) return (a.hi > b.hi) ? 1 : -1;
    if (a.lo != b.lo) return (a.lo > b.lo) ? 1 : -1;
    return 0;
}

/* Multiply two uint64_t to get U128 */
static U128 u64_mul_wide(uint64_t a, uint64_t b) {
    U128 result;

#if defined(__SIZEOF_INT128__)
    __uint128_t r = (__uint128_t)a * (__uint128_t)b;
    result.lo = (uint64_t)r;
    result.hi = (uint64_t)(r >> 64);
#else
    uint64_t a_lo = a & 0xFFFFFFFF;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFF;
    uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t carry = ((p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF)) >> 32;

    result.lo = p0 + (p1 << 32) + (p2 << 32);
    result.hi = p3 + (p1 >> 32) + (p2 >> 32) + carry;
#endif

    return result;
}

/* Right shift U128 by n bits */
__attribute__((unused))
static U128 u128_shr(U128 x, int n) {
    if (n >= 128) return (U128){0, 0};
    if (n >= 64) {
        return (U128){ x.hi >> (n - 64), 0 };
    }
    if (n == 0) return x;
    return (U128){
        (x.lo >> n) | (x.hi << (64 - n)),
        x.hi >> n
    };
}

/*============================================================================
 * Q128 Creation
 *============================================================================*/

Seraph_Q128 seraph_q128_from_frac(int64_t num, int64_t denom) {
    if (SERAPH_IS_VOID_I64(num) || SERAPH_IS_VOID_I64(denom) || denom == 0) {
        return SERAPH_Q128_VOID;
    }

    /* Handle sign */
    bool negative = (num < 0) != (denom < 0);
    uint64_t abs_num = (num < 0) ? (uint64_t)(-num) : (uint64_t)num;
    uint64_t abs_denom = (uint64_t)(denom < 0 ? -denom : denom);

    /* Integer part */
    int64_t int_part = (int64_t)(abs_num / abs_denom);
    uint64_t remainder = abs_num % abs_denom;

    /* Fractional part: (remainder * 2^64) / denom */
    U128 wide_remainder = { 0, remainder };  /* remainder << 64 */
    uint64_t frac_part = 0;

    /* Long division for fractional part */
    for (int i = 0; i < 64; i++) {
        wide_remainder = u128_add(wide_remainder, wide_remainder);  /* << 1 */
        if (wide_remainder.hi >= abs_denom) {
            wide_remainder.hi -= abs_denom;
            frac_part |= (1ULL << (63 - i));
        }
    }

    if (negative) {
        /* Negate result */
        if (frac_part == 0) {
            return (Seraph_Q128){ -int_part, 0 };
        } else {
            return (Seraph_Q128){ -int_part - 1, ~frac_part + 1 };
        }
    }

    return (Seraph_Q128){ int_part, frac_part };
}

Seraph_Q128 seraph_q128_from_double(double d) {
    if (d != d) return SERAPH_Q128_VOID;  /* NaN check */

    bool negative = d < 0;
    if (negative) d = -d;

    int64_t int_part = (int64_t)d;
    double frac = d - (double)int_part;

    /* Convert fractional part: frac * 2^64 */
    uint64_t frac_part = (uint64_t)(frac * 18446744073709551616.0);

    Seraph_Q128 result = { int_part, frac_part };
    if (negative) result = seraph_q128_neg(result);

    return result;
}

/*============================================================================
 * Q128 Conversion
 *============================================================================*/

double seraph_q128_to_double(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return 0.0 / 0.0;  /* NaN */

    double result = (double)x.hi + (double)x.lo / 18446744073709551616.0;
    return result;
}

int seraph_q128_to_string(Seraph_Q128 x, char* buf, int buf_size, int decimals) {
    if (!buf || buf_size < 2) return 0;

    if (seraph_q128_is_void(x)) {
        strncpy(buf, "VOID", buf_size - 1);
        buf[buf_size - 1] = '\0';
        return 4;
    }

    int pos = 0;

    /* Handle negative */
    bool negative = x.hi < 0;
    if (negative) {
        x = seraph_q128_neg(x);
        if (pos < buf_size - 1) buf[pos++] = '-';
    }

    /* Integer part */
    pos += snprintf(buf + pos, buf_size - pos, "%lld", (long long)x.hi);

    /* Fractional part */
    if (decimals > 0 && pos < buf_size - 1) {
        buf[pos++] = '.';

        uint64_t frac = x.lo;
        for (int i = 0; i < decimals && pos < buf_size - 1; i++) {
            /* Multiply frac by 10 */
            U128 wide = u64_mul_wide(frac, 10);
            int digit = (int)wide.hi;
            frac = wide.lo;
            buf[pos++] = '0' + digit;
        }
    }

    buf[pos] = '\0';
    return pos;
}

/*============================================================================
 * Q128 Basic Arithmetic
 *============================================================================*/

Seraph_Q128 seraph_q128_add(Seraph_Q128 a, Seraph_Q128 b) {
    Seraph_Q128 void_mask = seraph_q128_void_mask2(a, b);

    Seraph_Q128 result;
    uint64_t carry = 0;
    result.lo = a.lo + b.lo;
    carry = (result.lo < a.lo) ? 1 : 0;
    result.hi = a.hi + b.hi + (int64_t)carry;

    /* Overflow check (branchless): same signs but result has different sign */
    int64_t same_sign = -(int64_t)((a.hi >= 0) == (b.hi >= 0));
    int64_t result_diff = -(int64_t)((a.hi >= 0) != (result.hi >= 0));
    int64_t overflow = same_sign & result_diff;
    Seraph_Q128 overflow_mask = { overflow, (uint64_t)overflow };

    Seraph_Q128 combined_mask = { void_mask.hi | overflow_mask.hi,
                                   void_mask.lo | overflow_mask.lo };
    return seraph_q128_select(SERAPH_Q128_VOID, result, combined_mask);
}

Seraph_Q128 seraph_q128_sub(Seraph_Q128 a, Seraph_Q128 b) {
    Seraph_Q128 void_mask = seraph_q128_void_mask2(a, b);

    Seraph_Q128 result;
    uint64_t borrow = (a.lo < b.lo) ? 1 : 0;
    result.lo = a.lo - b.lo;
    result.hi = a.hi - b.hi - (int64_t)borrow;

    return seraph_q128_select(SERAPH_Q128_VOID, result, void_mask);
}

Seraph_Q128 seraph_q128_neg(Seraph_Q128 x) {
    Seraph_Q128 void_mask = seraph_q128_void_mask(x);

    /* Branchless negation: if lo==0, result is {-hi, 0}, else {~hi, ~lo+1} */
    int64_t lo_zero = -(int64_t)(x.lo == 0);
    Seraph_Q128 neg_when_lo_zero = { -x.hi, 0 };
    Seraph_Q128 neg_when_lo_nonzero = { ~x.hi, ~x.lo + 1 };

    Seraph_Q128 lo_zero_mask = { lo_zero, (uint64_t)lo_zero };
    Seraph_Q128 result = seraph_q128_select(neg_when_lo_zero, neg_when_lo_nonzero, lo_zero_mask);

    return seraph_q128_select(SERAPH_Q128_VOID, result, void_mask);
}

Seraph_Q128 seraph_q128_abs(Seraph_Q128 x) {
    Seraph_Q128 void_mask = seraph_q128_void_mask(x);

    /* Branchless: negate if negative */
    int64_t is_neg = -(int64_t)(x.hi < 0);
    Seraph_Q128 neg_x = seraph_q128_neg(x);
    Seraph_Q128 neg_mask = { is_neg, (uint64_t)is_neg };
    Seraph_Q128 result = seraph_q128_select(neg_x, x, neg_mask);

    return seraph_q128_select(SERAPH_Q128_VOID, result, void_mask);
}

Seraph_Q128 seraph_q128_mul(Seraph_Q128 a, Seraph_Q128 b) {
    Seraph_Q128 void_mask = seraph_q128_void_mask2(a, b);

    /* Determine sign (branchless) */
    int64_t negative = -(int64_t)((a.hi < 0) != (b.hi < 0));

    /* Get absolute values (branchless) */
    Seraph_Q128 abs_a = seraph_q128_abs(a);
    Seraph_Q128 abs_b = seraph_q128_abs(b);

    /* Full 256-bit multiplication, then extract middle 128 bits */
    U128 p00 = u64_mul_wide(abs_a.lo, abs_b.lo);
    U128 p01 = u64_mul_wide(abs_a.lo, (uint64_t)abs_b.hi);
    U128 p10 = u64_mul_wide((uint64_t)abs_a.hi, abs_b.lo);
    U128 p11 = u64_mul_wide((uint64_t)abs_a.hi, (uint64_t)abs_b.hi);

    /* Middle 128 bits */
    uint64_t result_lo = p00.hi;
    uint64_t result_hi = p11.lo;

    uint64_t carry1 = (result_lo + p01.lo < result_lo) ? 1 : 0;
    result_lo += p01.lo;
    result_hi += p01.hi + carry1;

    uint64_t carry2 = (result_lo + p10.lo < result_lo) ? 1 : 0;
    result_lo += p10.lo;
    result_hi += p10.hi + carry2;

    Seraph_Q128 result = { (int64_t)result_hi, result_lo };

    /* Negate if needed (branchless) */
    Seraph_Q128 neg_result = seraph_q128_neg(result);
    Seraph_Q128 neg_mask = { negative, (uint64_t)negative };
    result = seraph_q128_select(neg_result, result, neg_mask);

    return seraph_q128_select(SERAPH_Q128_VOID, result, void_mask);
}

Seraph_Q128 seraph_q128_div(Seraph_Q128 a, Seraph_Q128 b) {
    Seraph_Q128 void_mask = seraph_q128_void_mask2(a, b);

    /* Check for division by zero (branchless mask) */
    int64_t is_zero = -(int64_t)(seraph_q128_is_zero(b));
    Seraph_Q128 zero_mask = { is_zero, (uint64_t)is_zero };
    Seraph_Q128 combined_void = { void_mask.hi | zero_mask.hi, void_mask.lo | zero_mask.lo };

    /* Determine sign (branchless) */
    int64_t negative = -(int64_t)((a.hi < 0) != (b.hi < 0));

    /* Get absolute values */
    Seraph_Q128 abs_a = seraph_q128_abs(a);
    Seraph_Q128 abs_b = seraph_q128_abs(b);

    /* Avoid division by zero in approximation by using safe value */
    Seraph_Q128 safe_b = seraph_q128_select(SERAPH_Q128_ONE, abs_b, zero_mask);

    /* Initial estimate for 1/b */
    double b_approx = seraph_q128_to_double(safe_b);
    Seraph_Q128 x = seraph_q128_from_double(1.0 / b_approx);

    /* Newton iterations: x = x * (2 - b*x) */
    Seraph_Q128 two = seraph_q128_from_i64(2);
    for (int i = 0; i < 4; i++) {
        Seraph_Q128 bx = seraph_q128_mul(safe_b, x);
        Seraph_Q128 diff = seraph_q128_sub(two, bx);
        x = seraph_q128_mul(x, diff);
    }

    Seraph_Q128 result = seraph_q128_mul(abs_a, x);

    /* Negate if needed (branchless) */
    Seraph_Q128 neg_result = seraph_q128_neg(result);
    Seraph_Q128 neg_mask = { negative, (uint64_t)negative };
    result = seraph_q128_select(neg_result, result, neg_mask);

    return seraph_q128_select(SERAPH_Q128_VOID, result, combined_void);
}

int seraph_q128_compare(Seraph_Q128 a, Seraph_Q128 b) {
    if (seraph_q128_is_void(a) || seraph_q128_is_void(b)) return -128;

    if (a.hi != b.hi) return (a.hi < b.hi) ? -1 : 1;
    if (a.lo != b.lo) return (a.lo < b.lo) ? -1 : 1;
    return 0;
}

/*============================================================================
 * Q128 Rounding
 *============================================================================*/

Seraph_Q128 seraph_q128_floor(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    return (Seraph_Q128){ x.hi, 0 };
}

Seraph_Q128 seraph_q128_ceil(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    if (x.lo == 0) return (Seraph_Q128){ x.hi, 0 };
    return (Seraph_Q128){ x.hi + 1, 0 };
}

Seraph_Q128 seraph_q128_trunc(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    if (x.hi >= 0) return (Seraph_Q128){ x.hi, 0 };
    if (x.lo == 0) return (Seraph_Q128){ x.hi, 0 };
    return (Seraph_Q128){ x.hi + 1, 0 };
}

Seraph_Q128 seraph_q128_round(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    if (x.lo >= 0x8000000000000000ULL) {
        return (Seraph_Q128){ x.hi + (x.hi >= 0 ? 1 : 0), 0 };
    }
    return (Seraph_Q128){ x.hi, 0 };
}

Seraph_Q128 seraph_q128_frac(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    if (x.hi >= 0) return (Seraph_Q128){ 0, x.lo };
    if (x.lo == 0) return SERAPH_Q128_ZERO;
    return (Seraph_Q128){ -1, x.lo };
}

/*============================================================================
 * Q128 Square Root
 *============================================================================*/

Seraph_Q128 seraph_q128_sqrt(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    if (x.hi < 0) return SERAPH_Q128_VOID;
    if (seraph_q128_is_zero(x)) return SERAPH_Q128_ZERO;

    /* Newton-Raphson: x_{n+1} = (x_n + S/x_n) / 2 */
    double approx = seraph_q128_to_double(x);
    Seraph_Q128 guess = seraph_q128_from_double(__builtin_sqrt(approx));

    Seraph_Q128 half = SERAPH_Q128_HALF;

    for (int i = 0; i < 6; i++) {
        Seraph_Q128 div = seraph_q128_div(x, guess);
        Seraph_Q128 sum = seraph_q128_add(guess, div);
        guess = seraph_q128_mul(sum, half);
    }

    return guess;
}

/*============================================================================
 * Q128 Trigonometric Functions (Simplified implementations)
 *============================================================================*/

/* Range reduction: reduce x to [-pi, pi] */
static Seraph_Q128 reduce_angle(Seraph_Q128 x) {
    Seraph_Q128 twopi = SERAPH_Q128_2PI;
    Seraph_Q128 pi = SERAPH_Q128_PI;

    /* x = x - floor(x / 2pi) * 2pi */
    Seraph_Q128 n = seraph_q128_floor(seraph_q128_div(x, twopi));
    x = seraph_q128_sub(x, seraph_q128_mul(n, twopi));

    /* Shift to [-pi, pi] */
    if (seraph_vbit_is_true(seraph_q128_gt(x, pi))) {
        x = seraph_q128_sub(x, twopi);
    }

    return x;
}

Seraph_Q128 seraph_q128_sin(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;

    x = reduce_angle(x);

    /* Taylor series: sin(x) = x - x^3/6 + x^5/120 - x^7/5040 + ... */
    Seraph_Q128 result = x;
    Seraph_Q128 term = x;
    Seraph_Q128 x2 = seraph_q128_mul(x, x);

    for (int n = 1; n <= 10; n++) {
        term = seraph_q128_mul(term, x2);
        term = seraph_q128_neg(term);
        Seraph_Q128 denom = seraph_q128_from_i64((2*n) * (2*n + 1));
        term = seraph_q128_div(term, denom);
        result = seraph_q128_add(result, term);
    }

    return result;
}

Seraph_Q128 seraph_q128_cos(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;

    x = reduce_angle(x);

    /* Taylor series: cos(x) = 1 - x^2/2 + x^4/24 - ... */
    Seraph_Q128 result = SERAPH_Q128_ONE;
    Seraph_Q128 term = SERAPH_Q128_ONE;
    Seraph_Q128 x2 = seraph_q128_mul(x, x);

    for (int n = 1; n <= 10; n++) {
        term = seraph_q128_mul(term, x2);
        term = seraph_q128_neg(term);
        Seraph_Q128 denom = seraph_q128_from_i64((2*n - 1) * (2*n));
        term = seraph_q128_div(term, denom);
        result = seraph_q128_add(result, term);
    }

    return result;
}

Seraph_Q128 seraph_q128_tan(Seraph_Q128 x) {
    Seraph_Q128 s = seraph_q128_sin(x);
    Seraph_Q128 c = seraph_q128_cos(x);
    return seraph_q128_div(s, c);
}

Seraph_Q128 seraph_q128_exp(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;

    /*
     * Range reduction: exp(x) = exp(x - k*ln(2)) * 2^k
     * Choose k so that |x - k*ln(2)| < 0.5
     * This keeps Taylor series argument small for better convergence.
     */

    /* k = round(x / ln(2)) */
    Seraph_Q128 x_over_ln2 = seraph_q128_div(x, SERAPH_Q128_LN2);
    int64_t k = seraph_q128_to_i64(seraph_q128_round(x_over_ln2));

    /* Clamp k to prevent overflow in 2^k */
    if (k > 62) k = 62;
    if (k < -62) return SERAPH_Q128_ZERO;  /* Underflow to zero */

    /* x_reduced = x - k * ln(2) */
    Seraph_Q128 k_q128 = seraph_q128_from_i64(k);
    Seraph_Q128 x_reduced = seraph_q128_sub(x, seraph_q128_mul(k_q128, SERAPH_Q128_LN2));

    /* Taylor series: exp(x_reduced) = 1 + x + x^2/2! + x^3/3! + ... */
    /* With |x_reduced| < 0.5, 25 terms gives excellent precision */
    Seraph_Q128 result = SERAPH_Q128_ONE;
    Seraph_Q128 term = SERAPH_Q128_ONE;

    for (int n = 1; n <= 25; n++) {
        term = seraph_q128_mul(term, x_reduced);
        term = seraph_q128_div(term, seraph_q128_from_i64(n));
        result = seraph_q128_add(result, term);
    }

    /* Multiply by 2^k using repeated squaring */
    if (k > 0) {
        Seraph_Q128 two = seraph_q128_from_i64(2);
        for (int64_t i = 0; i < k; i++) {
            result = seraph_q128_mul(result, two);
        }
    } else if (k < 0) {
        Seraph_Q128 half = SERAPH_Q128_HALF;
        for (int64_t i = 0; i < -k; i++) {
            result = seraph_q128_mul(result, half);
        }
    }

    return result;
}

Seraph_Q128 seraph_q128_ln(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    if (!seraph_q128_is_positive(x)) return SERAPH_Q128_VOID;

    /* Newton-Raphson: find y such that e^y = x */
    double approx = seraph_q128_to_double(x);
    Seraph_Q128 y = seraph_q128_from_double(__builtin_log(approx));

    for (int i = 0; i < 5; i++) {
        Seraph_Q128 ey = seraph_q128_exp(y);
        Seraph_Q128 diff = seraph_q128_sub(x, ey);
        Seraph_Q128 correction = seraph_q128_div(diff, ey);
        y = seraph_q128_add(y, correction);
    }

    return y;
}

Seraph_Q128 seraph_q128_log2(Seraph_Q128 x) {
    Seraph_Q128 ln_x = seraph_q128_ln(x);
    return seraph_q128_div(ln_x, SERAPH_Q128_LN2);
}

Seraph_Q128 seraph_q128_log10(Seraph_Q128 x) {
    Seraph_Q128 ln_x = seraph_q128_ln(x);
    Seraph_Q128 ln_10 = seraph_q128_ln(seraph_q128_from_i64(10));
    return seraph_q128_div(ln_x, ln_10);
}

/*
 * Binary exponentiation for integer powers (exact)
 */
static Seraph_Q128 q128_pow_int(Seraph_Q128 base, int64_t n) {
    if (n == 0) return SERAPH_Q128_ONE;

    bool negative = n < 0;
    uint64_t exp = negative ? (uint64_t)(-n) : (uint64_t)n;

    Seraph_Q128 result = SERAPH_Q128_ONE;
    Seraph_Q128 power = base;

    /* Binary exponentiation: O(log n) multiplications */
    while (exp > 0) {
        if (exp & 1) {
            result = seraph_q128_mul(result, power);
        }
        power = seraph_q128_mul(power, power);
        exp >>= 1;
    }

    /* For negative exponent: 1 / result */
    if (negative) {
        result = seraph_q128_div(SERAPH_Q128_ONE, result);
    }

    return result;
}

/*
 * Check if Q128 is an integer (no fractional part)
 */
static bool q128_is_integer(Seraph_Q128 x) {
    return x.lo == 0;
}

Seraph_Q128 seraph_q128_pow(Seraph_Q128 base, Seraph_Q128 exp) {
    if (seraph_q128_is_void(base) || seraph_q128_is_void(exp)) {
        return SERAPH_Q128_VOID;
    }

    /* Handle zero base */
    if (seraph_q128_is_zero(base)) {
        if (seraph_q128_is_positive(exp)) return SERAPH_Q128_ZERO;
        return SERAPH_Q128_VOID;  /* 0^0 or 0^negative */
    }

    /* Special case: exp = 0 */
    if (seraph_q128_is_zero(exp)) {
        return SERAPH_Q128_ONE;
    }

    /* Special case: exp = 1 */
    if (exp.hi == 1 && exp.lo == 0) {
        return base;
    }

    /* Integer exponent: use exact binary exponentiation */
    if (q128_is_integer(exp)) {
        return q128_pow_int(base, exp.hi);
    }

    /* Special case: exp = 0.5 -> sqrt */
    if (exp.hi == 0 && exp.lo == 0x8000000000000000ULL) {
        return seraph_q128_sqrt(base);
    }

    /* Special case: exp = 0.25 -> sqrt(sqrt(x)) */
    if (exp.hi == 0 && exp.lo == 0x4000000000000000ULL) {
        return seraph_q128_sqrt(seraph_q128_sqrt(base));
    }

    /* Negative base with non-integer exponent is VOID (complex result) */
    if (base.hi < 0) {
        return SERAPH_Q128_VOID;
    }

    /* General case: base^exp = e^(exp * ln(base)) */
    Seraph_Q128 ln_base = seraph_q128_ln(base);
    Seraph_Q128 product = seraph_q128_mul(exp, ln_base);
    return seraph_q128_exp(product);
}

/* Placeholder implementations for remaining functions */
Seraph_Q128 seraph_q128_asin(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    /* Simplified: use Newton-Raphson on sin(y) = x */
    double approx = seraph_q128_to_double(x);
    return seraph_q128_from_double(__builtin_asin(approx));
}

Seraph_Q128 seraph_q128_acos(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    double approx = seraph_q128_to_double(x);
    return seraph_q128_from_double(__builtin_acos(approx));
}

Seraph_Q128 seraph_q128_atan(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    double approx = seraph_q128_to_double(x);
    return seraph_q128_from_double(__builtin_atan(approx));
}

Seraph_Q128 seraph_q128_atan2(Seraph_Q128 y, Seraph_Q128 x) {
    if (seraph_q128_is_void(x) || seraph_q128_is_void(y)) return SERAPH_Q128_VOID;
    double y_d = seraph_q128_to_double(y);
    double x_d = seraph_q128_to_double(x);
    return seraph_q128_from_double(__builtin_atan2(y_d, x_d));
}

Seraph_Q128 seraph_q128_sinh(Seraph_Q128 x) {
    /* sinh(x) = (e^x - e^-x) / 2 */
    Seraph_Q128 ex = seraph_q128_exp(x);
    Seraph_Q128 enx = seraph_q128_exp(seraph_q128_neg(x));
    Seraph_Q128 diff = seraph_q128_sub(ex, enx);
    return seraph_q128_mul(diff, SERAPH_Q128_HALF);
}

Seraph_Q128 seraph_q128_cosh(Seraph_Q128 x) {
    /* cosh(x) = (e^x + e^-x) / 2 */
    Seraph_Q128 ex = seraph_q128_exp(x);
    Seraph_Q128 enx = seraph_q128_exp(seraph_q128_neg(x));
    Seraph_Q128 sum = seraph_q128_add(ex, enx);
    return seraph_q128_mul(sum, SERAPH_Q128_HALF);
}

Seraph_Q128 seraph_q128_tanh(Seraph_Q128 x) {
    return seraph_q128_div(seraph_q128_sinh(x), seraph_q128_cosh(x));
}

Seraph_Q128 seraph_q128_lerp(Seraph_Q128 a, Seraph_Q128 b, Seraph_Q128 t) {
    /* a + t * (b - a) */
    Seraph_Q128 diff = seraph_q128_sub(b, a);
    Seraph_Q128 scaled = seraph_q128_mul(t, diff);
    return seraph_q128_add(a, scaled);
}

Seraph_Q128 seraph_q128_smoothstep(Seraph_Q128 edge0, Seraph_Q128 edge1, Seraph_Q128 x) {
    /* t = clamp((x - edge0) / (edge1 - edge0), 0, 1) */
    /* return t * t * (3 - 2 * t) */
    Seraph_Q128 range = seraph_q128_sub(edge1, edge0);
    Seraph_Q128 t = seraph_q128_div(seraph_q128_sub(x, edge0), range);

    /* Clamp t to [0, 1] */
    if (seraph_vbit_is_true(seraph_q128_lt(t, SERAPH_Q128_ZERO))) t = SERAPH_Q128_ZERO;
    if (seraph_vbit_is_true(seraph_q128_gt(t, SERAPH_Q128_ONE))) t = SERAPH_Q128_ONE;

    /* t * t * (3 - 2*t) */
    Seraph_Q128 three = seraph_q128_from_i64(3);
    Seraph_Q128 two = seraph_q128_from_i64(2);
    Seraph_Q128 t2 = seraph_q128_mul(t, t);
    Seraph_Q128 inner = seraph_q128_sub(three, seraph_q128_mul(two, t));
    return seraph_q128_mul(t2, inner);
}
