/**
 * @file celestial_to_arm64.h
 * @brief Celestial IR to ARM64 Code Generator
 *
 * This module compiles Celestial IR to ARM64 (AArch64) native code.
 * It follows the same architecture as celestial_to_x64.h but targets
 * the ARM64 instruction set.
 *
 * ARM64 ABI for SERAPH:
 *   X0-X7:  Arguments and return values
 *   X8:     Indirect result location
 *   X9-X15: Caller-saved temporaries
 *   X16-X17: Intra-procedure-call scratch
 *   X18:    Platform register (reserved)
 *   X19-X26: Callee-saved registers
 *   X27:    Substrate context (SERAPH ABI)
 *   X28:    Capability context (SERAPH ABI)
 *   X29:    Frame pointer
 *   X30:    Link register
 *   SP:     Stack pointer
 */

#ifndef SERAPH_SERAPHIM_CELESTIAL_TO_ARM64_H
#define SERAPH_SERAPHIM_CELESTIAL_TO_ARM64_H

#include "celestial_ir.h"
#include "arm64_encode.h"
#include "seraph/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Register Allocation
 *============================================================================*/

/**
 * @brief Live interval for register allocation
 */
typedef struct ARM64_LiveInterval {
    uint32_t              vreg_id;    /**< Virtual register ID */
    uint32_t              start;      /**< Start position */
    uint32_t              end;        /**< End position */
    ARM64_Reg             assigned;   /**< Assigned physical register */
    struct ARM64_LiveInterval* next;  /**< Next interval in list */
} ARM64_LiveInterval;

/**
 * @brief Register allocator state
 */
typedef struct {
    ARM64_LiveInterval* intervals;    /**< All intervals (sorted by start) */
    ARM64_LiveInterval* active;       /**< Currently active intervals */

    /* Free register pools */
    uint32_t            free_regs;    /**< Bitmask of free caller-saved regs */
    uint32_t            free_callee;  /**< Bitmask of free callee-saved regs */

    /* Spill management */
    int32_t             spill_offset; /**< Current spill slot offset */
    int32_t*            spill_slots;  /**< Virtual reg -> spill slot */
    size_t              spill_count;  /**< Number of spilled registers */

    Seraph_Arena*       arena;        /**< Memory allocator */
} ARM64_RegAlloc;

/**
 * @brief Initialize register allocator
 */
void arm64_regalloc_init(ARM64_RegAlloc* ra, Seraph_Arena* arena);

/**
 * @brief Perform register allocation for a function
 */
void arm64_regalloc_function(ARM64_RegAlloc* ra, Celestial_Function* fn);

/**
 * @brief Get physical register for virtual register
 */
ARM64_Reg arm64_regalloc_get(ARM64_RegAlloc* ra, uint32_t vreg);

/*============================================================================
 * Compilation Context
 *============================================================================*/

/**
 * @brief Label for forward references
 */
typedef struct ARM64_Label {
    const char*         name;         /**< Label name */
    size_t              offset;       /**< Position in output buffer (in instructions) */
    int                 resolved;     /**< Whether offset is known */
    struct ARM64_Label* next;         /**< Next label */
} ARM64_Label;

/**
 * @brief Forward reference (branch target)
 */
typedef struct ARM64_Fixup {
    size_t              patch_pos;    /**< Position to patch (in instructions) */
    ARM64_Label*        target;       /**< Target label */
    int                 is_cond;      /**< Is conditional branch */
    struct ARM64_Fixup* next;         /**< Next fixup */
} ARM64_Fixup;

/**
 * @brief ARM64 compilation context
 */
typedef struct {
    ARM64_Buffer*       code;         /**< Output code buffer */
    Celestial_Module*   module;       /**< IR module being compiled */
    Celestial_Function* function;     /**< Current function */

    ARM64_RegAlloc      regalloc;     /**< Register allocator */

    /* Labels and fixups */
    ARM64_Label*        labels;       /**< All labels */
    ARM64_Fixup*        fixups;       /**< Forward references to patch */

    /* Block label mapping */
    ARM64_Label**       block_labels; /**< block_id -> label */

    /* Stack frame info */
    int32_t             frame_size;   /**< Total stack frame size */
    int32_t             local_size;   /**< Size of local variables */
    int32_t             save_size;    /**< Size of saved registers */

    Seraph_Arena*       arena;        /**< Memory allocator */
} ARM64_Context;

/**
 * @brief Initialize ARM64 compilation context
 */
void arm64_context_init(ARM64_Context* ctx, ARM64_Buffer* code,
                        Celestial_Module* module, Seraph_Arena* arena);

/**
 * @brief Get or create label for a block
 */
ARM64_Label* arm64_get_block_label(ARM64_Context* ctx, Celestial_Block* block);

/**
 * @brief Resolve all forward references
 */
void arm64_resolve_fixups(ARM64_Context* ctx);

/*============================================================================
 * Code Generation Entry Points
 *============================================================================*/

/**
 * @brief Compile entire module to ARM64
 */
void arm64_compile_module(ARM64_Context* ctx);

/**
 * @brief Compile a single function
 */
void arm64_compile_function(ARM64_Context* ctx, Celestial_Function* fn);

/**
 * @brief Generate function prologue
 */
void arm64_emit_prologue(ARM64_Context* ctx);

/**
 * @brief Generate function epilogue
 */
void arm64_emit_epilogue(ARM64_Context* ctx);

/**
 * @brief Compile a single block
 */
void arm64_compile_block(ARM64_Context* ctx, Celestial_Block* block);

/*============================================================================
 * Instruction Lowering
 *============================================================================*/

/**
 * @brief Lower a single IR instruction to ARM64
 */
void arm64_lower_instr(ARM64_Context* ctx, Celestial_Instr* instr);

/**
 * @brief Lower arithmetic instructions
 */
void arm64_lower_arith(ARM64_Context* ctx, Celestial_Instr* instr);

/**
 * @brief Lower comparison instructions
 */
void arm64_lower_cmp(ARM64_Context* ctx, Celestial_Instr* instr);

/**
 * @brief Lower control flow instructions
 */
void arm64_lower_control(ARM64_Context* ctx, Celestial_Instr* instr);

/**
 * @brief Lower VOID operations
 */
void arm64_lower_void_op(ARM64_Context* ctx, Celestial_Instr* instr);

/**
 * @brief Lower capability operations
 */
void arm64_lower_cap_op(ARM64_Context* ctx, Celestial_Instr* instr);

/**
 * @brief Lower substrate operations
 */
void arm64_lower_substrate_op(ARM64_Context* ctx, Celestial_Instr* instr);

/*============================================================================
 * Helpers
 *============================================================================*/

/**
 * @brief Load value into register
 */
ARM64_Reg arm64_load_value(ARM64_Context* ctx, Celestial_Value* val);

/**
 * @brief Store register to value location
 */
void arm64_store_value(ARM64_Context* ctx, ARM64_Reg reg, Celestial_Value* val);

/**
 * @brief Load immediate into register
 */
void arm64_load_imm(ARM64_Context* ctx, ARM64_Reg reg, int64_t imm);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_CELESTIAL_TO_ARM64_H */
