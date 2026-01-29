/**
 * @file bits.c
 * @brief MC2: Bit Operations Implementation
 */

#include "seraph/bits.h"
#include <string.h>

/* BMI2 intrinsics for PDEP/PEXT hardware acceleration */
#if defined(__BMI2__) && (defined(__GNUC__) || defined(__clang__))
#include <x86intrin.h>
#elif defined(__BMI2__) && defined(_MSC_VER)
#include <immintrin.h>
#endif

/*============================================================================
 * Batch Operations
 *============================================================================*/

/**
 * @brief Population count for array of 64-bit values
 */
void seraph_popcount_batch_u64(const uint64_t* src, uint8_t* dst, size_t count) {
    if (!src || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_popcount_u64(src[i]);
    }
}

/**
 * @brief Leading zero count for array
 */
void seraph_clz_batch_u64(const uint64_t* src, uint8_t* dst, size_t count) {
    if (!src || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_clz_u64(src[i]);
    }
}

/**
 * @brief Trailing zero count for array
 */
void seraph_ctz_batch_u64(const uint64_t* src, uint8_t* dst, size_t count) {
    if (!src || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_ctz_u64(src[i]);
    }
}

/*============================================================================
 * Bit Array Operations
 *============================================================================*/

/**
 * @brief Set a bit in a bit array (packed uint64_t)
 */
void seraph_bitarray_set(uint64_t* arr, size_t bit_index) {
    if (!arr) return;
    size_t word_idx = bit_index / 64;
    uint8_t bit_pos = bit_index % 64;
    arr[word_idx] |= (1ULL << bit_pos);
}

/**
 * @brief Clear a bit in a bit array
 */
void seraph_bitarray_clear(uint64_t* arr, size_t bit_index) {
    if (!arr) return;
    size_t word_idx = bit_index / 64;
    uint8_t bit_pos = bit_index % 64;
    arr[word_idx] &= ~(1ULL << bit_pos);
}

/**
 * @brief Get a bit from a bit array
 */
bool seraph_bitarray_get(const uint64_t* arr, size_t bit_index) {
    if (!arr) return false;
    size_t word_idx = bit_index / 64;
    uint8_t bit_pos = bit_index % 64;
    return (arr[word_idx] >> bit_pos) & 1;
}

/**
 * @brief Toggle a bit in a bit array
 */
void seraph_bitarray_toggle(uint64_t* arr, size_t bit_index) {
    if (!arr) return;
    size_t word_idx = bit_index / 64;
    uint8_t bit_pos = bit_index % 64;
    arr[word_idx] ^= (1ULL << bit_pos);
}

/**
 * @brief Count total set bits in bit array
 */
size_t seraph_bitarray_popcount(const uint64_t* arr, size_t word_count) {
    if (!arr || word_count == 0) return 0;

    size_t total = 0;
    for (size_t i = 0; i < word_count; i++) {
        uint8_t pc = seraph_popcount_u64(arr[i]);
        if (pc != 0xFF) total += pc;
    }
    return total;
}

/**
 * @brief Find first set bit in bit array
 * @return Bit index, or SIZE_MAX if none found
 */
size_t seraph_bitarray_ffs(const uint64_t* arr, size_t word_count) {
    if (!arr || word_count == 0) return SIZE_MAX;

    for (size_t i = 0; i < word_count; i++) {
        if (arr[i] != 0) {
            uint8_t bit = seraph_ctz_u64(arr[i]);
            if (bit != 0xFF) {
                return i * 64 + bit;
            }
        }
    }
    return SIZE_MAX;
}

/**
 * @brief Find next set bit after given position
 * @return Bit index, or SIZE_MAX if none found
 */
size_t seraph_bitarray_next_set(const uint64_t* arr, size_t word_count, size_t after) {
    if (!arr || word_count == 0) return SIZE_MAX;

    size_t start_word = (after + 1) / 64;
    uint8_t start_bit = (after + 1) % 64;

    /* Check first word (masked) */
    if (start_word < word_count && start_bit < 64) {
        uint64_t masked = arr[start_word] & (UINT64_MAX << start_bit);
        if (masked != 0) {
            uint8_t bit = seraph_ctz_u64(masked);
            if (bit != 0xFF) {
                return start_word * 64 + bit;
            }
        }
        start_word++;
    }

    /* Check remaining words */
    for (size_t i = start_word; i < word_count; i++) {
        if (arr[i] != 0) {
            uint8_t bit = seraph_ctz_u64(arr[i]);
            if (bit != 0xFF) {
                return i * 64 + bit;
            }
        }
    }

    return SIZE_MAX;
}

/*============================================================================
 * Parity Operations
 *============================================================================*/

/**
 * @brief Compute parity (XOR of all bits)
 * @return 0 if even number of 1s, 1 if odd, 0xFF if VOID
 */
uint8_t seraph_parity_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_parityll(x);
#else
    x ^= x >> 32;
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return (uint8_t)(x & 1);
#endif
}

uint8_t seraph_parity_u32(uint32_t x) {
    if (SERAPH_IS_VOID_U32(x)) return 0xFF;
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_parity(x);
#else
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return (uint8_t)(x & 1);
#endif
}

/*============================================================================
 * Deposit and Extract (PDEP/PEXT equivalents)
 *============================================================================*/

/**
 * @brief Parallel bit deposit (software PDEP)
 *
 * Deposits bits from src into positions specified by mask.
 * e.g., pdep(0b1101, 0b01010100) = 0b01000100
 */
uint64_t seraph_pdep_u64(uint64_t src, uint64_t mask) {
    if (SERAPH_IS_VOID_U64(src) || SERAPH_IS_VOID_U64(mask)) {
        return SERAPH_VOID_U64;
    }

#if defined(__BMI2__)
    return _pdep_u64(src, mask);
#else
    uint64_t result = 0;
    uint64_t bit = 1;

    while (mask != 0) {
        uint64_t lowest = mask & (~mask + 1);  /* Lowest set bit */
        if (src & bit) {
            result |= lowest;
        }
        mask &= mask - 1;  /* Clear lowest bit */
        bit <<= 1;
    }

    return result;
#endif
}

/**
 * @brief Parallel bit extract (software PEXT)
 *
 * Extracts bits at positions specified by mask and compresses them.
 * e.g., pext(0b01000100, 0b01010100) = 0b101
 */
uint64_t seraph_pext_u64(uint64_t src, uint64_t mask) {
    if (SERAPH_IS_VOID_U64(src) || SERAPH_IS_VOID_U64(mask)) {
        return SERAPH_VOID_U64;
    }

#if defined(__BMI2__)
    return _pext_u64(src, mask);
#else
    uint64_t result = 0;
    uint64_t bit = 1;

    while (mask != 0) {
        uint64_t lowest = mask & (~mask + 1);
        if (src & lowest) {
            result |= bit;
        }
        mask &= mask - 1;
        bit <<= 1;
    }

    return result;
#endif
}
