/**
 * @file ast_to_ir.h
 * @brief AST to Celestial IR Converter
 *
 * This module bridges the Seraphim frontend (AST) with the backend (Celestial IR).
 * It converts the parsed AST into Celestial IR suitable for optimization and code
 * generation.
 *
 * The converter maintains a symbol table for tracking variables, handles
 * expression evaluation, and generates proper VOID-aware code following
 * SERAPH semantics.
 */

#ifndef SERAPH_SERAPHIM_AST_TO_IR_H
#define SERAPH_SERAPHIM_AST_TO_IR_H

#include "ast.h"
#include "celestial_ir.h"
#include "types.h"
#include "seraph/arena.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Symbol Table
 *============================================================================*/

/**
 * @brief A symbol in the symbol table
 */
typedef struct IR_Symbol {
    const char*           name;       /**< Symbol name */
    size_t                name_len;   /**< Name length */
    Celestial_Value*      value;      /**< IR value (vreg, param, or global) */
    Celestial_Type*       type;       /**< Symbol type */
    uint32_t              is_mutable; /**< Can be modified */
    struct IR_Symbol*     next;       /**< Next symbol in scope */
} IR_Symbol;

/**
 * @brief A scope in the symbol table
 */
typedef struct IR_Scope {
    IR_Symbol*            symbols;    /**< Head of symbol chain */
    struct IR_Scope*      parent;     /**< Parent scope */
} IR_Scope;

/**
 * @brief IR generation context
 */
typedef struct IR_Context {
    Celestial_Module*     module;         /**< Current module */
    Celestial_Function*   function;       /**< Current function */
    Celestial_Block*      current_block;  /**< Current basic block */
    Celestial_Builder     builder;        /**< Instruction builder */

    IR_Scope*             scope;          /**< Current scope */
    Seraph_Arena*         arena;          /**< Memory arena */
    Seraph_Type_Context*  types;          /**< Type context */

    /* Control flow targets */
    Celestial_Block*      break_target;   /**< Target for break */
    Celestial_Block*      continue_target;/**< Target for continue */

    /* Function return handling */
    Celestial_Type*       return_type;    /**< Function return type */
    Celestial_Block*      exit_block;     /**< Exit block for unified returns */
    Celestial_Value*      return_slot;    /**< Stack slot for return value */

    /* Struct type registry (for method self parameter type lookup) */
    Celestial_Type**      struct_types;   /**< Array of struct types */
    const char**          struct_names;   /**< Array of struct names */
    size_t                struct_count;   /**< Number of registered structs */
    size_t                struct_capacity;/**< Capacity of struct arrays */

    /* Enum variant registry (for enum variant value lookup) */
    const char**          enum_variant_names;   /**< Variant names */
    size_t*               enum_variant_name_lens;/**< Variant name lengths */
    int64_t*              enum_variant_values;  /**< Discriminant values */
    size_t                enum_variant_count;   /**< Number of variants */
    size_t                enum_variant_capacity;/**< Capacity */

    /* Error tracking */
    int                   has_error;      /**< Error flag */
    const char*           error_msg;      /**< Error message */

    /* Statistics */
    uint32_t              temp_counter;   /**< Temporary name counter */
} IR_Context;

/*============================================================================
 * Context Management
 *============================================================================*/

/**
 * @brief Initialize an IR context
 */
Seraph_Vbit ir_context_init(IR_Context* ctx, Seraph_Arena* arena,
                            Seraph_Type_Context* types);

/**
 * @brief Clean up an IR context
 */
void ir_context_cleanup(IR_Context* ctx);

/**
 * @brief Push a new scope
 */
void ir_scope_push(IR_Context* ctx);

/**
 * @brief Pop the current scope
 */
void ir_scope_pop(IR_Context* ctx);

/**
 * @brief Add a symbol to the current scope
 */
Seraph_Vbit ir_symbol_add(IR_Context* ctx, const char* name, size_t name_len,
                          Celestial_Value* value, Celestial_Type* type,
                          int is_mutable);

/**
 * @brief Look up a symbol by name
 */
IR_Symbol* ir_symbol_lookup(IR_Context* ctx, const char* name, size_t name_len);

/*============================================================================
 * Main Conversion Interface
 *============================================================================*/

/**
 * @brief Convert an AST module to Celestial IR
 *
 * This is the main entry point for AST to IR conversion.
 *
 * @param module_ast The AST module to convert
 * @param types Type context (from semantic analysis)
 * @param arena Memory arena for allocations
 * @return The generated IR module, or NULL on error
 */
Celestial_Module* ir_convert_module(Seraph_AST_Node* module_ast,
                                     Seraph_Type_Context* types,
                                     Seraph_Arena* arena);

/*============================================================================
 * Declaration Conversion
 *============================================================================*/

/**
 * @brief Convert a function declaration
 */
Celestial_Function* ir_convert_fn_decl(IR_Context* ctx,
                                        Seraph_AST_Node* fn_decl);

/**
 * @brief Convert a let/const declaration
 */
Seraph_Vbit ir_convert_let_decl(IR_Context* ctx, Seraph_AST_Node* let_decl);

/**
 * @brief Convert a struct declaration
 */
Celestial_Type* ir_convert_struct_decl(IR_Context* ctx,
                                        Seraph_AST_Node* struct_decl);

/*============================================================================
 * Expression Conversion
 *============================================================================*/

/**
 * @brief Convert an expression to IR, producing a value
 */
Celestial_Value* ir_convert_expr(IR_Context* ctx, Seraph_AST_Node* expr);

/**
 * @brief Convert a binary expression
 */
Celestial_Value* ir_convert_binary(IR_Context* ctx, Seraph_AST_Node* binary);

/**
 * @brief Convert a unary expression
 */
Celestial_Value* ir_convert_unary(IR_Context* ctx, Seraph_AST_Node* unary);

/**
 * @brief Convert a function call
 */
Celestial_Value* ir_convert_call(IR_Context* ctx, Seraph_AST_Node* call);

/**
 * @brief Convert an if expression
 */
Celestial_Value* ir_convert_if_expr(IR_Context* ctx, Seraph_AST_Node* if_expr);

/**
 * @brief Convert a block expression
 */
Celestial_Value* ir_convert_block(IR_Context* ctx, Seraph_AST_Node* block);

/**
 * @brief Convert VOID propagation (??)
 */
Celestial_Value* ir_convert_void_prop(IR_Context* ctx,
                                       Seraph_AST_Node* void_prop);

/**
 * @brief Convert VOID assertion (!!)
 */
Celestial_Value* ir_convert_void_assert(IR_Context* ctx,
                                         Seraph_AST_Node* void_assert);

/*============================================================================
 * Statement Conversion
 *============================================================================*/

/**
 * @brief Convert a statement to IR
 */
void ir_convert_stmt(IR_Context* ctx, Seraph_AST_Node* stmt);

/**
 * @brief Convert a return statement
 */
void ir_convert_return(IR_Context* ctx, Seraph_AST_Node* ret_stmt);

/**
 * @brief Convert a for loop
 */
void ir_convert_for(IR_Context* ctx, Seraph_AST_Node* for_stmt);

/**
 * @brief Convert a while loop
 */
void ir_convert_while(IR_Context* ctx, Seraph_AST_Node* while_stmt);

/**
 * @brief Convert a substrate block (persist/aether)
 */
void ir_convert_substrate_block(IR_Context* ctx, Seraph_AST_Node* block,
                                 Celestial_Substrate_Kind kind);

/*============================================================================
 * Type Conversion
 *============================================================================*/

/**
 * @brief Convert an AST type to Celestial IR type
 */
Celestial_Type* ir_convert_type(IR_Context* ctx, Seraph_AST_Node* type_node);

/**
 * @brief Get the IR type for a primitive token type
 */
Celestial_Type* ir_type_from_primitive(IR_Context* ctx,
                                        Seraph_Token_Type prim);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_AST_TO_IR_H */
