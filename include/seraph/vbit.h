/**
 * @file vbit.h
 * @brief MC1: VBIT Three-Valued Logic (Kleene)
 *
 * Three values: FALSE (0x00), TRUE (0x01), VOID (0xFF)
 * Implements Kleene's strong three-valued logic where:
 *   - FALSE AND anything = FALSE
 *   - TRUE OR anything = TRUE
 *   - VOID propagates otherwise
 */

#ifndef SERAPH_VBIT_H
#define SERAPH_VBIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "seraph/void.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * VBIT Type Definition
 *============================================================================*/

/**
 * @brief Three-valued logic type
 *
 * Uses uint8_t storage:
 *   0x00 = FALSE
 *   0x01 = TRUE
 *   0xFF = VOID (unknown/error)
 */
typedef uint8_t Seraph_Vbit;

/** @name VBIT Constants */
/**@{*/
#define SERAPH_VBIT_FALSE  ((Seraph_Vbit)0x00)
#define SERAPH_VBIT_TRUE   ((Seraph_Vbit)0x01)
#define SERAPH_VBIT_VOID   ((Seraph_Vbit)0xFF)
/**@}*/

/*============================================================================
 * VBIT Detection
 *============================================================================*/

/**
 * @brief Check if VBIT is FALSE
 */
static inline bool seraph_vbit_is_false(Seraph_Vbit v) {
    return v == SERAPH_VBIT_FALSE;
}

/**
 * @brief Check if VBIT is TRUE
 */
static inline bool seraph_vbit_is_true(Seraph_Vbit v) {
    return v == SERAPH_VBIT_TRUE;
}

/**
 * @brief Check if VBIT is VOID
 */
static inline bool seraph_vbit_is_void(Seraph_Vbit v) {
    return v == SERAPH_VBIT_VOID;
}

/**
 * @brief Check if VBIT is valid (not VOID)
 */
static inline bool seraph_vbit_is_valid(Seraph_Vbit v) {
    return v == SERAPH_VBIT_FALSE || v == SERAPH_VBIT_TRUE;
}

/*============================================================================
 * VBIT Logic Operations
 *============================================================================*/

/**
 * @brief NOT operation (Kleene)
 *
 * | A     | NOT A |
 * |-------|-------|
 * | FALSE | TRUE  |
 * | TRUE  | FALSE |
 * | VOID  | VOID  |
 */
static inline Seraph_Vbit seraph_vbit_not(Seraph_Vbit a) {
    if (a == SERAPH_VBIT_VOID) return SERAPH_VBIT_VOID;
    return a == SERAPH_VBIT_FALSE ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief AND operation (Kleene)
 *
 * FALSE dominates: FALSE AND x = FALSE
 * Otherwise VOID propagates
 *
 * | A     | B     | A AND B |
 * |-------|-------|---------|
 * | FALSE | *     | FALSE   |
 * | *     | FALSE | FALSE   |
 * | TRUE  | TRUE  | TRUE    |
 * | TRUE  | VOID  | VOID    |
 * | VOID  | TRUE  | VOID    |
 * | VOID  | VOID  | VOID    |
 */
static inline Seraph_Vbit seraph_vbit_and(Seraph_Vbit a, Seraph_Vbit b) {
    /* FALSE dominates */
    if (a == SERAPH_VBIT_FALSE || b == SERAPH_VBIT_FALSE) {
        return SERAPH_VBIT_FALSE;
    }
    /* VOID propagates */
    if (a == SERAPH_VBIT_VOID || b == SERAPH_VBIT_VOID) {
        return SERAPH_VBIT_VOID;
    }
    /* Both TRUE */
    return SERAPH_VBIT_TRUE;
}

/**
 * @brief OR operation (Kleene)
 *
 * TRUE dominates: TRUE OR x = TRUE
 * Otherwise VOID propagates
 *
 * | A     | B     | A OR B |
 * |-------|-------|--------|
 * | TRUE  | *     | TRUE   |
 * | *     | TRUE  | TRUE   |
 * | FALSE | FALSE | FALSE  |
 * | FALSE | VOID  | VOID   |
 * | VOID  | FALSE | VOID   |
 * | VOID  | VOID  | VOID   |
 */
static inline Seraph_Vbit seraph_vbit_or(Seraph_Vbit a, Seraph_Vbit b) {
    /* TRUE dominates */
    if (a == SERAPH_VBIT_TRUE || b == SERAPH_VBIT_TRUE) {
        return SERAPH_VBIT_TRUE;
    }
    /* VOID propagates */
    if (a == SERAPH_VBIT_VOID || b == SERAPH_VBIT_VOID) {
        return SERAPH_VBIT_VOID;
    }
    /* Both FALSE */
    return SERAPH_VBIT_FALSE;
}

/**
 * @brief XOR operation (Kleene)
 *
 * VOID always propagates (need both values to compute)
 */
static inline Seraph_Vbit seraph_vbit_xor(Seraph_Vbit a, Seraph_Vbit b) {
    if (a == SERAPH_VBIT_VOID || b == SERAPH_VBIT_VOID) {
        return SERAPH_VBIT_VOID;
    }
    return (a != b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief NAND operation (Kleene)
 *
 * NOT (A AND B)
 */
static inline Seraph_Vbit seraph_vbit_nand(Seraph_Vbit a, Seraph_Vbit b) {
    return seraph_vbit_not(seraph_vbit_and(a, b));
}

/**
 * @brief NOR operation (Kleene)
 *
 * NOT (A OR B)
 */
static inline Seraph_Vbit seraph_vbit_nor(Seraph_Vbit a, Seraph_Vbit b) {
    return seraph_vbit_not(seraph_vbit_or(a, b));
}

/**
 * @brief XNOR operation (Kleene)
 *
 * NOT (A XOR B) = A IFF B
 */
static inline Seraph_Vbit seraph_vbit_xnor(Seraph_Vbit a, Seraph_Vbit b) {
    return seraph_vbit_not(seraph_vbit_xor(a, b));
}

/**
 * @brief IMPLIES operation (Material Implication)
 *
 * A → B = (NOT A) OR B
 *
 * | A     | B     | A → B |
 * |-------|-------|-------|
 * | FALSE | *     | TRUE  |
 * | TRUE  | FALSE | FALSE |
 * | TRUE  | TRUE  | TRUE  |
 * | TRUE  | VOID  | VOID  |
 * | VOID  | TRUE  | TRUE  |
 * | VOID  | FALSE | VOID  |
 * | VOID  | VOID  | VOID  |
 */
static inline Seraph_Vbit seraph_vbit_implies(Seraph_Vbit a, Seraph_Vbit b) {
    return seraph_vbit_or(seraph_vbit_not(a), b);
}

/**
 * @brief IFF operation (Equivalence / Biconditional)
 *
 * A ↔ B = (A → B) AND (B → A)
 */
static inline Seraph_Vbit seraph_vbit_iff(Seraph_Vbit a, Seraph_Vbit b) {
    return seraph_vbit_and(seraph_vbit_implies(a, b), seraph_vbit_implies(b, a));
}

/*============================================================================
 * VBIT Comparison Operations
 *============================================================================*/

/**
 * @brief Compare two uint64 values for equality, returning VBIT
 *
 * Returns VOID if either operand is VOID
 */
static inline Seraph_Vbit seraph_vbit_eq_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        return SERAPH_VBIT_VOID;
    }
    return (a == b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Compare for inequality
 */
static inline Seraph_Vbit seraph_vbit_ne_u64(uint64_t a, uint64_t b) {
    return seraph_vbit_not(seraph_vbit_eq_u64(a, b));
}

/**
 * @brief Compare for less-than
 */
static inline Seraph_Vbit seraph_vbit_lt_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        return SERAPH_VBIT_VOID;
    }
    return (a < b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Compare for less-than-or-equal
 */
static inline Seraph_Vbit seraph_vbit_le_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        return SERAPH_VBIT_VOID;
    }
    return (a <= b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Compare for greater-than
 */
static inline Seraph_Vbit seraph_vbit_gt_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        return SERAPH_VBIT_VOID;
    }
    return (a > b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Compare for greater-than-or-equal
 */
static inline Seraph_Vbit seraph_vbit_ge_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        return SERAPH_VBIT_VOID;
    }
    return (a >= b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Compare signed int64 values for equality
 */
static inline Seraph_Vbit seraph_vbit_eq_i64(int64_t a, int64_t b) {
    if (SERAPH_IS_VOID_I64(a) || SERAPH_IS_VOID_I64(b)) {
        return SERAPH_VBIT_VOID;
    }
    return (a == b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Compare signed for less-than
 */
static inline Seraph_Vbit seraph_vbit_lt_i64(int64_t a, int64_t b) {
    if (SERAPH_IS_VOID_I64(a) || SERAPH_IS_VOID_I64(b)) {
        return SERAPH_VBIT_VOID;
    }
    return (a < b) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * VBIT Conversion Functions
 *============================================================================*/

/**
 * @brief Convert VBIT to bool, with default for VOID
 */
static inline bool seraph_vbit_to_bool(Seraph_Vbit v, bool default_val) {
    if (v == SERAPH_VBIT_VOID) return default_val;
    return v == SERAPH_VBIT_TRUE;
}

/**
 * @brief Convert bool to VBIT
 */
static inline Seraph_Vbit seraph_vbit_from_bool(bool b) {
    return b ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Convert integer to VBIT (zero = FALSE, non-zero = TRUE, VOID = VOID)
 */
static inline Seraph_Vbit seraph_vbit_from_u64(uint64_t x) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VBIT_VOID;
    return x ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * VBIT Array Operations
 *============================================================================*/

/**
 * @brief Check if all VBITs in array are TRUE
 *
 * Returns VOID if any element is VOID
 * Returns FALSE if any element is FALSE
 * Returns TRUE only if all elements are TRUE
 */
Seraph_Vbit seraph_vbit_all_true(const Seraph_Vbit* values, size_t count);

/**
 * @brief Check if any VBIT in array is TRUE
 *
 * Returns TRUE if any element is TRUE
 * Returns VOID if any element is VOID (and none are TRUE)
 * Returns FALSE only if all elements are FALSE
 */
Seraph_Vbit seraph_vbit_any_true(const Seraph_Vbit* values, size_t count);

/**
 * @brief Count TRUE values in array (ignoring VOID)
 */
size_t seraph_vbit_count_true(const Seraph_Vbit* values, size_t count);

/**
 * @brief Count FALSE values in array (ignoring VOID)
 */
size_t seraph_vbit_count_false(const Seraph_Vbit* values, size_t count);

/**
 * @brief Count VOID values in array
 */
size_t seraph_vbit_count_void(const Seraph_Vbit* values, size_t count);

/**
 * @brief Apply NOT to each element of array
 */
void seraph_vbit_not_array(const Seraph_Vbit* src, Seraph_Vbit* dst, size_t count);

/**
 * @brief Apply AND element-wise to two arrays
 */
void seraph_vbit_and_array(const Seraph_Vbit* a, const Seraph_Vbit* b,
                           Seraph_Vbit* dst, size_t count);

/**
 * @brief Apply OR element-wise to two arrays
 */
void seraph_vbit_or_array(const Seraph_Vbit* a, const Seraph_Vbit* b,
                          Seraph_Vbit* dst, size_t count);

/*============================================================================
 * VBIT Conditional Selection
 *============================================================================*/

/**
 * @brief Three-valued conditional selection
 *
 * If cond is TRUE, return true_val
 * If cond is FALSE, return false_val
 * If cond is VOID, return VOID
 */
static inline uint64_t seraph_vbit_select_u64(Seraph_Vbit cond,
                                              uint64_t true_val,
                                              uint64_t false_val) {
    if (cond == SERAPH_VBIT_VOID) return SERAPH_VOID_U64;
    return cond == SERAPH_VBIT_TRUE ? true_val : false_val;
}

/**
 * @brief Coalesce: return first non-VOID value, or VOID if all are VOID
 */
static inline Seraph_Vbit seraph_vbit_coalesce(Seraph_Vbit a, Seraph_Vbit b) {
    if (a != SERAPH_VBIT_VOID) return a;
    return b;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_VBIT_H */
