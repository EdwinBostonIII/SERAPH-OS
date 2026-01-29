/**
 * @file celestial_to_x64.h
 * @brief MC29: Celestial IR to x86-64 Backend
 *
 * This is the bridge between Celestial IR and native x86-64 machine code.
 * It performs:
 *   1. Register Allocation - Linear scan allocator for virtual → physical mapping
 *   2. Instruction Selection - Pattern matching from IR to x64 instructions
 *   3. Code Generation - Emit machine code via x64_encode.c
 *   4. VOID Lowering - Implement SERAPH's tristate semantics in hardware
 *   5. Capability Lowering - Bounds/generation checks for memory safety
 *
 * ARCHITECTURE DECISIONS:
 *
 * 1. RESERVED REGISTERS (SERAPH ABI):
 *    - RSP: Stack pointer (system)
 *    - RBP: Frame pointer (debugging/unwinding)
 *    - R13: Current substrate context pointer
 *    - R14: Capability context pointer (revocation table)
 *    - R15: Reserved for VOID state (future optimization)
 *
 * 2. VOID REPRESENTATION:
 *    For 64-bit scalar values: Bit 63 = VOID flag
 *    When bit 63 is set, the value is VOID (undefined/error).
 *    This allows fast checking via BT (bit test) instruction.
 *
 * 3. CAPABILITY LAYOUT (256 bits):
 *    [0-7]   base     : Base address
 *    [8-15]  length   : Bounds length
 *    [16-23] generation: Temporal safety counter
 *    [24-31] permissions: Access rights bitmap
 *
 * 4. GALACTIC NUMBERS (512 bits):
 *    4 components of Q64.64 fixed-point (128 bits each).
 *    Decomposed to scalar operations on x64 (no AVX-512 dependency).
 *
 * NIH COMPLIANCE: This file has ZERO external dependencies beyond
 * standard C and SERAPH's own headers.
 */

#ifndef SERAPH_SERAPHIM_CELESTIAL_TO_X64_H
#define SERAPH_SERAPHIM_CELESTIAL_TO_X64_H

#include <stdint.h>
#include <stddef.h>
#include "seraph/vbit.h"
#include "seraph/arena.h"
#include "seraph/seraphim/celestial_ir.h"
#include "seraph/seraphim/x64_encode.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** VOID flag is bit 63 for 64-bit scalar values */
#define SERAPH_X64_VOID_BIT 63

/** VOID value constant (bit 63 set) */
#define SERAPH_X64_VOID_VALUE 0x8000000000000000ULL

/** Maximum virtual registers per function */
#define SERAPH_X64_MAX_VREGS 4096

/** Maximum spill slots per function */
#define SERAPH_X64_MAX_SPILL_SLOTS 1024

/** Maximum basic blocks per function */
#define SERAPH_X64_MAX_BLOCKS 4096

/*============================================================================
 * SERAPH x64 ABI - Reserved Registers
 *============================================================================*/

/** Stack pointer (system reserved) */
#define SERAPH_X64_RSP X64_RSP

/** Frame pointer (for debugging/stack unwinding) */
#define SERAPH_X64_RBP X64_RBP

/** Substrate context pointer */
#define SERAPH_X64_SUBSTRATE_REG X64_R13

/** Capability context pointer (revocation table) */
#define SERAPH_X64_CAP_CTX_REG X64_R14

/** Reserved for future VOID optimization */
#define SERAPH_X64_VOID_REG X64_R15

/*============================================================================
 * Capability Context Offsets
 *============================================================================*/

/** Offset to current generation counter in capability context */
#define SERAPH_CAP_CTX_GEN_OFFSET 0

/** Offset to revocation table in capability context */
#define SERAPH_CAP_CTX_REVOKE_TABLE_OFFSET 8

/*============================================================================
 * Capability Structure Offsets
 *============================================================================*/

#define SERAPH_CAP_BASE_OFFSET 0
#define SERAPH_CAP_LENGTH_OFFSET 8
#define SERAPH_CAP_GEN_OFFSET 16
#define SERAPH_CAP_PERMS_OFFSET 24
#define SERAPH_CAP_SIZE 32

/** Capability permission bits */
#define SERAPH_CAP_PERM_READ   (1 << 0)
#define SERAPH_CAP_PERM_WRITE  (1 << 1)
#define SERAPH_CAP_PERM_EXEC   (1 << 2)
#define SERAPH_CAP_PERM_DERIVE (1 << 3)

/*============================================================================
 * Live Interval (for Register Allocation)
 *============================================================================*/

/**
 * @brief Live interval for a virtual register
 *
 * Tracks the lifetime of a virtual register for linear scan allocation.
 */
typedef struct {
    uint32_t vreg_id;        /**< Virtual register ID from Celestial IR */
    uint32_t start;          /**< First use (instruction index) */
    uint32_t end;            /**< Last use (instruction index) */
    X64_Reg  phys_reg;       /**< Assigned physical register (or X64_NONE) */
    int32_t  spill_offset;   /**< Stack offset if spilled (-1 if not) */
    uint8_t  reg_class;      /**< 0=GP, 1=XMM (for future SIMD) */
    uint8_t  is_param;       /**< 1 if this is a function parameter */
    uint8_t  is_callee_save; /**< 1 if assigned to callee-saved register */
    uint8_t  reserved;       /**< Padding */
} X64_LiveInterval;

/*============================================================================
 * Register Allocator State
 *============================================================================*/

/**
 * @brief Register allocator state
 */
typedef struct {
    /** Live intervals for all virtual registers */
    X64_LiveInterval* intervals;
    uint32_t          interval_count;
    uint32_t          interval_capacity;

    /** Active intervals (currently occupying registers) */
    uint32_t*         active;           /**< Indices into intervals array */
    uint32_t          active_count;

    /** Free register pool (bit set = available) */
    uint32_t          gp_free;          /**< Bitmask of free GP registers */

    /** Spill slot management */
    int32_t           next_spill_offset; /**< Next available spill slot */
    uint32_t          max_spill_size;    /**< Total spill area size */

    /** Statistics */
    uint32_t          spill_count;
    uint32_t          reload_count;
} X64_RegAlloc;

/*============================================================================
 * Block Layout Information
 *============================================================================*/

/**
 * @brief Information about a basic block in the generated code
 */
typedef struct {
    Celestial_Block* ir_block;   /**< Original IR block */
    uint32_t         label_id;   /**< Label ID for this block */
    size_t           code_offset;/**< Offset in output buffer */
    uint32_t         instr_start;/**< First instruction index */
    uint32_t         instr_count;/**< Number of instructions */
} X64_BlockInfo;

/*============================================================================
 * Module-Wide Call Fixup Tracking
 *============================================================================*/

/** Maximum function calls that can be tracked for patching */
#define SERAPH_X64_MAX_CALL_FIXUPS 4096

/** Maximum function pointer loads that can be tracked for patching */
#define SERAPH_X64_MAX_FNPTR_FIXUPS 4096

/** Maximum functions per module */
#define SERAPH_X64_MAX_FUNCTIONS 1024

/**
 * @brief Tracks a function call site that needs patching
 */
typedef struct {
    size_t              call_site;   /**< Offset of rel32 to patch in output */
    Celestial_Function* callee;      /**< Function being called */
} X64_CallFixup;

/**
 * @brief Tracks a function pointer load that needs patching
 */
typedef struct {
    size_t              fixup_site;  /**< Offset of rel32 to patch in output */
    Celestial_Function* fn;          /**< Function whose address is being loaded */
} X64_FnptrFixup;

/**
 * @brief Tracks a function's location in the output
 */
typedef struct {
    Celestial_Function* fn;          /**< The function */
    size_t              offset;      /**< Offset in output buffer */
} X64_FunctionEntry;

/**
 * @brief Module-wide compilation context for call resolution
 */
typedef struct {
    /** Function location table */
    X64_FunctionEntry*  functions;
    size_t              function_count;
    size_t              function_capacity;

    /** Call fixup table */
    X64_CallFixup*      call_fixups;
    size_t              call_fixup_count;
    size_t              call_fixup_capacity;

    /** Function pointer fixup table */
    X64_FnptrFixup*     fnptr_fixups;
    size_t              fnptr_fixup_count;
    size_t              fnptr_fixup_capacity;
} X64_ModuleContext;

/*============================================================================
 * Compilation Context
 *============================================================================*/

/**
 * @brief Context for compiling a function
 */
typedef struct {
    /** Source IR */
    Celestial_Module*   module;
    Celestial_Function* function;

    /** Output */
    X64_Buffer*         output;
    X64_Labels*         labels;

    /** Register allocation */
    X64_RegAlloc        regalloc;

    /** Block information */
    X64_BlockInfo*      blocks;
    uint32_t            block_count;

    /** Value → physical location mapping */
    struct {
        uint32_t value_id;
        X64_Reg  reg;
        int32_t  stack_offset;  /**< -1 if in register */
    }* value_locations;
    uint32_t            value_loc_count;
    uint32_t            value_loc_capacity;

    /** Current instruction index (for live interval computation) */
    uint32_t            current_instr_idx;

    /** Stack frame information */
    int32_t             frame_size;
    int32_t             locals_offset;

    /** Memory allocation */
    Seraph_Arena*       arena;

    /** Module-wide context for call fixup resolution */
    X64_ModuleContext*  mod_ctx;

    /** Error tracking */
    const char*         error_msg;
    uint32_t            error_line;
} X64_CompileContext;

/*============================================================================
 * Module Compilation
 *============================================================================*/

/**
 * @brief Compile an entire Celestial module to x86-64
 *
 * @param mod The Celestial IR module to compile
 * @param output Buffer to write machine code to
 * @param arena Arena for temporary allocations
 * @return VBIT_TRUE on success, VBIT_FALSE on error
 */
Seraph_Vbit celestial_compile_module(Celestial_Module* mod,
                                      X64_Buffer* output,
                                      Seraph_Arena* arena);

/**
 * @brief Compile a single function to x86-64
 *
 * @param fn The function to compile
 * @param mod The containing module (for globals/types)
 * @param output Buffer to write machine code to
 * @param labels Label table for jump targets
 * @param arena Arena for temporary allocations
 * @return VBIT_TRUE on success, VBIT_FALSE on error
 */
Seraph_Vbit celestial_compile_function(Celestial_Function* fn,
                                        Celestial_Module* mod,
                                        X64_Buffer* output,
                                        X64_Labels* labels,
                                        Seraph_Arena* arena,
                                        X64_ModuleContext* mod_ctx);

/*============================================================================
 * Register Allocation
 *============================================================================*/

/**
 * @brief Initialize register allocator
 *
 * @param ra Register allocator to initialize
 * @param arena Arena for allocations
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_regalloc_init(X64_RegAlloc* ra, Seraph_Arena* arena);

/**
 * @brief Compute live intervals for a function
 *
 * @param ctx Compilation context
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_compute_live_intervals(X64_CompileContext* ctx);

/**
 * @brief Perform linear scan register allocation
 *
 * @param ctx Compilation context
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_linear_scan_allocate(X64_CompileContext* ctx);

/**
 * @brief Get physical location for a Celestial value
 *
 * @param ctx Compilation context
 * @param value The value to look up
 * @param out_reg Output: physical register (or X64_NONE if spilled)
 * @param out_offset Output: stack offset if spilled
 * @return VBIT_TRUE if found
 */
Seraph_Vbit x64_get_value_location(X64_CompileContext* ctx,
                                    Celestial_Value* value,
                                    X64_Reg* out_reg,
                                    int32_t* out_offset);

/*============================================================================
 * Instruction Lowering
 *============================================================================*/

/**
 * @brief Lower a single Celestial instruction to x64
 *
 * @param ctx Compilation context
 * @param instr The instruction to lower
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_lower_instruction(X64_CompileContext* ctx,
                                   Celestial_Instr* instr);

/**
 * @brief Lower arithmetic instruction (ADD, SUB, MUL, DIV)
 */
Seraph_Vbit x64_lower_arithmetic(X64_CompileContext* ctx,
                                  Celestial_Instr* instr);

/**
 * @brief Lower bitwise instruction (AND, OR, XOR, NOT, shifts)
 */
Seraph_Vbit x64_lower_bitwise(X64_CompileContext* ctx,
                               Celestial_Instr* instr);

/**
 * @brief Lower comparison instruction
 */
Seraph_Vbit x64_lower_comparison(X64_CompileContext* ctx,
                                  Celestial_Instr* instr);

/**
 * @brief Lower control flow instruction (JUMP, BRANCH, CALL, RETURN)
 */
Seraph_Vbit x64_lower_control_flow(X64_CompileContext* ctx,
                                    Celestial_Instr* instr);

/**
 * @brief Lower VOID operation (TEST, PROP, ASSERT, COALESCE)
 */
Seraph_Vbit x64_lower_void_op(X64_CompileContext* ctx,
                               Celestial_Instr* instr);

/**
 * @brief Lower capability operation (CREATE, LOAD, STORE, CHECK)
 */
Seraph_Vbit x64_lower_capability_op(X64_CompileContext* ctx,
                                     Celestial_Instr* instr);

/**
 * @brief Lower memory operation (LOAD, STORE, ALLOCA)
 */
Seraph_Vbit x64_lower_memory_op(X64_CompileContext* ctx,
                                 Celestial_Instr* instr);

/**
 * @brief Lower Galactic number operation
 */
Seraph_Vbit x64_lower_galactic_op(X64_CompileContext* ctx,
                                   Celestial_Instr* instr);

/**
 * @brief Lower substrate operation (ENTER, EXIT, ATLAS_*, AETHER_*)
 */
Seraph_Vbit x64_lower_substrate_op(X64_CompileContext* ctx,
                                    Celestial_Instr* instr);

/**
 * @brief Lower type conversion (TRUNC, ZEXT, SEXT, BITCAST)
 */
Seraph_Vbit x64_lower_conversion(X64_CompileContext* ctx,
                                  Celestial_Instr* instr);

/*============================================================================
 * Code Generation Helpers
 *============================================================================*/

/**
 * @brief Emit function prologue
 *
 * Sets up stack frame, saves callee-saved registers, initializes
 * substrate and capability context pointers.
 *
 * @param ctx Compilation context
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_emit_prologue(X64_CompileContext* ctx);

/**
 * @brief Emit function epilogue
 *
 * Restores callee-saved registers, tears down stack frame.
 *
 * @param ctx Compilation context
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_emit_epilogue(X64_CompileContext* ctx);

/**
 * @brief Load a value into a register
 *
 * If the value is already in a register, may emit a MOV.
 * If spilled, emits a load from stack.
 *
 * @param ctx Compilation context
 * @param value The value to load
 * @param dst_reg Destination register
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_load_value(X64_CompileContext* ctx,
                            Celestial_Value* value,
                            X64_Reg dst_reg);

/**
 * @brief Store a register to a value's location
 *
 * @param ctx Compilation context
 * @param src_reg Source register
 * @param value The value to store to
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_store_value(X64_CompileContext* ctx,
                             X64_Reg src_reg,
                             Celestial_Value* value);

/**
 * @brief Emit VOID check for a value
 *
 * Tests bit 63 of the value. If set, jumps to void_label.
 *
 * @param ctx Compilation context
 * @param reg Register containing the value
 * @param void_label Label to jump to if VOID
 */
void x64_emit_void_check(X64_CompileContext* ctx,
                         X64_Reg reg,
                         uint32_t void_label);

/**
 * @brief Emit VOID propagation code
 *
 * If value is VOID, return VOID from function.
 *
 * @param ctx Compilation context
 * @param reg Register containing the value
 */
void x64_emit_void_propagate(X64_CompileContext* ctx, X64_Reg reg);

/**
 * @brief Emit capability bounds check
 *
 * Checks that offset is within capability bounds.
 * Jumps to fail_label if out of bounds.
 *
 * @param ctx Compilation context
 * @param cap_reg Register pointing to capability
 * @param offset_reg Register containing offset
 * @param fail_label Label for bounds failure
 */
void x64_emit_cap_bounds_check(X64_CompileContext* ctx,
                               X64_Reg cap_reg,
                               X64_Reg offset_reg,
                               uint32_t fail_label);

/**
 * @brief Emit capability generation check
 *
 * Checks that capability generation is valid.
 * Jumps to fail_label if revoked.
 *
 * @param ctx Compilation context
 * @param cap_reg Register pointing to capability
 * @param fail_label Label for revocation failure
 */
void x64_emit_cap_gen_check(X64_CompileContext* ctx,
                            X64_Reg cap_reg,
                            uint32_t fail_label);

/**
 * @brief Emit capability permission check
 *
 * Checks that capability has required permission.
 * Jumps to fail_label if permission denied.
 *
 * @param ctx Compilation context
 * @param cap_reg Register pointing to capability
 * @param perm_mask Permission bits required
 * @param fail_label Label for permission failure
 */
void x64_emit_cap_perm_check(X64_CompileContext* ctx,
                             X64_Reg cap_reg,
                             uint32_t perm_mask,
                             uint32_t fail_label);

/*============================================================================
 * 128-bit Arithmetic Helpers (for Q64.64 / Scalar)
 *============================================================================*/

/**
 * @brief Emit 128-bit addition (for Q64.64 fixed-point)
 *
 * Inputs: (rdx:rax) and (r9:r8)
 * Output: (rdx:rax)
 *
 * @param ctx Compilation context
 */
void x64_emit_add128(X64_CompileContext* ctx);

/**
 * @brief Emit 128-bit subtraction
 *
 * @param ctx Compilation context
 */
void x64_emit_sub128(X64_CompileContext* ctx);

/**
 * @brief Emit 128x128 → 128 multiplication (Q64.64)
 *
 * Multiplies two Q64.64 values and returns the correctly scaled result.
 *
 * @param ctx Compilation context
 */
void x64_emit_mul128(X64_CompileContext* ctx);

/*============================================================================
 * Galactic Number Helpers (512-bit)
 *============================================================================*/

/**
 * @brief Emit Galactic addition
 *
 * Adds two Galactic numbers (4 Q64.64 components each).
 *
 * @param ctx Compilation context
 * @param dst_offset Stack offset for destination
 * @param src1_offset Stack offset for first source
 * @param src2_offset Stack offset for second source
 */
void x64_emit_galactic_add(X64_CompileContext* ctx,
                           int32_t dst_offset,
                           int32_t src1_offset,
                           int32_t src2_offset);

/**
 * @brief Emit Galactic multiplication (chain rule)
 *
 * Multiplies two Galactic numbers with proper derivative propagation.
 *
 * @param ctx Compilation context
 * @param dst_offset Stack offset for destination
 * @param src1_offset Stack offset for first source
 * @param src2_offset Stack offset for second source
 */
void x64_emit_galactic_mul(X64_CompileContext* ctx,
                           int32_t dst_offset,
                           int32_t src1_offset,
                           int32_t src2_offset);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Get x64 operand size from Celestial type
 *
 * @param type The Celestial type
 * @return X64_Size enum value
 */
X64_Size x64_size_from_type(Celestial_Type* type);

/**
 * @brief Get x64 condition code from Celestial comparison opcode
 *
 * @param opcode The Celestial comparison opcode
 * @return X64_Condition enum value
 */
X64_Condition x64_cc_from_cir_cmp(Celestial_Opcode opcode);

/**
 * @brief Check if a register is allocatable (not reserved)
 *
 * @param reg The register to check
 * @return true if can be allocated
 */
static inline int x64_is_allocatable(X64_Reg reg) {
    /* Reserved: RSP, RBP (stack/frame pointers)
     * Scratch registers (clobbered by instruction lowering): RAX, RCX, RDX, RDI
     * All others are allocatable including callee-saved R12-R15 */
    return reg != X64_RSP && reg != X64_RBP &&
           reg != X64_RAX && reg != X64_RCX && reg != X64_RDX && reg != X64_RDI;
}

/**
 * @brief Check if register is caller-saved
 *
 * @param reg The register to check
 * @return true if caller-saved (volatile)
 */
static inline int x64_is_caller_saved(X64_Reg reg) {
    return reg == X64_RAX || reg == X64_RCX || reg == X64_RDX ||
           reg == X64_RSI || reg == X64_RDI ||
           reg == X64_R8  || reg == X64_R9  || reg == X64_R10 || reg == X64_R11;
}

/**
 * @brief Check if register is callee-saved
 *
 * @param reg The register to check
 * @return true if callee-saved (must be preserved)
 */
static inline int x64_is_callee_saved(X64_Reg reg) {
    return reg == X64_RBX || reg == X64_R12 ||
           reg == X64_R13 || reg == X64_R14 || reg == X64_R15;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_CELESTIAL_TO_X64_H */
