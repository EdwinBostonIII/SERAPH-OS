/**
 * @file types.h
 * @brief Seraphim Compiler - Type System Interface
 *
 * MC26: Seraphim Language Type System
 *
 * The type system defines all types in the Seraphim language and provides
 * type checking, inference, and unification.
 *
 * Type Categories:
 * - Primitives: u8, u16, u32, u64, i8, i16, i32, i64, bool, char
 * - Numeric: scalar (Q32.32), dual (128-bit AD), galactic (256-bit AD)
 * - Composite: arrays, slices, tuples, structs, enums
 * - References: &T, &mut T, &volatile T, &atlas T, &aether T
 * - VOID-able: ??T (type that may be VOID)
 * - Functions: fn(A, B) -> R
 * - Type variables: for polymorphism
 *
 * Key Features:
 * - VOID-aware: The type system understands ??T types
 * - Substrate-aware: References track their substrate (volatile/atlas/aether)
 * - Effect-aware: Function types include effect annotations
 */

#ifndef SERAPH_SERAPHIM_TYPES_H
#define SERAPH_SERAPHIM_TYPES_H

#include "../arena.h"
#include "../vbit.h"
#include "token.h"
#include "ast.h"

/*============================================================================
 * Type Kind Enumeration
 *============================================================================*/

/**
 * @brief Kind of type in the type system
 */
typedef enum {
    /* Error type */
    SERAPH_TYPE_VOID        = 0xFF,     /**< Error/unknown type */

    /* Primitive types */
    SERAPH_TYPE_U8          = 0x00,     /**< Unsigned 8-bit */
    SERAPH_TYPE_U16         = 0x01,     /**< Unsigned 16-bit */
    SERAPH_TYPE_U32         = 0x02,     /**< Unsigned 32-bit */
    SERAPH_TYPE_U64         = 0x03,     /**< Unsigned 64-bit */
    SERAPH_TYPE_I8          = 0x04,     /**< Signed 8-bit */
    SERAPH_TYPE_I16         = 0x05,     /**< Signed 16-bit */
    SERAPH_TYPE_I32         = 0x06,     /**< Signed 32-bit */
    SERAPH_TYPE_I64         = 0x07,     /**< Signed 64-bit */
    SERAPH_TYPE_BOOL        = 0x08,     /**< Boolean */
    SERAPH_TYPE_CHAR        = 0x09,     /**< Unicode char */
    SERAPH_TYPE_UNIT        = 0x0A,     /**< Unit type () */

    /* Numeric types */
    SERAPH_TYPE_SCALAR      = 0x10,     /**< Q32.32 fixed-point */
    SERAPH_TYPE_DUAL        = 0x11,     /**< 128-bit dual number */
    SERAPH_TYPE_GALACTIC    = 0x12,     /**< 256-bit galactic number */

    /* Composite types */
    SERAPH_TYPE_ARRAY       = 0x20,     /**< [T; N] fixed-size array */
    SERAPH_TYPE_SLICE       = 0x21,     /**< [T] slice */
    SERAPH_TYPE_TUPLE       = 0x22,     /**< (A, B, C) tuple */
    SERAPH_TYPE_STRUCT      = 0x23,     /**< Named struct type */
    SERAPH_TYPE_ENUM        = 0x24,     /**< Named enum type */

    /* Reference types */
    SERAPH_TYPE_REF         = 0x30,     /**< &T immutable reference */
    SERAPH_TYPE_REF_MUT     = 0x31,     /**< &mut T mutable reference */

    /* Special types */
    SERAPH_TYPE_VOIDABLE    = 0x40,     /**< ??T VOID-able type */
    SERAPH_TYPE_FN          = 0x41,     /**< Function type */
    SERAPH_TYPE_TYPEVAR     = 0x42,     /**< Type variable (polymorphism) */
    SERAPH_TYPE_NEVER       = 0x43,     /**< Never type (diverges) */

} Seraph_Type_Kind;

/*============================================================================
 * Substrate (Memory Location)
 *============================================================================*/

/**
 * @brief Where a reference points to
 */
typedef enum {
    SERAPH_SUBSTRATE_VOLATILE   = 0,    /**< Normal RAM (default) */
    SERAPH_SUBSTRATE_ATLAS      = 1,    /**< Atlas persistent storage */
    SERAPH_SUBSTRATE_AETHER     = 2,    /**< Aether distributed memory */
} Seraph_Substrate;

/*============================================================================
 * Effect Flags
 *============================================================================*/

/**
 * @brief Effect flags for function types
 *
 * Effects are tracked at compile time to ensure safety.
 */
typedef enum {
    SERAPH_EFFECT_NONE      = 0x00,     /**< Pure - no effects */
    SERAPH_EFFECT_VOID      = 0x01,     /**< May produce VOID */
    SERAPH_EFFECT_PERSIST   = 0x02,     /**< Accesses persistent storage */
    SERAPH_EFFECT_NETWORK   = 0x04,     /**< Accesses network */
    SERAPH_EFFECT_TIMER     = 0x08,     /**< Uses timers */
    SERAPH_EFFECT_IO        = 0x10,     /**< General I/O */
    SERAPH_EFFECT_ALL       = 0xFF,     /**< All effects (unsafe) */
} Seraph_Effect_Flags;

/*============================================================================
 * Type Structure
 *============================================================================*/

/**
 * @brief A type in the Seraphim type system
 *
 * Types are allocated from the arena and form a DAG structure.
 */
typedef struct Seraph_Type {
    Seraph_Type_Kind kind;              /**< What kind of type */

    union {
        /* Array: [T; N] */
        struct {
            struct Seraph_Type* elem;   /**< Element type */
            uint64_t size;              /**< Number of elements */
        } array;

        /* Slice: [T] */
        struct {
            struct Seraph_Type* elem;   /**< Element type */
        } slice;

        /* Tuple: (A, B, C) */
        struct {
            struct Seraph_Type** elems; /**< Element types */
            size_t count;               /**< Number of elements */
        } tuple;

        /* Struct/Enum reference */
        struct {
            const char* name;           /**< Type name */
            size_t name_len;
            Seraph_AST_Node* decl;      /**< Declaration AST node */
        } named;

        /* Reference: &T or &mut T */
        struct {
            struct Seraph_Type* inner;  /**< Referenced type */
            Seraph_Substrate substrate; /**< Where it points */
            int is_mut;                 /**< Mutable reference? */
        } ref;

        /* VOID-able: ??T */
        struct {
            struct Seraph_Type* inner;  /**< Underlying type */
        } voidable;

        /* Function: fn(A, B) -> R */
        struct {
            struct Seraph_Type** params;/**< Parameter types */
            size_t param_count;
            struct Seraph_Type* ret;    /**< Return type */
            Seraph_Effect_Flags effects;/**< Effect annotations */
        } fn;

        /* Type variable: 'T */
        struct {
            uint32_t id;                /**< Unique ID for this variable */
            const char* name;           /**< Name if any */
            size_t name_len;
            struct Seraph_Type* bound;  /**< Unified to this type (or NULL) */
        } typevar;
    };

} Seraph_Type;

/*============================================================================
 * Type Context (Type Checker State)
 *============================================================================*/

/**
 * @brief Type checking diagnostic
 */
typedef struct Seraph_Type_Diag {
    Seraph_Source_Loc loc;              /**< Location of error */
    const char* message;                /**< Error message */
    struct Seraph_Type* expected;       /**< Expected type (if applicable) */
    struct Seraph_Type* actual;         /**< Actual type (if applicable) */
    struct Seraph_Type_Diag* next;      /**< Next diagnostic */
} Seraph_Type_Diag;

/**
 * @brief Symbol table entry
 */
typedef struct Seraph_Symbol {
    const char* name;                   /**< Symbol name */
    size_t name_len;
    Seraph_Type* type;                  /**< Symbol's type */
    Seraph_AST_Node* decl;              /**< Declaration node */
    int is_mut;                         /**< Mutable binding? */
    struct Seraph_Symbol* next;         /**< Next in scope chain */
} Seraph_Symbol;

/**
 * @brief Scope for symbol lookup
 */
typedef struct Seraph_Scope {
    Seraph_Symbol* symbols;             /**< Symbols in this scope */
    struct Seraph_Scope* parent;        /**< Enclosing scope */
} Seraph_Scope;

/**
 * @brief Type checking context
 */
typedef struct {
    Seraph_Arena* arena;                /**< Arena for allocations */

    /* Symbol tables */
    Seraph_Scope* scope;                /**< Current scope */
    Seraph_Scope* global;               /**< Global scope */

    /* Type inference */
    uint32_t next_typevar_id;           /**< Next type variable ID */

    /* Current function context */
    Seraph_Type* current_fn_ret;        /**< Expected return type */
    Seraph_Effect_Flags allowed_effects;/**< Effects allowed in context */

    /* Diagnostics */
    Seraph_Type_Diag* diagnostics;      /**< Error/warning list */
    int error_count;
    int warning_count;

} Seraph_Type_Context;

/*============================================================================
 * Type Construction
 *============================================================================*/

/**
 * @brief Create a primitive type
 */
Seraph_Type* seraph_type_prim(Seraph_Arena* arena, Seraph_Type_Kind kind);

/**
 * @brief Create an array type
 */
Seraph_Type* seraph_type_array(Seraph_Arena* arena, Seraph_Type* elem, uint64_t size);

/**
 * @brief Create a slice type
 */
Seraph_Type* seraph_type_slice(Seraph_Arena* arena, Seraph_Type* elem);

/**
 * @brief Create a tuple type
 */
Seraph_Type* seraph_type_tuple(Seraph_Arena* arena, Seraph_Type** elems, size_t count);

/**
 * @brief Create a reference type
 */
Seraph_Type* seraph_type_ref(Seraph_Arena* arena, Seraph_Type* inner,
                              int is_mut, Seraph_Substrate substrate);

/**
 * @brief Create a VOID-able type
 */
Seraph_Type* seraph_type_voidable(Seraph_Arena* arena, Seraph_Type* inner);

/**
 * @brief Create a function type
 */
Seraph_Type* seraph_type_fn(Seraph_Arena* arena,
                             Seraph_Type** params, size_t param_count,
                             Seraph_Type* ret, Seraph_Effect_Flags effects);

/**
 * @brief Create a fresh type variable
 */
Seraph_Type* seraph_type_var(Seraph_Type_Context* ctx,
                              const char* name, size_t name_len);

/**
 * @brief Create VOID (error) type
 */
Seraph_Type* seraph_type_void(Seraph_Arena* arena);

/**
 * @brief Create unit type ()
 */
Seraph_Type* seraph_type_unit(Seraph_Arena* arena);

/**
 * @brief Create never type (!)
 */
Seraph_Type* seraph_type_never(Seraph_Arena* arena);

/*============================================================================
 * Type Queries
 *============================================================================*/

/**
 * @brief Check if type is VOID (error)
 */
SERAPH_INLINE int seraph_type_is_void(const Seraph_Type* t) {
    return t == NULL || t->kind == SERAPH_TYPE_VOID;
}

/**
 * @brief Check if type is a primitive integer
 */
int seraph_type_is_integer(const Seraph_Type* t);

/**
 * @brief Check if type is a numeric type (int, float, galactic)
 */
int seraph_type_is_numeric(const Seraph_Type* t);

/**
 * @brief Check if type is a reference
 */
int seraph_type_is_ref(const Seraph_Type* t);

/**
 * @brief Check if type is VOID-able
 */
int seraph_type_is_voidable(const Seraph_Type* t);

/**
 * @brief Check if type is copyable (no move semantics)
 */
int seraph_type_is_copy(const Seraph_Type* t);

/**
 * @brief Get the size of a type in bytes (0 for unsized types)
 */
size_t seraph_type_size(const Seraph_Type* t);

/**
 * @brief Get the alignment of a type in bytes
 */
size_t seraph_type_align(const Seraph_Type* t);

/*============================================================================
 * Type Comparison and Unification
 *============================================================================*/

/**
 * @brief Check if two types are equal
 */
int seraph_type_eq(const Seraph_Type* a, const Seraph_Type* b);

/**
 * @brief Check if type `sub` is a subtype of `super`
 *
 * Subtyping rules:
 * - T <: ??T (non-voidable is subtype of voidable)
 * - &T <: &??T
 * - Covariance in return types, contravariance in parameter types
 */
int seraph_type_subtype(const Seraph_Type* sub, const Seraph_Type* super);

/**
 * @brief Unify two types (for type inference)
 *
 * Returns unified type or VOID on failure.
 * May bind type variables.
 */
Seraph_Type* seraph_type_unify(Seraph_Type_Context* ctx,
                                Seraph_Type* a, Seraph_Type* b);

/**
 * @brief Find the join (least upper bound) of two types
 */
Seraph_Type* seraph_type_join(Seraph_Type_Context* ctx,
                               Seraph_Type* a, Seraph_Type* b);

/*============================================================================
 * Type Context Management
 *============================================================================*/

/**
 * @brief Initialize a type checking context
 */
Seraph_Vbit seraph_type_context_init(Seraph_Type_Context* ctx, Seraph_Arena* arena);

/**
 * @brief Push a new scope
 */
void seraph_type_push_scope(Seraph_Type_Context* ctx);

/**
 * @brief Pop the current scope
 */
void seraph_type_pop_scope(Seraph_Type_Context* ctx);

/**
 * @brief Define a symbol in the current scope
 */
Seraph_Vbit seraph_type_define(Seraph_Type_Context* ctx,
                                const char* name, size_t name_len,
                                Seraph_Type* type, Seraph_AST_Node* decl,
                                int is_mut);

/**
 * @brief Look up a symbol by name
 */
Seraph_Symbol* seraph_type_lookup(Seraph_Type_Context* ctx,
                                   const char* name, size_t name_len);

/*============================================================================
 * Type Checking
 *============================================================================*/

/**
 * @brief Type-check a module
 */
Seraph_Vbit seraph_type_check_module(Seraph_Type_Context* ctx,
                                      Seraph_AST_Node* module);

/**
 * @brief Type-check a declaration
 */
Seraph_Vbit seraph_type_check_decl(Seraph_Type_Context* ctx,
                                    Seraph_AST_Node* decl);

/**
 * @brief Infer and check the type of an expression
 */
Seraph_Type* seraph_type_check_expr(Seraph_Type_Context* ctx,
                                     Seraph_AST_Node* expr);

/**
 * @brief Check that expression has expected type
 */
Seraph_Vbit seraph_type_check_expect(Seraph_Type_Context* ctx,
                                      Seraph_AST_Node* expr,
                                      Seraph_Type* expected);

/**
 * @brief Type-check a statement
 */
Seraph_Vbit seraph_type_check_stmt(Seraph_Type_Context* ctx,
                                    Seraph_AST_Node* stmt);

/**
 * @brief Type-check a block
 */
Seraph_Type* seraph_type_check_block(Seraph_Type_Context* ctx,
                                      Seraph_AST_Node* block);

/*============================================================================
 * Type from AST
 *============================================================================*/

/**
 * @brief Convert an AST type node to a Type
 */
Seraph_Type* seraph_type_from_ast(Seraph_Type_Context* ctx,
                                   Seraph_AST_Node* ast_type);

/**
 * @brief Convert a token to primitive type
 */
Seraph_Type* seraph_type_from_token(Seraph_Arena* arena,
                                     Seraph_Token_Type tok);

/*============================================================================
 * Diagnostics
 *============================================================================*/

/**
 * @brief Report a type error
 */
void seraph_type_error(Seraph_Type_Context* ctx, Seraph_Source_Loc loc,
                        const char* format, ...);

/**
 * @brief Report a type mismatch
 */
void seraph_type_mismatch(Seraph_Type_Context* ctx, Seraph_Source_Loc loc,
                           Seraph_Type* expected, Seraph_Type* actual);

/**
 * @brief Check if context has errors
 */
SERAPH_INLINE int seraph_type_has_errors(const Seraph_Type_Context* ctx) {
    return ctx != NULL && ctx->error_count > 0;
}

/**
 * @brief Print all diagnostics
 */
void seraph_type_print_diagnostics(const Seraph_Type_Context* ctx);

/*============================================================================
 * Type Printing
 *============================================================================*/

/**
 * @brief Print a type to a buffer
 *
 * @param t The type to print
 * @param buf Buffer to write to
 * @param buf_size Size of buffer
 * @return Number of characters written (excluding null)
 */
size_t seraph_type_print(const Seraph_Type* t, char* buf, size_t buf_size);

/**
 * @brief Get name of a type kind
 */
const char* seraph_type_kind_name(Seraph_Type_Kind kind);

#endif /* SERAPH_SERAPHIM_TYPES_H */
