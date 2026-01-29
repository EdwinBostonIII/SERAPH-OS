/**
 * @file integers.h
 * @brief MC4: Entropic Arithmetic - Integer operations with overflow control
 *
 * Three arithmetic modes:
 *   VOID:     Overflow returns VOID (safest)
 *   WRAP:     Overflow wraps around (fastest)
 *   SATURATE: Overflow clamps to limit (useful for graphics/audio)
 */

#ifndef SERAPH_INTEGERS_H
#define SERAPH_INTEGERS_H

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include "seraph/void.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Arithmetic Mode
 *============================================================================*/

/**
 * @brief Overflow handling mode for arithmetic operations
 */
typedef enum {
    SERAPH_ARITH_VOID     = 0,  /**< Overflow returns VOID */
    SERAPH_ARITH_WRAP     = 1,  /**< Overflow wraps around (modular) */
    SERAPH_ARITH_SATURATE = 2   /**< Overflow clamps to limit */
} Seraph_ArithMode;

/*============================================================================
 * Saturation Limits
 *
 * Because VOID uses the max value, saturation stops one short.
 *============================================================================*/

#define SERAPH_SAT_MAX_U8   ((uint8_t)0xFE)
#define SERAPH_SAT_MAX_U16  ((uint16_t)0xFFFE)
#define SERAPH_SAT_MAX_U32  ((uint32_t)0xFFFFFFFEUL)
#define SERAPH_SAT_MAX_U64  ((uint64_t)0xFFFFFFFFFFFFFFFEULL)

/* For signed, min is actual INT_MIN, max is one less than abs(INT_MIN) to preserve symmetry */
#define SERAPH_SAT_MAX_I8   ((int8_t)126)
#define SERAPH_SAT_MIN_I8   ((int8_t)-128)
#define SERAPH_SAT_MAX_I16  ((int16_t)32766)
#define SERAPH_SAT_MIN_I16  ((int16_t)-32768)
#define SERAPH_SAT_MAX_I32  ((int32_t)2147483646)
#define SERAPH_SAT_MIN_I32  ((int32_t)(-2147483647 - 1))
#define SERAPH_SAT_MAX_I64  ((int64_t)9223372036854775806LL)
#define SERAPH_SAT_MIN_I64  ((int64_t)(-9223372036854775807LL - 1))

/*============================================================================
 * Addition (Unsigned)
 *============================================================================*/

/**
 * @brief Add two unsigned 64-bit integers with mode (branchless)
 */
static inline uint64_t seraph_add_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode) {
    uint64_t result = a + b;
    uint64_t overflow = -(uint64_t)(result < a);
    uint64_t void_mask = seraph_void_mask2_u64(a, b);

    /* Mode-specific overflow handling (branchless) */
    uint64_t is_void_mode = -(uint64_t)(mode == SERAPH_ARITH_VOID);
    uint64_t is_sat_mode = -(uint64_t)(mode == SERAPH_ARITH_SATURATE);

    /* VOID mode: overflow -> VOID */
    uint64_t void_result = seraph_select_u64(SERAPH_VOID_U64, result, overflow & is_void_mode);
    /* SAT mode: overflow -> SAT_MAX */
    uint64_t sat_result = seraph_select_u64(SERAPH_SAT_MAX_U64, void_result, overflow & is_sat_mode);
    /* WRAP mode: just use result (handled by sat_result when neither mask set) */

    return seraph_select_u64(SERAPH_VOID_U64, sat_result, void_mask);
}

/**
 * @brief Add two unsigned 32-bit integers with mode (branchless)
 */
static inline uint32_t seraph_add_u32(uint32_t a, uint32_t b, Seraph_ArithMode mode) {
    uint32_t result = a + b;
    uint32_t overflow = -(uint32_t)(result < a);
    uint32_t void_mask = seraph_void_mask2_u32(a, b);
    uint32_t is_void_mode = -(uint32_t)(mode == SERAPH_ARITH_VOID);
    uint32_t is_sat_mode = -(uint32_t)(mode == SERAPH_ARITH_SATURATE);
    uint32_t void_result = seraph_select_u32(SERAPH_VOID_U32, result, overflow & is_void_mode);
    uint32_t sat_result = seraph_select_u32(SERAPH_SAT_MAX_U32, void_result, overflow & is_sat_mode);
    return seraph_select_u32(SERAPH_VOID_U32, sat_result, void_mask);
}

/*============================================================================
 * Addition (Signed)
 *============================================================================*/

/**
 * @brief Add two signed 64-bit integers with mode (branchless)
 */
static inline int64_t seraph_add_i64(int64_t a, int64_t b, Seraph_ArithMode mode) {
    int64_t result = (int64_t)((uint64_t)a + (uint64_t)b);
    int64_t pos_overflow = -(int64_t)((a > 0) & (b > 0) & (result <= 0));
    int64_t neg_overflow = -(int64_t)((a < 0) & (b < 0) & (result >= 0));
    int64_t any_overflow = pos_overflow | neg_overflow;
    int64_t void_mask = seraph_void_mask2_i64(a, b);
    int64_t is_void_mode = -(int64_t)(mode == SERAPH_ARITH_VOID);
    int64_t is_sat_mode = -(int64_t)(mode == SERAPH_ARITH_SATURATE);
    int64_t void_result = seraph_select_i64(SERAPH_VOID_I64, result, any_overflow & is_void_mode);
    int64_t sat_val = seraph_select_i64(SERAPH_SAT_MIN_I64, SERAPH_SAT_MAX_I64, neg_overflow);
    int64_t sat_result = seraph_select_i64(sat_val, void_result, any_overflow & is_sat_mode);
    return seraph_select_i64(SERAPH_VOID_I64, sat_result, void_mask);
}

static inline int32_t seraph_add_i32(int32_t a, int32_t b, Seraph_ArithMode mode) {
    int32_t result = (int32_t)((uint32_t)a + (uint32_t)b);
    int32_t pos_overflow = -(int32_t)((a > 0) & (b > 0) & (result <= 0));
    int32_t neg_overflow = -(int32_t)((a < 0) & (b < 0) & (result >= 0));
    int32_t any_overflow = pos_overflow | neg_overflow;
    int32_t void_mask = seraph_void_mask2_i32(a, b);
    int32_t is_void_mode = -(int32_t)(mode == SERAPH_ARITH_VOID);
    int32_t is_sat_mode = -(int32_t)(mode == SERAPH_ARITH_SATURATE);
    int32_t void_result = seraph_select_i32(SERAPH_VOID_I32, result, any_overflow & is_void_mode);
    int32_t sat_val = seraph_select_i32(SERAPH_SAT_MIN_I32, SERAPH_SAT_MAX_I32, neg_overflow);
    int32_t sat_result = seraph_select_i32(sat_val, void_result, any_overflow & is_sat_mode);
    return seraph_select_i32(SERAPH_VOID_I32, sat_result, void_mask);
}

/*============================================================================
 * Subtraction (Unsigned)
 *============================================================================*/

static inline uint64_t seraph_sub_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode) {
    uint64_t result = a - b;
    uint64_t underflow = -(uint64_t)(b > a);
    uint64_t void_mask = seraph_void_mask2_u64(a, b);
    uint64_t is_void_mode = -(uint64_t)(mode == SERAPH_ARITH_VOID);
    uint64_t is_sat_mode = -(uint64_t)(mode == SERAPH_ARITH_SATURATE);
    uint64_t void_result = seraph_select_u64(SERAPH_VOID_U64, result, underflow & is_void_mode);
    uint64_t sat_result = seraph_select_u64(0, void_result, underflow & is_sat_mode);
    return seraph_select_u64(SERAPH_VOID_U64, sat_result, void_mask);
}

static inline uint32_t seraph_sub_u32(uint32_t a, uint32_t b, Seraph_ArithMode mode) {
    uint32_t result = a - b;
    uint32_t underflow = -(uint32_t)(b > a);
    uint32_t void_mask = seraph_void_mask2_u32(a, b);
    uint32_t is_void_mode = -(uint32_t)(mode == SERAPH_ARITH_VOID);
    uint32_t is_sat_mode = -(uint32_t)(mode == SERAPH_ARITH_SATURATE);
    uint32_t void_result = seraph_select_u32(SERAPH_VOID_U32, result, underflow & is_void_mode);
    uint32_t sat_result = seraph_select_u32(0, void_result, underflow & is_sat_mode);
    return seraph_select_u32(SERAPH_VOID_U32, sat_result, void_mask);
}

/*============================================================================
 * Subtraction (Signed)
 *============================================================================*/

static inline int64_t seraph_sub_i64(int64_t a, int64_t b, Seraph_ArithMode mode) {
    int64_t result = (int64_t)((uint64_t)a - (uint64_t)b);
    int64_t pos_overflow = -(int64_t)((a > 0) & (b < 0) & (result <= 0));
    int64_t neg_overflow = -(int64_t)((a < 0) & (b > 0) & (result >= 0));
    int64_t any_overflow = pos_overflow | neg_overflow;
    int64_t void_mask = seraph_void_mask2_i64(a, b);
    int64_t is_void_mode = -(int64_t)(mode == SERAPH_ARITH_VOID);
    int64_t is_sat_mode = -(int64_t)(mode == SERAPH_ARITH_SATURATE);
    int64_t void_result = seraph_select_i64(SERAPH_VOID_I64, result, any_overflow & is_void_mode);
    int64_t sat_val = seraph_select_i64(SERAPH_SAT_MIN_I64, SERAPH_SAT_MAX_I64, neg_overflow);
    int64_t sat_result = seraph_select_i64(sat_val, void_result, any_overflow & is_sat_mode);
    return seraph_select_i64(SERAPH_VOID_I64, sat_result, void_mask);
}

/*============================================================================
 * Multiplication (Unsigned)
 *============================================================================*/

static inline uint64_t seraph_mul_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode) {
    uint64_t result = a * b;
    /* Overflow check: a != 0 && b > MAX/a. Use safe division to avoid div-by-zero. */
    uint64_t safe_a = a | (-(uint64_t)(a == 0));
    uint64_t overflow = -(uint64_t)((a != 0) & (b > UINT64_MAX / safe_a));
    uint64_t void_mask = seraph_void_mask2_u64(a, b);
    uint64_t is_void_mode = -(uint64_t)(mode == SERAPH_ARITH_VOID);
    uint64_t is_sat_mode = -(uint64_t)(mode == SERAPH_ARITH_SATURATE);
    uint64_t void_result = seraph_select_u64(SERAPH_VOID_U64, result, overflow & is_void_mode);
    uint64_t sat_result = seraph_select_u64(SERAPH_SAT_MAX_U64, void_result, overflow & is_sat_mode);
    return seraph_select_u64(SERAPH_VOID_U64, sat_result, void_mask);
}

static inline uint32_t seraph_mul_u32(uint32_t a, uint32_t b, Seraph_ArithMode mode) {
    uint64_t wide = (uint64_t)a * (uint64_t)b;
    uint32_t result = (uint32_t)wide;
    uint32_t overflow = -(uint32_t)(wide > UINT32_MAX);
    uint32_t void_mask = seraph_void_mask2_u32(a, b);
    uint32_t is_void_mode = -(uint32_t)(mode == SERAPH_ARITH_VOID);
    uint32_t is_sat_mode = -(uint32_t)(mode == SERAPH_ARITH_SATURATE);
    uint32_t void_result = seraph_select_u32(SERAPH_VOID_U32, result, overflow & is_void_mode);
    uint32_t sat_result = seraph_select_u32(SERAPH_SAT_MAX_U32, void_result, overflow & is_sat_mode);
    return seraph_select_u32(SERAPH_VOID_U32, sat_result, void_mask);
}

/*============================================================================
 * Multiplication (Signed)
 *============================================================================*/

static inline int64_t seraph_mul_i64(int64_t a, int64_t b, Seraph_ArithMode mode) {
    int64_t result = (int64_t)((uint64_t)a * (uint64_t)b);
    /* Overflow detection is complex for signed; use safe divisor trick */
    int64_t safe_a = a | (-(int64_t)(a == 0));
    int64_t safe_b = b | (-(int64_t)(b == 0));
    int64_t ov1 = -(int64_t)((a > 0) & (b > 0) & (a > INT64_MAX / safe_b));
    int64_t ov2 = -(int64_t)((a > 0) & (b < 0) & (b < INT64_MIN / safe_a));
    int64_t ov3 = -(int64_t)((a < 0) & (b > 0) & (a < INT64_MIN / safe_b));
    int64_t ov4 = -(int64_t)((a < 0) & (b < 0) & (a != 0) & (b < INT64_MAX / safe_a));
    int64_t overflow = ov1 | ov2 | ov3 | ov4;
    int64_t void_mask = seraph_void_mask2_i64(a, b);
    int64_t is_void_mode = -(int64_t)(mode == SERAPH_ARITH_VOID);
    int64_t is_sat_mode = -(int64_t)(mode == SERAPH_ARITH_SATURATE);
    int64_t void_result = seraph_select_i64(SERAPH_VOID_I64, result, overflow & is_void_mode);
    int64_t same_sign = -(int64_t)((a > 0) == (b > 0));
    int64_t sat_val = seraph_select_i64(SERAPH_SAT_MIN_I64, SERAPH_SAT_MAX_I64, ~same_sign);
    int64_t sat_result = seraph_select_i64(sat_val, void_result, overflow & is_sat_mode);
    return seraph_select_i64(SERAPH_VOID_I64, sat_result, void_mask);
}

/*============================================================================
 * Division (Unsigned)
 *
 * Division by zero ALWAYS returns VOID (no sensible wrap/saturate)
 *============================================================================*/

static inline uint64_t seraph_div_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode) {
    (void)mode;
    uint64_t safe_b = b | (-(uint64_t)(b == 0));
    uint64_t result = a / safe_b;
    uint64_t void_mask = seraph_void_mask2_u64(a, b) | (-(uint64_t)(b == 0));
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

static inline uint32_t seraph_div_u32(uint32_t a, uint32_t b, Seraph_ArithMode mode) {
    (void)mode;
    uint32_t safe_b = b | (-(uint32_t)(b == 0));
    uint32_t result = a / safe_b;
    uint32_t void_mask = seraph_void_mask2_u32(a, b) | (-(uint32_t)(b == 0));
    return seraph_select_u32(SERAPH_VOID_U32, result, void_mask);
}

/*============================================================================
 * Division (Signed)
 *
 * Special case: INT_MIN / -1 overflows
 *============================================================================*/

static inline int64_t seraph_div_i64(int64_t a, int64_t b, Seraph_ArithMode mode) {
    /* Check for dangerous conditions BEFORE dividing to avoid UB */
    int64_t div_zero = -(int64_t)(b == 0);
    int64_t overflow = -(int64_t)((a == INT64_MIN) & (b == -1));
    int64_t void_mask = seraph_void_mask2_i64(a, b) | div_zero;

    /* Make divisor safe (avoid div-by-zero UB) */
    /* Also avoid INT64_MIN / -1 by substituting safe divisor */
    int64_t is_dangerous = div_zero | overflow;
    int64_t safe_b = (is_dangerous) ? 1 : b;  /* Use 1 if dangerous */

    int64_t result = a / safe_b;

    int64_t is_void_mode = -(int64_t)(mode == SERAPH_ARITH_VOID);
    int64_t is_sat_mode = -(int64_t)(mode == SERAPH_ARITH_SATURATE);
    int64_t void_result = seraph_select_i64(SERAPH_VOID_I64, result, overflow & is_void_mode);
    int64_t sat_result = seraph_select_i64(SERAPH_SAT_MAX_I64, void_result, overflow & is_sat_mode);
    /* WRAP mode: INT64_MIN / -1 wraps to INT64_MIN */
    int64_t wrap_result = seraph_select_i64(INT64_MIN, sat_result, overflow & ~is_void_mode & ~is_sat_mode);
    return seraph_select_i64(SERAPH_VOID_I64, wrap_result, void_mask);
}

/*============================================================================
 * Modulo (Unsigned)
 *============================================================================*/

static inline uint64_t seraph_mod_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode) {
    (void)mode;
    uint64_t safe_b = b | (-(uint64_t)(b == 0));
    uint64_t result = a % safe_b;
    uint64_t void_mask = seraph_void_mask2_u64(a, b) | (-(uint64_t)(b == 0));
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

/*============================================================================
 * Negation (Signed)
 *============================================================================*/

static inline int64_t seraph_neg_i64(int64_t a, Seraph_ArithMode mode) {
    int64_t result = -a;
    int64_t overflow = -(int64_t)(a == INT64_MIN);
    int64_t void_mask = seraph_void_mask_i64(a);
    int64_t is_void_mode = -(int64_t)(mode == SERAPH_ARITH_VOID);
    int64_t is_sat_mode = -(int64_t)(mode == SERAPH_ARITH_SATURATE);
    int64_t void_result = seraph_select_i64(SERAPH_VOID_I64, result, overflow & is_void_mode);
    int64_t sat_result = seraph_select_i64(SERAPH_SAT_MAX_I64, void_result, overflow & is_sat_mode);
    return seraph_select_i64(SERAPH_VOID_I64, sat_result, void_mask);
}

/*============================================================================
 * Absolute Value (returns unsigned)
 *============================================================================*/

static inline uint64_t seraph_abs_i64(int64_t a, Seraph_ArithMode mode) {
    (void)mode;
    /* Branchless abs: mask = a >> 63 (all 1s if negative, all 0s if positive) */
    int64_t mask = a >> 63;
    uint64_t abs_val = (uint64_t)((a + mask) ^ mask);
    /* Handle INT64_MIN: its absolute value is 2^63 */
    uint64_t is_min = -(uint64_t)(a == INT64_MIN);
    uint64_t result = seraph_select_u64((uint64_t)INT64_MAX + 1, abs_val, is_min);
    uint64_t void_mask = (uint64_t)seraph_void_mask_i64(a);
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

/*============================================================================
 * Checked Operations (with overflow flag)
 *============================================================================*/

/**
 * @brief Add with overflow detection (branchless result, flag set via pointer)
 */
static inline uint64_t seraph_add_u64_checked(uint64_t a, uint64_t b, bool* overflow) {
    uint64_t result = a + b;
    uint64_t void_mask = seraph_void_mask2_u64(a, b);
    *overflow = (result < a) | (void_mask != 0);
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

static inline int64_t seraph_add_i64_checked(int64_t a, int64_t b, bool* overflow) {
    int64_t result = (int64_t)((uint64_t)a + (uint64_t)b);
    int64_t void_mask = seraph_void_mask2_i64(a, b);
    *overflow = ((a > 0) & (b > 0) & (result <= 0)) | ((a < 0) & (b < 0) & (result >= 0)) | (void_mask != 0);
    return seraph_select_i64(SERAPH_VOID_I64, result, void_mask);
}

/**
 * @brief Multiply with overflow detection (branchless)
 */
static inline uint64_t seraph_mul_u64_checked(uint64_t a, uint64_t b, bool* overflow) {
    uint64_t safe_a = a | (-(uint64_t)(a == 0));
    uint64_t void_mask = seraph_void_mask2_u64(a, b);
    *overflow = ((a != 0) & (b > UINT64_MAX / safe_a)) | (void_mask != 0);
    return a * b;
}

/*============================================================================
 * Min/Max with VOID
 *============================================================================*/

static inline uint64_t seraph_min_u64(uint64_t a, uint64_t b) {
    uint64_t a_void = seraph_void_mask_u64(a);
    uint64_t b_void = seraph_void_mask_u64(b);
    uint64_t min_ab = (a < b) ? a : b;  /* Compiler generates cmov */
    /* If a is VOID, use b; if b is VOID, use a; else min */
    uint64_t result = seraph_select_u64(a, min_ab, b_void);
    return seraph_select_u64(b, result, a_void);
}

static inline uint64_t seraph_max_u64(uint64_t a, uint64_t b) {
    uint64_t a_void = seraph_void_mask_u64(a);
    uint64_t b_void = seraph_void_mask_u64(b);
    uint64_t max_ab = (a > b) ? a : b;
    uint64_t result = seraph_select_u64(a, max_ab, b_void);
    return seraph_select_u64(b, result, a_void);
}

static inline int64_t seraph_min_i64(int64_t a, int64_t b) {
    int64_t a_void = seraph_void_mask_i64(a);
    int64_t b_void = seraph_void_mask_i64(b);
    int64_t min_ab = (a < b) ? a : b;
    int64_t result = seraph_select_i64(a, min_ab, b_void);
    return seraph_select_i64(b, result, a_void);
}

static inline int64_t seraph_max_i64(int64_t a, int64_t b) {
    int64_t a_void = seraph_void_mask_i64(a);
    int64_t b_void = seraph_void_mask_i64(b);
    int64_t max_ab = (a > b) ? a : b;
    int64_t result = seraph_select_i64(a, max_ab, b_void);
    return seraph_select_i64(b, result, a_void);
}

/*============================================================================
 * Clamp
 *============================================================================*/

static inline uint64_t seraph_clamp_u64(uint64_t x, uint64_t lo, uint64_t hi) {
    uint64_t clamped = (x < lo) ? lo : ((x > hi) ? hi : x);
    uint64_t void_mask = seraph_void_mask_u64(x) | seraph_void_mask_u64(lo) | seraph_void_mask_u64(hi);
    return seraph_select_u64(SERAPH_VOID_U64, clamped, void_mask);
}

static inline int64_t seraph_clamp_i64(int64_t x, int64_t lo, int64_t hi) {
    int64_t clamped = (x < lo) ? lo : ((x > hi) ? hi : x);
    int64_t void_mask = seraph_void_mask_i64(x) | seraph_void_mask_i64(lo) | seraph_void_mask_i64(hi);
    return seraph_select_i64(SERAPH_VOID_I64, clamped, void_mask);
}

/*============================================================================
 * Power (integer exponentiation)
 *============================================================================*/

/**
 * @brief Integer power with mode
 */
uint64_t seraph_pow_u64(uint64_t base, uint32_t exp, Seraph_ArithMode mode);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_INTEGERS_H */
