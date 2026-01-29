/**
 * @file celestial_ir.h
 * @brief MC28: Celestial IR - SERAPH's Native Intermediate Representation
 *
 * Celestial IR is NOT LLVM IR. It is purpose-built for SERAPH's unique semantics:
 *
 * 1. VOID-FIRST: Every value carries VOID infection potential. Operations
 *    propagate VOID automatically. Division by zero produces VOID, not trap.
 *
 * 2. CAPABILITY-AWARE: Memory access through capabilities with generation
 *    checking. No raw pointers - only bounded, revocable capabilities.
 *
 * 3. SUBSTRATE-CONSCIOUS: Operations know their memory substrate context
 *    (Volatile, Atlas/Persistent, Aether/Network). Different code paths
 *    for different substrates.
 *
 * 4. EFFECT-TRACKED: Every operation has known effects. Pure functions
 *    have no effects. Effect composition is explicit.
 *
 * 5. GALACTIC-NATIVE: Hyper-dual numbers for automatic differentiation
 *    are first-class. Derivatives flow through computation.
 *
 * Celestial IR sits between the AST and machine code, enabling:
 * - Optimization passes that understand SERAPH semantics
 * - Multiple backend targets (x64, ARM64 future)
 * - Verification of safety properties
 *
 * Philosophy: The IR encodes SERAPH's worldview. Every instruction
 * reflects our belief that safety trumps speed, that VOID is wisdom,
 * and that capabilities are the foundation of trust.
 */

#ifndef SERAPH_SERAPHIM_CELESTIAL_IR_H
#define SERAPH_SERAPHIM_CELESTIAL_IR_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "seraph/vbit.h"
#include "seraph/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct Celestial_Module       Celestial_Module;
typedef struct Celestial_Function     Celestial_Function;
typedef struct Celestial_Block        Celestial_Block;
typedef struct Celestial_Instr        Celestial_Instr;
typedef struct Celestial_Value        Celestial_Value;
typedef struct Celestial_Type         Celestial_Type;
typedef struct Celestial_String_Const Celestial_String_Const;

/*============================================================================
 * Type System
 *============================================================================*/

/**
 * @brief Celestial IR type kinds
 */
typedef enum {
    /* Primitive types */
    CIR_TYPE_VOID,          /**< The VOID type itself (unit type) */
    CIR_TYPE_BOOL,          /**< Boolean (1 bit logical) */
    CIR_TYPE_I8,            /**< 8-bit signed integer */
    CIR_TYPE_I16,           /**< 16-bit signed integer */
    CIR_TYPE_I32,           /**< 32-bit signed integer */
    CIR_TYPE_I64,           /**< 64-bit signed integer */
    CIR_TYPE_U8,            /**< 8-bit unsigned integer */
    CIR_TYPE_U16,           /**< 16-bit unsigned integer */
    CIR_TYPE_U32,           /**< 32-bit unsigned integer */
    CIR_TYPE_U64,           /**< 64-bit unsigned integer */

    /* SERAPH-specific primitive types */
    CIR_TYPE_SCALAR,        /**< Q64.64 fixed-point (128 bits) */
    CIR_TYPE_DUAL,          /**< Dual number (value + derivative) */
    CIR_TYPE_GALACTIC,      /**< Hyper-dual quaternion (512 bits) */

    /* Compound types */
    CIR_TYPE_CAPABILITY,    /**< Capability (base + length + gen + perms) */
    CIR_TYPE_STRUCT,        /**< User-defined struct */
    CIR_TYPE_ARRAY,         /**< Fixed-size array */
    CIR_TYPE_SLICE,         /**< Dynamic slice (cap + length) */
    CIR_TYPE_STR,           /**< String (ptr + length fat pointer) */
    CIR_TYPE_ENUM,          /**< Tagged union (discriminant + payload) */
    CIR_TYPE_FUNCTION,      /**< Function pointer type */

    /* Special types */
    CIR_TYPE_VOIDABLE,      /**< Type that may contain VOID value */
    CIR_TYPE_SUBSTRATE,     /**< Substrate context handle */
    CIR_TYPE_POINTER,       /**< Raw pointer type (*T) for bootstrap/self-hosting */
} Celestial_Type_Kind;

/**
 * @brief Celestial IR type
 */
struct Celestial_Type {
    Celestial_Type_Kind kind;

    union {
        /* For CIR_TYPE_STRUCT */
        struct {
            const char* name;
            size_t      name_len;
            Celestial_Type** fields;
            const char**     field_names;
            size_t           field_count;
        } struct_type;

        /* For CIR_TYPE_ARRAY */
        struct {
            Celestial_Type* elem_type;
            size_t          length;
        } array_type;

        /* For CIR_TYPE_SLICE */
        struct {
            Celestial_Type* elem_type;
        } slice_type;

        /* For CIR_TYPE_ENUM */
        struct {
            const char* name;
            size_t      name_len;
            const char** variant_names;     /**< Variant names */
            size_t*      variant_name_lens;
            Celestial_Type** variant_types; /**< Payload types (NULL if no payload) */
            size_t       variant_count;
        } enum_type;

        /* For CIR_TYPE_FUNCTION */
        struct {
            Celestial_Type*  ret_type;
            Celestial_Type** param_types;
            size_t           param_count;
            uint32_t         effects;  /* Effect flags */
        } func_type;

        /* For CIR_TYPE_VOIDABLE */
        struct {
            Celestial_Type* inner_type;
        } voidable_type;

        /* For CIR_TYPE_POINTER */
        struct {
            Celestial_Type* pointee_type;  /**< Type being pointed to */
        } pointer_type;
    };
};

/*============================================================================
 * Substrate Context
 *============================================================================*/

/**
 * @brief Memory substrate kinds
 */
typedef enum {
    CIR_SUBSTRATE_VOLATILE,   /**< Normal volatile memory */
    CIR_SUBSTRATE_ATLAS,      /**< Persistent storage (NVMe) */
    CIR_SUBSTRATE_AETHER,     /**< Distributed memory (network) */
} Celestial_Substrate_Kind;

/*============================================================================
 * Effect System
 *============================================================================*/

/**
 * @brief Effect flags for functions and blocks
 */
typedef enum {
    CIR_EFFECT_NONE     = 0,
    CIR_EFFECT_VOID     = (1 << 0),   /**< May produce VOID */
    CIR_EFFECT_READ     = (1 << 1),   /**< Reads memory */
    CIR_EFFECT_WRITE    = (1 << 2),   /**< Writes memory */
    CIR_EFFECT_PERSIST  = (1 << 3),   /**< Accesses Atlas */
    CIR_EFFECT_NETWORK  = (1 << 4),   /**< Accesses Aether */
    CIR_EFFECT_TIMER    = (1 << 5),   /**< Uses Chronon */
    CIR_EFFECT_ALLOC    = (1 << 6),   /**< Allocates memory */
    CIR_EFFECT_PANIC    = (1 << 7),   /**< May panic */
    CIR_EFFECT_DIVERGE  = (1 << 8),   /**< May not terminate */

    /* Composite effects */
    CIR_EFFECT_PURE     = CIR_EFFECT_NONE,
    CIR_EFFECT_IO       = CIR_EFFECT_READ | CIR_EFFECT_WRITE,
} Celestial_Effect;

/*============================================================================
 * Values and Virtual Registers
 *============================================================================*/

/**
 * @brief Value kinds in Celestial IR
 */
typedef enum {
    CIR_VALUE_CONST,        /**< Compile-time constant */
    CIR_VALUE_VREG,         /**< Virtual register (SSA) */
    CIR_VALUE_PARAM,        /**< Function parameter */
    CIR_VALUE_GLOBAL,       /**< Global variable */
    CIR_VALUE_VOID_CONST,   /**< The VOID constant for a type */
    CIR_VALUE_STRING,       /**< String constant reference */
    CIR_VALUE_FNPTR,        /**< Function pointer reference */
} Celestial_Value_Kind;

/**
 * @brief A value in Celestial IR
 */
struct Celestial_Value {
    Celestial_Value_Kind kind;
    Celestial_Type*      type;
    uint32_t             id;         /**< Unique ID for this value */

    /* For tracking VOID infection */
    Seraph_Vbit          may_be_void; /**< TRUE if value might be VOID */

    /* For alloca: the type being allocated (array type, struct type, etc.) */
    Celestial_Type*      alloca_type;

    union {
        /* For CIR_VALUE_CONST */
        struct {
            union {
                int64_t  i64;
                uint64_t u64;
                double   f64;
                struct { int64_t real, dual; } dual;
                struct { int64_t w, x, y, z; } galactic;
            };
        } constant;

        /* For CIR_VALUE_VREG */
        struct {
            Celestial_Instr* def;    /**< Instruction that defines this */
        } vreg;

        /* For CIR_VALUE_PARAM */
        struct {
            uint32_t index;          /**< Parameter index */
        } param;

        /* For CIR_VALUE_GLOBAL */
        struct {
            const char* name;
            size_t      name_len;
        } global;

        /* For CIR_VALUE_STRING */
        struct {
            Celestial_String_Const* str_const;  /**< String constant reference */
        } string;

        /* For CIR_VALUE_FNPTR */
        struct {
            Celestial_Function* fn;  /**< Function being referenced */
        } fnptr;
    };
};

/*============================================================================
 * Instructions
 *============================================================================*/

/**
 * @brief Celestial IR opcode categories
 */
typedef enum {
    /*------------------------------------------------------------------------
     * Arithmetic (VOID-propagating)
     *------------------------------------------------------------------------*/
    CIR_ADD,            /**< Add (VOID if overflow or operand VOID) */
    CIR_SUB,            /**< Subtract */
    CIR_MUL,            /**< Multiply */
    CIR_DIV,            /**< Divide (VOID if divisor zero) */
    CIR_MOD,            /**< Modulo (VOID if divisor zero) */
    CIR_NEG,            /**< Negate */

    /*------------------------------------------------------------------------
     * Bitwise
     *------------------------------------------------------------------------*/
    CIR_AND,            /**< Bitwise AND */
    CIR_OR,             /**< Bitwise OR */
    CIR_XOR,            /**< Bitwise XOR */
    CIR_NOT,            /**< Bitwise NOT */
    CIR_SHL,            /**< Shift left */
    CIR_SHR,            /**< Shift right (logical) */
    CIR_SAR,            /**< Shift right (arithmetic) */

    /*------------------------------------------------------------------------
     * Comparison (produces Vbit, not bool)
     *------------------------------------------------------------------------*/
    CIR_EQ,             /**< Equal */
    CIR_NE,             /**< Not equal */
    CIR_LT,             /**< Less than (signed) */
    CIR_LE,             /**< Less or equal */
    CIR_GT,             /**< Greater than */
    CIR_GE,             /**< Greater or equal */
    CIR_ULT,            /**< Less than (unsigned) */
    CIR_ULE,            /**< Less or equal (unsigned) */
    CIR_UGT,            /**< Greater than (unsigned) */
    CIR_UGE,            /**< Greater or equal (unsigned) */

    /*------------------------------------------------------------------------
     * VOID Operations (SERAPH-specific)
     *------------------------------------------------------------------------*/
    CIR_VOID_TEST,      /**< Test if value is VOID -> Vbit */
    CIR_VOID_PROP,      /**< Propagate VOID (?? operator) */
    CIR_VOID_ASSERT,    /**< Assert non-VOID (!! operator) */
    CIR_VOID_COALESCE,  /**< VOID coalescing (value ?? default) */
    CIR_VOID_CONST,     /**< Load VOID constant for type */

    /*------------------------------------------------------------------------
     * Capability Operations (SERAPH-specific)
     *------------------------------------------------------------------------*/
    CIR_CAP_CREATE,     /**< Create capability (base, len, gen, perms) */
    CIR_CAP_LOAD,       /**< Load through capability (bounds-checked) */
    CIR_CAP_STORE,      /**< Store through capability (bounds-checked) */
    CIR_CAP_CHECK,      /**< Check capability validity -> Vbit */
    CIR_CAP_NARROW,     /**< Narrow capability bounds */
    CIR_CAP_SPLIT,      /**< Split capability into two */
    CIR_CAP_REVOKE,     /**< Increment generation (revoke) */

    /*------------------------------------------------------------------------
     * Memory Operations
     *------------------------------------------------------------------------*/
    CIR_LOAD,           /**< Load from volatile memory (raw) */
    CIR_STORE,          /**< Store to volatile memory (raw) */
    CIR_ALLOCA,         /**< Stack allocation */
    CIR_MEMCPY,         /**< Memory copy */
    CIR_MEMSET,         /**< Memory set */

    /*------------------------------------------------------------------------
     * Substrate Operations (SERAPH-specific)
     *------------------------------------------------------------------------*/
    CIR_SUBSTRATE_ENTER,   /**< Enter substrate context */
    CIR_SUBSTRATE_EXIT,    /**< Exit substrate context */
    CIR_ATLAS_LOAD,        /**< Load from Atlas (persistent) */
    CIR_ATLAS_STORE,       /**< Store to Atlas (journaled) */
    CIR_ATLAS_BEGIN,       /**< Begin Atlas transaction */
    CIR_ATLAS_COMMIT,      /**< Commit Atlas transaction */
    CIR_ATLAS_ROLLBACK,    /**< Rollback Atlas transaction */
    CIR_AETHER_LOAD,       /**< Load from Aether (network) */
    CIR_AETHER_STORE,      /**< Store to Aether (write-back) */
    CIR_AETHER_SYNC,       /**< Synchronize Aether state */

    /*------------------------------------------------------------------------
     * Control Flow
     *------------------------------------------------------------------------*/
    CIR_JUMP,           /**< Unconditional jump */
    CIR_BRANCH,         /**< Conditional branch (on Vbit) */
    CIR_SWITCH,         /**< Multi-way branch */
    CIR_CALL,           /**< Function call */
    CIR_CALL_INDIRECT,  /**< Indirect call through function pointer */
    CIR_SYSCALL,        /**< Direct syscall (for self-hosting) */
    CIR_TAIL_CALL,      /**< Tail call */
    CIR_RETURN,         /**< Return from function */

    /*------------------------------------------------------------------------
     * Galactic Operations (SERAPH-specific)
     *------------------------------------------------------------------------*/
    CIR_GALACTIC_ADD,   /**< Galactic addition (preserves derivatives) */
    CIR_GALACTIC_MUL,   /**< Galactic multiplication (chain rule) */
    CIR_GALACTIC_DIV,   /**< Galactic division (quotient rule) */
    CIR_GALACTIC_PREDICT, /**< Extrapolate using derivatives */
    CIR_GALACTIC_EXTRACT, /**< Extract component (w, x, y, z) */
    CIR_GALACTIC_INSERT,  /**< Insert component */

    /*------------------------------------------------------------------------
     * Chronon Operations (SERAPH-specific)
     *------------------------------------------------------------------------*/
    CIR_CHRONON_NOW,    /**< Get current strand-local time */
    CIR_CHRONON_DELTA,  /**< Get time since last call */
    CIR_CHRONON_BUDGET, /**< Check remaining time budget */
    CIR_CHRONON_YIELD,  /**< Yield if budget exhausted */

    /*------------------------------------------------------------------------
     * Type Conversions
     *------------------------------------------------------------------------*/
    CIR_TRUNC,          /**< Truncate to smaller type */
    CIR_ZEXT,           /**< Zero-extend to larger type */
    CIR_SEXT,           /**< Sign-extend to larger type */
    CIR_BITCAST,        /**< Reinterpret bits as different type */
    CIR_TO_SCALAR,      /**< Convert int to Scalar */
    CIR_FROM_SCALAR,    /**< Convert Scalar to int */
    CIR_TO_GALACTIC,    /**< Promote Scalar to Galactic */
    CIR_FROM_GALACTIC,  /**< Extract Scalar from Galactic */

    /*------------------------------------------------------------------------
     * Struct/Array Operations
     *------------------------------------------------------------------------*/
    CIR_EXTRACTFIELD,   /**< Extract struct field */
    CIR_INSERTFIELD,    /**< Insert struct field */
    CIR_EXTRACTELEM,    /**< Extract array element */
    CIR_INSERTELEM,     /**< Insert array element */
    CIR_GEP,            /**< Get element pointer */

    /*------------------------------------------------------------------------
     * Miscellaneous
     *------------------------------------------------------------------------*/
    CIR_NOP,            /**< No operation (placeholder for removed instrs) */
    CIR_PHI,            /**< SSA phi node */
    CIR_SELECT,         /**< Conditional select */
    CIR_UNREACHABLE,    /**< Mark unreachable code */
    CIR_TRAP,           /**< Explicit trap (for debugging) */
} Celestial_Opcode;

/**
 * @brief A Celestial IR instruction
 */
struct Celestial_Instr {
    Celestial_Opcode     opcode;
    Celestial_Value*     result;        /**< Result value (NULL if void) */
    Celestial_Value**    operands;      /**< Operand values */
    size_t               operand_count;

    /* For control flow */
    Celestial_Block*     target1;       /**< Branch/jump target */
    Celestial_Block*     target2;       /**< False branch target */

    /* For calls */
    Celestial_Function*  callee;        /**< Called function */

    /* Effect tracking */
    uint32_t             effects;       /**< Effects of this instruction */

    /* Source location for debugging */
    uint32_t             line;
    uint32_t             column;

    /* Linked list in block */
    Celestial_Instr*     next;
    Celestial_Instr*     prev;
};

/*============================================================================
 * Basic Blocks
 *============================================================================*/

/**
 * @brief A basic block in Celestial IR
 */
struct Celestial_Block {
    uint32_t             id;            /**< Block ID */
    const char*          name;          /**< Optional name (for debugging) */

    /* Instructions */
    Celestial_Instr*     first;         /**< First instruction */
    Celestial_Instr*     last;          /**< Last instruction (terminator) */
    size_t               instr_count;

    /* Control flow graph */
    Celestial_Block**    preds;         /**< Predecessor blocks */
    size_t               pred_count;
    Celestial_Block**    succs;         /**< Successor blocks */
    size_t               succ_count;

    /* Substrate context at block entry */
    Celestial_Substrate_Kind substrate;

    /* Linked list in function */
    Celestial_Block*     next;
    Celestial_Block*     prev;

    /* For dominance analysis */
    Celestial_Block*     idom;          /**< Immediate dominator */
    uint32_t             dom_depth;
};

/*============================================================================
 * Functions
 *============================================================================*/

/**
 * @brief A function in Celestial IR
 */
struct Celestial_Function {
    const char*          name;
    size_t               name_len;
    Celestial_Type*      type;          /**< Function type */

    /* Parameters */
    Celestial_Value**    params;
    size_t               param_count;

    /* Blocks */
    Celestial_Block*     entry;         /**< Entry block */
    Celestial_Block*     blocks;        /**< All blocks (linked list) */
    size_t               block_count;

    /* Effect declaration */
    uint32_t             declared_effects;

    /* For SSA construction */
    uint32_t             next_vreg_id;
    uint32_t             next_block_id;

    /* Linked list in module */
    Celestial_Function*  next;
};

/*============================================================================
 * Module
 *============================================================================*/

/**
 * @brief A Celestial IR module
 */
/**
 * @brief String constant entry in the string table
 */
typedef struct Celestial_String_Const {
    const char*      data;        /**< String bytes (escape-processed) */
    size_t           len;         /**< String length in bytes */
    uint32_t         id;          /**< Unique ID for codegen */
    struct Celestial_String_Const* next;
} Celestial_String_Const;

/**
 * @brief A Celestial IR module
 */
struct Celestial_Module {
    const char*          name;
    size_t               name_len;

    /* Functions */
    Celestial_Function*  functions;
    size_t               function_count;

    /* Global variables */
    Celestial_Value**    globals;
    size_t               global_count;

    /* Types */
    Celestial_Type**     types;
    size_t               type_count;

    /* String constants (for rodata section) */
    Celestial_String_Const* strings;
    size_t                  string_count;

    /* Arena for allocations */
    Seraph_Arena*        arena;
};

/*============================================================================
 * Module Creation and Management
 *============================================================================*/

/**
 * @brief Create a new Celestial IR module
 *
 * @param name Module name
 * @param arena Arena for allocations
 * @return New module, or NULL on error
 */
Celestial_Module* celestial_module_create(const char* name, Seraph_Arena* arena);

/**
 * @brief Free a Celestial IR module
 *
 * @param module Module to free
 */
void celestial_module_free(Celestial_Module* module);

/*============================================================================
 * Type Creation
 *============================================================================*/

/**
 * @brief Get primitive type
 */
Celestial_Type* celestial_type_primitive(Celestial_Module* mod,
                                          Celestial_Type_Kind kind);

/**
 * @brief Create voidable type (type that may contain VOID)
 */
Celestial_Type* celestial_type_voidable(Celestial_Module* mod,
                                         Celestial_Type* inner);

/**
 * @brief Create capability type
 */
Celestial_Type* celestial_type_capability(Celestial_Module* mod);

/**
 * @brief Create struct type
 */
Celestial_Type* celestial_type_struct(Celestial_Module* mod,
                                       const char* name,
                                       size_t name_len,
                                       Celestial_Type** fields,
                                       const char** field_names,
                                       size_t field_count);

/**
 * @brief Create array type
 */
Celestial_Type* celestial_type_array(Celestial_Module* mod,
                                      Celestial_Type* elem,
                                      size_t length);

/**
 * @brief Create string type (fat pointer: data ptr + length)
 */
Celestial_Type* celestial_type_str(Celestial_Module* mod);

/**
 * @brief Create enum type (tagged union)
 *
 * @param mod Module
 * @param name Enum name
 * @param name_len Length of name
 * @param variant_names Array of variant names
 * @param variant_name_lens Array of variant name lengths
 * @param variant_types Array of variant payload types (NULL entries for no payload)
 * @param variant_count Number of variants
 * @return Enum type, or NULL on error
 */
Celestial_Type* celestial_type_enum(Celestial_Module* mod,
                                     const char* name,
                                     size_t name_len,
                                     const char** variant_names,
                                     size_t* variant_name_lens,
                                     Celestial_Type** variant_types,
                                     size_t variant_count);

/**
 * @brief Add a string constant to the module
 *
 * @param mod Module
 * @param data String data (will be copied with escape processing)
 * @param len Length of raw string (before escape processing)
 * @return String constant entry, or NULL on error
 */
Celestial_String_Const* celestial_add_string_const(Celestial_Module* mod,
                                                    const char* data,
                                                    size_t len);

/**
 * @brief Create function type
 */
Celestial_Type* celestial_type_function(Celestial_Module* mod,
                                         Celestial_Type* ret,
                                         Celestial_Type** params,
                                         size_t param_count,
                                         uint32_t effects);

/**
 * @brief Create pointer type (*T) for bootstrap/self-hosting
 *
 * Note: In production SERAPH, capabilities are preferred over raw pointers.
 * This type exists to support self-hosting bootstrapping.
 *
 * @param mod Module
 * @param pointee Type being pointed to
 * @return Pointer type, or NULL on error
 */
Celestial_Type* celestial_type_pointer(Celestial_Module* mod,
                                        Celestial_Type* pointee);

/**
 * @brief Get size of type in bytes
 */
size_t celestial_type_size(Celestial_Type* type);

/**
 * @brief Get alignment of type in bytes
 */
size_t celestial_type_align(Celestial_Type* type);

/*============================================================================
 * Function Creation
 *============================================================================*/

/**
 * @brief Create a new function
 */
Celestial_Function* celestial_function_create(Celestial_Module* mod,
                                               const char* name,
                                               Celestial_Type* type);

/**
 * @brief Create entry block for function
 */
Celestial_Block* celestial_function_entry(Celestial_Function* fn);

/**
 * @brief Create new block in function
 */
Celestial_Block* celestial_block_create(Celestial_Function* fn,
                                         const char* name);

/*============================================================================
 * Value Creation
 *============================================================================*/

/**
 * @brief Create constant value
 */
Celestial_Value* celestial_const_i64(Celestial_Module* mod, int64_t value);
Celestial_Value* celestial_const_u64(Celestial_Module* mod, uint64_t value);
Celestial_Value* celestial_const_i32(Celestial_Module* mod, int32_t value);
Celestial_Value* celestial_const_u32(Celestial_Module* mod, uint32_t value);
Celestial_Value* celestial_const_i16(Celestial_Module* mod, int16_t value);
Celestial_Value* celestial_const_u16(Celestial_Module* mod, uint16_t value);
Celestial_Value* celestial_const_i8(Celestial_Module* mod, int8_t value);
Celestial_Value* celestial_const_u8(Celestial_Module* mod, uint8_t value);
Celestial_Value* celestial_const_bool(Celestial_Module* mod, int value);

/**
 * @brief Create VOID constant for type
 */
Celestial_Value* celestial_const_void(Celestial_Module* mod,
                                       Celestial_Type* type);

/**
 * @brief Create Galactic constant
 */
Celestial_Value* celestial_const_galactic(Celestial_Module* mod,
                                           int64_t w, int64_t x,
                                           int64_t y, int64_t z);

/**
 * @brief Create string constant value
 *
 * @param mod Module
 * @param str_const String constant from celestial_add_string_const
 * @return String value (fat pointer: data ptr + length)
 */
Celestial_Value* celestial_const_string(Celestial_Module* mod,
                                         Celestial_String_Const* str_const);

/*============================================================================
 * Instruction Building
 *============================================================================*/

/**
 * @brief Instruction builder context
 */
typedef struct {
    Celestial_Module*   module;
    Celestial_Function* function;
    Celestial_Block*    block;
    Celestial_Instr*    insert_point;   /**< Insert before this, or NULL for end */
} Celestial_Builder;

/**
 * @brief Initialize builder
 */
void celestial_builder_init(Celestial_Builder* builder,
                            Celestial_Module* mod);

/**
 * @brief Position builder at end of block
 */
void celestial_builder_position(Celestial_Builder* builder,
                                Celestial_Block* block);

/*------------------------------------------------------------------------
 * Arithmetic Instructions
 *------------------------------------------------------------------------*/

Celestial_Value* celestial_build_add(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_sub(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_mul(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_div(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_mod(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_neg(Celestial_Builder* b,
                                      Celestial_Value* val,
                                      const char* name);

/*------------------------------------------------------------------------
 * Bitwise Instructions
 *------------------------------------------------------------------------*/

Celestial_Value* celestial_build_and(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_or(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name);

Celestial_Value* celestial_build_xor(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_not(Celestial_Builder* b,
                                      Celestial_Value* val,
                                      const char* name);

Celestial_Value* celestial_build_shl(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

Celestial_Value* celestial_build_shr(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

/*------------------------------------------------------------------------
 * Type Conversion Instructions
 *------------------------------------------------------------------------*/

/**
 * @brief Truncate value to smaller integer type
 */
Celestial_Value* celestial_build_trunc(Celestial_Builder* b,
                                        Celestial_Value* value,
                                        Celestial_Type* target_type,
                                        const char* name);

/**
 * @brief Zero-extend value to larger integer type
 */
Celestial_Value* celestial_build_zext(Celestial_Builder* b,
                                       Celestial_Value* value,
                                       Celestial_Type* target_type,
                                       const char* name);

/**
 * @brief Sign-extend value to larger integer type
 */
Celestial_Value* celestial_build_sext(Celestial_Builder* b,
                                       Celestial_Value* value,
                                       Celestial_Type* target_type,
                                       const char* name);

/*------------------------------------------------------------------------
 * VOID Instructions (SERAPH-specific)
 *------------------------------------------------------------------------*/

/**
 * @brief Test if value is VOID
 *
 * @return Vbit result (TRUE if VOID, FALSE if not, UNKNOWN if might be)
 */
Celestial_Value* celestial_build_void_test(Celestial_Builder* b,
                                            Celestial_Value* value,
                                            const char* name);

/**
 * @brief VOID propagation (?? operator)
 *
 * If value is VOID, immediately return VOID from function.
 * Otherwise, return the value unchanged.
 */
Celestial_Value* celestial_build_void_prop(Celestial_Builder* b,
                                            Celestial_Value* value,
                                            const char* name);

/**
 * @brief VOID assertion (!! operator)
 *
 * If value is VOID, panic with message.
 * Otherwise, return the value unchanged.
 */
Celestial_Value* celestial_build_void_assert(Celestial_Builder* b,
                                              Celestial_Value* value,
                                              const char* name);

/**
 * @brief VOID coalescing (value ?? default)
 *
 * If value is VOID, return default.
 * Otherwise, return value.
 */
Celestial_Value* celestial_build_void_coalesce(Celestial_Builder* b,
                                                Celestial_Value* value,
                                                Celestial_Value* default_val,
                                                const char* name);

/*------------------------------------------------------------------------
 * Capability Instructions (SERAPH-specific)
 *------------------------------------------------------------------------*/

/**
 * @brief Create capability
 */
Celestial_Value* celestial_build_cap_create(Celestial_Builder* b,
                                             Celestial_Value* base,
                                             Celestial_Value* length,
                                             Celestial_Value* generation,
                                             Celestial_Value* perms,
                                             const char* name);

/**
 * @brief Load through capability (bounds-checked)
 *
 * Returns VOID if capability is invalid or out of bounds.
 */
Celestial_Value* celestial_build_cap_load(Celestial_Builder* b,
                                           Celestial_Value* cap,
                                           Celestial_Value* offset,
                                           Celestial_Type* type,
                                           const char* name);

/**
 * @brief Store through capability (bounds-checked)
 *
 * No-op if capability is invalid or out of bounds (silent failure).
 */
void celestial_build_cap_store(Celestial_Builder* b,
                               Celestial_Value* cap,
                               Celestial_Value* offset,
                               Celestial_Value* value);

/*------------------------------------------------------------------------
 * Substrate Instructions (SERAPH-specific)
 *------------------------------------------------------------------------*/

/**
 * @brief Enter substrate context (persist/aether)
 */
Celestial_Value* celestial_build_substrate_enter(Celestial_Builder* b,
                                                  Celestial_Substrate_Kind kind,
                                                  const char* name);

/**
 * @brief Exit substrate context
 */
void celestial_build_substrate_exit(Celestial_Builder* b,
                                    Celestial_Value* context);

/**
 * @brief Begin Atlas transaction
 */
Celestial_Value* celestial_build_atlas_begin(Celestial_Builder* b,
                                              const char* name);

/**
 * @brief Commit Atlas transaction
 */
void celestial_build_atlas_commit(Celestial_Builder* b,
                                  Celestial_Value* tx);

/*------------------------------------------------------------------------
 * Galactic Instructions (SERAPH-specific)
 *------------------------------------------------------------------------*/

/**
 * @brief Galactic addition (preserves derivatives)
 */
Celestial_Value* celestial_build_galactic_add(Celestial_Builder* b,
                                               Celestial_Value* lhs,
                                               Celestial_Value* rhs,
                                               const char* name);

/**
 * @brief Galactic multiplication (chain rule)
 */
Celestial_Value* celestial_build_galactic_mul(Celestial_Builder* b,
                                               Celestial_Value* lhs,
                                               Celestial_Value* rhs,
                                               const char* name);

/**
 * @brief Galactic prediction (extrapolate using derivatives)
 */
Celestial_Value* celestial_build_galactic_predict(Celestial_Builder* b,
                                                   Celestial_Value* galactic,
                                                   Celestial_Value* delta_t,
                                                   const char* name);

/*------------------------------------------------------------------------
 * Control Flow Instructions
 *------------------------------------------------------------------------*/

/**
 * @brief Unconditional jump
 */
void celestial_build_jump(Celestial_Builder* b, Celestial_Block* target);

/**
 * @brief Conditional branch
 */
void celestial_build_branch(Celestial_Builder* b,
                            Celestial_Value* cond,
                            Celestial_Block* then_block,
                            Celestial_Block* else_block);

/**
 * @brief Return from function
 */
void celestial_build_return(Celestial_Builder* b, Celestial_Value* value);

/**
 * @brief Function call
 */
Celestial_Value* celestial_build_call(Celestial_Builder* b,
                                       Celestial_Function* callee,
                                       Celestial_Value** args,
                                       size_t arg_count,
                                       const char* name);

/**
 * @brief Indirect function call through function pointer
 *
 * @param b Builder
 * @param fn_ptr Function pointer value
 * @param args Arguments array
 * @param arg_count Number of arguments
 * @param name Name for result value
 * @return Result of call
 */
Celestial_Value* celestial_build_call_indirect(Celestial_Builder* b,
                                                Celestial_Value* fn_ptr,
                                                Celestial_Value** args,
                                                size_t arg_count,
                                                const char* name);

/**
 * @brief Build syscall instruction (for self-hosting file I/O)
 *
 * Emits a direct syscall instruction using the Linux syscall convention:
 * - Syscall number in RAX
 * - Arguments in RDI, RSI, RDX, R10, R8, R9
 * - Result returned in RAX
 *
 * @param b Builder
 * @param syscall_num Syscall number value
 * @param args Arguments array (up to 6)
 * @param arg_count Number of arguments (0-6)
 * @param name Name for result value
 * @return Result of syscall (in RAX)
 */
Celestial_Value* celestial_build_syscall(Celestial_Builder* b,
                                          Celestial_Value* syscall_num,
                                          Celestial_Value** args,
                                          size_t arg_count,
                                          const char* name);

/**
 * @brief Get function pointer value for a function
 *
 * @param mod Module
 * @param fn Function to get pointer for
 * @return Function pointer value
 */
Celestial_Value* celestial_get_fn_ptr(Celestial_Module* mod,
                                       Celestial_Function* fn);

/*------------------------------------------------------------------------
 * Comparison Instructions
 *------------------------------------------------------------------------*/

Celestial_Value* celestial_build_eq(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name);

Celestial_Value* celestial_build_lt(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name);

Celestial_Value* celestial_build_le(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name);

Celestial_Value* celestial_build_gt(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name);

Celestial_Value* celestial_build_ge(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name);

Celestial_Value* celestial_build_ne(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name);

/*------------------------------------------------------------------------
 * Memory Instructions
 *------------------------------------------------------------------------*/

Celestial_Value* celestial_build_alloca(Celestial_Builder* b,
                                         Celestial_Type* type,
                                         const char* name);

Celestial_Value* celestial_build_load(Celestial_Builder* b,
                                       Celestial_Value* ptr,
                                       Celestial_Type* type,
                                       const char* name);

void celestial_build_store(Celestial_Builder* b,
                           Celestial_Value* ptr,
                           Celestial_Value* value);

/*============================================================================
 * Struct/Array Operations
 *============================================================================*/

/**
 * @brief Get element pointer (for struct field or array element access)
 *
 * Returns a pointer to the specified field of a struct.
 *
 * @param b Builder
 * @param struct_ptr Pointer to struct
 * @param struct_type Type of the struct
 * @param field_idx Index of the field (0-based)
 * @param name Name for the result value
 * @return Pointer to the field
 */
Celestial_Value* celestial_build_gep(Celestial_Builder* b,
                                      Celestial_Value* struct_ptr,
                                      Celestial_Type* struct_type,
                                      size_t field_idx,
                                      const char* name);

/**
 * @brief Extract field from struct value
 *
 * @param b Builder
 * @param struct_val The struct value
 * @param field_idx Index of the field
 * @param name Name for the result
 * @return The field value
 */
Celestial_Value* celestial_build_extractfield(Celestial_Builder* b,
                                               Celestial_Value* struct_val,
                                               size_t field_idx,
                                               const char* name);

/**
 * @brief Insert field into struct value
 *
 * @param b Builder
 * @param struct_val The struct value to modify
 * @param field_val The value to insert
 * @param field_idx Index of the field
 * @param name Name for the result
 * @return New struct value with field updated
 */
Celestial_Value* celestial_build_insertfield(Celestial_Builder* b,
                                              Celestial_Value* struct_val,
                                              Celestial_Value* field_val,
                                              size_t field_idx,
                                              const char* name);

/**
 * @brief Calculate pointer to array element (GEP for arrays)
 *
 * @param b Builder
 * @param array_ptr Pointer to array
 * @param array_type Type of the array
 * @param index Runtime index value
 * @param name Name for the result
 * @return Pointer to the indexed element
 */
Celestial_Value* celestial_build_array_gep(Celestial_Builder* b,
                                            Celestial_Value* array_ptr,
                                            Celestial_Type* array_type,
                                            Celestial_Value* index,
                                            const char* name);

/**
 * @brief Extract element from array at runtime index
 *
 * @param b Builder
 * @param array_val The array value
 * @param index Runtime index value
 * @param name Name for the result
 * @return The element at the given index
 */
Celestial_Value* celestial_build_extractelem(Celestial_Builder* b,
                                              Celestial_Value* array_val,
                                              Celestial_Value* index,
                                              const char* name);

/**
 * @brief Insert element into array at runtime index
 *
 * @param b Builder
 * @param array_val The array value
 * @param elem_val The value to insert
 * @param index Runtime index value
 * @param name Name for the result
 * @return New array value with element updated
 */
Celestial_Value* celestial_build_insertelem(Celestial_Builder* b,
                                             Celestial_Value* array_val,
                                             Celestial_Value* elem_val,
                                             Celestial_Value* index,
                                             const char* name);

/*============================================================================
 * Type Utilities
 *============================================================================*/

/**
 * @brief Calculate size of a type in bytes
 *
 * @param type The type
 * @return Size in bytes
 */
size_t celestial_type_size(Celestial_Type* type);

/**
 * @brief Calculate offset of a struct field
 *
 * @param struct_type The struct type
 * @param field_idx Index of the field
 * @return Offset in bytes
 */
size_t celestial_type_field_offset(Celestial_Type* struct_type, size_t field_idx);

/*============================================================================
 * Verification
 *============================================================================*/

/**
 * @brief Verify module is well-formed
 *
 * Checks:
 * - All types are valid
 * - All values have types
 * - All terminators are present
 * - Effect declarations match actual effects
 * - Substrate contexts are properly nested
 *
 * @return VBIT_TRUE if valid
 */
Seraph_Vbit celestial_verify_module(Celestial_Module* mod);

/**
 * @brief Verify function is well-formed
 */
Seraph_Vbit celestial_verify_function(Celestial_Function* fn);

/*============================================================================
 * Debug Output
 *============================================================================*/

/**
 * @brief Print module to file
 */
void celestial_print_module(Celestial_Module* mod, FILE* out);

/**
 * @brief Print function to file
 */
void celestial_print_function(Celestial_Function* fn, FILE* out);

/*============================================================================
 * Optimization Passes
 *============================================================================*/

/**
 * @brief Run constant folding optimization
 *
 * Evaluates constant expressions at compile time,
 * replacing operations like `2 + 3` with the result `5`.
 *
 * @param mod Module to optimize
 * @return Number of instructions folded
 */
int celestial_fold_constants(Celestial_Module* mod);

/**
 * @brief Run dead code elimination
 *
 * Removes instructions whose results are never used
 * and have no side effects.
 *
 * @param mod Module to optimize
 * @return Number of instructions eliminated
 */
int celestial_eliminate_dead_code(Celestial_Module* mod);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_CELESTIAL_IR_H */
