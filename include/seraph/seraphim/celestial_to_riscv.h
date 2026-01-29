/**
 * @file celestial_to_riscv.h
 * @brief Celestial IR to RISC-V Code Generator
 *
 * This module compiles Celestial IR to RISC-V (RV64IMAC) native code.
 *
 * RISC-V ABI for SERAPH:
 *   a0-a7:  Arguments and return values
 *   t0-t6:  Caller-saved temporaries
 *   s0-s9:  Callee-saved registers
 *   s10:    Substrate context (SERAPH ABI)
 *   s11:    Capability context (SERAPH ABI)
 *   sp:     Stack pointer
 *   ra:     Return address
 */

#ifndef SERAPH_SERAPHIM_CELESTIAL_TO_RISCV_H
#define SERAPH_SERAPHIM_CELESTIAL_TO_RISCV_H

#include "celestial_ir.h"
#include "riscv_encode.h"
#include "seraph/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Register Allocation
 *============================================================================*/

typedef struct RV_LiveInterval {
    uint32_t              vreg_id;
    uint32_t              start;
    uint32_t              end;
    RV_Reg                assigned;
    struct RV_LiveInterval* next;
} RV_LiveInterval;

typedef struct {
    RV_LiveInterval*    intervals;
    RV_LiveInterval*    active;
    uint32_t            free_temps;
    uint32_t            free_saved;
    int32_t             spill_offset;
    Seraph_Arena*       arena;
} RV_RegAlloc;

void rv_regalloc_init(RV_RegAlloc* ra, Seraph_Arena* arena);
void rv_regalloc_function(RV_RegAlloc* ra, Celestial_Function* fn);
RV_Reg rv_regalloc_get(RV_RegAlloc* ra, uint32_t vreg);

/*============================================================================
 * Compilation Context
 *============================================================================*/

typedef struct RV_Label {
    const char*         name;
    size_t              offset;
    int                 resolved;
    struct RV_Label*    next;
} RV_Label;

typedef struct RV_Fixup {
    size_t              patch_pos;
    RV_Label*           target;
    int                 is_branch;
    struct RV_Fixup*    next;
} RV_Fixup;

typedef struct {
    RV_Buffer*          code;
    Celestial_Module*   module;
    Celestial_Function* function;
    RV_RegAlloc         regalloc;
    RV_Label*           labels;
    RV_Fixup*           fixups;
    RV_Label**          block_labels;
    int32_t             frame_size;
    int32_t             local_size;
    int32_t             save_size;
    Seraph_Arena*       arena;
} RV_Context;

void rv_context_init(RV_Context* ctx, RV_Buffer* code,
                     Celestial_Module* module, Seraph_Arena* arena);
RV_Label* rv_get_block_label(RV_Context* ctx, Celestial_Block* block);
void rv_resolve_fixups(RV_Context* ctx);

/*============================================================================
 * Code Generation
 *============================================================================*/

void rv_compile_module(RV_Context* ctx);
void rv_compile_function(RV_Context* ctx, Celestial_Function* fn);
void rv_emit_prologue(RV_Context* ctx);
void rv_emit_epilogue(RV_Context* ctx);
void rv_compile_block(RV_Context* ctx, Celestial_Block* block);
void rv_lower_instr(RV_Context* ctx, Celestial_Instr* instr);
void rv_lower_arith(RV_Context* ctx, Celestial_Instr* instr);
void rv_lower_cmp(RV_Context* ctx, Celestial_Instr* instr);
void rv_lower_control(RV_Context* ctx, Celestial_Instr* instr);
void rv_lower_void_op(RV_Context* ctx, Celestial_Instr* instr);

/*============================================================================
 * Helpers
 *============================================================================*/

RV_Reg rv_load_value(RV_Context* ctx, Celestial_Value* val);
void rv_store_value(RV_Context* ctx, RV_Reg reg, Celestial_Value* val);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_CELESTIAL_TO_RISCV_H */
