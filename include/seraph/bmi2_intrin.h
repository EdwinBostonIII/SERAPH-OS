/**
 * @file bmi2_intrin.h
 * @brief SERAPH BMI2 Intrinsics Layer - Zero-FPU Integer Multiply
 *
 * MC26: SERAPH Performance Revolution - Pillar Infrastructure
 *
 * Provides BMI2-accelerated integer multiplication primitives for
 * the Zero-FPU architecture. These intrinsics enable high-performance
 * fixed-point math without touching the FPU.
 *
 * BMI2 Instructions Used:
 *   - MULX: Unsigned multiply without affecting flags
 *   - ADCX: Add with carry (carry chain, CF only)
 *   - ADOX: Add with overflow (carry chain, OF only)
 *
 * The parallel carry chains of ADCX/ADOX enable multi-limb multiplication
 * with minimal register pressure and no flag clobbering.
 */

#ifndef SERAPH_BMI2_INTRIN_H
#define SERAPH_BMI2_INTRIN_H

#include <stdint.h>
#include "seraph/void.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Feature Detection
 *============================================================================*/

/**
 * @brief Check if BMI2 is available at runtime
 *
 * Uses CPUID to detect BMI2 support.
 *
 * @return 1 if BMI2 available, 0 otherwise
 */
static inline int seraph_bmi2_available(void) {
#if defined(__GNUC__) || defined(__clang__)
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0)
    );
    return (ebx >> 8) & 1;  /* BMI2 is bit 8 of EBX from CPUID.07H */
#elif defined(_MSC_VER)
    int cpuInfo[4];
    __cpuidex(cpuInfo, 7, 0);
    return (cpuInfo[1] >> 8) & 1;
#else
    return 0;
#endif
}

/*============================================================================
 * MULX - Unsigned Multiply Without Flags
 *============================================================================*/

/**
 * @brief MULX: 64x64 -> 128-bit multiply without flag modification
 *
 * Computes a * b = hi:lo where:
 *   - lo = lower 64 bits
 *   - hi = upper 64 bits
 *
 * Unlike MUL, MULX does not modify RFLAGS.
 *
 * @param a First operand
 * @param b Second operand (in RDX for hardware)
 * @param hi Output: high 64 bits
 * @return Low 64 bits of product
 */
static inline uint64_t seraph_mulx_u64(uint64_t a, uint64_t b, uint64_t* hi) {
#if defined(__GNUC__) || defined(__clang__)
#ifdef __BMI2__
    uint64_t lo;
    __asm__ volatile(
        "mulx %3, %0, %1"
        : "=r"(lo), "=r"(*hi)
        : "d"(b), "r"(a)
    );
    return lo;
#else
    /* Fallback: Use 128-bit type if available */
    __uint128_t prod = (__uint128_t)a * b;
    *hi = (uint64_t)(prod >> 64);
    return (uint64_t)prod;
#endif
#elif defined(_MSC_VER)
    return _mulx_u64(b, a, hi);
#else
    /* Portable fallback - less efficient */
    uint64_t a_lo = a & 0xFFFFFFFFULL;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFFULL;
    uint64_t b_hi = b >> 32;

    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;

    uint64_t mid = p1 + (p0 >> 32);
    mid += p2;
    if (mid < p2) p3++;  /* Carry */

    *hi = p3 + (mid >> 32);
    return (p0 & 0xFFFFFFFFULL) | (mid << 32);
#endif
}

/*============================================================================
 * ADCX/ADOX - Parallel Carry Chains
 *============================================================================*/

/**
 * @brief ADCX: Add with carry (CF only)
 *
 * Computes: dst = a + b + CF
 * Updates only CF, preserves OF.
 *
 * @param a First operand
 * @param b Second operand
 * @param carry_in Input carry (0 or 1)
 * @param carry_out Output carry
 * @return Sum
 */
static inline uint64_t seraph_adcx_u64(uint64_t a, uint64_t b,
                                        uint8_t carry_in, uint8_t* carry_out) {
#if defined(__GNUC__) || defined(__clang__)
#ifdef __ADX__
    uint64_t result;
    unsigned char cf = carry_in;
    __asm__ volatile(
        "adcx %2, %0"
        : "+r"(a), "+@ccc"(cf)
        : "r"(b)
    );
    *carry_out = cf;
    return a;
#else
    /* Fallback without ADX */
    __uint128_t sum = (__uint128_t)a + b + carry_in;
    *carry_out = (sum >> 64) ? 1 : 0;
    return (uint64_t)sum;
#endif
#else
    /* Portable fallback */
    uint64_t sum = a + b;
    uint8_t c1 = (sum < a) ? 1 : 0;
    sum += carry_in;
    uint8_t c2 = (sum < carry_in) ? 1 : 0;
    *carry_out = c1 | c2;
    return sum;
#endif
}

/**
 * @brief ADOX: Add with overflow carry (OF only)
 *
 * Computes: dst = a + b + OF
 * Updates only OF, preserves CF.
 *
 * @param a First operand
 * @param b Second operand
 * @param carry_in Input overflow (0 or 1)
 * @param carry_out Output overflow
 * @return Sum
 */
static inline uint64_t seraph_adox_u64(uint64_t a, uint64_t b,
                                        uint8_t carry_in, uint8_t* carry_out) {
#if defined(__GNUC__) || defined(__clang__)
#ifdef __ADX__
    unsigned char of = carry_in;
    __asm__ volatile(
        "adox %2, %0"
        : "+r"(a), "+@cco"(of)
        : "r"(b)
    );
    *carry_out = of;
    return a;
#else
    /* Fallback - same as ADCX since we're just adding */
    __uint128_t sum = (__uint128_t)a + b + carry_in;
    *carry_out = (sum >> 64) ? 1 : 0;
    return (uint64_t)sum;
#endif
#else
    /* Portable fallback */
    uint64_t sum = a + b;
    uint8_t c1 = (sum < a) ? 1 : 0;
    sum += carry_in;
    uint8_t c2 = (sum < carry_in) ? 1 : 0;
    *carry_out = c1 | c2;
    return sum;
#endif
}

/*============================================================================
 * Multi-Limb Multiplication (Q64 Support)
 *============================================================================*/

/**
 * @brief Multiply 128-bit by 64-bit -> 192-bit (truncated to 128)
 *
 * For Q64.64 fixed-point multiplication where we keep the middle 128 bits.
 *
 * @param a_lo Lower 64 bits of a
 * @param a_hi Upper 64 bits of a
 * @param b 64-bit multiplier
 * @param result_lo Output: lower 64 bits (after shift)
 * @param result_hi Output: upper 64 bits (after shift)
 */
static inline void seraph_mul128x64_mid128(
    uint64_t a_lo, uint64_t a_hi,
    uint64_t b,
    uint64_t* result_lo, uint64_t* result_hi)
{
    /* Product: (a_hi:a_lo) * b = p2:p1:p0 (192 bits)
     * We want middle 128 bits: p1:p0[63:0] becomes result
     * Actually for Q64.64, we want bits [191:64] shifted
     */
    uint64_t p0_hi, p1_hi;
    uint64_t p0_lo = seraph_mulx_u64(a_lo, b, &p0_hi);
    uint64_t p1_lo = seraph_mulx_u64(a_hi, b, &p1_hi);
    (void)p0_lo;  /* Used for full precision; middle-128 doesn't need it */

    /* Add partial products with carry chain */
    uint8_t cf = 0;
    uint64_t mid = seraph_adcx_u64(p0_hi, p1_lo, 0, &cf);
    uint64_t hi = p1_hi + cf;

    /* For Q64.64: keep bits [127:64] of 192-bit result */
    *result_lo = mid;
    *result_hi = hi;
}

/**
 * @brief Full 128x128 -> 256-bit multiply
 *
 * Computes (a_hi:a_lo) * (b_hi:b_lo) = result[3:0]
 * Uses parallel ADCX/ADOX chains for maximum throughput.
 *
 * @param a_lo Lower 64 bits of a
 * @param a_hi Upper 64 bits of a
 * @param b_lo Lower 64 bits of b
 * @param b_hi Upper 64 bits of b
 * @param result Array of 4 uint64_t for 256-bit result
 */
static inline void seraph_mul128x128_full(
    uint64_t a_lo, uint64_t a_hi,
    uint64_t b_lo, uint64_t b_hi,
    uint64_t result[4])
{
    uint64_t t0, t1, t2, t3;
    uint64_t p_hi;

    /* Schoolbook multiplication with MULX
     *
     *           a_hi : a_lo
     *         x b_hi : b_lo
     *         ---------------
     *                 a_lo*b_lo  -> t0,t1
     *           a_hi*b_lo        -> add to t1,t2
     *           a_lo*b_hi        -> add to t1,t2
     *     a_hi*b_hi              -> add to t2,t3
     */

    /* First product: a_lo * b_lo */
    t0 = seraph_mulx_u64(a_lo, b_lo, &t1);

    /* Second product: a_hi * b_lo, add to t1:t2 */
    uint64_t p1_lo = seraph_mulx_u64(a_hi, b_lo, &p_hi);
    uint8_t cf1 = 0, cf2 = 0;
    t1 = seraph_adcx_u64(t1, p1_lo, 0, &cf1);
    t2 = p_hi + cf1;

    /* Third product: a_lo * b_hi, add to t1:t2 */
    uint64_t p2_lo = seraph_mulx_u64(a_lo, b_hi, &p_hi);
    t1 = seraph_adox_u64(t1, p2_lo, 0, &cf2);
    uint8_t cf3 = 0;
    t2 = seraph_adcx_u64(t2, p_hi, cf2, &cf3);
    t3 = cf3;

    /* Fourth product: a_hi * b_hi, add to t2:t3 */
    uint64_t p3_lo = seraph_mulx_u64(a_hi, b_hi, &p_hi);
    uint8_t cf4 = 0;
    t2 = seraph_adox_u64(t2, p3_lo, 0, &cf4);
    t3 = seraph_adcx_u64(t3, p_hi, cf4, &cf1);

    result[0] = t0;
    result[1] = t1;
    result[2] = t2;
    result[3] = t3;
}

/*============================================================================
 * Q32.32 Fixed-Point Multiply (64-bit values, 32.32 format)
 *============================================================================*/

/**
 * @brief Q32.32 fixed-point multiply
 *
 * Multiplies two Q32.32 values, returning Q32.32 result.
 * Uses MULX for efficient 64x64 -> 128 then shifts.
 *
 * @param a First Q32.32 value
 * @param b Second Q32.32 value
 * @return Product in Q32.32
 */
static inline int64_t seraph_q32_mul(int64_t a, int64_t b) {
    /* Handle signs */
    int negative = (a < 0) ^ (b < 0);
    uint64_t ua = (a < 0) ? (uint64_t)(-a) : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)(-b) : (uint64_t)b;

    /* 64x64 -> 128, then shift right by 32 */
    uint64_t hi;
    uint64_t lo = seraph_mulx_u64(ua, ub, &hi);

    /* Result is bits [95:32] of 128-bit product */
    uint64_t result = (hi << 32) | (lo >> 32);

    return negative ? -(int64_t)result : (int64_t)result;
}

/**
 * @brief Q16.16 fixed-point multiply
 *
 * Multiplies two Q16.16 values (stored in 32-bit).
 *
 * @param a First Q16.16 value
 * @param b Second Q16.16 value
 * @return Product in Q16.16
 */
static inline int32_t seraph_q16_mul(int32_t a, int32_t b) {
    int64_t prod = (int64_t)a * b;
    return (int32_t)(prod >> 16);
}

/*============================================================================
 * Branchless Utilities
 *
 * Note: seraph_select_u64 is defined in seraph/void.h
 *============================================================================*/

/**
 * @brief Branchless absolute value
 */
static inline int64_t seraph_abs_i64(int64_t x) {
    int64_t mask = x >> 63;  /* All 1s if negative */
    return (x ^ mask) - mask;
}

/**
 * @brief Branchless sign extraction
 *
 * Returns -1, 0, or 1.
 */
static inline int seraph_sign_i64(int64_t x) {
    return (x > 0) - (x < 0);
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_BMI2_INTRIN_H */
