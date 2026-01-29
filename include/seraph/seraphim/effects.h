/**
 * @file effects.h
 * @brief Seraphim Compiler - Effect System Interface
 *
 * MC26: Seraphim Language Effect System
 *
 * The effect system tracks side effects at compile time to ensure safety.
 * Functions must declare their effects, and the compiler verifies that
 * function bodies don't exceed their declared effects.
 *
 * Effect Categories:
 * - VOID:    May produce VOID values (division, array access, etc.)
 * - PERSIST: Accesses Atlas persistent storage
 * - NETWORK: Accesses Aether distributed memory
 * - TIMER:   Uses timer operations
 * - IO:      General I/O operations
 *
 * Rules:
 * - Pure functions ([pure]) have no effects (NONE)
 * - Effects propagate up the call stack
 * - A function may only call functions with effects <= its own declared effects
 * - Effect violations are compile-time errors
 *
 * Example:
 *   [pure]
 *   fn add(a: i32, b: i32) -> i32 { a + b }  // OK - no effects
 *
 *   effects(VOID)
 *   fn divide(a: i32, b: i32) -> ??i32 { a / b }  // OK - declares VOID
 *
 *   [pure]
 *   fn bad(a: i32, b: i32) -> ??i32 { a / b }  // ERROR - VOID not declared
 */

#ifndef SERAPH_SERAPHIM_EFFECTS_H
#define SERAPH_SERAPHIM_EFFECTS_H

#include "../arena.h"
#include "../vbit.h"
#include "types.h"

/*============================================================================
 * Effect Flags (re-exported from types.h for convenience)
 *============================================================================*/

/* Seraph_Effect_Flags is defined in types.h:
 * SERAPH_EFFECT_NONE    = 0x00   - Pure, no effects
 * SERAPH_EFFECT_VOID    = 0x01   - May produce VOID
 * SERAPH_EFFECT_PERSIST = 0x02   - Accesses Atlas
 * SERAPH_EFFECT_NETWORK = 0x04   - Accesses Aether
 * SERAPH_EFFECT_TIMER   = 0x08   - Uses timers
 * SERAPH_EFFECT_IO      = 0x10   - General I/O
 * SERAPH_EFFECT_ALL     = 0xFF   - All effects (unsafe)
 */

/*============================================================================
 * Effect Operations
 *============================================================================*/

/**
 * @brief Combine two effect sets (union)
 *
 * Returns the union of two effect sets. If either operation has an effect,
 * the result has that effect.
 */
SERAPH_INLINE Seraph_Effect_Flags seraph_effect_union(
    Seraph_Effect_Flags a, Seraph_Effect_Flags b) {
    return (Seraph_Effect_Flags)(a | b);
}

/**
 * @brief Intersect two effect sets
 *
 * Returns the intersection of two effect sets. Only effects present in
 * both sets are in the result.
 */
SERAPH_INLINE Seraph_Effect_Flags seraph_effect_intersect(
    Seraph_Effect_Flags a, Seraph_Effect_Flags b) {
    return (Seraph_Effect_Flags)(a & b);
}

/**
 * @brief Check if effect set `sub` is a subset of `super`
 *
 * Returns true if all effects in `sub` are also in `super`.
 * Used to verify that a function body's effects don't exceed declared effects.
 */
SERAPH_INLINE int seraph_effect_subset(
    Seraph_Effect_Flags sub, Seraph_Effect_Flags super) {
    return (sub & ~super) == 0;
}

/**
 * @brief Check if an effect set contains a specific effect
 */
SERAPH_INLINE int seraph_effect_has(
    Seraph_Effect_Flags set, Seraph_Effect_Flags effect) {
    return (set & effect) != 0;
}

/**
 * @brief Check if an effect set is pure (no effects)
 */
SERAPH_INLINE int seraph_effect_is_pure(Seraph_Effect_Flags set) {
    return set == SERAPH_EFFECT_NONE;
}

/**
 * @brief Get the name of a single effect flag
 */
const char* seraph_effect_name(Seraph_Effect_Flags effect);

/**
 * @brief Print an effect set to a buffer
 *
 * @param set The effect set to print
 * @param buf Buffer to write to
 * @param buf_size Size of buffer
 * @return Number of characters written (excluding null)
 */
size_t seraph_effect_print(Seraph_Effect_Flags set, char* buf, size_t buf_size);

/*============================================================================
 * Effect Diagnostic
 *============================================================================*/

/**
 * @brief Effect violation diagnostic
 */
typedef struct Seraph_Effect_Diag {
    Seraph_Source_Loc loc;              /**< Location of violation */
    const char* message;                /**< Error message */
    Seraph_Effect_Flags required;       /**< Effects that were needed */
    Seraph_Effect_Flags allowed;        /**< Effects that were declared */
    struct Seraph_Effect_Diag* next;    /**< Next diagnostic */
} Seraph_Effect_Diag;

/*============================================================================
 * Effect Context
 *============================================================================*/

/**
 * @brief Effect checking context
 *
 * Tracks the current effect checking state, including what effects are
 * allowed in the current scope and what effects have been inferred.
 */
typedef struct {
    Seraph_Arena* arena;                /**< Arena for allocations */

    /* Current function context */
    Seraph_Effect_Flags allowed;        /**< Effects allowed in current fn */
    Seraph_Effect_Flags inferred;       /**< Effects inferred so far */

    /* Function stack (for nested functions/lambdas) */
    struct {
        Seraph_Effect_Flags allowed;
        Seraph_Effect_Flags inferred;
    } fn_stack[32];                     /**< Stack of function contexts */
    int fn_depth;                       /**< Current depth in stack */

    /* Diagnostics */
    Seraph_Effect_Diag* diagnostics;    /**< Error list */
    int error_count;

    /* Type context reference (for looking up function types) */
    Seraph_Type_Context* type_ctx;

} Seraph_Effect_Context;

/*============================================================================
 * Context Management
 *============================================================================*/

/**
 * @brief Initialize an effect checking context
 */
Seraph_Vbit seraph_effect_context_init(Seraph_Effect_Context* ctx,
                                        Seraph_Arena* arena,
                                        Seraph_Type_Context* type_ctx);

/**
 * @brief Enter a function scope with declared effects
 *
 * Pushes current state and sets up for checking a function body.
 */
void seraph_effect_enter_fn(Seraph_Effect_Context* ctx,
                             Seraph_Effect_Flags declared);

/**
 * @brief Exit a function scope
 *
 * Pops the function state and restores the previous context.
 * Returns VBIT_FALSE if the function had effect violations.
 */
Seraph_Vbit seraph_effect_exit_fn(Seraph_Effect_Context* ctx);

/**
 * @brief Add an inferred effect to the current context
 */
void seraph_effect_add(Seraph_Effect_Context* ctx, Seraph_Effect_Flags effect);

/**
 * @brief Check if adding an effect would violate constraints
 *
 * Returns VBIT_TRUE if the effect is allowed, VBIT_FALSE otherwise.
 */
Seraph_Vbit seraph_effect_check(Seraph_Effect_Context* ctx,
                                 Seraph_Effect_Flags effect);

/**
 * @brief Get the current inferred effects
 */
SERAPH_INLINE Seraph_Effect_Flags seraph_effect_current(
    const Seraph_Effect_Context* ctx) {
    return ctx != NULL ? ctx->inferred : SERAPH_EFFECT_NONE;
}

/**
 * @brief Get the current allowed effects
 */
SERAPH_INLINE Seraph_Effect_Flags seraph_effect_allowed(
    const Seraph_Effect_Context* ctx) {
    return ctx != NULL ? ctx->allowed : SERAPH_EFFECT_ALL;
}

/*============================================================================
 * Effect Inference
 *============================================================================*/

/**
 * @brief Infer effects of an expression
 *
 * Analyzes the expression and adds any inferred effects to the context.
 * Returns the inferred effect set.
 */
Seraph_Effect_Flags seraph_effect_infer_expr(Seraph_Effect_Context* ctx,
                                              Seraph_AST_Node* expr);

/**
 * @brief Infer effects of a statement
 */
Seraph_Effect_Flags seraph_effect_infer_stmt(Seraph_Effect_Context* ctx,
                                              Seraph_AST_Node* stmt);

/**
 * @brief Infer effects of a block
 */
Seraph_Effect_Flags seraph_effect_infer_block(Seraph_Effect_Context* ctx,
                                               Seraph_AST_Node* block);

/**
 * @brief Get effects from a function type
 */
SERAPH_INLINE Seraph_Effect_Flags seraph_effect_from_fn_type(
    const Seraph_Type* fn_type) {
    if (fn_type == NULL || fn_type->kind != SERAPH_TYPE_FN) {
        return SERAPH_EFFECT_ALL;  /* Unknown function, assume all effects */
    }
    return fn_type->fn.effects;
}

/*============================================================================
 * Effect Checking
 *============================================================================*/

/**
 * @brief Check a function declaration for effect violations
 *
 * Verifies that the function body's inferred effects don't exceed
 * the declared effects.
 */
Seraph_Vbit seraph_effect_check_fn(Seraph_Effect_Context* ctx,
                                    Seraph_AST_Node* fn_decl);

/**
 * @brief Check a module for effect violations
 */
Seraph_Vbit seraph_effect_check_module(Seraph_Effect_Context* ctx,
                                        Seraph_AST_Node* module);

/*============================================================================
 * Diagnostics
 *============================================================================*/

/**
 * @brief Report an effect violation
 */
void seraph_effect_violation(Seraph_Effect_Context* ctx,
                              Seraph_Source_Loc loc,
                              Seraph_Effect_Flags required,
                              Seraph_Effect_Flags allowed);

/**
 * @brief Report an effect error with custom message
 */
void seraph_effect_error(Seraph_Effect_Context* ctx,
                          Seraph_Source_Loc loc,
                          const char* format, ...);

/**
 * @brief Check if context has errors
 */
SERAPH_INLINE int seraph_effect_has_errors(const Seraph_Effect_Context* ctx) {
    return ctx != NULL && ctx->error_count > 0;
}

/**
 * @brief Print all effect diagnostics
 */
void seraph_effect_print_diagnostics(const Seraph_Effect_Context* ctx);

/*============================================================================
 * Intrinsic Effect Inference
 *============================================================================*/

/**
 * @brief Get effects for a built-in operation by name
 *
 * Used to determine effects of intrinsic functions.
 */
Seraph_Effect_Flags seraph_effect_for_builtin(const char* name, size_t name_len);

/**
 * @brief Get effects for an operator
 *
 * Division, modulo, etc. may produce VOID.
 */
Seraph_Effect_Flags seraph_effect_for_operator(Seraph_Token_Type op);

#endif /* SERAPH_SERAPHIM_EFFECTS_H */
