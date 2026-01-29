/**
 * @file proofs.h
 * @brief Seraphim Compiler - Proof Generation Interface
 *
 * MC26: Seraphim Language Proof Generation
 *
 * Generates compile-time proofs for various safety properties:
 * - Bounds checking: Array accesses are within bounds
 * - VOID handling: All VOID values are properly handled
 * - Effect verification: Effects match declarations
 * - Permission checking: Capability permissions are valid
 * - Generation validity: Temporal safety via generations
 * - Substrate access: Substrate references are valid
 *
 * These proofs can be embedded in the compiled output for:
 * - Runtime verification (debug builds)
 * - Documentation (explaining safety guarantees)
 * - Formal verification tools
 */

#ifndef SERAPH_SERAPHIM_PROOFS_H
#define SERAPH_SERAPHIM_PROOFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "seraph/seraphim/ast.h"
#include "seraph/seraphim/types.h"
#include "seraph/arena.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Proof Kind Enumeration
 *============================================================================*/

/**
 * @brief Kind of compile-time proof
 */
typedef enum {
    SERAPH_PROOF_BOUNDS       = 0x01,   /**< Array bounds checked */
    SERAPH_PROOF_VOID         = 0x02,   /**< VOID value handled */
    SERAPH_PROOF_EFFECT       = 0x03,   /**< Effects verified */
    SERAPH_PROOF_PERMISSION   = 0x04,   /**< Capability permissions valid */
    SERAPH_PROOF_GENERATION   = 0x05,   /**< Generation (temporal) valid */
    SERAPH_PROOF_SUBSTRATE    = 0x06,   /**< Substrate access valid */
    SERAPH_PROOF_TYPE         = 0x07,   /**< Type safety verified */
    SERAPH_PROOF_INIT         = 0x08,   /**< Variable initialized */
    SERAPH_PROOF_OVERFLOW     = 0x09,   /**< Arithmetic overflow checked */
    SERAPH_PROOF_NULL         = 0x0A,   /**< Null pointer checked */
    SERAPH_PROOF_INVARIANT    = 0x0B,   /**< Loop/data invariant */
    SERAPH_PROOF_TERMINATION  = 0x0C,   /**< Loop termination */
    SERAPH_PROOF_VOID_KIND    = 0xFF,   /**< Invalid/unknown proof */
} Seraph_Proof_Kind;

/*============================================================================
 * Proof Status
 *============================================================================*/

/**
 * @brief Status of a proof
 */
typedef enum {
    SERAPH_PROOF_STATUS_PROVEN     = 0x01,  /**< Statically verified */
    SERAPH_PROOF_STATUS_ASSUMED    = 0x02,  /**< Assumed true (precondition) */
    SERAPH_PROOF_STATUS_RUNTIME    = 0x03,  /**< Requires runtime check */
    SERAPH_PROOF_STATUS_FAILED     = 0x04,  /**< Could not prove */
    SERAPH_PROOF_STATUS_SKIPPED    = 0x05,  /**< Not checked */
} Seraph_Proof_Status;

/*============================================================================
 * Proof Structure
 *============================================================================*/

/**
 * @brief A single compile-time proof
 */
typedef struct Seraph_Proof {
    Seraph_Proof_Kind kind;             /**< What property is proven */
    Seraph_Proof_Status status;         /**< Proof status */
    Seraph_Source_Loc loc;              /**< Source location */
    const char* description;            /**< Human-readable description */
    uint64_t metadata;                  /**< Kind-specific metadata */

    /* For bounds proofs */
    struct {
        uint64_t array_size;            /**< Size of array (if known) */
        uint64_t index_min;             /**< Minimum index value */
        uint64_t index_max;             /**< Maximum index value */
    } bounds;

    /* For effect proofs */
    struct {
        uint32_t required_effects;      /**< Effects operation requires */
        uint32_t allowed_effects;       /**< Effects function allows */
    } effects;

    /* For permission proofs */
    struct {
        uint8_t required_perms;         /**< Permissions operation requires */
        uint8_t granted_perms;          /**< Permissions capability grants */
    } permissions;

    struct Seraph_Proof* next;          /**< Linked list */
} Seraph_Proof;

/*============================================================================
 * Proof Table
 *============================================================================*/

/**
 * @brief Collection of proofs for a module
 */
typedef struct {
    Seraph_Proof* proofs;               /**< Linked list of proofs */
    size_t count;                       /**< Number of proofs */
    size_t proven_count;                /**< Number successfully proven */
    size_t runtime_count;               /**< Number requiring runtime checks */
    size_t failed_count;                /**< Number that failed */
    Seraph_Arena* arena;                /**< Arena for allocations */
} Seraph_Proof_Table;

/*============================================================================
 * Proof Table Operations
 *============================================================================*/

/**
 * @brief Initialize a proof table
 *
 * @param table Table to initialize
 * @param arena Arena for allocations
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_proof_table_init(Seraph_Proof_Table* table, Seraph_Arena* arena);

/**
 * @brief Add a proof to the table
 *
 * @param table The proof table
 * @param proof Proof to add (will be copied)
 */
void seraph_proof_add(Seraph_Proof_Table* table, Seraph_Proof proof);

/**
 * @brief Add a bounds proof
 *
 * @param table The proof table
 * @param loc Source location
 * @param array_size Size of array (0 if unknown)
 * @param index_min Minimum index value
 * @param index_max Maximum index value
 * @param status Proof status
 */
void seraph_proof_add_bounds(Seraph_Proof_Table* table,
                              Seraph_Source_Loc loc,
                              uint64_t array_size,
                              uint64_t index_min,
                              uint64_t index_max,
                              Seraph_Proof_Status status);

/**
 * @brief Add a VOID handling proof
 *
 * @param table The proof table
 * @param loc Source location
 * @param description How VOID is handled
 * @param status Proof status
 */
void seraph_proof_add_void(Seraph_Proof_Table* table,
                            Seraph_Source_Loc loc,
                            const char* description,
                            Seraph_Proof_Status status);

/**
 * @brief Add an effect proof
 *
 * @param table The proof table
 * @param loc Source location
 * @param required Required effects
 * @param allowed Allowed effects
 * @param status Proof status
 */
void seraph_proof_add_effect(Seraph_Proof_Table* table,
                              Seraph_Source_Loc loc,
                              uint32_t required,
                              uint32_t allowed,
                              Seraph_Proof_Status status);

/**
 * @brief Add a permission proof
 *
 * @param table The proof table
 * @param loc Source location
 * @param required Required permissions
 * @param granted Granted permissions
 * @param status Proof status
 */
void seraph_proof_add_permission(Seraph_Proof_Table* table,
                                  Seraph_Source_Loc loc,
                                  uint8_t required,
                                  uint8_t granted,
                                  Seraph_Proof_Status status);

/**
 * @brief Add a type safety proof
 *
 * @param table The proof table
 * @param loc Source location
 * @param description Type constraint description
 * @param status Proof status
 */
void seraph_proof_add_type(Seraph_Proof_Table* table,
                            Seraph_Source_Loc loc,
                            const char* description,
                            Seraph_Proof_Status status);

/*============================================================================
 * Proof Generation
 *============================================================================*/

/**
 * @brief Generate proofs for a module
 *
 * Analyzes the AST and generates all applicable proofs.
 *
 * @param table The proof table to populate
 * @param module The AST module to analyze
 */
void seraph_proof_generate(Seraph_Proof_Table* table, Seraph_AST_Node* module);

/**
 * @brief Generate proofs for a function
 *
 * @param table The proof table
 * @param fn_decl Function declaration AST
 */
void seraph_proof_generate_fn(Seraph_Proof_Table* table, Seraph_AST_Node* fn_decl);

/**
 * @brief Generate proofs for an expression
 *
 * @param table The proof table
 * @param expr Expression AST
 */
void seraph_proof_generate_expr(Seraph_Proof_Table* table, Seraph_AST_Node* expr);

/**
 * @brief Generate proofs for a statement
 *
 * @param table The proof table
 * @param stmt Statement AST
 */
void seraph_proof_generate_stmt(Seraph_Proof_Table* table, Seraph_AST_Node* stmt);

/*============================================================================
 * Proof Verification
 *============================================================================*/

/**
 * @brief Verify all proofs in the table
 *
 * @param table The proof table
 * @return 1 if all proofs pass, 0 if any failed
 */
int seraph_proof_verify_all(const Seraph_Proof_Table* table);

/**
 * @brief Count proofs by status
 *
 * @param table The proof table
 * @param status Status to count
 * @return Number of proofs with given status
 */
size_t seraph_proof_count_by_status(const Seraph_Proof_Table* table,
                                     Seraph_Proof_Status status);

/**
 * @brief Count proofs by kind
 *
 * @param table The proof table
 * @param kind Kind to count
 * @return Number of proofs of given kind
 */
size_t seraph_proof_count_by_kind(const Seraph_Proof_Table* table,
                                   Seraph_Proof_Kind kind);

/*============================================================================
 * Proof Output
 *============================================================================*/

/**
 * @brief Print all proofs to stderr
 *
 * @param table The proof table
 */
void seraph_proof_print_all(const Seraph_Proof_Table* table);

/**
 * @brief Print proof summary statistics
 *
 * @param table The proof table
 */
void seraph_proof_print_summary(const Seraph_Proof_Table* table);

/**
 * @brief Generate proof annotations as C comments
 *
 * @param table The proof table
 * @param output File to write to
 */
void seraph_proof_emit_comments(const Seraph_Proof_Table* table, FILE* output);

/**
 * @brief Get the name of a proof kind
 */
const char* seraph_proof_kind_name(Seraph_Proof_Kind kind);

/**
 * @brief Get the name of a proof status
 */
const char* seraph_proof_status_name(Seraph_Proof_Status status);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_PROOFS_H */
