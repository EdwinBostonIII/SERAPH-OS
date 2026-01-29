/**
 * @file semantic_byte.h
 * @brief MC3: Semantic Byte - Byte with per-bit validity mask
 *
 * A Semantic Byte is 16 bits: 8 bits of value + 8 bits of validity mask.
 * Each bit in the mask indicates whether the corresponding value bit is
 * valid (1) or VOID (0).
 */

#ifndef SERAPH_SEMANTIC_BYTE_H
#define SERAPH_SEMANTIC_BYTE_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Semantic Byte Type
 *============================================================================*/

/**
 * @brief Byte with per-bit validity tracking
 *
 * mask: 1 = valid, 0 = VOID for each bit position
 * value: actual data (only meaningful where mask=1)
 *
 * Invariant: (value & ~mask) == 0 (void bits should be 0)
 */
typedef struct {
    uint8_t mask;   /**< Validity mask (1=valid, 0=VOID) */
    uint8_t value;  /**< Data value */
} Seraph_SemanticByte;

/*============================================================================
 * Creation Functions
 *============================================================================*/

/**
 * @brief Create semantic byte from raw value (all bits valid)
 */
static inline Seraph_SemanticByte seraph_sbyte_from_u8(uint8_t value) {
    return (Seraph_SemanticByte){ .mask = 0xFF, .value = value };
}

/**
 * @brief Create semantic byte with explicit mask
 *
 * Value bits where mask=0 are cleared to maintain invariant.
 */
static inline Seraph_SemanticByte seraph_sbyte_create(uint8_t value, uint8_t mask) {
    return (Seraph_SemanticByte){ .mask = mask, .value = value & mask };
}

/**
 * @brief Create fully VOID semantic byte
 */
static inline Seraph_SemanticByte seraph_sbyte_void(void) {
    return (Seraph_SemanticByte){ .mask = 0x00, .value = 0x00 };
}

/**
 * @brief Create semantic byte from individual bits
 *
 * @param bits Array of 8 Seraph_Vbit values (LSB first)
 */
Seraph_SemanticByte seraph_sbyte_from_vbits(const Seraph_Vbit bits[8]);

/*============================================================================
 * Extraction Functions
 *============================================================================*/

/**
 * @brief Convert to u8, returns VOID_U8 if any bit is invalid
 */
static inline uint8_t seraph_sbyte_to_u8(Seraph_SemanticByte sb) {
    if (sb.mask != 0xFF) return SERAPH_VOID_U8;
    return sb.value;
}

/**
 * @brief Convert to u8 with default for invalid bits
 *
 * Invalid bits are replaced with corresponding bits from default_val.
 */
static inline uint8_t seraph_sbyte_to_u8_default(Seraph_SemanticByte sb,
                                                  uint8_t default_val) {
    return (sb.value & sb.mask) | (default_val & ~sb.mask);
}

/**
 * @brief Get a single bit as Seraph_Vbit
 */
static inline Seraph_Vbit seraph_sbyte_get_bit(Seraph_SemanticByte sb, uint8_t pos) {
    if (pos >= 8) return SERAPH_VBIT_VOID;
    if (!(sb.mask & (1 << pos))) return SERAPH_VBIT_VOID;
    return (sb.value & (1 << pos)) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Extract to array of Seraph_Vbit (LSB first)
 */
void seraph_sbyte_to_vbits(Seraph_SemanticByte sb, Seraph_Vbit bits[8]);

/*============================================================================
 * Validity Checks
 *============================================================================*/

/**
 * @brief Check if all bits are valid
 */
static inline bool seraph_sbyte_is_valid(Seraph_SemanticByte sb) {
    return sb.mask == 0xFF;
}

/**
 * @brief Check if all bits are VOID
 */
static inline bool seraph_sbyte_is_void(Seraph_SemanticByte sb) {
    return sb.mask == 0x00;
}

/**
 * @brief Check if any bit is VOID
 */
static inline bool seraph_sbyte_has_void(Seraph_SemanticByte sb) {
    return sb.mask != 0xFF;
}

/**
 * @brief Count number of valid bits
 */
static inline uint8_t seraph_sbyte_valid_count(Seraph_SemanticByte sb) {
    /* Population count of mask */
    uint8_t m = sb.mask;
    m = (m & 0x55) + ((m >> 1) & 0x55);
    m = (m & 0x33) + ((m >> 2) & 0x33);
    m = (m & 0x0F) + ((m >> 4) & 0x0F);
    return m;
}

/**
 * @brief Count number of VOID bits
 */
static inline uint8_t seraph_sbyte_void_count(Seraph_SemanticByte sb) {
    return 8 - seraph_sbyte_valid_count(sb);
}

/*============================================================================
 * Bitwise Operations
 *============================================================================*/

/**
 * @brief Bitwise NOT
 *
 * VOID bits remain VOID.
 */
static inline Seraph_SemanticByte seraph_sbyte_not(Seraph_SemanticByte sb) {
    return (Seraph_SemanticByte){
        .mask = sb.mask,
        .value = (~sb.value) & sb.mask
    };
}

/**
 * @brief Bitwise AND
 *
 * Result bit is valid only if BOTH input bits are valid.
 * This is conservative: we need both operands to know the result.
 */
static inline Seraph_SemanticByte seraph_sbyte_and(Seraph_SemanticByte a,
                                                    Seraph_SemanticByte b) {
    uint8_t result_mask = a.mask & b.mask;
    return (Seraph_SemanticByte){
        .mask = result_mask,
        .value = (a.value & b.value) & result_mask
    };
}

/**
 * @brief Bitwise OR
 *
 * Result bit is valid only if BOTH input bits are valid.
 */
static inline Seraph_SemanticByte seraph_sbyte_or(Seraph_SemanticByte a,
                                                   Seraph_SemanticByte b) {
    uint8_t result_mask = a.mask & b.mask;
    return (Seraph_SemanticByte){
        .mask = result_mask,
        .value = (a.value | b.value) & result_mask
    };
}

/**
 * @brief Bitwise XOR
 *
 * Result bit is valid only if BOTH input bits are valid.
 */
static inline Seraph_SemanticByte seraph_sbyte_xor(Seraph_SemanticByte a,
                                                    Seraph_SemanticByte b) {
    uint8_t result_mask = a.mask & b.mask;
    return (Seraph_SemanticByte){
        .mask = result_mask,
        .value = (a.value ^ b.value) & result_mask
    };
}

/*============================================================================
 * Optimistic Bitwise Operations
 *============================================================================*/

/**
 * @brief Optimistic AND - valid if output can be determined
 *
 * If either input has a valid 0 bit, output is valid 0 (regardless of other).
 * This leverages the fact that 0 AND x = 0.
 */
static inline Seraph_SemanticByte seraph_sbyte_and_optimistic(Seraph_SemanticByte a,
                                                               Seraph_SemanticByte b) {
    /* Positions where we know result is 0: valid 0 in either input */
    uint8_t known_zero = (a.mask & ~a.value) | (b.mask & ~b.value);
    /* Positions where we know result is 1: valid 1 in both inputs */
    uint8_t known_one = (a.mask & a.value) & (b.mask & b.value);
    /* Result mask: know result if either known_zero or known_one */
    uint8_t result_mask = known_zero | known_one;

    return (Seraph_SemanticByte){
        .mask = result_mask,
        .value = known_one
    };
}

/**
 * @brief Optimistic OR - valid if output can be determined
 *
 * If either input has a valid 1 bit, output is valid 1.
 * This leverages the fact that 1 OR x = 1.
 */
static inline Seraph_SemanticByte seraph_sbyte_or_optimistic(Seraph_SemanticByte a,
                                                              Seraph_SemanticByte b) {
    /* Positions where we know result is 1: valid 1 in either input */
    uint8_t known_one = (a.mask & a.value) | (b.mask & b.value);
    /* Positions where we know result is 0: valid 0 in both inputs */
    uint8_t known_zero = (a.mask & ~a.value) & (b.mask & ~b.value);
    /* Result mask */
    uint8_t result_mask = known_zero | known_one;

    return (Seraph_SemanticByte){
        .mask = result_mask,
        .value = known_one
    };
}

/*============================================================================
 * Merge Operations
 *============================================================================*/

/**
 * @brief Merge two semantic bytes
 *
 * Combines valid bits from both sources.
 * If both sources are valid for same bit but disagree, that bit becomes VOID.
 */
static inline Seraph_SemanticByte seraph_sbyte_merge(Seraph_SemanticByte a,
                                                      Seraph_SemanticByte b) {
    /* Bits where both are valid */
    uint8_t both_valid = a.mask & b.mask;
    /* Bits where both are valid AND agree */
    uint8_t agree = ~(a.value ^ b.value);
    /* Conflict mask: both valid but disagree */
    uint8_t conflict = both_valid & ~agree;

    /* Result mask: valid from a OR valid from b, minus conflicts */
    uint8_t result_mask = (a.mask | b.mask) & ~conflict;
    /* Result value: merge values, prefer a for overlapping valid bits */
    uint8_t result_value = ((a.value & a.mask) | (b.value & b.mask & ~a.mask)) & result_mask;

    return (Seraph_SemanticByte){
        .mask = result_mask,
        .value = result_value
    };
}

/**
 * @brief Coalesce: use a's valid bits, fill rest from b
 *
 * Unlike merge, this prefers a over b unconditionally.
 */
static inline Seraph_SemanticByte seraph_sbyte_coalesce(Seraph_SemanticByte a,
                                                         Seraph_SemanticByte b) {
    uint8_t result_mask = a.mask | b.mask;
    uint8_t result_value = (a.value & a.mask) | (b.value & b.mask & ~a.mask);
    return (Seraph_SemanticByte){
        .mask = result_mask,
        .value = result_value & result_mask
    };
}

/*============================================================================
 * Masking Operations
 *============================================================================*/

/**
 * @brief Set specified bits to VOID
 */
static inline Seraph_SemanticByte seraph_sbyte_mask_out(Seraph_SemanticByte sb,
                                                         uint8_t void_mask) {
    uint8_t new_mask = sb.mask & ~void_mask;
    return (Seraph_SemanticByte){
        .mask = new_mask,
        .value = sb.value & new_mask
    };
}

/**
 * @brief Keep only specified bits valid, rest become VOID
 */
static inline Seraph_SemanticByte seraph_sbyte_mask_keep(Seraph_SemanticByte sb,
                                                          uint8_t keep_mask) {
    uint8_t new_mask = sb.mask & keep_mask;
    return (Seraph_SemanticByte){
        .mask = new_mask,
        .value = sb.value & new_mask
    };
}

/**
 * @brief Set a specific bit value
 */
static inline Seraph_SemanticByte seraph_sbyte_set_bit(Seraph_SemanticByte sb,
                                                        uint8_t pos, Seraph_Vbit val) {
    if (pos >= 8) return sb;

    uint8_t bit = 1 << pos;

    if (val == SERAPH_VBIT_VOID) {
        /* Make bit VOID */
        return (Seraph_SemanticByte){
            .mask = sb.mask & ~bit,
            .value = sb.value & ~bit
        };
    } else {
        /* Make bit valid with specified value */
        return (Seraph_SemanticByte){
            .mask = sb.mask | bit,
            .value = (val == SERAPH_VBIT_TRUE) ? (sb.value | bit) : (sb.value & ~bit)
        };
    }
}

/*============================================================================
 * Comparison
 *============================================================================*/

/**
 * @brief Check equality (same mask and same valid values)
 */
static inline bool seraph_sbyte_eq(Seraph_SemanticByte a, Seraph_SemanticByte b) {
    if (a.mask != b.mask) return false;
    return (a.value & a.mask) == (b.value & b.mask);
}

/**
 * @brief Three-valued equality comparison
 *
 * Returns TRUE if equal where both valid
 * Returns FALSE if different where both valid
 * Returns VOID if can't determine (one is VOID where other differs)
 */
Seraph_Vbit seraph_sbyte_eq_vbit(Seraph_SemanticByte a, Seraph_SemanticByte b);

/*============================================================================
 * Shift Operations
 *============================================================================*/

/**
 * @brief Shift left (bits shifted out are lost, new bits are VOID)
 */
static inline Seraph_SemanticByte seraph_sbyte_shl(Seraph_SemanticByte sb, uint8_t n) {
    if (n >= 8) return seraph_sbyte_void();
    return (Seraph_SemanticByte){
        .mask = (sb.mask << n) & 0xFF,
        .value = (sb.value << n) & 0xFF
    };
}

/**
 * @brief Shift right (bits shifted out are lost, new bits are VOID)
 */
static inline Seraph_SemanticByte seraph_sbyte_shr(Seraph_SemanticByte sb, uint8_t n) {
    if (n >= 8) return seraph_sbyte_void();
    return (Seraph_SemanticByte){
        .mask = sb.mask >> n,
        .value = sb.value >> n
    };
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SEMANTIC_BYTE_H */
