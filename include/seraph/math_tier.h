/**
 * @file math_tier.h
 * @brief SERAPH Math Tier Architecture
 *
 * MC26: SERAPH Performance Revolution - Pillar 5
 *
 * Defines the tiered math system with compile-time precision selection.
 * Each tier optimizes for different use cases while maintaining
 * the Zero-FPU guarantee.
 *
 * Tier Architecture:
 *
 *   TIER 0: Q16.16 (Graphics/Audio)
 *     - 32-bit values, 16-bit fraction
 *     - Zero lookup tables (polynomial only)
 *     - Suitable for: sprites, UI, audio synthesis
 *     - Accuracy: ~4 decimal digits
 *
 *   TIER 1: Q32.32 (Physics)
 *     - 64-bit values, 32-bit fraction
 *     - Micro-tables allowed (256 entries)
 *     - Suitable for: physics simulation, 3D math
 *     - Accuracy: ~9 decimal digits
 *
 *   TIER 2: Q64.64 (Financial/Scientific)
 *     - 128-bit values, 64-bit fraction
 *     - Full precision tables and interpolation
 *     - Suitable for: financial calculations, scientific
 *     - Accuracy: ~18 decimal digits
 *
 * Compile-Time Selection:
 *   #define SERAPH_MATH_TIER 0  // Use Q16 only
 *   #define SERAPH_MATH_TIER 1  // Use Q32 (default)
 *   #define SERAPH_MATH_TIER 2  // Use Q64
 *
 * Thread Mode:
 *   SERAPH_STRAND_INTEGER_ONLY - Flag for strands (threads) that
 *   must never use FPU instructions. The scheduler avoids saving/
 *   restoring FPU state for these strands.
 */

#ifndef SERAPH_MATH_TIER_H
#define SERAPH_MATH_TIER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Tier Configuration
 *============================================================================*/

/** Default to Tier 1 (Q32.32 physics precision) */
#ifndef SERAPH_MATH_TIER
#define SERAPH_MATH_TIER 1
#endif

/** Compile-time tier checks */
#if SERAPH_MATH_TIER < 0 || SERAPH_MATH_TIER > 2
#error "SERAPH_MATH_TIER must be 0, 1, or 2"
#endif

/*============================================================================
 * Tier 0: Q16.16 (Graphics/Audio)
 *============================================================================*/

#if SERAPH_MATH_TIER >= 0

#include "seraph/q16_trig.h"

/** Q16 is the base tier - always available */
typedef Q16 Seraph_Q16;

#define SERAPH_Q16_AVAILABLE 1

/** Tier 0 configuration */
#define SERAPH_TIER0_BITS       32
#define SERAPH_TIER0_FRAC_BITS  16
#define SERAPH_TIER0_TABLE_SIZE 0   /* No tables */

#endif /* SERAPH_MATH_TIER >= 0 */

/*============================================================================
 * Tier 1: Q32.32 (Physics)
 *============================================================================*/

#if SERAPH_MATH_TIER >= 1

/** Q32.32 fixed-point type */
typedef int64_t Seraph_Q32;

#define SERAPH_Q32_AVAILABLE 1

/** Tier 1 configuration */
#define SERAPH_TIER1_BITS       64
#define SERAPH_TIER1_FRAC_BITS  32
#define SERAPH_TIER1_TABLE_SIZE 256  /* Micro-table allowed */

/** Q32.32 constants */
#define SERAPH_Q32_ONE      ((Seraph_Q32)0x100000000LL)
#define SERAPH_Q32_HALF     ((Seraph_Q32)0x80000000LL)
#define SERAPH_Q32_NEG_ONE  ((Seraph_Q32)0xFFFFFFFF00000000LL)

/** Q32.32 basic operations */
static inline Seraph_Q32 seraph_q32_from_i32(int32_t x) {
    return (Seraph_Q32)x << 32;
}

static inline int32_t seraph_q32_to_i32(Seraph_Q32 x) {
    return (int32_t)(x >> 32);
}

static inline Seraph_Q32 seraph_q32_mul(Seraph_Q32 a, Seraph_Q32 b) {
    /* Use BMI2 for efficient 128-bit product */
    int negative = (a < 0) ^ (b < 0);
    uint64_t ua = (a < 0) ? (uint64_t)(-a) : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)(-b) : (uint64_t)b;

    uint64_t hi;
    uint64_t lo;

#if defined(__GNUC__) || defined(__clang__)
    __uint128_t prod = (__uint128_t)ua * ub;
    hi = (uint64_t)(prod >> 64);
    lo = (uint64_t)prod;
#else
    lo = ua * ub;  /* Simplified - would use seraph_mulx_u64 */
    hi = 0;        /* This is incomplete without BMI2 */
#endif

    /* Result is bits [95:32] */
    Seraph_Q32 result = ((int64_t)hi << 32) | (lo >> 32);
    return negative ? -result : result;
}

static inline Seraph_Q32 seraph_q32_div(Seraph_Q32 a, Seraph_Q32 b) {
    if (b == 0) return (a >= 0) ? 0x7FFFFFFFFFFFFFFFLL : 0x8000000000000001LL;

#if defined(__GNUC__) || defined(__clang__)
    __int128 dividend = (__int128)a << 32;
    return (Seraph_Q32)(dividend / b);
#else
    /* Fallback - less precise */
    return (a / (b >> 16)) << 16;
#endif
}

/** Q32.32 trig functions (using micro-table) */
Seraph_Q32 seraph_q32_sin(Seraph_Q32 angle);
Seraph_Q32 seraph_q32_cos(Seraph_Q32 angle);
void seraph_q32_sincos(Seraph_Q32 angle, Seraph_Q32* sin_out, Seraph_Q32* cos_out);

#endif /* SERAPH_MATH_TIER >= 1 */

/*============================================================================
 * Tier 2: Q64.64 (Financial/Scientific)
 *============================================================================*/

#if SERAPH_MATH_TIER >= 2

#include "seraph/q64_trig.h"

/** Q64 is available at tier 2 */
typedef Q64 Seraph_Q64;

#define SERAPH_Q64_AVAILABLE 1

/** Tier 2 configuration */
#define SERAPH_TIER2_BITS       128
#define SERAPH_TIER2_FRAC_BITS  64
#define SERAPH_TIER2_TABLE_SIZE 256  /* Full precision micro-table */

#endif /* SERAPH_MATH_TIER >= 2 */

/*============================================================================
 * Strand Integer-Only Mode
 *============================================================================*/

/**
 * @brief Strand flag for integer-only execution
 *
 * When set on a strand (thread), the scheduler:
 *   1. Does NOT save/restore FPU state on context switch
 *   2. Does NOT allocate FPU context memory
 *   3. Traps if FPU instruction is executed (debug builds)
 *
 * This saves significant context switch overhead for threads
 * that only use integer math (e.g., audio processing with Q16).
 */
#define SERAPH_STRAND_INTEGER_ONLY  (1ULL << 0)

/**
 * @brief Strand flag for lazy FPU allocation
 *
 * FPU context is not allocated until first FPU instruction.
 * Useful for strands that might use FPU occasionally.
 */
#define SERAPH_STRAND_LAZY_FPU      (1ULL << 1)

/**
 * @brief Check if strand is integer-only
 */
static inline int seraph_strand_is_integer_only(uint64_t flags) {
    return (flags & SERAPH_STRAND_INTEGER_ONLY) != 0;
}

/**
 * @brief Check if strand uses lazy FPU
 */
static inline int seraph_strand_is_lazy_fpu(uint64_t flags) {
    return (flags & SERAPH_STRAND_LAZY_FPU) != 0;
}

/*============================================================================
 * Tier Selection Macros
 *============================================================================*/

/** Select appropriate type based on tier */
#if SERAPH_MATH_TIER == 0
    typedef Seraph_Q16 Seraph_Scalar;
    #define seraph_scalar_sin       q16_sin
    #define seraph_scalar_cos       q16_cos
    #define seraph_scalar_sincos    q16_sincos
    #define seraph_scalar_tan       q16_tan
    #define seraph_scalar_sqrt      q16_sqrt
    #define seraph_scalar_mul       q16_mul
    #define seraph_scalar_div       q16_div
    #define SERAPH_SCALAR_ONE       Q16_ONE
    #define SERAPH_SCALAR_ZERO      0
#elif SERAPH_MATH_TIER == 1
    typedef Seraph_Q32 Seraph_Scalar;
    #define seraph_scalar_sin       seraph_q32_sin
    #define seraph_scalar_cos       seraph_q32_cos
    #define seraph_scalar_sincos    seraph_q32_sincos
    #define seraph_scalar_mul       seraph_q32_mul
    #define seraph_scalar_div       seraph_q32_div
    #define SERAPH_SCALAR_ONE       SERAPH_Q32_ONE
    #define SERAPH_SCALAR_ZERO      0
#elif SERAPH_MATH_TIER == 2
    typedef Seraph_Q64 Seraph_Scalar;
    #define seraph_scalar_sin       q64_sin
    #define seraph_scalar_cos       q64_cos
    #define seraph_scalar_sincos    q64_sincos
    #define seraph_scalar_mul       q64_mul
    #define seraph_scalar_div       q64_div
    #define SERAPH_SCALAR_ONE       Q64_ONE
    #define SERAPH_SCALAR_ZERO      ((Q64){0, 0})
#endif

/*============================================================================
 * Runtime Tier Information
 *============================================================================*/

/**
 * @brief Get current math tier
 */
static inline int seraph_math_tier(void) {
    return SERAPH_MATH_TIER;
}

/**
 * @brief Get tier name string
 */
static inline const char* seraph_math_tier_name(void) {
#if SERAPH_MATH_TIER == 0
    return "Q16.16 (Graphics)";
#elif SERAPH_MATH_TIER == 1
    return "Q32.32 (Physics)";
#elif SERAPH_MATH_TIER == 2
    return "Q64.64 (Financial)";
#else
    return "Unknown";
#endif
}

/**
 * @brief Get total bits for current tier
 */
static inline int seraph_math_tier_bits(void) {
#if SERAPH_MATH_TIER == 0
    return SERAPH_TIER0_BITS;
#elif SERAPH_MATH_TIER == 1
    return SERAPH_TIER1_BITS;
#elif SERAPH_MATH_TIER == 2
    return SERAPH_TIER2_BITS;
#else
    return 0;
#endif
}

/**
 * @brief Get fraction bits for current tier
 */
static inline int seraph_math_tier_frac_bits(void) {
#if SERAPH_MATH_TIER == 0
    return SERAPH_TIER0_FRAC_BITS;
#elif SERAPH_MATH_TIER == 1
    return SERAPH_TIER1_FRAC_BITS;
#elif SERAPH_MATH_TIER == 2
    return SERAPH_TIER2_FRAC_BITS;
#else
    return 0;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_MATH_TIER_H */
