/**
 * @file checker.h
 * @brief Seraphim Compiler - Type Checker Interface
 *
 * MC26: Seraphim Language Type Checker
 *
 * The type checker validates that all expressions and statements are
 * well-typed according to the Seraphim type system. It integrates with
 * the effect system to verify effect annotations.
 *
 * Key Features:
 * - VOID propagation checking (??)
 * - VOID assertion checking (!!)
 * - Substrate block validation
 * - Recover block validation
 * - Effect-aware function checking
 */

#ifndef SERAPH_SERAPHIM_CHECKER_H
#define SERAPH_SERAPHIM_CHECKER_H

#include "seraph/seraphim/ast.h"
#include "seraph/seraphim/types.h"
#include "seraph/seraphim/effects.h"
#include "seraph/arena.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Checker Diagnostic
 *============================================================================*/

/**
 * @brief Type checker diagnostic
 */
typedef struct Seraph_Checker_Diag {
    Seraph_Source_Loc loc;              /**< Location of error */
    const char* message;                /**< Error message */
    int is_error;                       /**< 1 = error, 0 = warning */
    struct Seraph_Checker_Diag* next;   /**< Next diagnostic */
} Seraph_Checker_Diag;

/*============================================================================
 * Type Checker Context
 *============================================================================*/

/**
 * @brief Type checker context
 *
 * Holds all state needed for type checking a module.
 */
typedef struct {
    Seraph_Arena* arena;                /**< Arena for allocations */
    Seraph_Type_Context* types;         /**< Type system context */
    Seraph_Effect_Context* effects;     /**< Effect system context */
    Seraph_Checker_Diag* diagnostics;   /**< Error/warning list */
    int error_count;                    /**< Total errors */
    int warning_count;                  /**< Total warnings */

    /* Current function context */
    Seraph_AST_Node* current_fn;        /**< Currently checking function */
    Seraph_Type* current_ret_type;      /**< Expected return type */
    int in_loop;                        /**< Inside a loop? */
    int in_recover;                     /**< Inside recover block? */
    int in_persist;                     /**< Inside persist block? */
    int in_aether;                      /**< Inside aether block? */
} Seraph_Checker;

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * @brief Initialize a type checker
 *
 * @param checker Checker to initialize
 * @param arena Arena for allocations
 * @param types Type context (may be NULL, will create internal)
 * @return VBIT_TRUE on success, VBIT_VOID on failure
 */
Seraph_Vbit seraph_checker_init(Seraph_Checker* checker,
                                 Seraph_Arena* arena,
                                 Seraph_Type_Context* types);

/**
 * @brief Set up effect context for the checker
 *
 * @param checker The checker
 * @param effects Effect context to use
 */
void seraph_checker_set_effects(Seraph_Checker* checker,
                                 Seraph_Effect_Context* effects);

/*============================================================================
 * Type Checking Entry Points
 *============================================================================*/

/**
 * @brief Type check a complete module
 *
 * @param checker The type checker
 * @param module The AST module to check
 * @return VBIT_TRUE if no errors, VBIT_FALSE if errors, VBIT_VOID if invalid
 */
Seraph_Vbit seraph_checker_check_module(Seraph_Checker* checker,
                                         Seraph_AST_Node* module);

/**
 * @brief Type check a function declaration
 *
 * @param checker The type checker
 * @param fn_decl The function declaration AST
 * @return VBIT_TRUE if valid, VBIT_FALSE if errors
 */
Seraph_Vbit seraph_checker_check_fn(Seraph_Checker* checker,
                                     Seraph_AST_Node* fn_decl);

/**
 * @brief Type check a struct declaration
 *
 * @param checker The type checker
 * @param struct_decl The struct declaration AST
 * @return VBIT_TRUE if valid, VBIT_FALSE if errors
 */
Seraph_Vbit seraph_checker_check_struct(Seraph_Checker* checker,
                                         Seraph_AST_Node* struct_decl);

/**
 * @brief Type check an enum declaration
 *
 * @param checker The type checker
 * @param enum_decl The enum declaration AST
 * @return VBIT_TRUE if valid, VBIT_FALSE if errors
 */
Seraph_Vbit seraph_checker_check_enum(Seraph_Checker* checker,
                                       Seraph_AST_Node* enum_decl);

/*============================================================================
 * Expression Type Checking
 *============================================================================*/

/**
 * @brief Infer the type of an expression
 *
 * @param checker The type checker
 * @param expr The expression AST
 * @return The inferred type, or VOID type on error
 */
Seraph_Type* seraph_checker_check_expr(Seraph_Checker* checker,
                                        Seraph_AST_Node* expr);

/**
 * @brief Check that expression has expected type
 *
 * @param checker The type checker
 * @param expr The expression to check
 * @param expected The expected type
 * @return VBIT_TRUE if matches, VBIT_FALSE if mismatch
 */
Seraph_Vbit seraph_checker_expect(Seraph_Checker* checker,
                                   Seraph_AST_Node* expr,
                                   Seraph_Type* expected);

/*============================================================================
 * VOID Operator Checking
 *============================================================================*/

/**
 * @brief Check VOID propagation operator (??)
 *
 * Validates that:
 * - Operand is a VOID-able type
 * - Current function allows VOID effect
 * - Propagation context is valid
 *
 * @param checker The type checker
 * @param node The void propagation AST node
 * @return VBIT_TRUE if valid, VBIT_FALSE if error
 */
Seraph_Vbit seraph_checker_check_void_prop(Seraph_Checker* checker,
                                            Seraph_AST_Node* node);

/**
 * @brief Check VOID assertion operator (!!)
 *
 * Validates that:
 * - Operand is a VOID-able type
 * - Assertion context is valid (not in pure function without VOID effect)
 *
 * @param checker The type checker
 * @param node The void assertion AST node
 * @return VBIT_TRUE if valid, VBIT_FALSE if error
 */
Seraph_Vbit seraph_checker_check_void_assert(Seraph_Checker* checker,
                                              Seraph_AST_Node* node);

/*============================================================================
 * Substrate Block Checking
 *============================================================================*/

/**
 * @brief Check a substrate block (persist/aether)
 *
 * Validates that:
 * - Block has valid substrate context
 * - All references within block match substrate type
 * - Effect annotations are consistent
 *
 * @param checker The type checker
 * @param node The substrate block AST node
 * @return VBIT_TRUE if valid, VBIT_FALSE if error
 */
Seraph_Vbit seraph_checker_check_substrate_block(Seraph_Checker* checker,
                                                  Seraph_AST_Node* node);

/**
 * @brief Check a persist {} block
 *
 * Validates Atlas persistent storage access.
 *
 * @param checker The type checker
 * @param node The persist block AST
 * @return VBIT_TRUE if valid, VBIT_FALSE if error
 */
Seraph_Vbit seraph_checker_check_persist(Seraph_Checker* checker,
                                          Seraph_AST_Node* node);

/**
 * @brief Check an aether {} block
 *
 * Validates Aether distributed memory access.
 *
 * @param checker The type checker
 * @param node The aether block AST
 * @return VBIT_TRUE if valid, VBIT_FALSE if error
 */
Seraph_Vbit seraph_checker_check_aether(Seraph_Checker* checker,
                                         Seraph_AST_Node* node);

/*============================================================================
 * Recover Block Checking
 *============================================================================*/

/**
 * @brief Check a recover {} else {} block
 *
 * Validates that:
 * - Try block may produce VOID
 * - Else block handles VOID case
 * - Both branches have compatible types
 *
 * @param checker The type checker
 * @param node The recover block AST
 * @return VBIT_TRUE if valid, VBIT_FALSE if error
 */
Seraph_Vbit seraph_checker_check_recover(Seraph_Checker* checker,
                                          Seraph_AST_Node* node);

/*============================================================================
 * Statement Checking
 *============================================================================*/

/**
 * @brief Type check a statement
 *
 * @param checker The type checker
 * @param stmt The statement AST
 * @return VBIT_TRUE if valid, VBIT_FALSE if error
 */
Seraph_Vbit seraph_checker_check_stmt(Seraph_Checker* checker,
                                       Seraph_AST_Node* stmt);

/**
 * @brief Type check a block
 *
 * @param checker The type checker
 * @param block The block AST
 * @return The type of the block's expression, or unit type
 */
Seraph_Type* seraph_checker_check_block(Seraph_Checker* checker,
                                         Seraph_AST_Node* block);

/*============================================================================
 * Diagnostics
 *============================================================================*/

/**
 * @brief Report a type error
 *
 * @param checker The type checker
 * @param loc Source location
 * @param format Printf-style format string
 */
void seraph_checker_error(Seraph_Checker* checker,
                           Seraph_Source_Loc loc,
                           const char* format, ...);

/**
 * @brief Report a type warning
 *
 * @param checker The type checker
 * @param loc Source location
 * @param format Printf-style format string
 */
void seraph_checker_warning(Seraph_Checker* checker,
                             Seraph_Source_Loc loc,
                             const char* format, ...);

/**
 * @brief Report a type mismatch error
 *
 * @param checker The type checker
 * @param loc Source location
 * @param expected Expected type
 * @param actual Actual type
 */
void seraph_checker_type_mismatch(Seraph_Checker* checker,
                                   Seraph_Source_Loc loc,
                                   Seraph_Type* expected,
                                   Seraph_Type* actual);

/**
 * @brief Check if checker has errors
 */
SERAPH_INLINE int seraph_checker_has_errors(const Seraph_Checker* checker) {
    return checker != NULL && checker->error_count > 0;
}

/**
 * @brief Print all diagnostics to stderr
 *
 * @param checker The type checker
 */
void seraph_checker_print_diagnostics(const Seraph_Checker* checker);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_CHECKER_H */
