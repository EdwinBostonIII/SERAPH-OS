/**
 * @file vbit.c
 * @brief MC1: VBIT Three-Valued Logic Implementation
 */

#include "seraph/vbit.h"
#include <string.h>

/*============================================================================
 * VBIT Array Operations Implementation
 *============================================================================*/

Seraph_Vbit seraph_vbit_all_true(const Seraph_Vbit* values, size_t count) {
    if (!values || count == 0) return SERAPH_VBIT_TRUE;  /* vacuous truth */

    bool has_void = false;

    for (size_t i = 0; i < count; i++) {
        if (values[i] == SERAPH_VBIT_FALSE) {
            return SERAPH_VBIT_FALSE;  /* FALSE dominates */
        }
        if (values[i] == SERAPH_VBIT_VOID) {
            has_void = true;
        }
    }

    /* If we got here, no FALSE found */
    return has_void ? SERAPH_VBIT_VOID : SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_vbit_any_true(const Seraph_Vbit* values, size_t count) {
    if (!values || count == 0) return SERAPH_VBIT_FALSE;

    bool has_void = false;

    for (size_t i = 0; i < count; i++) {
        if (values[i] == SERAPH_VBIT_TRUE) {
            return SERAPH_VBIT_TRUE;  /* TRUE dominates */
        }
        if (values[i] == SERAPH_VBIT_VOID) {
            has_void = true;
        }
    }

    /* If we got here, no TRUE found */
    return has_void ? SERAPH_VBIT_VOID : SERAPH_VBIT_FALSE;
}

size_t seraph_vbit_count_true(const Seraph_Vbit* values, size_t count) {
    if (!values || count == 0) return 0;

    size_t true_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (values[i] == SERAPH_VBIT_TRUE) {
            true_count++;
        }
    }
    return true_count;
}

size_t seraph_vbit_count_false(const Seraph_Vbit* values, size_t count) {
    if (!values || count == 0) return 0;

    size_t false_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (values[i] == SERAPH_VBIT_FALSE) {
            false_count++;
        }
    }
    return false_count;
}

size_t seraph_vbit_count_void(const Seraph_Vbit* values, size_t count) {
    if (!values || count == 0) return 0;

    size_t void_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (values[i] == SERAPH_VBIT_VOID) {
            void_count++;
        }
    }
    return void_count;
}

void seraph_vbit_not_array(const Seraph_Vbit* src, Seraph_Vbit* dst, size_t count) {
    if (!src || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_vbit_not(src[i]);
    }
}

void seraph_vbit_and_array(const Seraph_Vbit* a, const Seraph_Vbit* b,
                           Seraph_Vbit* dst, size_t count) {
    if (!a || !b || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_vbit_and(a[i], b[i]);
    }
}

void seraph_vbit_or_array(const Seraph_Vbit* a, const Seraph_Vbit* b,
                          Seraph_Vbit* dst, size_t count) {
    if (!a || !b || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_vbit_or(a[i], b[i]);
    }
}

/*============================================================================
 * Additional VBIT Utility Functions
 *============================================================================*/

/**
 * @brief Apply XOR element-wise
 */
void seraph_vbit_xor_array(const Seraph_Vbit* a, const Seraph_Vbit* b,
                           Seraph_Vbit* dst, size_t count) {
    if (!a || !b || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = seraph_vbit_xor(a[i], b[i]);
    }
}

/**
 * @brief Filter indices where VBIT is TRUE
 * @param values Input VBIT array
 * @param count Number of elements
 * @param indices Output array for indices (must be at least count elements)
 * @return Number of TRUE indices found
 */
size_t seraph_vbit_filter_true(const Seraph_Vbit* values, size_t count,
                               size_t* indices) {
    if (!values || !indices || count == 0) return 0;

    size_t found = 0;
    for (size_t i = 0; i < count; i++) {
        if (values[i] == SERAPH_VBIT_TRUE) {
            indices[found++] = i;
        }
    }
    return found;
}

/**
 * @brief Map function over VBIT array
 */
void seraph_vbit_map(const Seraph_Vbit* src, Seraph_Vbit* dst, size_t count,
                     Seraph_Vbit (*func)(Seraph_Vbit)) {
    if (!src || !dst || !func || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = func(src[i]);
    }
}

/**
 * @brief Reduce VBIT array with AND
 */
Seraph_Vbit seraph_vbit_reduce_and(const Seraph_Vbit* values, size_t count) {
    return seraph_vbit_all_true(values, count);
}

/**
 * @brief Reduce VBIT array with OR
 */
Seraph_Vbit seraph_vbit_reduce_or(const Seraph_Vbit* values, size_t count) {
    return seraph_vbit_any_true(values, count);
}

/**
 * @brief Pack VBIT array into bitmap (TRUE=1, FALSE/VOID=0)
 * @param values Input VBIT array
 * @param count Number of VBITs
 * @param bitmap Output bitmap (must be at least (count+7)/8 bytes)
 */
void seraph_vbit_pack_bitmap(const Seraph_Vbit* values, size_t count, uint8_t* bitmap) {
    if (!values || !bitmap || count == 0) return;

    size_t bytes = (count + 7) / 8;
    memset(bitmap, 0, bytes);

    for (size_t i = 0; i < count; i++) {
        if (values[i] == SERAPH_VBIT_TRUE) {
            bitmap[i / 8] |= (1u << (i % 8));
        }
    }
}

/**
 * @brief Unpack bitmap to VBIT array (1=TRUE, 0=FALSE)
 */
void seraph_vbit_unpack_bitmap(const uint8_t* bitmap, size_t count, Seraph_Vbit* values) {
    if (!bitmap || !values || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        if (bitmap[i / 8] & (1u << (i % 8))) {
            values[i] = SERAPH_VBIT_TRUE;
        } else {
            values[i] = SERAPH_VBIT_FALSE;
        }
    }
}

/**
 * @brief Three-way comparison with VBIT result
 *
 * Returns -1 (as VBIT encoding) if a < b, 0 if a == b, +1 if a > b
 * Encoded as: FALSE (0) for <, special value 0x80 for ==, TRUE (1) for >
 * Returns VOID if either is VOID
 */
int seraph_vbit_compare_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        return -128;  /* Special VOID indicator */
    }
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}
