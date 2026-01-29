/**
 * @file bits.h
 * @brief MC2: Bit Operations with VOID awareness
 *
 * All bit operations that can fail (out of range, invalid input)
 * return VOID instead of undefined behavior.
 *
 * Byte ordering: LSB-first (Little-Endian, x86-64 native)
 */

#ifndef SERAPH_BITS_H
#define SERAPH_BITS_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Bit Range Type
 *============================================================================*/

/**
 * @brief Represents a contiguous range of bits
 */
typedef struct {
    uint8_t start;   /**< Starting bit position (0-indexed) */
    uint8_t length;  /**< Number of bits in range */
} Seraph_BitRange;

/**
 * @brief Check if bit range is valid for 64-bit value
 */
static inline bool seraph_bitrange_valid_64(Seraph_BitRange range) {
    return range.length > 0 && (range.start + range.length) <= 64;
}

/**
 * @brief Check if bit range is valid for 32-bit value
 */
static inline bool seraph_bitrange_valid_32(Seraph_BitRange range) {
    return range.length > 0 && (range.start + range.length) <= 32;
}

/*============================================================================
 * Single Bit Operations (64-bit)
 *============================================================================*/

/**
 * @brief Get a single bit
 * @param x Source value
 * @param pos Bit position (0-63)
 * @return 0 or 1, or VOID if invalid
 */
static inline uint64_t seraph_bit_get_u64(uint64_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U64(x) || pos >= 64) return SERAPH_VOID_U64;
    return (x >> pos) & 1;
}

/**
 * @brief Set a single bit to 1
 */
static inline uint64_t seraph_bit_set_u64(uint64_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U64(x) || pos >= 64) return SERAPH_VOID_U64;
    return x | (1ULL << pos);
}

/**
 * @brief Clear a single bit to 0
 */
static inline uint64_t seraph_bit_clear_u64(uint64_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U64(x) || pos >= 64) return SERAPH_VOID_U64;
    return x & ~(1ULL << pos);
}

/**
 * @brief Toggle a single bit
 */
static inline uint64_t seraph_bit_toggle_u64(uint64_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U64(x) || pos >= 64) return SERAPH_VOID_U64;
    return x ^ (1ULL << pos);
}

/**
 * @brief Test if a bit is set
 */
static inline Seraph_Vbit seraph_bit_test_u64(uint64_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U64(x) || pos >= 64) return SERAPH_VBIT_VOID;
    return ((x >> pos) & 1) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Single Bit Operations (32-bit)
 *============================================================================*/

static inline uint32_t seraph_bit_get_u32(uint32_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U32(x) || pos >= 32) return SERAPH_VOID_U32;
    return (x >> pos) & 1;
}

static inline uint32_t seraph_bit_set_u32(uint32_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U32(x) || pos >= 32) return SERAPH_VOID_U32;
    return x | (1UL << pos);
}

static inline uint32_t seraph_bit_clear_u32(uint32_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U32(x) || pos >= 32) return SERAPH_VOID_U32;
    return x & ~(1UL << pos);
}

static inline uint32_t seraph_bit_toggle_u32(uint32_t x, uint8_t pos) {
    if (SERAPH_IS_VOID_U32(x) || pos >= 32) return SERAPH_VOID_U32;
    return x ^ (1UL << pos);
}

/*============================================================================
 * Bit Range Operations
 *============================================================================*/

/**
 * @brief Extract bits from a range
 * @param x Source value
 * @param start Starting bit position
 * @param len Number of bits to extract
 * @return Extracted bits (shifted to LSB), or VOID if invalid
 */
static inline uint64_t seraph_bits_extract_u64(uint64_t x, uint8_t start, uint8_t len) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
    if (len == 0 || start >= 64 || (start + len) > 64) return SERAPH_VOID_U64;
    uint64_t mask = (len == 64) ? UINT64_MAX : ((1ULL << len) - 1);
    return (x >> start) & mask;
}

/**
 * @brief Insert bits into a range
 * @param x Target value
 * @param val Value to insert (uses low 'len' bits)
 * @param start Starting bit position
 * @param len Number of bits to insert
 * @return Modified value, or VOID if invalid
 */
static inline uint64_t seraph_bits_insert_u64(uint64_t x, uint64_t val,
                                               uint8_t start, uint8_t len) {
    if (SERAPH_IS_VOID_U64(x) || SERAPH_IS_VOID_U64(val)) return SERAPH_VOID_U64;
    if (len == 0 || start >= 64 || (start + len) > 64) return SERAPH_VOID_U64;

    uint64_t mask = (len == 64) ? UINT64_MAX : ((1ULL << len) - 1);
    uint64_t clear_mask = ~(mask << start);
    return (x & clear_mask) | ((val & mask) << start);
}

/**
 * @brief Extract using BitRange struct
 */
static inline uint64_t seraph_bitrange_extract_u64(uint64_t x, Seraph_BitRange range) {
    return seraph_bits_extract_u64(x, range.start, range.length);
}

/**
 * @brief Insert using BitRange struct
 */
static inline uint64_t seraph_bitrange_insert_u64(uint64_t x, uint64_t val,
                                                   Seraph_BitRange range) {
    return seraph_bits_insert_u64(x, val, range.start, range.length);
}

/*============================================================================
 * Shift Operations
 *============================================================================*/

/**
 * @brief Logical shift left
 * @return VOID if shift >= 64 or x is VOID
 */
static inline uint64_t seraph_shl_u64(uint64_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U64(x) || n >= 64) return SERAPH_VOID_U64;
    return x << n;
}

/**
 * @brief Logical shift right
 * @return VOID if shift >= 64 or x is VOID
 */
static inline uint64_t seraph_shr_u64(uint64_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U64(x) || n >= 64) return SERAPH_VOID_U64;
    return x >> n;
}

/**
 * @brief Arithmetic shift right (preserves sign)
 * @return VOID if shift >= 64 or x is VOID
 */
static inline int64_t seraph_sar_i64(int64_t x, uint8_t n) {
    if (SERAPH_IS_VOID_I64(x) || n >= 64) return SERAPH_VOID_I64;
    return x >> n;  /* C99 implementation-defined but usually arithmetic */
}

/**
 * @brief Rotate left
 */
static inline uint64_t seraph_rol_u64(uint64_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
    n &= 63;  /* Rotation by 64 = rotation by 0 */
    if (n == 0) return x;
    return (x << n) | (x >> (64 - n));
}

/**
 * @brief Rotate right
 */
static inline uint64_t seraph_ror_u64(uint64_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
    n &= 63;
    if (n == 0) return x;
    return (x >> n) | (x << (64 - n));
}

/* 32-bit variants */
static inline uint32_t seraph_shl_u32(uint32_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U32(x) || n >= 32) return SERAPH_VOID_U32;
    return x << n;
}

static inline uint32_t seraph_shr_u32(uint32_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U32(x) || n >= 32) return SERAPH_VOID_U32;
    return x >> n;
}

static inline uint32_t seraph_rol_u32(uint32_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U32(x)) return SERAPH_VOID_U32;
    n &= 31;
    if (n == 0) return x;
    return (x << n) | (x >> (32 - n));
}

static inline uint32_t seraph_ror_u32(uint32_t x, uint8_t n) {
    if (SERAPH_IS_VOID_U32(x)) return SERAPH_VOID_U32;
    n &= 31;
    if (n == 0) return x;
    return (x >> n) | (x << (32 - n));
}

/*============================================================================
 * Population Count and Bit Scanning
 *============================================================================*/

/**
 * @brief Count number of bits set to 1
 * @return Population count, or 0xFF for VOID input
 */
static inline uint8_t seraph_popcount_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_popcountll(x);
#else
    /* Fallback: bit manipulation */
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (uint8_t)((x * 0x0101010101010101ULL) >> 56);
#endif
}

static inline uint8_t seraph_popcount_u32(uint32_t x) {
    if (SERAPH_IS_VOID_U32(x)) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_popcount(x);
#else
    x = x - ((x >> 1) & 0x55555555UL);
    x = (x & 0x33333333UL) + ((x >> 2) & 0x33333333UL);
    x = (x + (x >> 4)) & 0x0F0F0F0FUL;
    return (uint8_t)((x * 0x01010101UL) >> 24);
#endif
}

/**
 * @brief Count leading zeros
 * @return Number of leading zeros, or 0xFF for VOID/zero input
 */
static inline uint8_t seraph_clz_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x) || x == 0) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_clzll(x);
#else
    uint8_t n = 0;
    if ((x & 0xFFFFFFFF00000000ULL) == 0) { n += 32; x <<= 32; }
    if ((x & 0xFFFF000000000000ULL) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF00000000000000ULL) == 0) { n += 8;  x <<= 8;  }
    if ((x & 0xF000000000000000ULL) == 0) { n += 4;  x <<= 4;  }
    if ((x & 0xC000000000000000ULL) == 0) { n += 2;  x <<= 2;  }
    if ((x & 0x8000000000000000ULL) == 0) { n += 1; }
    return n;
#endif
}

static inline uint8_t seraph_clz_u32(uint32_t x) {
    if (SERAPH_IS_VOID_U32(x) || x == 0) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_clz(x);
#else
    uint8_t n = 0;
    if ((x & 0xFFFF0000UL) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF000000UL) == 0) { n += 8;  x <<= 8;  }
    if ((x & 0xF0000000UL) == 0) { n += 4;  x <<= 4;  }
    if ((x & 0xC0000000UL) == 0) { n += 2;  x <<= 2;  }
    if ((x & 0x80000000UL) == 0) { n += 1; }
    return n;
#endif
}

/**
 * @brief Count trailing zeros
 * @return Number of trailing zeros, or 0xFF for VOID/zero input
 */
static inline uint8_t seraph_ctz_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x) || x == 0) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_ctzll(x);
#else
    return (uint8_t)(63 - seraph_clz_u64(x & (~x + 1)));
#endif
}

static inline uint8_t seraph_ctz_u32(uint32_t x) {
    if (SERAPH_IS_VOID_U32(x) || x == 0) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_ctz(x);
#else
    return (uint8_t)(31 - seraph_clz_u32(x & (~x + 1)));
#endif
}

/**
 * @brief Find first set bit (1-indexed, like ffs)
 * @return Bit position + 1, or 0 if none set, or 0xFF for VOID
 */
static inline uint8_t seraph_ffs_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return 0xFF;
    if (x == 0) return 0;
    return seraph_ctz_u64(x) + 1;
}

/**
 * @brief Find last set bit (1-indexed)
 * @return Bit position + 1, or 0 if none set, or 0xFF for VOID
 */
static inline uint8_t seraph_fls_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return 0xFF;
    if (x == 0) return 0;
    return 64 - seraph_clz_u64(x);
}

/*============================================================================
 * Mask Generation
 *============================================================================*/

/**
 * @brief Generate mask with n low bits set
 * e.g., seraph_mask_low(4) = 0x0F
 */
static inline uint64_t seraph_mask_low_u64(uint8_t n) {
    if (n == 0) return 0;
    if (n >= 64) return UINT64_MAX;
    return (1ULL << n) - 1;
}

/**
 * @brief Generate mask with n high bits set
 * e.g., seraph_mask_high(4) = 0xF000000000000000
 */
static inline uint64_t seraph_mask_high_u64(uint8_t n) {
    if (n == 0) return 0;
    if (n >= 64) return UINT64_MAX;
    return UINT64_MAX << (64 - n);
}

/**
 * @brief Generate mask for a specific range
 */
static inline uint64_t seraph_mask_range_u64(uint8_t start, uint8_t len) {
    if (len == 0 || start >= 64 || (start + len) > 64) return 0;
    uint64_t mask = (len == 64) ? UINT64_MAX : ((1ULL << len) - 1);
    return mask << start;
}

/*============================================================================
 * Byte Manipulation
 *============================================================================*/

/**
 * @brief Reverse byte order (endian swap)
 */
static inline uint64_t seraph_bswap_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(x);
#else
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8)  |
           ((x & 0x000000FF00000000ULL) >> 8)  |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
#endif
}

static inline uint32_t seraph_bswap_u32(uint32_t x) {
    if (SERAPH_IS_VOID_U32(x)) return SERAPH_VOID_U32;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(x);
#else
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) << 8)  |
           ((x & 0x00FF0000U) >> 8)  |
           ((x & 0xFF000000U) >> 24);
#endif
}

/**
 * @brief Reverse all bits
 */
static inline uint64_t seraph_bitrev_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
    x = ((x & 0x5555555555555555ULL) << 1) | ((x >> 1) & 0x5555555555555555ULL);
    x = ((x & 0x3333333333333333ULL) << 2) | ((x >> 2) & 0x3333333333333333ULL);
    x = ((x & 0x0F0F0F0F0F0F0F0FULL) << 4) | ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL);
    return seraph_bswap_u64(x);
}

/*============================================================================
 * Power of Two Operations
 *============================================================================*/

/**
 * @brief Check if value is a power of two
 */
static inline Seraph_Vbit seraph_is_pow2_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VBIT_VOID;
    if (x == 0) return SERAPH_VBIT_FALSE;
    return ((x & (x - 1)) == 0) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Round up to next power of two
 * @return VOID if already max or overflow would occur
 */
static inline uint64_t seraph_next_pow2_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
    if (x == 0) return 1;
    if (x > (1ULL << 63)) return SERAPH_VOID_U64;  /* Would overflow */

    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return x + 1;
}

/**
 * @brief Get log base 2 (floor)
 * @return VOID if x is 0 or VOID
 */
static inline uint8_t seraph_log2_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x) || x == 0) return 0xFF;
    return 63 - seraph_clz_u64(x);
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_BITS_H */
