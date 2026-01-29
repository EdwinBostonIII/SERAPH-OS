/**
 * @file ast.h
 * @brief Seraphim Compiler - Abstract Syntax Tree
 *
 * MC26: Seraphim Language AST
 *
 * Defines all AST node types for the Seraphim language.
 * Nodes are arena-allocated for efficient memory management.
 * AST_VOID (0xFF) is used for error nodes.
 */

#ifndef SERAPH_SERAPHIM_AST_H
#define SERAPH_SERAPHIM_AST_H

#include "token.h"
#include "../arena.h"
#include "../vbit.h"

/*============================================================================
 * AST Node Kind
 *============================================================================*/

/**
 * @brief All AST node kinds
 *
 * Uses 0xFF for VOID following SERAPH conventions.
 */
typedef enum {
    /*--------------------------------------------------------------------
     * Error Node
     *--------------------------------------------------------------------*/
    AST_VOID                = 0xFF,  /**< Error/invalid node */

    /*--------------------------------------------------------------------
     * Module Level
     *--------------------------------------------------------------------*/
    AST_MODULE              = 0x00,  /**< Top-level module */

    /*--------------------------------------------------------------------
     * Declarations (0x10-0x1F)
     *--------------------------------------------------------------------*/
    AST_DECL_FN             = 0x10,  /**< Function declaration */
    AST_DECL_LET            = 0x11,  /**< let binding */
    AST_DECL_CONST          = 0x12,  /**< const binding */
    AST_DECL_STRUCT         = 0x13,  /**< struct definition */
    AST_DECL_ENUM           = 0x14,  /**< enum definition */
    AST_DECL_TYPE           = 0x15,  /**< type alias */
    AST_DECL_IMPL           = 0x16,  /**< impl block */
    AST_DECL_USE            = 0x17,  /**< use declaration */
    AST_DECL_FOREIGN        = 0x18,  /**< foreign block */

    /*--------------------------------------------------------------------
     * Expressions (0x20-0x4F)
     *--------------------------------------------------------------------*/
    /* Literals */
    AST_EXPR_INT_LIT        = 0x20,  /**< Integer literal */
    AST_EXPR_FLOAT_LIT      = 0x21,  /**< Float literal */
    AST_EXPR_STRING_LIT     = 0x22,  /**< String literal */
    AST_EXPR_CHAR_LIT       = 0x23,  /**< Character literal */
    AST_EXPR_BOOL_LIT       = 0x24,  /**< Boolean literal */
    AST_EXPR_VOID_LIT       = 0x25,  /**< VOID literal */

    /* References */
    AST_EXPR_IDENT          = 0x28,  /**< Identifier reference */
    AST_EXPR_PATH           = 0x29,  /**< Path (foo::bar::baz) */

    /* Operators */
    AST_EXPR_BINARY         = 0x30,  /**< Binary operation */
    AST_EXPR_UNARY          = 0x31,  /**< Unary operation */
    AST_EXPR_VOID_PROP      = 0x32,  /**< expr?? */
    AST_EXPR_VOID_ASSERT    = 0x33,  /**< expr!! */

    /* Calls and access */
    AST_EXPR_CALL           = 0x38,  /**< Function call */
    AST_EXPR_FIELD          = 0x39,  /**< Field access (expr.field) */
    AST_EXPR_INDEX          = 0x3A,  /**< Index access (expr[index]) */
    AST_EXPR_METHOD_CALL    = 0x3B,  /**< Method call (expr.method()) */

    /* Compound */
    AST_EXPR_BLOCK          = 0x40,  /**< Block expression { ... } */
    AST_EXPR_IF             = 0x41,  /**< If expression */
    AST_EXPR_MATCH          = 0x42,  /**< Match expression */
    AST_EXPR_ARRAY          = 0x43,  /**< Array literal [a, b, c] */
    AST_EXPR_STRUCT_INIT    = 0x44,  /**< Struct initializer Point { x: 1, y: 2 } */
    AST_EXPR_CAST           = 0x45,  /**< Type cast (expr as Type) */
    AST_EXPR_RANGE          = 0x46,  /**< Range (a..b or a..=b) */
    AST_EXPR_CLOSURE        = 0x47,  /**< Closure |x| expr */

    /*--------------------------------------------------------------------
     * Statements (0x50-0x5F)
     *--------------------------------------------------------------------*/
    AST_STMT_EXPR           = 0x50,  /**< Expression statement */
    AST_STMT_RETURN         = 0x51,  /**< return expr; */
    AST_STMT_BREAK          = 0x52,  /**< break; */
    AST_STMT_CONTINUE       = 0x53,  /**< continue; */
    AST_STMT_FOR            = 0x54,  /**< for loop */
    AST_STMT_WHILE          = 0x55,  /**< while loop */

    /* Substrate blocks */
    AST_STMT_PERSIST        = 0x58,  /**< persist { } block */
    AST_STMT_AETHER         = 0x59,  /**< aether { } block */
    AST_STMT_RECOVER        = 0x5A,  /**< recover { } else { } */

    /*--------------------------------------------------------------------
     * Types (0x60-0x6F)
     *--------------------------------------------------------------------*/
    AST_TYPE_PRIMITIVE      = 0x60,  /**< Primitive type (u64, bool, etc.) */
    AST_TYPE_NAMED          = 0x61,  /**< Named type (MyStruct) */
    AST_TYPE_PATH           = 0x62,  /**< Path type (foo::bar::Type) */
    AST_TYPE_ARRAY          = 0x63,  /**< Array type [T; N] */
    AST_TYPE_SLICE          = 0x64,  /**< Slice type [T] */
    AST_TYPE_POINTER        = 0x65,  /**< Pointer type *T */
    AST_TYPE_REF            = 0x66,  /**< Reference type &T */
    AST_TYPE_MUT_REF        = 0x67,  /**< Mutable reference &mut T */
    AST_TYPE_SUBSTRATE_REF  = 0x68,  /**< Substrate ref &volatile/atlas/aether T */
    AST_TYPE_FN             = 0x69,  /**< Function type fn(A, B) -> C */
    AST_TYPE_VOID_ABLE      = 0x6A,  /**< VOID-able type ??T */
    AST_TYPE_TUPLE          = 0x6B,  /**< Tuple type (A, B, C) */

    /*--------------------------------------------------------------------
     * Auxiliary (0x70-0x7F)
     *--------------------------------------------------------------------*/
    AST_PARAM               = 0x70,  /**< Function parameter */
    AST_FIELD_DEF           = 0x71,  /**< Struct field definition */
    AST_ENUM_VARIANT        = 0x72,  /**< Enum variant */
    AST_MATCH_ARM           = 0x73,  /**< Match arm (pattern => expr) */
    AST_EFFECT_LIST         = 0x74,  /**< Effect annotation list */
    AST_PATTERN             = 0x75,  /**< Pattern for matching */
    AST_FIELD_INIT          = 0x76,  /**< Field initializer (name: expr) */
    AST_GENERIC_PARAM       = 0x77,  /**< Generic type parameter */

} Seraph_AST_Kind;

/*============================================================================
 * AST Node Structures
 *============================================================================*/

/* Forward declaration */
typedef struct Seraph_AST_Node Seraph_AST_Node;

/**
 * @brief Common header for all AST nodes
 */
typedef struct {
    Seraph_AST_Kind kind;           /**< Node kind */
    Seraph_Source_Loc loc;          /**< Source location */
    Seraph_AST_Node* next;          /**< Next sibling (for lists) */
} Seraph_AST_Header;

/*--------------------------------------------------------------------
 * Module
 *--------------------------------------------------------------------*/

/**
 * @brief Top-level module containing all declarations
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Module name (may be NULL) */
    size_t name_len;
    Seraph_AST_Node* decls;         /**< Linked list of declarations */
    size_t decl_count;
} Seraph_AST_Module;

/*--------------------------------------------------------------------
 * Declarations
 *--------------------------------------------------------------------*/

/**
 * @brief Function declaration
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Function name */
    size_t name_len;
    Seraph_AST_Node* params;        /**< Parameter list (linked) */
    size_t param_count;
    Seraph_AST_Node* ret_type;      /**< Return type (or NULL for void) */
    Seraph_AST_Node* body;          /**< Function body (block) */
    Seraph_AST_Node* effects;       /**< Effect annotations (or NULL) */
    uint32_t is_pure : 1;           /**< [pure] annotation */
    uint32_t is_foreign : 1;        /**< In foreign block */
    uint32_t is_method : 1;         /**< In impl block */
    uint32_t is_forward : 1;        /**< Forward declaration (no body) */
    const char* impl_type_name;     /**< Type name for methods (set during IR) */
    size_t impl_type_name_len;
} Seraph_AST_FnDecl;

/**
 * @brief Let or const binding
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Variable name */
    size_t name_len;
    Seraph_AST_Node* type;          /**< Type annotation (or NULL) */
    Seraph_AST_Node* init;          /**< Initializer expression */
    uint32_t is_mut : 1;            /**< let mut */
    uint32_t is_const : 1;          /**< const binding */
} Seraph_AST_LetDecl;

/**
 * @brief Struct definition
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Struct name */
    size_t name_len;
    Seraph_AST_Node* generics;      /**< Generic parameters (or NULL) */
    Seraph_AST_Node* fields;        /**< Field definitions (linked) */
    size_t field_count;
} Seraph_AST_StructDecl;

/**
 * @brief Enum definition
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Enum name */
    size_t name_len;
    Seraph_AST_Node* generics;      /**< Generic parameters (or NULL) */
    Seraph_AST_Node* variants;      /**< Variants (linked) */
    size_t variant_count;
} Seraph_AST_EnumDecl;

/**
 * @brief Type alias
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Alias name */
    size_t name_len;
    Seraph_AST_Node* generics;      /**< Generic parameters (or NULL) */
    Seraph_AST_Node* target;        /**< Target type */
} Seraph_AST_TypeDecl;

/**
 * @brief Impl block
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* type;          /**< Type being implemented */
    Seraph_AST_Node* methods;       /**< Methods (linked) */
    size_t method_count;
} Seraph_AST_ImplDecl;

/**
 * @brief Use declaration
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* path;          /**< Import path */
} Seraph_AST_UseDecl;

/**
 * @brief Foreign block
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* decls;         /**< Foreign declarations */
    size_t decl_count;
} Seraph_AST_ForeignDecl;

/*--------------------------------------------------------------------
 * Expressions
 *--------------------------------------------------------------------*/

/**
 * @brief Integer literal
 */
typedef struct {
    Seraph_AST_Header hdr;
    uint64_t value;                 /**< Literal value */
    Seraph_Num_Suffix suffix;       /**< Type suffix */
} Seraph_AST_IntLit;

/**
 * @brief Float literal
 */
typedef struct {
    Seraph_AST_Header hdr;
    double value;                   /**< Literal value */
    Seraph_Num_Suffix suffix;       /**< Type suffix (s, d, g) */
} Seraph_AST_FloatLit;

/**
 * @brief String literal
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* value;              /**< String content (escaped) */
    size_t len;
} Seraph_AST_StringLit;

/**
 * @brief Char literal
 */
typedef struct {
    Seraph_AST_Header hdr;
    char value;
} Seraph_AST_CharLit;

/**
 * @brief Boolean literal
 */
typedef struct {
    Seraph_AST_Header hdr;
    int value;                      /**< 0 = false, 1 = true */
} Seraph_AST_BoolLit;

/**
 * @brief Identifier reference
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Identifier name */
    size_t name_len;
} Seraph_AST_Ident;

/**
 * @brief Path (foo::bar::baz)
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* segments;      /**< Path segments (idents, linked) */
    size_t segment_count;
} Seraph_AST_Path;

/**
 * @brief Binary expression
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_Token_Type op;           /**< Operator */
    Seraph_AST_Node* left;          /**< Left operand */
    Seraph_AST_Node* right;         /**< Right operand */
} Seraph_AST_Binary;

/**
 * @brief Unary expression
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_Token_Type op;           /**< Operator (-, !, ~) */
    Seraph_AST_Node* operand;       /**< Operand */
} Seraph_AST_Unary;

/**
 * @brief VOID propagation (expr??)
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* operand;       /**< Expression */
    Seraph_AST_Node* default_val;   /**< Default value (for ?? with RHS) */
} Seraph_AST_VoidProp;

/**
 * @brief VOID assertion (expr!!)
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* operand;       /**< Expression */
} Seraph_AST_VoidAssert;

/**
 * @brief Function call
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* callee;        /**< Function being called */
    Seraph_AST_Node* args;          /**< Arguments (linked) */
    size_t arg_count;
} Seraph_AST_Call;

/**
 * @brief Field access (expr.field)
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* object;        /**< Object expression */
    const char* field;              /**< Field name */
    size_t field_len;
} Seraph_AST_Field;

/**
 * @brief Method call (receiver.method(args))
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* receiver;      /**< Receiver object expression */
    const char* method;             /**< Method name */
    size_t method_len;
    Seraph_AST_Node* args;          /**< Arguments (linked) */
    size_t arg_count;
} Seraph_AST_MethodCall;

/**
 * @brief Index access (expr[index])
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* object;        /**< Array/slice expression */
    Seraph_AST_Node* index;         /**< Index expression */
} Seraph_AST_Index;

/**
 * @brief Block expression
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* stmts;         /**< Statements (linked) */
    size_t stmt_count;
    Seraph_AST_Node* expr;          /**< Final expression (or NULL) */
} Seraph_AST_Block;

/**
 * @brief If expression
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* cond;          /**< Condition */
    Seraph_AST_Node* then_branch;   /**< Then block */
    Seraph_AST_Node* else_branch;   /**< Else block (or NULL) */
} Seraph_AST_If;

/**
 * @brief Match expression
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* scrutinee;     /**< Value being matched */
    Seraph_AST_Node* arms;          /**< Match arms (linked) */
    size_t arm_count;
} Seraph_AST_Match;

/**
 * @brief Array literal
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* elements;      /**< Elements (linked) */
    size_t elem_count;
} Seraph_AST_Array;

/**
 * @brief Struct initializer
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* type_path;     /**< Type path */
    Seraph_AST_Node* fields;        /**< Field initializers (linked) */
    size_t field_count;
} Seraph_AST_StructInit;

/**
 * @brief Type cast expression (expr as Type)
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* operand;       /**< Expression to cast */
    Seraph_AST_Node* target_type;   /**< Target type */
} Seraph_AST_Cast;

/**
 * @brief Range expression
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* start;         /**< Start (or NULL for ..end) */
    Seraph_AST_Node* end;           /**< End (or NULL for start..) */
    int inclusive;                  /**< 1 for ..=, 0 for .. */
} Seraph_AST_Range;

/**
 * @brief Captured variable reference
 */
typedef struct Seraph_AST_Capture {
    const char* name;               /**< Variable name */
    size_t name_len;
    int by_ref;                     /**< 1 = capture by reference, 0 = by value */
    struct Seraph_AST_Capture* next;
} Seraph_AST_Capture;

/**
 * @brief Closure expression |x, y| -> T { body }
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* params;        /**< Parameter list (linked) */
    size_t param_count;
    Seraph_AST_Node* ret_type;      /**< Return type (or NULL) */
    Seraph_AST_Node* body;          /**< Body expression or block */
    Seraph_AST_Capture* captures;   /**< Captured variables (filled during analysis) */
    size_t capture_count;
    uint32_t closure_id;            /**< Unique ID for lambda-lifted function */
} Seraph_AST_Closure;

/*--------------------------------------------------------------------
 * Statements
 *--------------------------------------------------------------------*/

/**
 * @brief Expression statement
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* expr;          /**< Expression */
} Seraph_AST_ExprStmt;

/**
 * @brief Return statement
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* expr;          /**< Return value (or NULL) */
} Seraph_AST_Return;

/**
 * @brief For loop
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* var;                /**< Loop variable name */
    size_t var_len;
    Seraph_AST_Node* iterable;      /**< Iterator expression */
    Seraph_AST_Node* body;          /**< Loop body */
} Seraph_AST_For;

/**
 * @brief While loop
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* cond;          /**< Condition */
    Seraph_AST_Node* body;          /**< Loop body */
} Seraph_AST_While;

/**
 * @brief Substrate block (persist/aether)
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* body;          /**< Block body */
} Seraph_AST_SubstrateBlock;

/**
 * @brief Recover block
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* try_body;      /**< Try block */
    Seraph_AST_Node* else_body;     /**< Else block */
} Seraph_AST_Recover;

/*--------------------------------------------------------------------
 * Types
 *--------------------------------------------------------------------*/

/**
 * @brief Primitive type
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_Token_Type prim;         /**< Primitive type token */
} Seraph_AST_PrimType;

/**
 * @brief Named type
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Type name */
    size_t name_len;
    Seraph_AST_Node* generics;      /**< Generic arguments (or NULL) */
} Seraph_AST_NamedType;

/**
 * @brief Array type [T; N]
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* elem_type;     /**< Element type */
    Seraph_AST_Node* size;          /**< Size expression */
} Seraph_AST_ArrayType;

/**
 * @brief Reference type
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* inner;         /**< Inner type */
    uint32_t is_mut : 1;            /**< Mutable reference */
    uint32_t substrate;             /**< 0=normal, 1=volatile, 2=atlas, 3=aether */
} Seraph_AST_RefType;

/**
 * @brief Function type
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* params;        /**< Parameter types (linked) */
    size_t param_count;
    Seraph_AST_Node* ret;           /**< Return type */
} Seraph_AST_FnType;

/**
 * @brief VOID-able type ??T
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* inner;         /**< Inner type */
} Seraph_AST_VoidType;

/*--------------------------------------------------------------------
 * Auxiliary
 *--------------------------------------------------------------------*/

/**
 * @brief Function parameter
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Parameter name */
    size_t name_len;
    Seraph_AST_Node* type;          /**< Parameter type */
    uint32_t is_mut : 1;            /**< Mutable parameter */
} Seraph_AST_Param;

/**
 * @brief Struct field definition
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Field name */
    size_t name_len;
    Seraph_AST_Node* type;          /**< Field type */
} Seraph_AST_FieldDef;

/**
 * @brief Enum variant
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Variant name */
    size_t name_len;
    Seraph_AST_Node* data;          /**< Variant data type (or NULL) */
} Seraph_AST_EnumVariant;

/**
 * @brief Match arm
 */
typedef struct {
    Seraph_AST_Header hdr;
    Seraph_AST_Node* pattern;       /**< Pattern */
    Seraph_AST_Node* guard;         /**< Guard condition (or NULL) */
    Seraph_AST_Node* body;          /**< Arm body */
} Seraph_AST_MatchArm;

/**
 * @brief Effect list
 */
typedef struct {
    Seraph_AST_Header hdr;
    uint32_t effects;               /**< Bitmask of effects */
} Seraph_AST_EffectList;

/**
 * @brief Field initializer (name: expr)
 */
typedef struct {
    Seraph_AST_Header hdr;
    const char* name;               /**< Field name */
    size_t name_len;
    Seraph_AST_Node* value;         /**< Value expression */
} Seraph_AST_FieldInit;

/**
 * @brief Pattern for matching
 */
typedef struct {
    Seraph_AST_Header hdr;
    int pattern_kind;               /**< Kind of pattern */
    union {
        struct {
            const char* name;
            size_t name_len;
        } ident;
        uint64_t int_val;
        Seraph_AST_Node* struct_fields;
    } data;
} Seraph_AST_Pattern;

/*============================================================================
 * Union Node Type
 *============================================================================*/

/**
 * @brief Union of all AST node types
 *
 * Always access through hdr.kind first to determine the actual type.
 */
struct Seraph_AST_Node {
    union {
        Seraph_AST_Header hdr;

        /* Module */
        Seraph_AST_Module module;

        /* Declarations */
        Seraph_AST_FnDecl fn_decl;
        Seraph_AST_LetDecl let_decl;
        Seraph_AST_StructDecl struct_decl;
        Seraph_AST_EnumDecl enum_decl;
        Seraph_AST_TypeDecl type_decl;
        Seraph_AST_ImplDecl impl_decl;
        Seraph_AST_UseDecl use_decl;
        Seraph_AST_ForeignDecl foreign_decl;

        /* Expressions */
        Seraph_AST_IntLit int_lit;
        Seraph_AST_FloatLit float_lit;
        Seraph_AST_StringLit string_lit;
        Seraph_AST_CharLit char_lit;
        Seraph_AST_BoolLit bool_lit;
        Seraph_AST_Ident ident;
        Seraph_AST_Path path;
        Seraph_AST_Binary binary;
        Seraph_AST_Unary unary;
        Seraph_AST_VoidProp void_prop;
        Seraph_AST_VoidAssert void_assert;
        Seraph_AST_Call call;
        Seraph_AST_MethodCall method_call;
        Seraph_AST_Field field;
        Seraph_AST_Index index;
        Seraph_AST_Block block;
        Seraph_AST_If if_expr;
        Seraph_AST_Match match;
        Seraph_AST_Array array;
        Seraph_AST_StructInit struct_init;
        Seraph_AST_Cast cast;
        Seraph_AST_Range range;
        Seraph_AST_Closure closure;

        /* Statements */
        Seraph_AST_ExprStmt expr_stmt;
        Seraph_AST_Return return_stmt;
        Seraph_AST_For for_stmt;
        Seraph_AST_While while_stmt;
        Seraph_AST_SubstrateBlock substrate_block;
        Seraph_AST_Recover recover_stmt;

        /* Types */
        Seraph_AST_PrimType prim_type;
        Seraph_AST_NamedType named_type;
        Seraph_AST_ArrayType array_type;
        Seraph_AST_RefType ref_type;
        Seraph_AST_FnType fn_type;
        Seraph_AST_VoidType void_type;

        /* Auxiliary */
        Seraph_AST_Param param;
        Seraph_AST_FieldDef field_def;
        Seraph_AST_EnumVariant enum_variant;
        Seraph_AST_MatchArm match_arm;
        Seraph_AST_EffectList effect_list;
        Seraph_AST_FieldInit field_init;
        Seraph_AST_Pattern pattern;
    };
};

/*============================================================================
 * AST Utilities
 *============================================================================*/

/**
 * @brief Check if an AST node is VOID (error)
 */
SERAPH_INLINE int seraph_ast_is_void(const Seraph_AST_Node* node) {
    return node == NULL || node->hdr.kind == AST_VOID;
}

/**
 * @brief Create a VOID AST node
 */
Seraph_AST_Node* seraph_ast_void(Seraph_Arena* arena, Seraph_Source_Loc loc);

/**
 * @brief Get the name of an AST node kind
 */
const char* seraph_ast_kind_name(Seraph_AST_Kind kind);

/**
 * @brief Print AST for debugging
 */
void seraph_ast_print(const Seraph_AST_Node* node, int indent);

/*============================================================================
 * AST Construction Helpers
 *============================================================================*/

/* Module */
Seraph_AST_Node* seraph_ast_module(Seraph_Arena* arena, Seraph_Source_Loc loc);

/* Declarations */
Seraph_AST_Node* seraph_ast_fn_decl(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                     const char* name, size_t name_len);
Seraph_AST_Node* seraph_ast_let_decl(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      const char* name, size_t name_len,
                                      int is_mut, int is_const);
Seraph_AST_Node* seraph_ast_struct_decl(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                         const char* name, size_t name_len);

/* Expressions */
Seraph_AST_Node* seraph_ast_int_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                     uint64_t value, Seraph_Num_Suffix suffix);
Seraph_AST_Node* seraph_ast_float_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                       double value, Seraph_Num_Suffix suffix);
Seraph_AST_Node* seraph_ast_string_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                        const char* value, size_t len);
Seraph_AST_Node* seraph_ast_bool_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      int value);
Seraph_AST_Node* seraph_ast_ident(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   const char* name, size_t name_len);
Seraph_AST_Node* seraph_ast_binary(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                    Seraph_Token_Type op,
                                    Seraph_AST_Node* left, Seraph_AST_Node* right);
Seraph_AST_Node* seraph_ast_unary(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   Seraph_Token_Type op, Seraph_AST_Node* operand);
Seraph_AST_Node* seraph_ast_call(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                  Seraph_AST_Node* callee);
Seraph_AST_Node* seraph_ast_field(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   Seraph_AST_Node* object,
                                   const char* field, size_t field_len);
Seraph_AST_Node* seraph_ast_block(Seraph_Arena* arena, Seraph_Source_Loc loc);
Seraph_AST_Node* seraph_ast_if(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                Seraph_AST_Node* cond,
                                Seraph_AST_Node* then_branch,
                                Seraph_AST_Node* else_branch);
Seraph_AST_Node* seraph_ast_struct_init(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                         Seraph_AST_Node* type_path);
Seraph_AST_Node* seraph_ast_field_init(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                        const char* name, size_t name_len,
                                        Seraph_AST_Node* value);
Seraph_AST_Node* seraph_ast_cast(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                  Seraph_AST_Node* operand, Seraph_AST_Node* target_type);
Seraph_AST_Node* seraph_ast_closure(Seraph_Arena* arena, Seraph_Source_Loc loc);

/* Types */
Seraph_AST_Node* seraph_ast_prim_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                       Seraph_Token_Type prim);
Seraph_AST_Node* seraph_ast_named_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                        const char* name, size_t name_len);
Seraph_AST_Node* seraph_ast_ref_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      Seraph_AST_Node* inner, int is_mut);
Seraph_AST_Node* seraph_ast_ptr_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      Seraph_AST_Node* inner);
Seraph_AST_Node* seraph_ast_void_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                       Seraph_AST_Node* inner);

/* Auxiliary */
Seraph_AST_Node* seraph_ast_param(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   const char* name, size_t name_len,
                                   Seraph_AST_Node* type);

/*============================================================================
 * AST List Helpers
 *============================================================================*/

/**
 * @brief Append a node to a linked list
 *
 * Returns the new head (unchanged if list was non-empty)
 */
void seraph_ast_append(Seraph_AST_Node** list, Seraph_AST_Node* node);

/**
 * @brief Count nodes in a linked list
 */
size_t seraph_ast_count(const Seraph_AST_Node* list);

#endif /* SERAPH_SERAPHIM_AST_H */
