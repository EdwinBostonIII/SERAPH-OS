/**
 * @file codegen.h
 * @brief Seraphim Compiler - C Code Generator Interface
 *
 * MC26: Seraphim Language Code Generator
 *
 * Generates C code from the Seraphim AST. The generated C code uses
 * SERAPH runtime primitives for:
 * - VOID handling (SERAPH_VOID_*, SERAPH_IS_VOID)
 * - Substrate access (Atlas transactions, Aether contexts)
 * - Effect tracking (compile-time verified, runtime assertions)
 *
 * Code Generation Patterns:
 *
 * VOID literal:
 *   VOID  ->  SERAPH_VOID_U64
 *
 * ?? operator (propagation):
 *   expr??  ->  ({ typeof(expr) __tmp = (expr);
 *                  if (SERAPH_IS_VOID(__tmp)) return SERAPH_VOID_<TYPE>;
 *                  __tmp; })
 *
 * !! operator (assertion):
 *   expr!!  ->  ({ typeof(expr) __tmp = (expr);
 *                  if (SERAPH_IS_VOID(__tmp)) seraph_panic("VOID assertion failed");
 *                  __tmp; })
 *
 * persist {} block:
 *   persist { body }  ->
 *     { Seraph_Atlas_Transaction* __tx = seraph_atlas_begin(&__atlas);
 *       <body>
 *       seraph_atlas_commit(&__atlas, __tx); }
 *
 * recover {} else {}:
 *   recover { try } else { else }  ->
 *     if (!setjmp(__recover_buf)) { <try> } else { <else> }
 */

#ifndef SERAPH_SERAPHIM_CODEGEN_H
#define SERAPH_SERAPHIM_CODEGEN_H

#include <stdio.h>
#include <stdint.h>
#include "seraph/seraphim/ast.h"
#include "seraph/seraphim/proofs.h"
#include "seraph/seraphim/types.h"
#include "seraph/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Code Generator Options
 *============================================================================*/

/**
 * @brief Code generation options
 */
typedef enum {
    SERAPH_CODEGEN_OPT_NONE          = 0,
    SERAPH_CODEGEN_OPT_DEBUG         = (1 << 0),   /**< Include debug info */
    SERAPH_CODEGEN_OPT_PROOFS        = (1 << 1),   /**< Embed proof comments */
    SERAPH_CODEGEN_OPT_RUNTIME_CHECK = (1 << 2),   /**< Add runtime checks */
    SERAPH_CODEGEN_OPT_OPTIMIZE      = (1 << 3),   /**< Enable optimizations */
    SERAPH_CODEGEN_OPT_LINE_DIRECTIVES = (1 << 4), /**< Emit #line directives */
} Seraph_Codegen_Options;

/*============================================================================
 * Code Generator Context
 *============================================================================*/

/**
 * @brief Code generator context
 */
typedef struct {
    FILE* output;                       /**< Output file */
    Seraph_Arena* arena;                /**< Arena for temp allocations */
    int indent_level;                   /**< Current indentation */
    const Seraph_Proof_Table* proofs;   /**< Proof table for embedding */
    Seraph_Type_Context* types;         /**< Type information */
    Seraph_Codegen_Options options;     /**< Generation options */

    /* Counters for unique names */
    uint32_t temp_counter;              /**< For __tmp_N variables */
    uint32_t label_counter;             /**< For __label_N labels */
    uint32_t recover_counter;           /**< For recover block IDs */

    /* Current function context */
    const char* current_fn_name;        /**< Current function name */
    size_t current_fn_name_len;
    int in_expression;                  /**< Inside expression context? */
    int in_recover;                     /**< Inside recover block? */
    int recover_depth;                  /**< Nested recover depth */
} Seraph_Codegen;

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * @brief Initialize code generator
 *
 * @param gen Generator to initialize
 * @param output Output file
 * @param arena Arena for allocations
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_codegen_init(Seraph_Codegen* gen,
                                 FILE* output,
                                 Seraph_Arena* arena);

/**
 * @brief Set proof table for embedding
 *
 * @param gen The code generator
 * @param proofs Proof table to embed
 */
void seraph_codegen_set_proofs(Seraph_Codegen* gen,
                                const Seraph_Proof_Table* proofs);

/**
 * @brief Set type context
 *
 * @param gen The code generator
 * @param types Type context
 */
void seraph_codegen_set_types(Seraph_Codegen* gen,
                               Seraph_Type_Context* types);

/**
 * @brief Set code generation options
 *
 * @param gen The code generator
 * @param options Options flags
 */
void seraph_codegen_set_options(Seraph_Codegen* gen,
                                 Seraph_Codegen_Options options);

/*============================================================================
 * Code Generation Entry Points
 *============================================================================*/

/**
 * @brief Generate C code for a complete module
 *
 * Outputs a complete C translation unit.
 *
 * @param gen The code generator
 * @param module The AST module
 */
void seraph_codegen_module(Seraph_Codegen* gen, Seraph_AST_Node* module);

/**
 * @brief Generate preamble (includes, macros, etc.)
 *
 * @param gen The code generator
 */
void seraph_codegen_preamble(Seraph_Codegen* gen);

/**
 * @brief Generate forward declarations
 *
 * @param gen The code generator
 * @param module The module to scan for declarations
 */
void seraph_codegen_forward_decls(Seraph_Codegen* gen, Seraph_AST_Node* module);

/*============================================================================
 * Declaration Generation
 *============================================================================*/

/**
 * @brief Generate C code for a function declaration
 *
 * @param gen The code generator
 * @param fn_decl The function declaration AST
 */
void seraph_codegen_fn_decl(Seraph_Codegen* gen, Seraph_AST_Node* fn_decl);

/**
 * @brief Generate C code for a struct declaration
 *
 * @param gen The code generator
 * @param struct_decl The struct declaration AST
 */
void seraph_codegen_struct_decl(Seraph_Codegen* gen, Seraph_AST_Node* struct_decl);

/**
 * @brief Generate C code for an enum declaration
 *
 * @param gen The code generator
 * @param enum_decl The enum declaration AST
 */
void seraph_codegen_enum_decl(Seraph_Codegen* gen, Seraph_AST_Node* enum_decl);

/*============================================================================
 * Expression Generation
 *============================================================================*/

/**
 * @brief Generate C code for an expression
 *
 * @param gen The code generator
 * @param expr The expression AST
 */
void seraph_codegen_expr(Seraph_Codegen* gen, Seraph_AST_Node* expr);

/**
 * @brief Generate C code for VOID propagation (??)
 *
 * @param gen The code generator
 * @param node The void propagation AST
 */
void seraph_codegen_void_prop(Seraph_Codegen* gen, Seraph_AST_Node* node);

/**
 * @brief Generate C code for VOID assertion (!!)
 *
 * @param gen The code generator
 * @param node The void assertion AST
 */
void seraph_codegen_void_assert(Seraph_Codegen* gen, Seraph_AST_Node* node);

/*============================================================================
 * Statement Generation
 *============================================================================*/

/**
 * @brief Generate C code for a statement
 *
 * @param gen The code generator
 * @param stmt The statement AST
 */
void seraph_codegen_stmt(Seraph_Codegen* gen, Seraph_AST_Node* stmt);

/**
 * @brief Generate C code for a block
 *
 * @param gen The code generator
 * @param block The block AST
 */
void seraph_codegen_block(Seraph_Codegen* gen, Seraph_AST_Node* block);

/**
 * @brief Generate C code for a persist {} block
 *
 * @param gen The code generator
 * @param node The persist block AST
 */
void seraph_codegen_persist(Seraph_Codegen* gen, Seraph_AST_Node* node);

/**
 * @brief Generate C code for an aether {} block
 *
 * @param gen The code generator
 * @param node The aether block AST
 */
void seraph_codegen_aether(Seraph_Codegen* gen, Seraph_AST_Node* node);

/**
 * @brief Generate C code for a recover {} else {} block
 *
 * @param gen The code generator
 * @param node The recover block AST
 */
void seraph_codegen_recover(Seraph_Codegen* gen, Seraph_AST_Node* node);

/*============================================================================
 * Type Generation
 *============================================================================*/

/**
 * @brief Generate C type for a Seraphim type
 *
 * @param gen The code generator
 * @param type_node The type AST node
 */
void seraph_codegen_type(Seraph_Codegen* gen, Seraph_AST_Node* type_node);

/**
 * @brief Get C type string for primitive type token
 *
 * @param tok_type Token type (SERAPH_TOK_U32, etc.)
 * @return C type string
 */
const char* seraph_codegen_prim_type_str(Seraph_Token_Type tok_type);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Write indentation
 *
 * @param gen The code generator
 */
void seraph_codegen_indent(Seraph_Codegen* gen);

/**
 * @brief Increase indentation level
 */
void seraph_codegen_indent_inc(Seraph_Codegen* gen);

/**
 * @brief Decrease indentation level
 */
void seraph_codegen_indent_dec(Seraph_Codegen* gen);

/**
 * @brief Write a line with indentation
 *
 * @param gen The code generator
 * @param format Printf-style format
 */
void seraph_codegen_writeln(Seraph_Codegen* gen, const char* format, ...);

/**
 * @brief Write without newline
 *
 * @param gen The code generator
 * @param format Printf-style format
 */
void seraph_codegen_write(Seraph_Codegen* gen, const char* format, ...);

/**
 * @brief Emit #line directive
 *
 * @param gen The code generator
 * @param loc Source location
 */
void seraph_codegen_line_directive(Seraph_Codegen* gen, Seraph_Source_Loc loc);

/**
 * @brief Get unique temporary variable name
 *
 * @param gen The code generator
 * @param buf Buffer to write name
 * @param buf_size Buffer size
 * @return Length written
 */
size_t seraph_codegen_temp_name(Seraph_Codegen* gen, char* buf, size_t buf_size);

/**
 * @brief Get unique label name
 *
 * @param gen The code generator
 * @param buf Buffer to write name
 * @param buf_size Buffer size
 * @return Length written
 */
size_t seraph_codegen_label_name(Seraph_Codegen* gen, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_CODEGEN_H */
