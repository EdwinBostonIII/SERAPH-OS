/**
 * @file q128.h
 * @brief MC5: Q128 Fixed-Point Numbers (Q64.64 format)
 *
 * 128-bit fixed-point: 64 bits integer (signed) + 64 bits fraction.
 * Provides uniform precision (~18 decimal digits) across all magnitudes.
 */

#ifndef SERAPH_Q128_H
#define SERAPH_Q128_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/integers.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Q128 Type Definition
 *============================================================================*/

/**
 * @brief Q64.64 fixed-point number
 *
 * Value = hi + lo/2^64
 *
 * hi: signed 64-bit integer part
 * lo: unsigned 64-bit fractional part (0 to 0.999...9)
 */
typedef struct {
    int64_t hi;    /**< Integer part (signed) */
    uint64_t lo;   /**< Fractional part (0 to 2^64-1 represents 0 to ~1) */
} Seraph_Q128;

/*============================================================================
 * Q128 Constants
 *============================================================================*/

/** VOID value (all 1s) */
#define SERAPH_Q128_VOID   ((Seraph_Q128){ (int64_t)-1, UINT64_MAX })

/** Zero */
#define SERAPH_Q128_ZERO   ((Seraph_Q128){ 0, 0 })

/** One */
#define SERAPH_Q128_ONE    ((Seraph_Q128){ 1, 0 })

/** Negative One */
#define SERAPH_Q128_NEG_ONE ((Seraph_Q128){ -1, 0 })

/** One Half (0.5) */
#define SERAPH_Q128_HALF   ((Seraph_Q128){ 0, 0x8000000000000000ULL })

/** Pi (3.14159265358979323846...) */
extern const Seraph_Q128 SERAPH_Q128_PI;

/** Pi/2 */
extern const Seraph_Q128 SERAPH_Q128_PI_2;

/** 2*Pi */
extern const Seraph_Q128 SERAPH_Q128_2PI;

/** e (2.71828182845904523536...) */
extern const Seraph_Q128 SERAPH_Q128_E;

/** ln(2) */
extern const Seraph_Q128 SERAPH_Q128_LN2;

/** sqrt(2) */
extern const Seraph_Q128 SERAPH_Q128_SQRT2;

/*============================================================================
 * Q128 Detection
 *============================================================================*/

/**
 * @brief Check if Q128 is VOID
 */
static inline bool seraph_q128_is_void(Seraph_Q128 x) {
    return x.hi == -1 && x.lo == UINT64_MAX;
}

/**
 * @brief Generate VOID mask for Q128 (branchless)
 * Returns: hi=-1,lo=-1 if VOID, hi=0,lo=0 otherwise
 */
static inline Seraph_Q128 seraph_q128_void_mask(Seraph_Q128 x) {
    int64_t mask = -(int64_t)((x.hi == -1) & (x.lo == UINT64_MAX));
    return (Seraph_Q128){ mask, (uint64_t)mask };
}

/**
 * @brief Generate combined VOID mask for two Q128 values
 */
static inline Seraph_Q128 seraph_q128_void_mask2(Seraph_Q128 a, Seraph_Q128 b) {
    int64_t mask = -(int64_t)(((a.hi == -1) & (a.lo == UINT64_MAX)) |
                               ((b.hi == -1) & (b.lo == UINT64_MAX)));
    return (Seraph_Q128){ mask, (uint64_t)mask };
}

/**
 * @brief Branchless select between Q128 values
 */
static inline Seraph_Q128 seraph_q128_select(Seraph_Q128 if_void, Seraph_Q128 if_valid,
                                              Seraph_Q128 mask) {
    return (Seraph_Q128){
        (if_void.hi & mask.hi) | (if_valid.hi & ~mask.hi),
        (if_void.lo & mask.lo) | (if_valid.lo & ~mask.lo)
    };
}

/**
 * @brief Check if Q128 exists (is not VOID)
 */
static inline bool seraph_q128_exists(Seraph_Q128 x) {
    return !seraph_q128_is_void(x);
}

/**
 * @brief Check if Q128 is zero
 */
static inline bool seraph_q128_is_zero(Seraph_Q128 x) {
    return x.hi == 0 && x.lo == 0;
}

/**
 * @brief Check if Q128 is negative
 */
static inline bool seraph_q128_is_negative(Seraph_Q128 x) {
    return ((x.hi != -1) | (x.lo != UINT64_MAX)) & (x.hi < 0);
}

/**
 * @brief Check if Q128 is positive
 */
static inline bool seraph_q128_is_positive(Seraph_Q128 x) {
    return ((x.hi != -1) | (x.lo != UINT64_MAX)) & ((x.hi > 0) | ((x.hi == 0) & (x.lo > 0)));
}

/*============================================================================
 * Q128 Creation
 *============================================================================*/

/**
 * @brief Create Q128 from signed 64-bit integer
 */
static inline Seraph_Q128 seraph_q128_from_i64(int64_t n) {
    if (SERAPH_IS_VOID_I64(n)) return SERAPH_Q128_VOID;
    return (Seraph_Q128){ n, 0 };
}

/**
 * @brief Create Q128 from unsigned 64-bit integer
 */
static inline Seraph_Q128 seraph_q128_from_u64(uint64_t n) {
    if (SERAPH_IS_VOID_U64(n)) return SERAPH_Q128_VOID;
    if (n > INT64_MAX) return SERAPH_Q128_VOID;  /* Would overflow */
    return (Seraph_Q128){ (int64_t)n, 0 };
}

/**
 * @brief Create Q128 from fraction (numerator / denominator)
 */
Seraph_Q128 seraph_q128_from_frac(int64_t num, int64_t denom);

/**
 * @brief Create Q128 from double (for initialization)
 *
 * Note: Loses precision beyond ~15 decimal digits.
 */
Seraph_Q128 seraph_q128_from_double(double d);

/*============================================================================
 * Q128 Conversion
 *============================================================================*/

/**
 * @brief Convert Q128 to signed 64-bit integer (truncates)
 */
static inline int64_t seraph_q128_to_i64(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return SERAPH_VOID_I64;
    return x.hi;
}

/**
 * @brief Convert Q128 to double (loses precision)
 */
double seraph_q128_to_double(Seraph_Q128 x);

/**
 * @brief Convert Q128 to string
 * @param x Value to convert
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param decimals Number of decimal places
 * @return Number of characters written (excluding null)
 */
int seraph_q128_to_string(Seraph_Q128 x, char* buf, int buf_size, int decimals);

/*============================================================================
 * Q128 Basic Arithmetic
 *============================================================================*/

/**
 * @brief Add two Q128 values
 */
Seraph_Q128 seraph_q128_add(Seraph_Q128 a, Seraph_Q128 b);

/**
 * @brief Subtract two Q128 values
 */
Seraph_Q128 seraph_q128_sub(Seraph_Q128 a, Seraph_Q128 b);

/**
 * @brief Multiply two Q128 values
 */
Seraph_Q128 seraph_q128_mul(Seraph_Q128 a, Seraph_Q128 b);

/**
 * @brief Divide two Q128 values
 * @return VOID if b is zero
 */
Seraph_Q128 seraph_q128_div(Seraph_Q128 a, Seraph_Q128 b);

/**
 * @brief Negate Q128 value
 */
Seraph_Q128 seraph_q128_neg(Seraph_Q128 x);

/**
 * @brief Absolute value
 */
Seraph_Q128 seraph_q128_abs(Seraph_Q128 x);

/*============================================================================
 * Q128 Comparison
 *============================================================================*/

/**
 * @brief Compare two Q128 values
 * @return -1 if a < b, 0 if a == b, +1 if a > b, or special value for VOID
 */
int seraph_q128_compare(Seraph_Q128 a, Seraph_Q128 b);

/**
 * @brief Equality comparison
 */
static inline Seraph_Vbit seraph_q128_eq(Seraph_Q128 a, Seraph_Q128 b) {
    if (seraph_q128_is_void(a) || seraph_q128_is_void(b)) return SERAPH_VBIT_VOID;
    return (a.hi == b.hi && a.lo == b.lo) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Less-than comparison
 */
static inline Seraph_Vbit seraph_q128_lt(Seraph_Q128 a, Seraph_Q128 b) {
    if (seraph_q128_is_void(a) || seraph_q128_is_void(b)) return SERAPH_VBIT_VOID;
    if (a.hi != b.hi) return (a.hi < b.hi) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
    return (a.lo < b.lo) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Less-than-or-equal comparison
 */
static inline Seraph_Vbit seraph_q128_le(Seraph_Q128 a, Seraph_Q128 b) {
    return seraph_vbit_or(seraph_q128_lt(a, b), seraph_q128_eq(a, b));
}

/**
 * @brief Greater-than comparison
 */
static inline Seraph_Vbit seraph_q128_gt(Seraph_Q128 a, Seraph_Q128 b) {
    return seraph_q128_lt(b, a);
}

/**
 * @brief Greater-than-or-equal comparison
 */
static inline Seraph_Vbit seraph_q128_ge(Seraph_Q128 a, Seraph_Q128 b) {
    return seraph_vbit_or(seraph_q128_gt(a, b), seraph_q128_eq(a, b));
}

/*============================================================================
 * Q128 Min/Max
 *============================================================================*/

static inline Seraph_Q128 seraph_q128_min(Seraph_Q128 a, Seraph_Q128 b) {
    if (seraph_q128_is_void(a)) return b;
    if (seraph_q128_is_void(b)) return a;
    return seraph_vbit_is_true(seraph_q128_lt(a, b)) ? a : b;
}

static inline Seraph_Q128 seraph_q128_max(Seraph_Q128 a, Seraph_Q128 b) {
    if (seraph_q128_is_void(a)) return b;
    if (seraph_q128_is_void(b)) return a;
    return seraph_vbit_is_true(seraph_q128_gt(a, b)) ? a : b;
}

/**
 * @brief Clamp value to range [lo, hi]
 */
static inline Seraph_Q128 seraph_q128_clamp(Seraph_Q128 x, Seraph_Q128 lo, Seraph_Q128 hi) {
    if (seraph_q128_is_void(x)) return SERAPH_Q128_VOID;
    if (seraph_q128_is_void(lo) || seraph_q128_is_void(hi)) return SERAPH_Q128_VOID;
    return seraph_q128_min(seraph_q128_max(x, lo), hi);
}

/*============================================================================
 * Q128 Rounding
 *============================================================================*/

/**
 * @brief Floor (round toward negative infinity)
 */
Seraph_Q128 seraph_q128_floor(Seraph_Q128 x);

/**
 * @brief Ceiling (round toward positive infinity)
 */
Seraph_Q128 seraph_q128_ceil(Seraph_Q128 x);

/**
 * @brief Truncate (round toward zero)
 */
Seraph_Q128 seraph_q128_trunc(Seraph_Q128 x);

/**
 * @brief Round to nearest integer
 */
Seraph_Q128 seraph_q128_round(Seraph_Q128 x);

/**
 * @brief Get fractional part only
 */
Seraph_Q128 seraph_q128_frac(Seraph_Q128 x);

/*============================================================================
 * Q128 Transcendental Functions
 *============================================================================*/

/**
 * @brief Square root (Newton-Raphson)
 * @return VOID if x < 0
 */
Seraph_Q128 seraph_q128_sqrt(Seraph_Q128 x);

/**
 * @brief Sine (Taylor series with range reduction)
 */
Seraph_Q128 seraph_q128_sin(Seraph_Q128 x);

/**
 * @brief Cosine
 */
Seraph_Q128 seraph_q128_cos(Seraph_Q128 x);

/**
 * @brief Tangent
 * @return VOID at singularities (π/2, 3π/2, etc.)
 */
Seraph_Q128 seraph_q128_tan(Seraph_Q128 x);

/**
 * @brief Arcsine
 * @return VOID if |x| > 1
 */
Seraph_Q128 seraph_q128_asin(Seraph_Q128 x);

/**
 * @brief Arccosine
 * @return VOID if |x| > 1
 */
Seraph_Q128 seraph_q128_acos(Seraph_Q128 x);

/**
 * @brief Arctangent
 */
Seraph_Q128 seraph_q128_atan(Seraph_Q128 x);

/**
 * @brief Two-argument arctangent
 */
Seraph_Q128 seraph_q128_atan2(Seraph_Q128 y, Seraph_Q128 x);

/**
 * @brief Exponential (e^x)
 */
Seraph_Q128 seraph_q128_exp(Seraph_Q128 x);

/**
 * @brief Natural logarithm
 * @return VOID if x <= 0
 */
Seraph_Q128 seraph_q128_ln(Seraph_Q128 x);

/**
 * @brief Base-2 logarithm
 * @return VOID if x <= 0
 */
Seraph_Q128 seraph_q128_log2(Seraph_Q128 x);

/**
 * @brief Base-10 logarithm
 * @return VOID if x <= 0
 */
Seraph_Q128 seraph_q128_log10(Seraph_Q128 x);

/**
 * @brief Power function (base^exp)
 */
Seraph_Q128 seraph_q128_pow(Seraph_Q128 base, Seraph_Q128 exp);

/**
 * @brief Hyperbolic sine
 */
Seraph_Q128 seraph_q128_sinh(Seraph_Q128 x);

/**
 * @brief Hyperbolic cosine
 */
Seraph_Q128 seraph_q128_cosh(Seraph_Q128 x);

/**
 * @brief Hyperbolic tangent
 */
Seraph_Q128 seraph_q128_tanh(Seraph_Q128 x);

/*============================================================================
 * Q128 Linear Interpolation
 *============================================================================*/

/**
 * @brief Linear interpolation: a + t*(b-a)
 */
Seraph_Q128 seraph_q128_lerp(Seraph_Q128 a, Seraph_Q128 b, Seraph_Q128 t);

/**
 * @brief Smooth interpolation (smoothstep)
 */
Seraph_Q128 seraph_q128_smoothstep(Seraph_Q128 edge0, Seraph_Q128 edge1, Seraph_Q128 x);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_Q128_H */
