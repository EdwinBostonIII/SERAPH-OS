/**
 * @file semantic_byte.c
 * @brief MC3: Semantic Byte Implementation
 */

#include "seraph/semantic_byte.h"

Seraph_SemanticByte seraph_sbyte_from_vbits(const Seraph_Vbit bits[8]) {
    uint8_t mask = 0;
    uint8_t value = 0;

    for (int i = 0; i < 8; i++) {
        if (bits[i] != SERAPH_VBIT_VOID) {
            mask |= (1 << i);
            if (bits[i] == SERAPH_VBIT_TRUE) {
                value |= (1 << i);
            }
        }
    }

    return (Seraph_SemanticByte){ .mask = mask, .value = value };
}

void seraph_sbyte_to_vbits(Seraph_SemanticByte sb, Seraph_Vbit bits[8]) {
    for (int i = 0; i < 8; i++) {
        if (sb.mask & (1 << i)) {
            bits[i] = (sb.value & (1 << i)) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
        } else {
            bits[i] = SERAPH_VBIT_VOID;
        }
    }
}

Seraph_Vbit seraph_sbyte_eq_vbit(Seraph_SemanticByte a, Seraph_SemanticByte b) {
    /* Check bits where both are valid */
    uint8_t both_valid = a.mask & b.mask;

    if (both_valid == 0) {
        /* No bits to compare - VOID (unknown) */
        return SERAPH_VBIT_VOID;
    }

    /* Check if any valid bits differ */
    uint8_t differ = (a.value ^ b.value) & both_valid;

    if (differ != 0) {
        /* At least one bit differs where both valid - definitely not equal */
        return SERAPH_VBIT_FALSE;
    }

    /* All compared bits are equal */
    /* But if either has VOID bits, we can't be sure of full equality */
    if (a.mask != 0xFF || b.mask != 0xFF) {
        return SERAPH_VBIT_VOID;  /* Partial equality, unknown overall */
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Semantic Byte Array Operations
 *============================================================================*/

/**
 * @brief Merge array of semantic bytes into one (where possible)
 */
Seraph_SemanticByte seraph_sbyte_merge_array(const Seraph_SemanticByte* arr, size_t count) {
    if (!arr || count == 0) return seraph_sbyte_void();

    Seraph_SemanticByte result = arr[0];
    for (size_t i = 1; i < count; i++) {
        result = seraph_sbyte_merge(result, arr[i]);
    }
    return result;
}

/**
 * @brief Count fully valid bytes in array
 */
size_t seraph_sbyte_count_valid(const Seraph_SemanticByte* arr, size_t count) {
    if (!arr || count == 0) return 0;

    size_t valid_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (seraph_sbyte_is_valid(arr[i])) {
            valid_count++;
        }
    }
    return valid_count;
}

/**
 * @brief Count fully VOID bytes in array
 */
size_t seraph_sbyte_count_void(const Seraph_SemanticByte* arr, size_t count) {
    if (!arr || count == 0) return 0;

    size_t void_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (seraph_sbyte_is_void(arr[i])) {
            void_count++;
        }
    }
    return void_count;
}

/**
 * @brief Extract valid bytes to output array
 * @return Number of valid bytes extracted
 */
size_t seraph_sbyte_extract_valid(const Seraph_SemanticByte* arr, size_t count,
                                   uint8_t* out, size_t out_size) {
    if (!arr || !out || count == 0 || out_size == 0) return 0;

    size_t extracted = 0;
    for (size_t i = 0; i < count && extracted < out_size; i++) {
        if (seraph_sbyte_is_valid(arr[i])) {
            out[extracted++] = arr[i].value;
        }
    }
    return extracted;
}

/**
 * @brief Convert byte array to semantic byte array (all valid)
 */
void seraph_sbyte_from_bytes(const uint8_t* bytes, Seraph_SemanticByte* out, size_t count) {
    if (!bytes || !out || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        out[i] = seraph_sbyte_from_u8(bytes[i]);
    }
}

/**
 * @brief Convert semantic byte array to bytes with default
 */
void seraph_sbyte_to_bytes_default(const Seraph_SemanticByte* arr, uint8_t* out,
                                    size_t count, uint8_t default_val) {
    if (!arr || !out || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        out[i] = seraph_sbyte_to_u8_default(arr[i], default_val);
    }
}

/*============================================================================
 * Bitwise Array Operations
 *============================================================================*/

/**
 * @brief Element-wise AND of two arrays
 */
void seraph_sbyte_and_array(const Seraph_SemanticByte* a,
                            const Seraph_SemanticByte* b,
                            Seraph_SemanticByte* out, size_t count) {
    if (!a || !b || !out || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        out[i] = seraph_sbyte_and(a[i], b[i]);
    }
}

/**
 * @brief Element-wise OR of two arrays
 */
void seraph_sbyte_or_array(const Seraph_SemanticByte* a,
                           const Seraph_SemanticByte* b,
                           Seraph_SemanticByte* out, size_t count) {
    if (!a || !b || !out || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        out[i] = seraph_sbyte_or(a[i], b[i]);
    }
}

/**
 * @brief Element-wise XOR of two arrays
 */
void seraph_sbyte_xor_array(const Seraph_SemanticByte* a,
                            const Seraph_SemanticByte* b,
                            Seraph_SemanticByte* out, size_t count) {
    if (!a || !b || !out || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        out[i] = seraph_sbyte_xor(a[i], b[i]);
    }
}

/**
 * @brief Element-wise NOT of array
 */
void seraph_sbyte_not_array(const Seraph_SemanticByte* arr,
                            Seraph_SemanticByte* out, size_t count) {
    if (!arr || !out || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        out[i] = seraph_sbyte_not(arr[i]);
    }
}
