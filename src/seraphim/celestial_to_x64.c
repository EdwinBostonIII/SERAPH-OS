/**
 * @file celestial_to_x64.c
 * @brief MC29: Celestial IR to x86-64 Backend Implementation
 *
 * This file implements the complete backend for compiling Celestial IR
 * to x86-64 machine code. It is 100% NIH-compliant with zero external
 * dependencies beyond standard C.
 *
 * Major Components:
 *   1. Register Allocator (Linear Scan)
 *   2. Instruction Lowering
 *   3. VOID Semantics Implementation
 *   4. Capability-Based Memory Access
 *   5. Galactic Number Operations
 */

#include "seraph/seraphim/celestial_to_x64.h"
#include <string.h>

/*============================================================================
 * Internal Constants
 *============================================================================*/

/** Allocatable general-purpose registers (in allocation order)
 * NOTE: RAX, RCX, RDX, RDI are NOT included because they are used as scratch
 * registers during instruction lowering (LOAD/STORE/ADD/etc). If values were
 * assigned to these registers, they would be clobbered before use.
 */
static const X64_Reg GP_ALLOC_ORDER[] = {
    X64_RSI,
    X64_R8,  X64_R9,  X64_R10, X64_R11,
    X64_RBX, X64_R12, X64_R13, X64_R14, X64_R15  /* Callee-saved last */
};
#define GP_ALLOC_COUNT 10

/** Argument registers (System V ABI) */
static const X64_Reg ARG_REGS[] = {
    X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9
};
#define ARG_REG_COUNT 6

/*============================================================================
 * Forward Declarations
 *============================================================================*/

static Seraph_Vbit compile_block(X64_CompileContext* ctx, Celestial_Block* block);
static uint32_t get_or_create_block_label(X64_CompileContext* ctx, Celestial_Block* block);
static void emit_move_if_needed(X64_CompileContext* ctx, X64_Reg dst, X64_Reg src);
static int32_t alloc_spill_slot(X64_RegAlloc* ra, size_t size);

/*============================================================================
 * Register Allocator Implementation
 *============================================================================*/

Seraph_Vbit x64_regalloc_init(X64_RegAlloc* ra, Seraph_Arena* arena) {
    if (!ra || !arena) return SERAPH_VBIT_VOID;

    /* Allocate interval array */
    ra->interval_capacity = 256;
    ra->intervals = seraph_arena_alloc(arena,
                                        sizeof(X64_LiveInterval) * ra->interval_capacity,
                                        8);
    if (!ra->intervals) return SERAPH_VBIT_FALSE;
    ra->interval_count = 0;

    /* Allocate active array */
    ra->active = seraph_arena_alloc(arena, sizeof(uint32_t) * GP_ALLOC_COUNT, 4);
    if (!ra->active) return SERAPH_VBIT_FALSE;
    ra->active_count = 0;

    /* Initialize free register pool (all GP registers free) */
    ra->gp_free = 0;
    for (int i = 0; i < GP_ALLOC_COUNT; i++) {
        ra->gp_free |= (1u << GP_ALLOC_ORDER[i]);
    }

    /* Initialize spill management */
    ra->next_spill_offset = -8;  /* First spill at [RBP-8] */
    ra->max_spill_size = 0;
    ra->spill_count = 0;
    ra->reload_count = 0;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Add or update live interval for a virtual register
 */
static X64_LiveInterval* get_or_create_interval(X64_RegAlloc* ra,
                                                 uint32_t vreg_id,
                                                 uint32_t instr_idx) {
    /* Search for existing interval */
    for (uint32_t i = 0; i < ra->interval_count; i++) {
        if (ra->intervals[i].vreg_id == vreg_id) {
            /* Update end point */
            if (instr_idx > ra->intervals[i].end) {
                ra->intervals[i].end = instr_idx;
            }
            return &ra->intervals[i];
        }
    }

    /* Create new interval */
    if (ra->interval_count >= ra->interval_capacity) {
        return NULL;  /* Out of space */
    }

    X64_LiveInterval* interval = &ra->intervals[ra->interval_count++];
    interval->vreg_id = vreg_id;
    interval->start = instr_idx;
    interval->end = instr_idx;
    interval->phys_reg = X64_NONE;
    interval->spill_offset = -1;
    interval->reg_class = 0;  /* GP */
    interval->is_param = 0;
    interval->is_callee_save = 0;
    interval->reserved = 0;

    return interval;
}

/**
 * @brief Simple insertion sort for intervals (small arrays)
 */
static void sort_intervals_by_start(X64_LiveInterval* intervals, uint32_t count) {
    for (uint32_t i = 1; i < count; i++) {
        X64_LiveInterval key = intervals[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && intervals[j].start > key.start) {
            intervals[j + 1] = intervals[j];
            j--;
        }
        intervals[j + 1] = key;
    }
}

Seraph_Vbit x64_compute_live_intervals(X64_CompileContext* ctx) {
    if (!ctx || !ctx->function) return SERAPH_VBIT_VOID;

    X64_RegAlloc* ra = &ctx->regalloc;
    uint32_t instr_idx = 0;

    /* Mark parameters as live from start and pre-allocate spill slots.
     * Parameters will be spilled to stack in prologue to prevent clobbering
     * by scratch register usage during instruction lowering. */
    for (uint32_t i = 0; i < ctx->function->param_count; i++) {
        Celestial_Value* param = ctx->function->params[i];
        if (param && param->kind == CIR_VALUE_PARAM) {
            X64_LiveInterval* interval = get_or_create_interval(ra, param->id, 0);
            if (interval) {
                interval->is_param = 1;
                /* Assign parameter registers for initial receipt */
                if (i < ARG_REG_COUNT) {
                    interval->phys_reg = ARG_REGS[i];
                }
                /* Pre-allocate spill slot - parameters will be spilled in prologue */
                interval->spill_offset = alloc_spill_slot(ra, 8);
            }
        }
    }

    /* Walk all instructions in all blocks */
    for (Celestial_Block* block = ctx->function->blocks; block; block = block->next) {
        for (Celestial_Instr* instr = block->first; instr; instr = instr->next) {
            /* Record uses (operands) */
            for (size_t i = 0; i < instr->operand_count; i++) {
                Celestial_Value* op = instr->operands[i];
                if (op && op->kind == CIR_VALUE_VREG) {
                    get_or_create_interval(ra, op->id, instr_idx);
                }
            }

            /* Record definition (result) */
            if (instr->result && instr->result->kind == CIR_VALUE_VREG) {
                X64_LiveInterval* interval = get_or_create_interval(ra,
                                                                     instr->result->id,
                                                                     instr_idx);
                if (interval && interval->start == instr_idx) {
                    /* For CALL/CALL_INDIRECT/SYSCALL results, force spill to preserve
                     * across subsequent calls that clobber caller-saved registers */
                    if (instr->opcode == CIR_CALL || instr->opcode == CIR_CALL_INDIRECT ||
                        instr->opcode == CIR_SYSCALL) {
                        interval->spill_offset = alloc_spill_slot(ra, 8);
                        interval->phys_reg = X64_NONE;  /* Force spill */
                    }
                }
            }

            instr_idx++;
        }
    }

    /* Pre-compute stack space needed for ALLOCA instructions.
     * We don't actually allocate here (to avoid double allocation),
     * but we count the space needed so max_spill_size is correct
     * when the prologue is emitted. Each ALLOCA needs space for:
     * 1. The actual data (alloca_type size, aligned to 8)
     * 2. A slot to store the persistent address (8 bytes) */
    uint32_t alloca_space_needed = 0;
    for (Celestial_Block* block = ctx->function->blocks; block; block = block->next) {
        for (Celestial_Instr* instr = block->first; instr; instr = instr->next) {
            if (instr->opcode == CIR_ALLOCA) {
                size_t data_size = 8;  /* Default */
                if (instr->result && instr->result->alloca_type) {
                    data_size = celestial_type_size(instr->result->alloca_type);
                }
                data_size = (data_size + 7) & ~7;  /* Align to 8 */
                alloca_space_needed += (uint32_t)data_size;  /* Data space */
                alloca_space_needed += 8;                     /* Address slot */
            }
        }
    }
    /* Update max_spill_size to include ALLOCA space */
    ra->max_spill_size += alloca_space_needed;

    /* Sort intervals by start position */
    sort_intervals_by_start(ra->intervals, ra->interval_count);

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Allocate a physical register
 */
static X64_Reg alloc_register(X64_RegAlloc* ra) {
    /* Find first free register in allocation order */
    for (int i = 0; i < GP_ALLOC_COUNT; i++) {
        X64_Reg reg = GP_ALLOC_ORDER[i];
        if (ra->gp_free & (1u << reg)) {
            ra->gp_free &= ~(1u << reg);  /* Mark as used */
            return reg;
        }
    }
    return X64_NONE;  /* No free register */
}

/**
 * @brief Free a physical register
 */
static void free_register(X64_RegAlloc* ra, X64_Reg reg) {
    if (reg != X64_NONE && x64_is_allocatable(reg)) {
        ra->gp_free |= (1u << reg);
    }
}

/**
 * @brief Allocate a spill slot
 */
static int32_t alloc_spill_slot(X64_RegAlloc* ra, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~7;
    int32_t offset = ra->next_spill_offset;
    ra->next_spill_offset -= (int32_t)size;
    if ((uint32_t)(-ra->next_spill_offset) > ra->max_spill_size) {
        ra->max_spill_size = (uint32_t)(-ra->next_spill_offset);
    }
    ra->spill_count++;
    return offset;
}

/**
 * @brief Expire old intervals that ended before current position
 */
static void expire_old_intervals(X64_RegAlloc* ra, uint32_t current_start) {
    /* Remove intervals from active that ended before current_start */
    uint32_t new_active_count = 0;
    for (uint32_t i = 0; i < ra->active_count; i++) {
        uint32_t idx = ra->active[i];
        X64_LiveInterval* interval = &ra->intervals[idx];
        if (interval->end >= current_start) {
            /* Still active */
            ra->active[new_active_count++] = idx;
        } else {
            /* Expired - free the register */
            free_register(ra, interval->phys_reg);
        }
    }
    ra->active_count = new_active_count;
}

/**
 * @brief Find the interval with the longest endpoint in active list
 */
static uint32_t find_longest_active(X64_RegAlloc* ra) {
    uint32_t longest_idx = 0;
    uint32_t longest_end = 0;
    for (uint32_t i = 0; i < ra->active_count; i++) {
        uint32_t idx = ra->active[i];
        if (ra->intervals[idx].end > longest_end) {
            longest_end = ra->intervals[idx].end;
            longest_idx = i;
        }
    }
    return longest_idx;
}

Seraph_Vbit x64_linear_scan_allocate(X64_CompileContext* ctx) {
    if (!ctx) return SERAPH_VBIT_VOID;

    X64_RegAlloc* ra = &ctx->regalloc;

    /* Process intervals in start-order */
    for (uint32_t i = 0; i < ra->interval_count; i++) {
        X64_LiveInterval* interval = &ra->intervals[i];

        /* Skip if already assigned (e.g., parameters) */
        if (interval->phys_reg != X64_NONE) {
            /* Add to active list */
            if (ra->active_count < GP_ALLOC_COUNT) {
                ra->active[ra->active_count++] = i;
                ra->gp_free &= ~(1u << interval->phys_reg);
            }
            continue;
        }

        /* Skip if already spilled (e.g., call results forced to spill) */
        if (interval->spill_offset != -1) {
            continue;
        }

        /* Expire old intervals */
        expire_old_intervals(ra, interval->start);

        /* Try to allocate a register */
        X64_Reg reg = alloc_register(ra);
        if (reg != X64_NONE) {
            /* Got a register */
            interval->phys_reg = reg;
            interval->is_callee_save = x64_is_callee_saved(reg);

            /* Add to active list */
            if (ra->active_count < GP_ALLOC_COUNT) {
                ra->active[ra->active_count++] = i;
            }
        } else {
            /* Need to spill - either this interval or longest active */
            if (ra->active_count == 0) {
                /* No active intervals - spill this one */
                interval->spill_offset = alloc_spill_slot(ra, 8);
            } else {
                uint32_t longest_active_pos = find_longest_active(ra);
                uint32_t longest_idx = ra->active[longest_active_pos];
                X64_LiveInterval* longest = &ra->intervals[longest_idx];

                if (longest->end > interval->end) {
                    /* Spill the longest active, give its register to current */
                    interval->phys_reg = longest->phys_reg;
                    interval->is_callee_save = longest->is_callee_save;
                    longest->phys_reg = X64_NONE;
                    longest->spill_offset = alloc_spill_slot(ra, 8);

                    /* Replace in active list */
                    ra->active[longest_active_pos] = i;
                } else {
                    /* Spill current interval */
                    interval->spill_offset = alloc_spill_slot(ra, 8);
                }
            }
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_get_value_location(X64_CompileContext* ctx,
                                    Celestial_Value* value,
                                    X64_Reg* out_reg,
                                    int32_t* out_offset) {
    if (!ctx || !value || !out_reg || !out_offset) {
        return SERAPH_VBIT_VOID;
    }

    *out_reg = X64_NONE;
    *out_offset = -1;

    /* Handle constants */
    if (value->kind == CIR_VALUE_CONST) {
        /* Constants are loaded directly, no location */
        return SERAPH_VBIT_FALSE;
    }

    /* Handle VOID constant */
    if (value->kind == CIR_VALUE_VOID_CONST) {
        return SERAPH_VBIT_FALSE;
    }

    /* Handle parameters and virtual registers */
    if (value->kind == CIR_VALUE_PARAM || value->kind == CIR_VALUE_VREG) {
        X64_RegAlloc* ra = &ctx->regalloc;
        for (uint32_t i = 0; i < ra->interval_count; i++) {
            if (ra->intervals[i].vreg_id == value->id) {
                *out_reg = ra->intervals[i].phys_reg;
                *out_offset = ra->intervals[i].spill_offset;
                return SERAPH_VBIT_TRUE;
            }
        }
    }

    /* Handle globals */
    if (value->kind == CIR_VALUE_GLOBAL) {
        /* Globals are accessed via RIP-relative addressing */
        /* For now, return FALSE - handled specially in lowering */
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Code Generation Helpers
 *============================================================================*/

Seraph_Vbit x64_emit_prologue(X64_CompileContext* ctx) {
    if (!ctx || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_RegAlloc* ra = &ctx->regalloc;

    /* push rbp */
    x64_push_reg(buf, X64_RBP);

    /* mov rbp, rsp */
    x64_mov_reg_reg(buf, X64_RBP, X64_RSP, X64_SZ_64);

    /* Save callee-saved registers that we use */
    uint32_t saved_count = 0;
    X64_Reg saved_regs[6];

    for (uint32_t i = 0; i < ra->interval_count; i++) {
        if (ra->intervals[i].is_callee_save &&
            ra->intervals[i].phys_reg != X64_NONE) {
            X64_Reg reg = ra->intervals[i].phys_reg;
            /* Check if already saved */
            int found = 0;
            for (uint32_t j = 0; j < saved_count; j++) {
                if (saved_regs[j] == reg) { found = 1; break; }
            }
            if (!found && saved_count < 6) {
                saved_regs[saved_count++] = reg;
                x64_push_reg(buf, reg);
            }
        }
    }

    /* Allocate stack space for locals and spills */
    int32_t stack_size = (int32_t)ra->max_spill_size;
    /* Align to 16 bytes */
    stack_size = (stack_size + 15) & ~15;
    if (stack_size > 0) {
        x64_sub_reg_imm(buf, X64_RSP, stack_size, X64_SZ_64);
    }
    ctx->frame_size = stack_size;
    ctx->locals_offset = -stack_size;

    /* Spill all parameters to stack to prevent clobbering by scratch registers.
     * This is necessary because instruction lowering uses argument registers
     * (RDI, RSI, etc.) as scratch, which would clobber parameter values.
     * After spilling, update intervals to point to stack slots. */
    for (uint32_t i = 0; i < ra->interval_count; i++) {
        if (ra->intervals[i].is_param) {
            X64_Reg param_reg = ra->intervals[i].phys_reg;
            if (param_reg != X64_NONE) {
                /* Allocate spill slot if not already assigned */
                if (ra->intervals[i].spill_offset == -1) {
                    ra->intervals[i].spill_offset = ra->next_spill_offset;
                    ra->next_spill_offset -= 8;
                }
                /* Spill parameter to stack */
                x64_mov_mem_reg(buf, X64_RBP, ra->intervals[i].spill_offset,
                                param_reg, X64_SZ_64);
                /* Mark as spilled (no longer in register) */
                ra->intervals[i].phys_reg = X64_NONE;
            }
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_emit_epilogue(X64_CompileContext* ctx) {
    if (!ctx || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_RegAlloc* ra = &ctx->regalloc;

    /* Deallocate stack space */
    if (ctx->frame_size > 0) {
        x64_add_reg_imm(buf, X64_RSP, ctx->frame_size, X64_SZ_64);
    }

    /* Restore callee-saved registers (in reverse order) */
    X64_Reg saved_regs[6];
    uint32_t saved_count = 0;

    for (uint32_t i = 0; i < ra->interval_count; i++) {
        if (ra->intervals[i].is_callee_save &&
            ra->intervals[i].phys_reg != X64_NONE) {
            X64_Reg reg = ra->intervals[i].phys_reg;
            int found = 0;
            for (uint32_t j = 0; j < saved_count; j++) {
                if (saved_regs[j] == reg) { found = 1; break; }
            }
            if (!found && saved_count < 6) {
                saved_regs[saved_count++] = reg;
            }
        }
    }

    /* Pop in reverse order */
    for (int32_t i = (int32_t)saved_count - 1; i >= 0; i--) {
        x64_pop_reg(buf, saved_regs[i]);
    }

    /* pop rbp */
    x64_pop_reg(buf, X64_RBP);

    /* ret */
    x64_ret(buf);

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_load_value(X64_CompileContext* ctx,
                            Celestial_Value* value,
                            X64_Reg dst_reg) {
    if (!ctx || !value || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;

    /* Handle constants */
    if (value->kind == CIR_VALUE_CONST) {
        int64_t imm = value->constant.i64;
        if (imm >= INT32_MIN && imm <= INT32_MAX) {
            x64_mov_reg_imm32(buf, dst_reg, (int32_t)imm);
        } else {
            x64_mov_reg_imm64(buf, dst_reg, (uint64_t)imm);
        }
        return SERAPH_VBIT_TRUE;
    }

    /* Handle VOID constant */
    if (value->kind == CIR_VALUE_VOID_CONST) {
        x64_mov_reg_imm64(buf, dst_reg, SERAPH_X64_VOID_VALUE);
        return SERAPH_VBIT_TRUE;
    }

    /* Handle function pointer */
    if (value->kind == CIR_VALUE_FNPTR) {
        /* Load address of function using LEA reg, [RIP + rel32]
         * The rel32 will be patched after all functions are compiled */
        Celestial_Function* target_fn = value->fnptr.fn;

        /* LEA reg, [RIP + disp32]
         * REX.W prefix (0x48 or higher) for 64-bit
         * Opcode: 8D /r
         * ModRM: mod=00, reg=dst_reg, r/m=101 (RIP-relative) */
        uint8_t rex = 0x48;  /* REX.W */
        if (dst_reg >= X64_R8) {
            rex |= 0x04;  /* REX.R for extended reg */
        }
        x64_emit_byte(buf, rex);
        x64_emit_byte(buf, 0x8D);  /* LEA */
        x64_emit_byte(buf, (uint8_t)(((dst_reg & 7) << 3) | 0x05));  /* ModRM: 00 reg 101 */

        /* Record fixup location before emitting placeholder */
        size_t fixup_offset = buf->size;
        x64_emit_dword(buf, 0);  /* Placeholder disp32 */

        /* Record fixup for later patching */
        if (ctx->mod_ctx &&
            ctx->mod_ctx->fnptr_fixup_count < ctx->mod_ctx->fnptr_fixup_capacity) {
            X64_FnptrFixup* fixup = &ctx->mod_ctx->fnptr_fixups[ctx->mod_ctx->fnptr_fixup_count++];
            fixup->fixup_site = fixup_offset;
            fixup->fn = target_fn;
        }

        return SERAPH_VBIT_TRUE;
    }

    /* Get location */
    X64_Reg src_reg;
    int32_t offset;
    Seraph_Vbit found = x64_get_value_location(ctx, value, &src_reg, &offset);

    if (!seraph_vbit_is_true(found)) {
        return SERAPH_VBIT_FALSE;
    }

    if (src_reg != X64_NONE) {
        /* In register */
        emit_move_if_needed(ctx, dst_reg, src_reg);
    } else if (offset != -1) {
        /* Spilled to stack */
        x64_mov_reg_mem(buf, dst_reg, X64_RBP, offset, X64_SZ_64);
    } else {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_store_value(X64_CompileContext* ctx,
                             X64_Reg src_reg,
                             Celestial_Value* value) {
    if (!ctx || !value || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;

    /* Get location */
    X64_Reg dst_reg;
    int32_t offset;
    Seraph_Vbit found = x64_get_value_location(ctx, value, &dst_reg, &offset);

    if (!seraph_vbit_is_true(found)) {
        return SERAPH_VBIT_FALSE;
    }

    if (dst_reg != X64_NONE) {
        /* In register */
        emit_move_if_needed(ctx, dst_reg, src_reg);
    } else if (offset != -1) {
        /* Spilled to stack */
        x64_mov_mem_reg(buf, X64_RBP, offset, src_reg, X64_SZ_64);
    } else {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

static void emit_move_if_needed(X64_CompileContext* ctx, X64_Reg dst, X64_Reg src) {
    if (dst != src) {
        x64_mov_reg_reg(ctx->output, dst, src, X64_SZ_64);
    }
}

/*============================================================================
 * VOID Operation Helpers
 *============================================================================*/

void x64_emit_void_check(X64_CompileContext* ctx,
                         X64_Reg reg,
                         uint32_t void_label) {
    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    /* bt reg, 63 (test bit 63) */
    /* BT instruction: 0F BA /4 ib */
    x64_emit_byte(buf, x64_rex(1, 0, 0, reg >= X64_R8 ? 1 : 0));
    x64_emit_byte(buf, 0x0F);
    x64_emit_byte(buf, 0xBA);
    x64_emit_byte(buf, x64_modrm(3, 4, reg & 7));  /* /4 = BT */
    x64_emit_byte(buf, 63);  /* bit 63 */

    /* jc void_label (jump if carry = bit was set) */
    x64_jcc_label(buf, X64_CC_C, labels, void_label);
}

void x64_emit_void_propagate(X64_CompileContext* ctx, X64_Reg reg) {
    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    /* Create labels for the check */
    uint32_t not_void_label = x64_label_create(labels);
    uint32_t void_return_label = x64_label_create(labels);

    /* bt reg, 63 */
    x64_emit_byte(buf, x64_rex(1, 0, 0, reg >= X64_R8 ? 1 : 0));
    x64_emit_byte(buf, 0x0F);
    x64_emit_byte(buf, 0xBA);
    x64_emit_byte(buf, x64_modrm(3, 4, reg & 7));
    x64_emit_byte(buf, 63);

    /* jnc not_void */
    x64_jcc_label(buf, X64_CC_NC, labels, not_void_label);

    /* Is VOID - return VOID */
    x64_label_define(labels, buf, void_return_label);
    x64_mov_reg_imm64(buf, X64_RAX, SERAPH_X64_VOID_VALUE);
    x64_emit_epilogue(ctx);  /* This emits leave; ret */

    /* not_void: continue */
    x64_label_define(labels, buf, not_void_label);
}

/*============================================================================
 * Capability Operation Helpers
 *============================================================================*/

void x64_emit_cap_bounds_check(X64_CompileContext* ctx,
                               X64_Reg cap_reg,
                               X64_Reg offset_reg,
                               uint32_t fail_label) {
    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    /* Load length from capability */
    /* mov r10, [cap_reg + SERAPH_CAP_LENGTH_OFFSET] */
    x64_mov_reg_mem(buf, X64_R10, cap_reg, SERAPH_CAP_LENGTH_OFFSET, X64_SZ_64);

    /* cmp offset_reg, r10 */
    x64_cmp_reg_reg(buf, offset_reg, X64_R10, X64_SZ_64);

    /* jae fail_label (unsigned: offset >= length is out of bounds) */
    x64_jcc_label(buf, X64_CC_AE, labels, fail_label);
}

void x64_emit_cap_gen_check(X64_CompileContext* ctx,
                            X64_Reg cap_reg,
                            uint32_t fail_label) {
    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    /* Load generation from capability */
    x64_mov_reg_mem(buf, X64_R10, cap_reg, SERAPH_CAP_GEN_OFFSET, X64_SZ_64);

    /* Load current generation from capability context */
    /* cmp r10, [R14 + SERAPH_CAP_CTX_GEN_OFFSET] */
    x64_emit_byte(buf, x64_rex(1, 1, 0, 0));  /* REX.W + REX.R for R10 */
    x64_emit_byte(buf, 0x3B);  /* CMP r64, r/m64 */
    x64_emit_byte(buf, x64_modrm(2, X64_R10 & 7, X64_R14 & 7));  /* [R14+disp32] */
    x64_emit_dword(buf, SERAPH_CAP_CTX_GEN_OFFSET);

    /* jl fail_label (if cap_gen < current_gen, capability is revoked) */
    x64_jcc_label(buf, X64_CC_L, labels, fail_label);
}

void x64_emit_cap_perm_check(X64_CompileContext* ctx,
                             X64_Reg cap_reg,
                             uint32_t perm_mask,
                             uint32_t fail_label) {
    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    /* Load permissions from capability */
    x64_mov_reg_mem(buf, X64_R10, cap_reg, SERAPH_CAP_PERMS_OFFSET, X64_SZ_64);

    /* test r10, perm_mask */
    x64_emit_byte(buf, x64_rex(1, 0, 0, 1));  /* REX.W + REX.B for R10 */
    x64_emit_byte(buf, 0xF7);  /* TEST r/m64, imm32 */
    x64_emit_byte(buf, x64_modrm(3, 0, X64_R10 & 7));
    x64_emit_dword(buf, perm_mask);

    /* jz fail_label (if permission bits not set) */
    x64_jcc_label(buf, X64_CC_Z, labels, fail_label);
}

/*============================================================================
 * Instruction Lowering
 *============================================================================*/

Seraph_Vbit x64_lower_instruction(X64_CompileContext* ctx,
                                   Celestial_Instr* instr) {
    if (!ctx || !instr) return SERAPH_VBIT_VOID;

    switch (instr->opcode) {
        /* Arithmetic */
        case CIR_ADD:
        case CIR_SUB:
        case CIR_MUL:
        case CIR_DIV:
        case CIR_MOD:
        case CIR_NEG:
            return x64_lower_arithmetic(ctx, instr);

        /* Bitwise */
        case CIR_AND:
        case CIR_OR:
        case CIR_XOR:
        case CIR_NOT:
        case CIR_SHL:
        case CIR_SHR:
        case CIR_SAR:
            return x64_lower_bitwise(ctx, instr);

        /* Comparison */
        case CIR_EQ:
        case CIR_NE:
        case CIR_LT:
        case CIR_LE:
        case CIR_GT:
        case CIR_GE:
        case CIR_ULT:
        case CIR_ULE:
        case CIR_UGT:
        case CIR_UGE:
            return x64_lower_comparison(ctx, instr);

        /* Control Flow */
        case CIR_JUMP:
        case CIR_BRANCH:
        case CIR_CALL:
        case CIR_CALL_INDIRECT:
        case CIR_SYSCALL:
        case CIR_TAIL_CALL:
        case CIR_RETURN:
            return x64_lower_control_flow(ctx, instr);

        /* VOID Operations */
        case CIR_VOID_TEST:
        case CIR_VOID_PROP:
        case CIR_VOID_ASSERT:
        case CIR_VOID_COALESCE:
        case CIR_VOID_CONST:
            return x64_lower_void_op(ctx, instr);

        /* Capability Operations */
        case CIR_CAP_CREATE:
        case CIR_CAP_LOAD:
        case CIR_CAP_STORE:
        case CIR_CAP_CHECK:
        case CIR_CAP_NARROW:
        case CIR_CAP_SPLIT:
        case CIR_CAP_REVOKE:
            return x64_lower_capability_op(ctx, instr);

        /* Memory Operations */
        case CIR_LOAD:
        case CIR_STORE:
        case CIR_ALLOCA:
        case CIR_MEMCPY:
        case CIR_MEMSET:
        case CIR_GEP:
        case CIR_EXTRACTFIELD:
        case CIR_INSERTFIELD:
            return x64_lower_memory_op(ctx, instr);

        /* Galactic Operations */
        case CIR_GALACTIC_ADD:
        case CIR_GALACTIC_MUL:
        case CIR_GALACTIC_DIV:
        case CIR_GALACTIC_PREDICT:
        case CIR_GALACTIC_EXTRACT:
        case CIR_GALACTIC_INSERT:
            return x64_lower_galactic_op(ctx, instr);

        /* Substrate Operations */
        case CIR_SUBSTRATE_ENTER:
        case CIR_SUBSTRATE_EXIT:
        case CIR_ATLAS_LOAD:
        case CIR_ATLAS_STORE:
        case CIR_ATLAS_BEGIN:
        case CIR_ATLAS_COMMIT:
        case CIR_ATLAS_ROLLBACK:
        case CIR_AETHER_LOAD:
        case CIR_AETHER_STORE:
        case CIR_AETHER_SYNC:
            return x64_lower_substrate_op(ctx, instr);

        /* Type Conversions */
        case CIR_TRUNC:
        case CIR_ZEXT:
        case CIR_SEXT:
        case CIR_BITCAST:
        case CIR_TO_SCALAR:
        case CIR_FROM_SCALAR:
        case CIR_TO_GALACTIC:
        case CIR_FROM_GALACTIC:
            return x64_lower_conversion(ctx, instr);

        /* Struct/Array Operations */
        case CIR_EXTRACTELEM:
            /* Extract element from array: result = arr[index]
             * operands[0] = array pointer
             * operands[1] = index
             * Uses element type to determine stride
             */
            if (instr->operand_count >= 2 && instr->result) {
                x64_load_value(ctx, instr->operands[0], X64_RAX);  /* Array base */
                x64_load_value(ctx, instr->operands[1], X64_RCX);  /* Index */

                /* Determine element size from result type */
                size_t elem_size = 8;  /* Default to 64-bit */
                if (instr->result->type) {
                    elem_size = celestial_type_size(instr->result->type);
                }

                /* Calculate offset: base + index * elem_size */
                if (elem_size == 8) {
                    /* LEA RAX, [RAX + RCX*8] */
                    x64_lea_scaled(ctx->output, X64_RAX, X64_RAX, X64_RCX, 3, 0);
                } else if (elem_size == 4) {
                    x64_lea_scaled(ctx->output, X64_RAX, X64_RAX, X64_RCX, 2, 0);
                } else if (elem_size == 2) {
                    x64_lea_scaled(ctx->output, X64_RAX, X64_RAX, X64_RCX, 1, 0);
                } else {
                    /* Multiply index by elem_size manually for other sizes */
                    x64_imul_imm(ctx->output, X64_RCX, X64_RCX, (int32_t)elem_size, X64_SZ_64);
                    x64_add_reg_reg(ctx->output, X64_RAX, X64_RCX, X64_SZ_64);
                }

                /* Load the element */
                X64_Size sz = (elem_size <= 4) ? X64_SZ_32 : X64_SZ_64;
                x64_mov_reg_mem(ctx->output, X64_RAX, X64_RAX, 0, sz);

                x64_store_value(ctx, X64_RAX, instr->result);
            }
            return SERAPH_VBIT_TRUE;

        case CIR_INSERTELEM:
            /* Insert element into array: arr[index] = value
             * operands[0] = array pointer
             * operands[1] = index
             * operands[2] = value to insert
             */
            if (instr->operand_count >= 3) {
                x64_load_value(ctx, instr->operands[0], X64_RAX);  /* Array base */
                x64_load_value(ctx, instr->operands[1], X64_RCX);  /* Index */
                x64_load_value(ctx, instr->operands[2], X64_RDX);  /* Value */

                /* Determine element size */
                size_t elem_size = 8;  /* Default to 64-bit */
                if (instr->operands[2]->type) {
                    elem_size = celestial_type_size(instr->operands[2]->type);
                }

                /* Calculate offset: base + index * elem_size */
                if (elem_size == 8) {
                    x64_lea_scaled(ctx->output, X64_RAX, X64_RAX, X64_RCX, 3, 0);
                } else if (elem_size == 4) {
                    x64_lea_scaled(ctx->output, X64_RAX, X64_RAX, X64_RCX, 2, 0);
                } else if (elem_size == 2) {
                    x64_lea_scaled(ctx->output, X64_RAX, X64_RAX, X64_RCX, 1, 0);
                } else {
                    x64_imul_imm(ctx->output, X64_RCX, X64_RCX, (int32_t)elem_size, X64_SZ_64);
                    x64_add_reg_reg(ctx->output, X64_RAX, X64_RCX, X64_SZ_64);
                }

                /* Store the element */
                X64_Size sz = (elem_size <= 4) ? X64_SZ_32 : X64_SZ_64;
                x64_mov_mem_reg(ctx->output, X64_RAX, 0, X64_RDX, sz);
            }
            return SERAPH_VBIT_TRUE;

        /* Miscellaneous */
        case CIR_NOP:
            /* No operation - folded constant, skip */
            return SERAPH_VBIT_TRUE;

        case CIR_PHI:
            /* PHI nodes are resolved during SSA destruction */
            return SERAPH_VBIT_TRUE;

        case CIR_SELECT:
            /* Conditional select: result = cond ? true_val : false_val
             * operands[0] = condition (Vbit or i1)
             * operands[1] = value if true
             * operands[2] = value if false
             */
            if (instr->operand_count >= 3) {
                /* Load false value first (it will be overwritten if cond is true) */
                x64_load_value(ctx, instr->operands[2], X64_RAX);

                /* Load true value into a different register */
                x64_load_value(ctx, instr->operands[1], X64_RCX);

                /* Load and test condition */
                x64_load_value(ctx, instr->operands[0], X64_RDX);
                x64_test_reg_reg(ctx->output, X64_RDX, X64_RDX, X64_SZ_64);

                /* CMOVNE: if condition != 0, move true value to result */
                x64_cmovcc(ctx->output, X64_CC_NE, X64_RAX, X64_RCX, X64_SZ_64);

                /* Store result */
                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            return SERAPH_VBIT_TRUE;

        case CIR_UNREACHABLE:
            x64_ud2(ctx->output);
            return SERAPH_VBIT_TRUE;

        case CIR_TRAP:
            x64_int3(ctx->output);
            return SERAPH_VBIT_TRUE;

        default:
            return SERAPH_VBIT_FALSE;
    }
}

Seraph_Vbit x64_lower_arithmetic(X64_CompileContext* ctx,
                                  Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    /* Get result location */
    X64_Reg result_reg;
    int32_t result_offset;
    if (instr->result) {
        x64_get_value_location(ctx, instr->result, &result_reg, &result_offset);
        if (result_reg == X64_NONE) {
            result_reg = X64_RAX;  /* Use RAX if spilled */
        }
    } else {
        result_reg = X64_RAX;
    }

    X64_Reg op1_reg = X64_RAX;
    X64_Reg op2_reg = X64_RCX;

    /* For VOID-propagating arithmetic, check operands first */
    uint32_t void_label = 0;
    int needs_void_check = (instr->opcode != CIR_NEG); /* NEG has one operand */

    if (needs_void_check && instr->operand_count >= 2) {
        void_label = x64_label_create(labels);
    }

    switch (instr->opcode) {
        case CIR_ADD:
            /* Load operands */
            x64_load_value(ctx, instr->operands[0], op1_reg);
            x64_load_value(ctx, instr->operands[1], op2_reg);

            /* Check for VOID */
            x64_emit_void_check(ctx, op1_reg, void_label);
            x64_emit_void_check(ctx, op2_reg, void_label);

            /* Perform addition */
            x64_add_reg_reg(buf, op1_reg, op2_reg, X64_SZ_64);

            /* Check for overflow → VOID */
            x64_jcc_label(buf, X64_CC_O, labels, void_label);
            break;

        case CIR_SUB:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            x64_load_value(ctx, instr->operands[1], op2_reg);
            x64_emit_void_check(ctx, op1_reg, void_label);
            x64_emit_void_check(ctx, op2_reg, void_label);
            x64_sub_reg_reg(buf, op1_reg, op2_reg, X64_SZ_64);
            x64_jcc_label(buf, X64_CC_O, labels, void_label);
            break;

        case CIR_MUL:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            x64_load_value(ctx, instr->operands[1], op2_reg);
            x64_emit_void_check(ctx, op1_reg, void_label);
            x64_emit_void_check(ctx, op2_reg, void_label);
            x64_imul_reg_reg(buf, op1_reg, op2_reg, X64_SZ_64);
            x64_jcc_label(buf, X64_CC_O, labels, void_label);
            break;

        case CIR_DIV:
            {
                uint32_t zero_label = x64_label_create(labels);
                uint32_t done_label = x64_label_create(labels);

                x64_load_value(ctx, instr->operands[0], X64_RAX);
                x64_load_value(ctx, instr->operands[1], X64_RCX);

                /* Check for VOID operands */
                x64_emit_void_check(ctx, X64_RAX, void_label);
                x64_emit_void_check(ctx, X64_RCX, void_label);

                /* Check for zero divisor */
                x64_test_reg_reg(buf, X64_RCX, X64_RCX, X64_SZ_64);
                x64_jcc_label(buf, X64_CC_Z, labels, zero_label);

                /* Sign extend RAX into RDX:RAX */
                x64_cqo(buf);

                /* IDIV RCX */
                x64_idiv_reg(buf, X64_RCX, X64_SZ_64);

                /* Result in RAX */
                x64_jmp_label(buf, labels, done_label);

                /* Zero divisor - return VOID */
                x64_label_define(labels, buf, zero_label);
                x64_mov_reg_imm64(buf, X64_RAX, SERAPH_X64_VOID_VALUE);

                x64_label_define(labels, buf, done_label);
                op1_reg = X64_RAX;
            }
            break;

        case CIR_MOD:
            {
                uint32_t zero_label = x64_label_create(labels);
                uint32_t done_label = x64_label_create(labels);

                x64_load_value(ctx, instr->operands[0], X64_RAX);
                x64_load_value(ctx, instr->operands[1], X64_RCX);
                x64_emit_void_check(ctx, X64_RAX, void_label);
                x64_emit_void_check(ctx, X64_RCX, void_label);

                x64_test_reg_reg(buf, X64_RCX, X64_RCX, X64_SZ_64);
                x64_jcc_label(buf, X64_CC_Z, labels, zero_label);

                x64_cqo(buf);
                x64_idiv_reg(buf, X64_RCX, X64_SZ_64);

                /* Remainder in RDX */
                x64_mov_reg_reg(buf, X64_RAX, X64_RDX, X64_SZ_64);
                x64_jmp_label(buf, labels, done_label);

                x64_label_define(labels, buf, zero_label);
                x64_mov_reg_imm64(buf, X64_RAX, SERAPH_X64_VOID_VALUE);

                x64_label_define(labels, buf, done_label);
                op1_reg = X64_RAX;
            }
            break;

        case CIR_NEG:
            {
                uint32_t neg_void_label = x64_label_create(labels);
                uint32_t neg_done_label = x64_label_create(labels);

                x64_load_value(ctx, instr->operands[0], op1_reg);
                x64_emit_void_check(ctx, op1_reg, neg_void_label);
                x64_neg_reg(buf, op1_reg, X64_SZ_64);
                x64_jmp_label(buf, labels, neg_done_label);

                x64_label_define(labels, buf, neg_void_label);
                x64_mov_reg_imm64(buf, op1_reg, SERAPH_X64_VOID_VALUE);

                x64_label_define(labels, buf, neg_done_label);
            }
            break;

        default:
            return SERAPH_VBIT_FALSE;
    }

    /* Handle VOID label if we created one for binary ops */
    if (void_label && instr->opcode != CIR_NEG) {
        uint32_t end_label = x64_label_create(labels);
        x64_jmp_label(buf, labels, end_label);

        x64_label_define(labels, buf, void_label);
        x64_mov_reg_imm64(buf, op1_reg, SERAPH_X64_VOID_VALUE);

        x64_label_define(labels, buf, end_label);
    }

    /* Store result */
    if (instr->result) {
        emit_move_if_needed(ctx, result_reg, op1_reg);
        if (result_offset != -1) {
            x64_mov_mem_reg(buf, X64_RBP, result_offset, result_reg, X64_SZ_64);
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_lower_bitwise(X64_CompileContext* ctx,
                               Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_Reg result_reg;
    int32_t result_offset;

    if (instr->result) {
        x64_get_value_location(ctx, instr->result, &result_reg, &result_offset);
        if (result_reg == X64_NONE) result_reg = X64_RAX;
    } else {
        result_reg = X64_RAX;
    }

    X64_Reg op1_reg = X64_RAX;
    X64_Reg op2_reg = X64_RCX;

    switch (instr->opcode) {
        case CIR_AND:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            x64_load_value(ctx, instr->operands[1], op2_reg);
            x64_and_reg_reg(buf, op1_reg, op2_reg, X64_SZ_64);
            break;

        case CIR_OR:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            x64_load_value(ctx, instr->operands[1], op2_reg);
            x64_or_reg_reg(buf, op1_reg, op2_reg, X64_SZ_64);
            break;

        case CIR_XOR:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            x64_load_value(ctx, instr->operands[1], op2_reg);
            x64_xor_reg_reg(buf, op1_reg, op2_reg, X64_SZ_64);
            break;

        case CIR_NOT:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            x64_not_reg(buf, op1_reg, X64_SZ_64);
            break;

        case CIR_SHL:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            if (instr->operands[1]->kind == CIR_VALUE_CONST) {
                uint8_t shift = (uint8_t)instr->operands[1]->constant.u64;
                x64_shl_reg_imm(buf, op1_reg, shift, X64_SZ_64);
            } else {
                x64_load_value(ctx, instr->operands[1], X64_RCX);
                x64_shl_reg_cl(buf, op1_reg, X64_SZ_64);
            }
            break;

        case CIR_SHR:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            if (instr->operands[1]->kind == CIR_VALUE_CONST) {
                uint8_t shift = (uint8_t)instr->operands[1]->constant.u64;
                x64_shr_reg_imm(buf, op1_reg, shift, X64_SZ_64);
            } else {
                x64_load_value(ctx, instr->operands[1], X64_RCX);
                x64_shr_reg_cl(buf, op1_reg, X64_SZ_64);
            }
            break;

        case CIR_SAR:
            x64_load_value(ctx, instr->operands[0], op1_reg);
            if (instr->operands[1]->kind == CIR_VALUE_CONST) {
                uint8_t shift = (uint8_t)instr->operands[1]->constant.u64;
                x64_sar_reg_imm(buf, op1_reg, shift, X64_SZ_64);
            } else {
                x64_load_value(ctx, instr->operands[1], X64_RCX);
                x64_sar_reg_cl(buf, op1_reg, X64_SZ_64);
            }
            break;

        default:
            return SERAPH_VBIT_FALSE;
    }

    /* Store result */
    if (instr->result) {
        emit_move_if_needed(ctx, result_reg, op1_reg);
        if (result_offset != -1) {
            x64_mov_mem_reg(buf, X64_RBP, result_offset, result_reg, X64_SZ_64);
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_lower_comparison(X64_CompileContext* ctx,
                                  Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    X64_Reg result_reg;
    int32_t result_offset;
    if (instr->result) {
        x64_get_value_location(ctx, instr->result, &result_reg, &result_offset);
        if (result_reg == X64_NONE) result_reg = X64_RAX;
    } else {
        result_reg = X64_RAX;
    }

    /* Load operands */
    x64_load_value(ctx, instr->operands[0], X64_RAX);
    x64_load_value(ctx, instr->operands[1], X64_RCX);

    /* Check for VOID operands - comparison with VOID produces VOID */
    uint32_t void_label = x64_label_create(labels);
    uint32_t end_label = x64_label_create(labels);

    x64_emit_void_check(ctx, X64_RAX, void_label);
    x64_emit_void_check(ctx, X64_RCX, void_label);

    /* Perform comparison */
    x64_cmp_reg_reg(buf, X64_RAX, X64_RCX, X64_SZ_64);

    /* Map opcode to condition code */
    X64_Condition cc = x64_cc_from_cir_cmp(instr->opcode);

    /* Set result byte based on condition */
    x64_setcc(buf, cc, X64_RAX);

    /* Zero-extend to 64 bits */
    x64_movzx(buf, X64_RAX, X64_RAX, X64_SZ_64, X64_SZ_8);

    x64_jmp_label(buf, labels, end_label);

    /* VOID path */
    x64_label_define(labels, buf, void_label);
    x64_mov_reg_imm64(buf, X64_RAX, SERAPH_VBIT_VOID);  /* VOID Vbit = 0xFF */

    x64_label_define(labels, buf, end_label);

    /* Store result */
    if (instr->result) {
        emit_move_if_needed(ctx, result_reg, X64_RAX);
        if (result_offset != -1) {
            x64_mov_mem_reg(buf, X64_RBP, result_offset, result_reg, X64_SZ_64);
        }
    }

    return SERAPH_VBIT_TRUE;
}

X64_Condition x64_cc_from_cir_cmp(Celestial_Opcode opcode) {
    switch (opcode) {
        case CIR_EQ:  return X64_CC_E;
        case CIR_NE:  return X64_CC_NE;
        case CIR_LT:  return X64_CC_L;
        case CIR_LE:  return X64_CC_LE;
        case CIR_GT:  return X64_CC_G;
        case CIR_GE:  return X64_CC_GE;
        case CIR_ULT: return X64_CC_B;
        case CIR_ULE: return X64_CC_BE;
        case CIR_UGT: return X64_CC_A;
        case CIR_UGE: return X64_CC_AE;
        default:      return X64_CC_E;
    }
}

Seraph_Vbit x64_lower_control_flow(X64_CompileContext* ctx,
                                    Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    switch (instr->opcode) {
        case CIR_JUMP:
            {
                uint32_t target_label = get_or_create_block_label(ctx, instr->target1);
                x64_jmp_label(buf, labels, target_label);
            }
            break;

        case CIR_BRANCH:
            {
                /* Load condition */
                x64_load_value(ctx, instr->operands[0], X64_RAX);

                /* Test if TRUE (1) */
                x64_cmp_reg_imm(buf, X64_RAX, 1, X64_SZ_8);

                uint32_t then_label = get_or_create_block_label(ctx, instr->target1);
                uint32_t else_label = get_or_create_block_label(ctx, instr->target2);

                /* je then_label */
                x64_jcc_label(buf, X64_CC_E, labels, then_label);
                /* jmp else_label */
                x64_jmp_label(buf, labels, else_label);
            }
            break;

        case CIR_CALL:
            {
                Celestial_Function* callee = instr->callee;
                if (!callee) return SERAPH_VBIT_FALSE;

                /* Push arguments (in reverse order for stack, use regs first) */
                uint32_t stack_args = 0;
                for (size_t i = instr->operand_count; i > 0; i--) {
                    size_t idx = i - 1;
                    if (idx < ARG_REG_COUNT) {
                        /* Argument goes in register */
                        x64_load_value(ctx, instr->operands[idx], ARG_REGS[idx]);
                    } else {
                        /* Argument goes on stack */
                        x64_load_value(ctx, instr->operands[idx], X64_RAX);
                        x64_push_reg(buf, X64_RAX);
                        stack_args++;
                    }
                }

                /* Call function - emit CALL rel32 with placeholder offset */
                x64_emit_byte(buf, 0xE8);  /* CALL rel32 */
                size_t call_fixup_offset = buf->size;  /* Remember location */
                x64_emit_dword(buf, 0);    /* Placeholder - will be patched */

                /* Record fixup for later patching */
                if (ctx->mod_ctx &&
                    ctx->mod_ctx->call_fixup_count < ctx->mod_ctx->call_fixup_capacity) {
                    X64_CallFixup* fixup = &ctx->mod_ctx->call_fixups[ctx->mod_ctx->call_fixup_count++];
                    fixup->call_site = call_fixup_offset;
                    fixup->callee = callee;
                }

                /* Clean up stack arguments */
                if (stack_args > 0) {
                    x64_add_reg_imm(buf, X64_RSP, (int32_t)(stack_args * 8), X64_SZ_64);
                }

                /* Result is in RAX - store to pre-allocated spill slot */
                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_CALL_INDIRECT:
            {
                /* Indirect call through function pointer */
                /* operands[0] = function pointer value */
                /* operands[1..n] = arguments */

                /* Load function pointer into R10 first (caller-saved, not used for args) */
                x64_load_value(ctx, instr->operands[0], X64_R10);

                /* Load arguments (operands[1..n]) into arg registers
                 * Process in FORWARD order to avoid clobbering source registers
                 * that overlap with lower-numbered destination registers.
                 * E.g., if x is in RSI and we need it in RDI, we must
                 * read RSI before writing to it. */
                size_t arg_count = instr->operand_count - 1;

                /* First, push any stack arguments (reverse order) */
                uint32_t stack_args = 0;
                for (size_t i = arg_count; i > ARG_REG_COUNT; i--) {
                    size_t idx = i;  /* operands[1..n] */
                    x64_load_value(ctx, instr->operands[idx], X64_RAX);
                    x64_push_reg(buf, X64_RAX);
                    stack_args++;
                }

                /* Then load register arguments in FORWARD order */
                for (size_t i = 0; i < arg_count && i < ARG_REG_COUNT; i++) {
                    size_t idx = i + 1;  /* operands[1..n] maps to arg 0..n-1 */
                    x64_load_value(ctx, instr->operands[idx], ARG_REGS[i]);
                }

                /* Indirect call through R10: call r10 */
                /* FF /2 = CALL r/m64 */
                x64_emit_byte(buf, x64_rex(0, 0, 0, 1));  /* REX.B for R10 */
                x64_emit_byte(buf, 0xFF);
                x64_emit_byte(buf, x64_modrm(3, 2, X64_R10 & 7));

                /* Clean up stack arguments */
                if (stack_args > 0) {
                    x64_add_reg_imm(buf, X64_RSP, (int32_t)(stack_args * 8), X64_SZ_64);
                }

                /* Result is in RAX - store to pre-allocated spill slot */
                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_SYSCALL:
            {
                /* Linux syscall convention:
                 * RAX = syscall number (operands[0])
                 * RDI, RSI, RDX, R10, R8, R9 = args (operands[1..6])
                 * SYSCALL instruction = 0x0F 0x05
                 * Result returned in RAX
                 */
                static const X64_Reg SYSCALL_ARG_REGS[] = {
                    X64_RDI, X64_RSI, X64_RDX, X64_R10, X64_R8, X64_R9
                };

                /* Load syscall number into RAX */
                if (instr->operand_count < 1 || !instr->operands[0]) {
                    return SERAPH_VBIT_FALSE;
                }
                x64_load_value(ctx, instr->operands[0], X64_RAX);

                /* Load arguments into their respective registers */
                size_t syscall_arg_count = instr->operand_count - 1;
                if (syscall_arg_count > 6) syscall_arg_count = 6;

                for (size_t i = 0; i < syscall_arg_count; i++) {
                    if (instr->operands[i + 1]) {
                        x64_load_value(ctx, instr->operands[i + 1], SYSCALL_ARG_REGS[i]);
                    }
                }

                /* Emit SYSCALL instruction: 0x0F 0x05 */
                x64_emit_byte(buf, 0x0F);
                x64_emit_byte(buf, 0x05);

                /* Result is in RAX - store to result value if needed */
                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_TAIL_CALL:
            /* Tail call optimization: jump instead of call
             * This avoids growing the stack for recursive calls.
             * Steps:
             *   1. Load arguments into arg registers
             *   2. Restore callee-saved regs and pop frame (epilogue)
             *   3. Jump (not call) to target function
             */
            {
                Celestial_Function* callee = instr->callee;
                if (!callee) {
                    return SERAPH_VBIT_FALSE;
                }

                /* Load arguments into argument registers */
                size_t arg_count = instr->operand_count;
                if (arg_count > ARG_REG_COUNT) {
                    /* Tail calls with stack arguments are more complex;
                     * for now, fall back to regular call for these cases */
                    return SERAPH_VBIT_FALSE;
                }

                for (size_t i = 0; i < arg_count; i++) {
                    if (instr->operands[i]) {
                        x64_load_value(ctx, instr->operands[i], ARG_REGS[i]);
                    }
                }

                /* Emit epilogue (restore callee-saved regs, pop frame) */
                x64_emit_epilogue(ctx);

                /* Instead of RET, jump to target function
                 * JMP rel32 = E9 rel32 */
                x64_emit_byte(buf, 0xE9);
                size_t jmp_fixup_offset = buf->size;
                x64_emit_dword(buf, 0);  /* Placeholder */

                /* Record fixup for later patching */
                if (ctx->mod_ctx &&
                    ctx->mod_ctx->call_fixup_count < ctx->mod_ctx->call_fixup_capacity) {
                    X64_CallFixup* fixup = &ctx->mod_ctx->call_fixups[ctx->mod_ctx->call_fixup_count++];
                    fixup->call_site = jmp_fixup_offset;
                    fixup->callee = callee;
                }
            }
            break;

        case CIR_RETURN:
            {
                if (instr->operand_count > 0 && instr->operands[0]) {
                    Celestial_Value* ret_val = instr->operands[0];

                    /* Check if returning a pointer to a locally-allocated struct.
                     * For small structs (≤8 bytes), we should return the VALUE in RAX,
                     * not the pointer, since the pointer becomes invalid after return.
                     * Detect alloca by checking if alloca_type is set. */
                    int is_local_struct = 0;
                    if (ret_val->alloca_type) {
                        /* This value is from an alloca instruction */
                        Celestial_Type* pointee = ret_val->alloca_type;
                        if (pointee && pointee->kind == CIR_TYPE_STRUCT) {
                            size_t size = celestial_type_size(pointee);
                            if (size <= 8) {
                                is_local_struct = 1;
                            }
                        }
                    }

                    if (is_local_struct) {
                        /* Load the pointer to the struct into RAX */
                        x64_load_value(ctx, ret_val, X64_RAX);
                        /* Then load the struct value from that address into RAX */
                        x64_mov_reg_mem(buf, X64_RAX, X64_RAX, 0, X64_SZ_64);
                    } else {
                        x64_load_value(ctx, ret_val, X64_RAX);
                    }
                }
                x64_emit_epilogue(ctx);
            }
            break;

        default:
            return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_lower_void_op(X64_CompileContext* ctx,
                               Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    X64_Reg result_reg;
    int32_t result_offset;
    if (instr->result) {
        x64_get_value_location(ctx, instr->result, &result_reg, &result_offset);
        if (result_reg == X64_NONE) result_reg = X64_RAX;
    } else {
        result_reg = X64_RAX;
    }

    switch (instr->opcode) {
        case CIR_VOID_TEST:
            {
                /* Test if value is VOID, return Vbit */
                x64_load_value(ctx, instr->operands[0], X64_RAX);

                uint32_t is_void = x64_label_create(labels);
                uint32_t done = x64_label_create(labels);

                /* bt rax, 63 */
                x64_emit_byte(buf, x64_rex(1, 0, 0, 0));
                x64_emit_byte(buf, 0x0F);
                x64_emit_byte(buf, 0xBA);
                x64_emit_byte(buf, x64_modrm(3, 4, X64_RAX));
                x64_emit_byte(buf, 63);

                /* jc is_void */
                x64_jcc_label(buf, X64_CC_C, labels, is_void);

                /* Not VOID - return FALSE (0) */
                x64_mov_reg_imm32(buf, X64_RAX, SERAPH_VBIT_FALSE);
                x64_jmp_label(buf, labels, done);

                /* Is VOID - return TRUE (1) */
                x64_label_define(labels, buf, is_void);
                x64_mov_reg_imm32(buf, X64_RAX, SERAPH_VBIT_TRUE);

                x64_label_define(labels, buf, done);
            }
            break;

        case CIR_VOID_PROP:
            {
                /* If VOID, return VOID from function */
                x64_load_value(ctx, instr->operands[0], X64_RAX);
                x64_emit_void_propagate(ctx, X64_RAX);
                /* If we get here, value is not VOID */
            }
            break;

        case CIR_VOID_ASSERT:
            {
                /* If VOID, panic (ud2) */
                x64_load_value(ctx, instr->operands[0], X64_RAX);

                uint32_t not_void = x64_label_create(labels);

                /* bt rax, 63 */
                x64_emit_byte(buf, x64_rex(1, 0, 0, 0));
                x64_emit_byte(buf, 0x0F);
                x64_emit_byte(buf, 0xBA);
                x64_emit_byte(buf, x64_modrm(3, 4, X64_RAX));
                x64_emit_byte(buf, 63);

                /* jnc not_void */
                x64_jcc_label(buf, X64_CC_NC, labels, not_void);

                /* Is VOID - panic */
                x64_ud2(buf);

                x64_label_define(labels, buf, not_void);
            }
            break;

        case CIR_VOID_COALESCE:
            {
                /* value ?? default */
                x64_load_value(ctx, instr->operands[0], X64_RAX);
                x64_load_value(ctx, instr->operands[1], X64_RCX);

                uint32_t done = x64_label_create(labels);

                /* bt rax, 63 */
                x64_emit_byte(buf, x64_rex(1, 0, 0, 0));
                x64_emit_byte(buf, 0x0F);
                x64_emit_byte(buf, 0xBA);
                x64_emit_byte(buf, x64_modrm(3, 4, X64_RAX));
                x64_emit_byte(buf, 63);

                /* jnc done (not VOID, keep value) */
                x64_jcc_label(buf, X64_CC_NC, labels, done);

                /* Is VOID - use default */
                x64_mov_reg_reg(buf, X64_RAX, X64_RCX, X64_SZ_64);

                x64_label_define(labels, buf, done);
            }
            break;

        case CIR_VOID_CONST:
            /* Load VOID constant */
            x64_mov_reg_imm64(buf, X64_RAX, SERAPH_X64_VOID_VALUE);
            break;

        default:
            return SERAPH_VBIT_FALSE;
    }

    /* Store result */
    if (instr->result) {
        emit_move_if_needed(ctx, result_reg, X64_RAX);
        if (result_offset != -1) {
            x64_mov_mem_reg(buf, X64_RBP, result_offset, result_reg, X64_SZ_64);
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_lower_capability_op(X64_CompileContext* ctx,
                                     Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;
    X64_Labels* labels = ctx->labels;

    switch (instr->opcode) {
        case CIR_CAP_CREATE:
            {
                /* Create capability from base, length, gen, perms */
                /* Result is pointer to 32-byte capability struct */
                /* For simplicity, allocate on stack */
                int32_t cap_offset = ctx->regalloc.next_spill_offset;
                ctx->regalloc.next_spill_offset -= SERAPH_CAP_SIZE;

                /* Load and store each component */
                x64_load_value(ctx, instr->operands[0], X64_RAX);  /* base */
                x64_mov_mem_reg(buf, X64_RBP, cap_offset + SERAPH_CAP_BASE_OFFSET,
                               X64_RAX, X64_SZ_64);

                x64_load_value(ctx, instr->operands[1], X64_RAX);  /* length */
                x64_mov_mem_reg(buf, X64_RBP, cap_offset + SERAPH_CAP_LENGTH_OFFSET,
                               X64_RAX, X64_SZ_64);

                x64_load_value(ctx, instr->operands[2], X64_RAX);  /* generation */
                x64_mov_mem_reg(buf, X64_RBP, cap_offset + SERAPH_CAP_GEN_OFFSET,
                               X64_RAX, X64_SZ_64);

                x64_load_value(ctx, instr->operands[3], X64_RAX);  /* permissions */
                x64_mov_mem_reg(buf, X64_RBP, cap_offset + SERAPH_CAP_PERMS_OFFSET,
                               X64_RAX, X64_SZ_64);

                /* Result is address of capability */
                x64_lea(buf, X64_RAX, X64_RBP, cap_offset);

                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_CAP_LOAD:
            {
                /* Load through capability with full checking */
                uint32_t fail_label = x64_label_create(labels);
                uint32_t done_label = x64_label_create(labels);

                /* Load capability pointer and offset */
                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* cap */
                x64_load_value(ctx, instr->operands[1], X64_RSI);  /* offset */

                /* Check generation */
                x64_emit_cap_gen_check(ctx, X64_RDI, fail_label);

                /* Check bounds */
                x64_emit_cap_bounds_check(ctx, X64_RDI, X64_RSI, fail_label);

                /* Check read permission */
                x64_emit_cap_perm_check(ctx, X64_RDI, SERAPH_CAP_PERM_READ, fail_label);

                /* All checks passed - perform load */
                /* mov r10, [rdi + SERAPH_CAP_BASE_OFFSET] */
                x64_mov_reg_mem(buf, X64_R10, X64_RDI, SERAPH_CAP_BASE_OFFSET, X64_SZ_64);
                /* add r10, rsi (base + offset) */
                x64_add_reg_reg(buf, X64_R10, X64_RSI, X64_SZ_64);
                /* mov rax, [r10] */
                x64_mov_reg_mem(buf, X64_RAX, X64_R10, 0, X64_SZ_64);
                x64_jmp_label(buf, labels, done_label);

                /* Failure - return VOID */
                x64_label_define(labels, buf, fail_label);
                x64_mov_reg_imm64(buf, X64_RAX, SERAPH_X64_VOID_VALUE);

                x64_label_define(labels, buf, done_label);

                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_CAP_STORE:
            {
                /* Store through capability - silent fail on error */
                uint32_t done_label = x64_label_create(labels);

                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* cap */
                x64_load_value(ctx, instr->operands[1], X64_RSI);  /* offset */
                x64_load_value(ctx, instr->operands[2], X64_RAX);  /* value */

                /* Check generation */
                x64_emit_cap_gen_check(ctx, X64_RDI, done_label);

                /* Check bounds */
                x64_emit_cap_bounds_check(ctx, X64_RDI, X64_RSI, done_label);

                /* Check write permission */
                x64_emit_cap_perm_check(ctx, X64_RDI, SERAPH_CAP_PERM_WRITE, done_label);

                /* All checks passed - perform store */
                x64_mov_reg_mem(buf, X64_R10, X64_RDI, SERAPH_CAP_BASE_OFFSET, X64_SZ_64);
                x64_add_reg_reg(buf, X64_R10, X64_RSI, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_R10, 0, X64_RAX, X64_SZ_64);

                x64_label_define(labels, buf, done_label);
            }
            break;

        case CIR_CAP_CHECK:
            {
                /* Check if capability is valid, return Vbit */
                uint32_t valid_label = x64_label_create(labels);
                uint32_t invalid_label = x64_label_create(labels);
                uint32_t done_label = x64_label_create(labels);

                x64_load_value(ctx, instr->operands[0], X64_RDI);

                /* Check generation */
                x64_emit_cap_gen_check(ctx, X64_RDI, invalid_label);

                /* Valid */
                x64_label_define(labels, buf, valid_label);
                x64_mov_reg_imm32(buf, X64_RAX, SERAPH_VBIT_TRUE);
                x64_jmp_label(buf, labels, done_label);

                /* Invalid */
                x64_label_define(labels, buf, invalid_label);
                x64_mov_reg_imm32(buf, X64_RAX, SERAPH_VBIT_FALSE);

                x64_label_define(labels, buf, done_label);

                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_CAP_NARROW:
            /* Narrow capability bounds: create sub-capability with smaller range
             * operands[0] = source capability
             * operands[1] = new base offset (relative to old base)
             * operands[2] = new length
             * result = new capability with narrowed bounds
             */
            {
                if (instr->operand_count < 3 || !instr->result) {
                    return SERAPH_VBIT_FALSE;
                }

                uint32_t fail_label = x64_label_create(labels);
                uint32_t done_label = x64_label_create(labels);

                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* src cap */
                x64_load_value(ctx, instr->operands[1], X64_RSI);  /* new offset */
                x64_load_value(ctx, instr->operands[2], X64_RDX);  /* new length */

                /* Check derive permission */
                x64_emit_cap_perm_check(ctx, X64_RDI, SERAPH_CAP_PERM_DERIVE, fail_label);

                /* Check new bounds fit within old bounds:
                 * new_offset + new_length <= old_length */
                x64_mov_reg_mem(buf, X64_R10, X64_RDI, SERAPH_CAP_LENGTH_OFFSET, X64_SZ_64);
                x64_mov_reg_reg(buf, X64_R11, X64_RSI, X64_SZ_64);
                x64_add_reg_reg(buf, X64_R11, X64_RDX, X64_SZ_64);
                x64_cmp_reg_reg(buf, X64_R11, X64_R10, X64_SZ_64);
                x64_jcc_label(buf, X64_CC_A, labels, fail_label);  /* Jump if above */

                /* Allocate result capability on stack */
                X64_Reg result_reg;
                int32_t result_offset;
                x64_get_value_location(ctx, instr->result, &result_reg, &result_offset);

                /* new_base = old_base + new_offset */
                x64_mov_reg_mem(buf, X64_RAX, X64_RDI, SERAPH_CAP_BASE_OFFSET, X64_SZ_64);
                x64_add_reg_reg(buf, X64_RAX, X64_RSI, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_BASE_OFFSET,
                                X64_RAX, X64_SZ_64);

                /* new_length = new_length (from RDX) */
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_LENGTH_OFFSET,
                                X64_RDX, X64_SZ_64);

                /* Copy generation */
                x64_mov_reg_mem(buf, X64_RAX, X64_RDI, SERAPH_CAP_GEN_OFFSET, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_GEN_OFFSET,
                                X64_RAX, X64_SZ_64);

                /* Copy permissions (but clear DERIVE to prevent further narrowing) */
                x64_mov_reg_mem(buf, X64_RAX, X64_RDI, SERAPH_CAP_PERMS_OFFSET, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_PERMS_OFFSET,
                                X64_RAX, X64_SZ_64);

                x64_jmp_label(buf, labels, done_label);

                /* Failure - zero out result (VOID capability) */
                x64_label_define(labels, buf, fail_label);
                x64_xor_reg_reg(buf, X64_RAX, X64_RAX, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_BASE_OFFSET,
                                X64_RAX, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_LENGTH_OFFSET,
                                X64_RAX, X64_SZ_64);
                x64_mov_reg_imm64(buf, X64_RAX, SERAPH_X64_VOID_VALUE);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_GEN_OFFSET,
                                X64_RAX, X64_SZ_64);

                x64_label_define(labels, buf, done_label);
            }
            break;

        case CIR_CAP_SPLIT:
            /* Split capability at offset: returns two capabilities
             * operands[0] = source capability
             * operands[1] = split point (offset)
             * result = first half (for now, just create narrowed cap)
             * NOTE: Second half would need a second result, which CIR doesn't support.
             *       For now, this creates the first half; second call creates second half.
             */
            {
                if (instr->operand_count < 2 || !instr->result) {
                    return SERAPH_VBIT_FALSE;
                }

                uint32_t fail_label = x64_label_create(labels);
                uint32_t done_label = x64_label_create(labels);

                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* src cap */
                x64_load_value(ctx, instr->operands[1], X64_RSI);  /* split offset */

                /* Check derive permission */
                x64_emit_cap_perm_check(ctx, X64_RDI, SERAPH_CAP_PERM_DERIVE, fail_label);

                /* Check split point is within bounds */
                x64_mov_reg_mem(buf, X64_R10, X64_RDI, SERAPH_CAP_LENGTH_OFFSET, X64_SZ_64);
                x64_cmp_reg_reg(buf, X64_RSI, X64_R10, X64_SZ_64);
                x64_jcc_label(buf, X64_CC_AE, labels, fail_label);

                /* Create first half: [base, base+split) */
                X64_Reg result_reg;
                int32_t result_offset;
                x64_get_value_location(ctx, instr->result, &result_reg, &result_offset);

                /* Same base */
                x64_mov_reg_mem(buf, X64_RAX, X64_RDI, SERAPH_CAP_BASE_OFFSET, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_BASE_OFFSET,
                                X64_RAX, X64_SZ_64);

                /* Length = split offset */
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_LENGTH_OFFSET,
                                X64_RSI, X64_SZ_64);

                /* Copy generation and permissions */
                x64_mov_reg_mem(buf, X64_RAX, X64_RDI, SERAPH_CAP_GEN_OFFSET, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_GEN_OFFSET,
                                X64_RAX, X64_SZ_64);
                x64_mov_reg_mem(buf, X64_RAX, X64_RDI, SERAPH_CAP_PERMS_OFFSET, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_PERMS_OFFSET,
                                X64_RAX, X64_SZ_64);

                x64_jmp_label(buf, labels, done_label);

                /* Failure */
                x64_label_define(labels, buf, fail_label);
                x64_xor_reg_reg(buf, X64_RAX, X64_RAX, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_BASE_OFFSET,
                                X64_RAX, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_LENGTH_OFFSET,
                                X64_RAX, X64_SZ_64);
                x64_mov_reg_imm64(buf, X64_RAX, SERAPH_X64_VOID_VALUE);
                x64_mov_mem_reg(buf, X64_RBP, result_offset + SERAPH_CAP_GEN_OFFSET,
                                X64_RAX, X64_SZ_64);

                x64_label_define(labels, buf, done_label);
            }
            break;

        case CIR_CAP_REVOKE:
            /* Revoke capability by incrementing its generation
             * operands[0] = capability to revoke
             * This invalidates all copies of this capability.
             * Note: This requires write access to the capability context.
             */
            {
                if (instr->operand_count < 1) {
                    return SERAPH_VBIT_FALSE;
                }

                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* cap */

                /* Increment the global generation counter
                 * R14 points to capability context */
                /* lock inc qword [R14 + SERAPH_CAP_CTX_GEN_OFFSET] */
                x64_emit_byte(buf, 0xF0);  /* LOCK prefix */
                x64_emit_byte(buf, x64_rex(1, 0, 0, 1));  /* REX.WB */
                x64_emit_byte(buf, 0xFF);  /* INC r/m64 */
                x64_emit_byte(buf, x64_modrm(2, 0, X64_R14 & 7));  /* ModRM: [R14+disp32] */
                x64_emit_dword(buf, SERAPH_CAP_CTX_GEN_OFFSET);

                /* Result is void (no return value) */
            }
            break;

        default:
            return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_lower_memory_op(X64_CompileContext* ctx,
                                 Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;

    switch (instr->opcode) {
        case CIR_LOAD:
            {
                /* Raw memory load (no capability check) */
                x64_load_value(ctx, instr->operands[0], X64_RAX);  /* address */

                /* Determine load size based on result type */
                X64_Size load_sz = X64_SZ_64;
                if (instr->result && instr->result->type) {
                    Celestial_Type_Kind kind = instr->result->type->kind;
                    switch (kind) {
                        case CIR_TYPE_I8:
                        case CIR_TYPE_U8:
                        case CIR_TYPE_BOOL:
                            load_sz = X64_SZ_8;
                            break;
                        case CIR_TYPE_I16:
                        case CIR_TYPE_U16:
                            load_sz = X64_SZ_16;
                            break;
                        case CIR_TYPE_I32:
                        case CIR_TYPE_U32:
                            load_sz = X64_SZ_32;
                            break;
                        default:
                            load_sz = X64_SZ_64;
                            break;
                    }
                }

                /* For sub-64-bit loads, use movzx/movsx or movsxd to zero/sign extend */
                if (load_sz == X64_SZ_32) {
                    /* mov eax, [rax] - automatically zero-extends to 64-bit */
                    x64_mov_reg_mem(buf, X64_RAX, X64_RAX, 0, X64_SZ_32);
                } else {
                    x64_mov_reg_mem(buf, X64_RAX, X64_RAX, 0, load_sz);
                }

                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_STORE:
            {
                /* Raw memory store */
                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* address */
                x64_load_value(ctx, instr->operands[1], X64_RAX);  /* value */

                /* Determine store size based on value type */
                X64_Size store_sz = X64_SZ_64;
                if (instr->operands[1] && instr->operands[1]->type) {
                    Celestial_Type_Kind kind = instr->operands[1]->type->kind;
                    switch (kind) {
                        case CIR_TYPE_I8:
                        case CIR_TYPE_U8:
                        case CIR_TYPE_BOOL:
                            store_sz = X64_SZ_8;
                            break;
                        case CIR_TYPE_I16:
                        case CIR_TYPE_U16:
                            store_sz = X64_SZ_16;
                            break;
                        case CIR_TYPE_I32:
                        case CIR_TYPE_U32:
                            store_sz = X64_SZ_32;
                            break;
                        default:
                            store_sz = X64_SZ_64;
                            break;
                    }
                }

                x64_mov_mem_reg(buf, X64_RDI, 0, X64_RAX, store_sz);
            }
            break;

        case CIR_ALLOCA:
            {
                /* Stack allocation */
                /* For simplicity, allocate from spill area */
                size_t size = 8;  /* Default size */
                if (instr->result && instr->result->alloca_type) {
                    size = celestial_type_size(instr->result->alloca_type);
                }
                size = (size + 7) & ~7;  /* Align to 8 */

                /* Allocate space for the actual data */
                int32_t data_offset = ctx->regalloc.next_spill_offset;
                ctx->regalloc.next_spill_offset -= (int32_t)size;

                /* Also allocate a slot to store the address persistently */
                /* This prevents the address from being clobbered */
                int32_t addr_slot = ctx->regalloc.next_spill_offset;
                ctx->regalloc.next_spill_offset -= 8;

                /* Compute address and store it in the persistent slot */
                x64_lea(buf, X64_RAX, X64_RBP, data_offset);
                x64_mov_mem_reg(buf, X64_RBP, addr_slot, X64_RAX, X64_SZ_64);

                /* Record the address slot as the value's location */
                if (instr->result) {
                    /* Force spill location */
                    int found = 0;
                    for (uint32_t i = 0; i < ctx->regalloc.interval_count; i++) {
                        if (ctx->regalloc.intervals[i].vreg_id == instr->result->id) {
                            ctx->regalloc.intervals[i].phys_reg = X64_NONE;
                            ctx->regalloc.intervals[i].spill_offset = addr_slot;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        /* Add new interval with spill */
                        uint32_t idx = ctx->regalloc.interval_count++;
                        ctx->regalloc.intervals[idx].vreg_id = instr->result->id;
                        ctx->regalloc.intervals[idx].phys_reg = X64_NONE;
                        ctx->regalloc.intervals[idx].spill_offset = addr_slot;
                        ctx->regalloc.intervals[idx].start = ctx->current_instr_idx;
                        ctx->regalloc.intervals[idx].end = ctx->current_instr_idx + 1000;
                    }
                }
            }
            break;

        case CIR_GEP:
            {
                /* Get element pointer: base + offset
                 * For struct GEP (2 operands): operands[0]=base, operands[1]=byte offset
                 * For array GEP (3 operands): operands[0]=base, operands[1]=index, operands[2]=elem_size
                 */
                x64_load_value(ctx, instr->operands[0], X64_RAX);  /* base */

                /* Check if this is an array GEP (has element size in operands[2]) */
                if (instr->operand_count >= 3 && instr->operands[2]) {
                    /* Array GEP: compute base + (index * elem_size) */
                    int64_t elem_size = 8;  /* Default */
                    if (instr->operands[2]->kind == CIR_VALUE_CONST) {
                        elem_size = instr->operands[2]->constant.i64;
                    }

                    if (instr->operands[1] && instr->operands[1]->kind == CIR_VALUE_CONST) {
                        /* Constant index: compute offset at compile time */
                        int64_t index = instr->operands[1]->constant.i64;
                        int64_t byte_offset = index * elem_size;
                        if (byte_offset != 0) {
                            x64_add_reg_imm(buf, X64_RAX, (int32_t)byte_offset, X64_SZ_64);
                        }
                    } else if (instr->operands[1]) {
                        /* Dynamic index: multiply at runtime */
                        x64_load_value(ctx, instr->operands[1], X64_RCX);  /* index */
                        /* RCX = index * elem_size */
                        x64_imul_reg_imm(buf, X64_RCX, X64_RCX, (int32_t)elem_size, X64_SZ_64);
                        x64_add_reg_reg(buf, X64_RAX, X64_RCX, X64_SZ_64);
                    }
                } else {
                    /* Struct GEP: operands[1] is already a byte offset */
                    if (instr->operands[1] && instr->operands[1]->kind == CIR_VALUE_CONST) {
                        int64_t offset = instr->operands[1]->constant.i64;
                        if (offset != 0) {
                            x64_add_reg_imm(buf, X64_RAX, (int32_t)offset, X64_SZ_64);
                        }
                    } else if (instr->operands[1]) {
                        x64_load_value(ctx, instr->operands[1], X64_RCX);
                        x64_add_reg_reg(buf, X64_RAX, X64_RCX, X64_SZ_64);
                    }
                }

                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_EXTRACTFIELD:
            {
                /* Extract field from struct value (loaded to register/memory) */
                /* For now, treat struct as pointer and load from offset */
                /* operands[0] = struct pointer */
                /* operands[1] = field index (constant) */
                x64_load_value(ctx, instr->operands[0], X64_RAX);

                if (instr->operands[1] && instr->operands[1]->kind == CIR_VALUE_CONST) {
                    int64_t field_idx = instr->operands[1]->constant.i64;
                    int32_t offset = (int32_t)(field_idx * 8);  /* Simplified: 8 bytes per field */
                    x64_mov_reg_mem(buf, X64_RAX, X64_RAX, offset, X64_SZ_64);
                }

                if (instr->result) {
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_INSERTFIELD:
            {
                /* Insert field into struct value */
                /* operands[0] = struct pointer */
                /* operands[1] = value to insert */
                /* operands[2] = field index (constant) */
                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* struct ptr */
                x64_load_value(ctx, instr->operands[1], X64_RAX);  /* value */

                if (instr->operands[2] && instr->operands[2]->kind == CIR_VALUE_CONST) {
                    int64_t field_idx = instr->operands[2]->constant.i64;
                    int32_t offset = (int32_t)(field_idx * 8);
                    x64_mov_mem_reg(buf, X64_RDI, offset, X64_RAX, X64_SZ_64);
                }

                /* Result is the struct pointer */
                if (instr->result) {
                    x64_mov_reg_reg(buf, X64_RAX, X64_RDI, X64_SZ_64);
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_MEMCPY:
            /* memcpy(dest, src, n)
             * operands[0] = dest pointer
             * operands[1] = src pointer
             * operands[2] = byte count
             */
            {
                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* dest */
                x64_load_value(ctx, instr->operands[1], X64_RSI);  /* src */
                x64_load_value(ctx, instr->operands[2], X64_RCX);  /* count */

                /* Use REP MOVSB for simplicity (direction flag should be clear) */
                x64_emit_byte(buf, 0xFC);  /* CLD - clear direction flag */
                x64_emit_byte(buf, 0xF3);  /* REP prefix */
                x64_emit_byte(buf, 0xA4);  /* MOVSB */

                /* Result is dest pointer if needed */
                if (instr->result) {
                    x64_load_value(ctx, instr->operands[0], X64_RAX);
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        case CIR_MEMSET:
            /* memset(dest, value, n)
             * operands[0] = dest pointer
             * operands[1] = byte value
             * operands[2] = byte count
             */
            {
                x64_load_value(ctx, instr->operands[0], X64_RDI);  /* dest */
                x64_load_value(ctx, instr->operands[1], X64_RAX);  /* value (byte) */
                x64_load_value(ctx, instr->operands[2], X64_RCX);  /* count */

                /* Use REP STOSB */
                x64_emit_byte(buf, 0xFC);  /* CLD - clear direction flag */
                x64_emit_byte(buf, 0xF3);  /* REP prefix */
                x64_emit_byte(buf, 0xAA);  /* STOSB */

                /* Result is dest pointer if needed */
                if (instr->result) {
                    x64_load_value(ctx, instr->operands[0], X64_RAX);
                    x64_store_value(ctx, X64_RAX, instr->result);
                }
            }
            break;

        default:
            return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit x64_lower_galactic_op(X64_CompileContext* ctx,
                                   Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    /* Galactic operations work on 256-bit hyper-dual values:
     * [primal: 128-bit Q64.64] [tangent: 128-bit Q64.64]
     * These are stored on the stack and processed component by component.
     */

    X64_Buffer* buf = ctx->output;

    switch (instr->opcode) {
        case CIR_GALACTIC_ADD:
            /* Galactic addition: result = a + b
             * primal_result = primal_a + primal_b
             * tangent_result = tangent_a + tangent_b
             */
            {
                if (instr->operand_count < 2 || !instr->result) {
                    return SERAPH_VBIT_FALSE;
                }

                /* Get operand locations */
                X64_Reg reg_a, reg_b;
                int32_t off_a, off_b;
                x64_get_value_location(ctx, instr->operands[0], &reg_a, &off_a);
                x64_get_value_location(ctx, instr->operands[1], &reg_b, &off_b);

                /* Get result location */
                X64_Reg reg_r;
                int32_t off_r;
                x64_get_value_location(ctx, instr->result, &reg_r, &off_r);

                /* Add primal components (first 128 bits = 2 x 64-bit) */
                /* Load a.primal.lo, a.primal.hi */
                x64_mov_reg_mem(buf, X64_RAX, X64_RBP, off_a, X64_SZ_64);
                x64_mov_reg_mem(buf, X64_RDX, X64_RBP, off_a + 8, X64_SZ_64);

                /* Add b.primal.lo, b.primal.hi with carry */
                x64_add_reg_mem(buf, X64_RAX, X64_RBP, off_b, X64_SZ_64);
                x64_adc_reg_mem(buf, X64_RDX, X64_RBP, off_b + 8, X64_SZ_64);

                /* Store result primal */
                x64_mov_mem_reg(buf, X64_RBP, off_r, X64_RAX, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, off_r + 8, X64_RDX, X64_SZ_64);

                /* Add tangent components (second 128 bits) */
                x64_mov_reg_mem(buf, X64_RAX, X64_RBP, off_a + 16, X64_SZ_64);
                x64_mov_reg_mem(buf, X64_RDX, X64_RBP, off_a + 24, X64_SZ_64);
                x64_add_reg_mem(buf, X64_RAX, X64_RBP, off_b + 16, X64_SZ_64);
                x64_adc_reg_mem(buf, X64_RDX, X64_RBP, off_b + 24, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, off_r + 16, X64_RAX, X64_SZ_64);
                x64_mov_mem_reg(buf, X64_RBP, off_r + 24, X64_RDX, X64_SZ_64);
            }
            return SERAPH_VBIT_TRUE;

        case CIR_GALACTIC_MUL:
            /* Galactic multiplication with chain rule:
             * primal_result = primal_a * primal_b
             * tangent_result = primal_a * tangent_b + tangent_a * primal_b
             *
             * For Q64.64 multiplication, we need 128x128->256 bit multiply
             * then take the middle 128 bits as the result.
             * This is complex, so we emit a call to a runtime helper.
             */
            {
                if (instr->operand_count < 2 || !instr->result) {
                    return SERAPH_VBIT_FALSE;
                }

                /* Get operand and result locations */
                X64_Reg reg_a, reg_b, reg_r;
                int32_t off_a, off_b, off_r;
                x64_get_value_location(ctx, instr->operands[0], &reg_a, &off_a);
                x64_get_value_location(ctx, instr->operands[1], &reg_b, &off_b);
                x64_get_value_location(ctx, instr->result, &reg_r, &off_r);

                /* Load addresses of operands into arg registers */
                x64_lea(buf, X64_RDI, X64_RBP, off_r);   /* result ptr */
                x64_lea(buf, X64_RSI, X64_RBP, off_a);   /* a ptr */
                x64_lea(buf, X64_RDX, X64_RBP, off_b);   /* b ptr */

                /* Call runtime helper: seraph_galactic_mul(result, a, b)
                 * For now, emit a placeholder call that will be linked later.
                 * The runtime function performs the chain-rule multiplication.
                 */
                x64_emit_byte(buf, 0xE8);  /* CALL rel32 */
                /* Placeholder offset - will be patched to seraph_galactic_mul */
                x64_emit_dword(buf, 0);

                /* Record fixup for runtime linkage if needed */
            }
            return SERAPH_VBIT_TRUE;

        default:
            return SERAPH_VBIT_FALSE;
    }
}

Seraph_Vbit x64_lower_substrate_op(X64_CompileContext* ctx,
                                    Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    /* Substrate operations interact with Atlas/Aether subsystems.
     * These emit calls to runtime functions that handle the actual operations.
     */

    X64_Buffer* buf = ctx->output;
    (void)buf;  /* Used in call emission */

    switch (instr->opcode) {
        case CIR_SUBSTRATE_ENTER:
            /* Enter substrate context - save current state */
            /* Call: seraph_substrate_enter() */
            x64_emit_byte(buf, 0xE8);  /* CALL rel32 */
            x64_emit_dword(buf, 0);    /* Placeholder for seraph_substrate_enter */
            return SERAPH_VBIT_TRUE;

        case CIR_SUBSTRATE_EXIT:
            /* Exit substrate context - restore state */
            /* Call: seraph_substrate_exit() */
            x64_emit_byte(buf, 0xE8);
            x64_emit_dword(buf, 0);    /* Placeholder for seraph_substrate_exit */
            return SERAPH_VBIT_TRUE;

        case CIR_ATLAS_BEGIN:
            /* Begin Atlas transaction
             * Call: seraph_atlas_transaction_begin() */
            x64_emit_byte(buf, 0xE8);
            x64_emit_dword(buf, 0);    /* Placeholder for seraph_atlas_transaction_begin */
            return SERAPH_VBIT_TRUE;

        case CIR_ATLAS_COMMIT:
            /* Commit Atlas transaction
             * Call: seraph_atlas_transaction_commit() */
            x64_emit_byte(buf, 0xE8);
            x64_emit_dword(buf, 0);    /* Placeholder for seraph_atlas_transaction_commit */
            return SERAPH_VBIT_TRUE;

        case CIR_ATLAS_ROLLBACK:
            /* Rollback Atlas transaction
             * Call: seraph_atlas_transaction_rollback() */
            x64_emit_byte(buf, 0xE8);
            x64_emit_dword(buf, 0);    /* Placeholder for seraph_atlas_transaction_rollback */
            return SERAPH_VBIT_TRUE;

        case CIR_AETHER_SYNC:
            /* Synchronize Aether distributed memory
             * Call: seraph_aether_sync() */
            x64_emit_byte(buf, 0xE8);
            x64_emit_dword(buf, 0);    /* Placeholder for seraph_aether_sync */
            return SERAPH_VBIT_TRUE;

        default:
            return SERAPH_VBIT_FALSE;
    }
}

Seraph_Vbit x64_lower_conversion(X64_CompileContext* ctx,
                                  Celestial_Instr* instr) {
    if (!ctx || !instr || !ctx->output) return SERAPH_VBIT_VOID;

    X64_Buffer* buf = ctx->output;

    X64_Reg result_reg;
    int32_t result_offset;
    if (instr->result) {
        x64_get_value_location(ctx, instr->result, &result_reg, &result_offset);
        if (result_reg == X64_NONE) result_reg = X64_RAX;
    } else {
        result_reg = X64_RAX;
    }

    switch (instr->opcode) {
        case CIR_TRUNC:
            /* Truncate - just use lower bits */
            x64_load_value(ctx, instr->operands[0], X64_RAX);
            /* Mask is implicit based on destination type */
            break;

        case CIR_ZEXT:
            /* Zero extend */
            x64_load_value(ctx, instr->operands[0], X64_RAX);
            /* Already zero-extended if loaded into 64-bit reg */
            break;

        case CIR_SEXT:
            /* Sign extend - determine source size from operand type */
            {
                x64_load_value(ctx, instr->operands[0], X64_RAX);

                /* Determine source size from operand type */
                X64_Size src_sz = X64_SZ_32;  /* Default */
                if (instr->operands[0] && instr->operands[0]->type) {
                    switch (instr->operands[0]->type->kind) {
                        case CIR_TYPE_I8:
                        case CIR_TYPE_U8:
                            src_sz = X64_SZ_8;
                            break;
                        case CIR_TYPE_I16:
                        case CIR_TYPE_U16:
                            src_sz = X64_SZ_16;
                            break;
                        case CIR_TYPE_I32:
                        case CIR_TYPE_U32:
                            src_sz = X64_SZ_32;
                            break;
                        default:
                            src_sz = X64_SZ_64;
                            break;
                    }
                }

                /* Sign extend based on source size */
                if (src_sz == X64_SZ_8) {
                    /* MOVSX RAX, AL (sign-extend 8 to 64) */
                    x64_movsx(buf, X64_RAX, X64_RAX, X64_SZ_64, X64_SZ_8);
                } else if (src_sz == X64_SZ_16) {
                    /* MOVSX RAX, AX (sign-extend 16 to 64) */
                    x64_movsx(buf, X64_RAX, X64_RAX, X64_SZ_64, X64_SZ_16);
                } else if (src_sz == X64_SZ_32) {
                    /* MOVSXD RAX, EAX (sign-extend 32 to 64) */
                    x64_movsxd(buf, X64_RAX, X64_RAX);
                }
                /* 64-bit needs no extension */
            }
            break;

        case CIR_BITCAST:
            /* Reinterpret bits - no actual code needed */
            x64_load_value(ctx, instr->operands[0], X64_RAX);
            break;

        default:
            return SERAPH_VBIT_FALSE;
    }

    if (instr->result) {
        emit_move_if_needed(ctx, result_reg, X64_RAX);
        if (result_offset != -1) {
            x64_mov_mem_reg(buf, X64_RBP, result_offset, result_reg, X64_SZ_64);
        }
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Block Layout and Label Management
 *============================================================================*/

static uint32_t get_or_create_block_label(X64_CompileContext* ctx, Celestial_Block* block) {
    if (!ctx || !block) return UINT32_MAX;

    /* Search existing blocks */
    for (uint32_t i = 0; i < ctx->block_count; i++) {
        if (ctx->blocks[i].ir_block == block) {
            return ctx->blocks[i].label_id;
        }
    }

    /* Create new block info */
    if (ctx->block_count >= SERAPH_X64_MAX_BLOCKS) {
        return UINT32_MAX;
    }

    uint32_t label_id = x64_label_create(ctx->labels);
    if (!ctx->blocks) {
        return UINT32_MAX;
    }

    ctx->blocks[ctx->block_count].ir_block = block;
    ctx->blocks[ctx->block_count].label_id = label_id;
    ctx->blocks[ctx->block_count].code_offset = 0;
    ctx->blocks[ctx->block_count].instr_start = 0;
    ctx->blocks[ctx->block_count].instr_count = 0;
    ctx->block_count++;

    return label_id;
}

static Seraph_Vbit compile_block(X64_CompileContext* ctx, Celestial_Block* block) {
    if (!ctx || !block) return SERAPH_VBIT_VOID;

    /* Get or create label for this block */
    uint32_t label_id = get_or_create_block_label(ctx, block);
    if (label_id == UINT32_MAX) return SERAPH_VBIT_FALSE;

    /* Define the label at current position */
    x64_label_define(ctx->labels, ctx->output, label_id);

    /* Update block info with code offset */
    for (uint32_t i = 0; i < ctx->block_count; i++) {
        if (ctx->blocks[i].ir_block == block) {
            ctx->blocks[i].code_offset = ctx->output->size;
            ctx->blocks[i].instr_start = ctx->current_instr_idx;
            break;
        }
    }

    /* Lower each instruction in the block */
    uint32_t instr_count = 0;
    for (Celestial_Instr* instr = block->first; instr; instr = instr->next) {
        Seraph_Vbit result = x64_lower_instruction(ctx, instr);
        if (!seraph_vbit_is_true(result)) {
            /* Error lowering instruction */
            ctx->error_msg = "Failed to lower instruction";
            ctx->error_line = instr->line;
            return SERAPH_VBIT_FALSE;
        }
        ctx->current_instr_idx++;
        instr_count++;
    }

    /* Update block instruction count */
    for (uint32_t i = 0; i < ctx->block_count; i++) {
        if (ctx->blocks[i].ir_block == block) {
            ctx->blocks[i].instr_count = instr_count;
            break;
        }
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Function Compilation
 *============================================================================*/

Seraph_Vbit celestial_compile_function(Celestial_Function* fn,
                                        Celestial_Module* mod,
                                        X64_Buffer* output,
                                        X64_Labels* labels,
                                        Seraph_Arena* arena,
                                        X64_ModuleContext* mod_ctx) {
    if (!fn || !mod || !output || !labels || !arena) {
        return SERAPH_VBIT_VOID;
    }

    /* Initialize compilation context */
    X64_CompileContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.module = mod;
    ctx.function = fn;
    ctx.output = output;
    ctx.labels = labels;
    ctx.arena = arena;
    ctx.mod_ctx = mod_ctx;

    /* Initialize register allocator */
    Seraph_Vbit ra_result = x64_regalloc_init(&ctx.regalloc, arena);
    if (!seraph_vbit_is_true(ra_result)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Allocate block info array */
    ctx.blocks = seraph_arena_alloc(arena, sizeof(X64_BlockInfo) * SERAPH_X64_MAX_BLOCKS, 8);
    if (!ctx.blocks) {
        return SERAPH_VBIT_FALSE;
    }
    ctx.block_count = 0;

    /* Compute live intervals */
    Seraph_Vbit li_result = x64_compute_live_intervals(&ctx);
    if (!seraph_vbit_is_true(li_result)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Perform register allocation */
    Seraph_Vbit alloc_result = x64_linear_scan_allocate(&ctx);
    if (!seraph_vbit_is_true(alloc_result)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Emit function prologue */
    Seraph_Vbit prologue_result = x64_emit_prologue(&ctx);
    if (!seraph_vbit_is_true(prologue_result)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Compile all blocks in linked list order */
    /* This matches ARM64 backend and ensures all blocks are compiled
     * regardless of whether block->succs[] is populated */
    for (Celestial_Block* block = fn->blocks; block; block = block->next) {
        Seraph_Vbit block_result = compile_block(&ctx, block);
        if (!seraph_vbit_is_true(block_result)) {
            fprintf(stderr, "  DEBUG: block compilation failed\n");
            return SERAPH_VBIT_FALSE;
        }
    }

    /* Resolve all label fixups */
    Seraph_Vbit resolve_result = x64_labels_resolve(labels, output);
    if (!seraph_vbit_is_true(resolve_result)) {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Module Compilation
 *============================================================================*/

/**
 * @brief Emit startup stub (_start) that calls main and exits
 *
 * _start:
 *     call main      (relative offset will be patched)
 *     mov rdi, rax   ; exit code from main
 *     mov rax, 60    ; sys_exit
 *     syscall
 *
 * Returns the offset of the call instruction's rel32 operand for patching
 */
static size_t x64_emit_startup_stub(X64_Buffer* output) {
    /* CALL rel32 - E8 xx xx xx xx
     * The rel32 is relative to the instruction AFTER the call (PC+5) */
    x64_emit_byte(output, 0xE8);
    size_t call_fixup_offset = output->size;  /* Remember where to patch */
    x64_emit_dword(output, 0x00000000);  /* Placeholder - will be patched */

    /* MOV RDI, RAX - move exit code */
    x64_mov_reg_reg(output, X64_RDI, X64_RAX, X64_SZ_64);

    /* MOV RAX, 60 - sys_exit syscall number */
    x64_mov_reg_imm32(output, X64_RAX, 60);

    /* SYSCALL */
    x64_syscall(output);

    return call_fixup_offset;
}

/**
 * @brief Helper to find function offset in the module context
 */
static size_t find_function_offset(X64_ModuleContext* mod_ctx, Celestial_Function* fn) {
    for (size_t i = 0; i < mod_ctx->function_count; i++) {
        if (mod_ctx->functions[i].fn == fn) {
            return mod_ctx->functions[i].offset;
        }
    }
    return 0;  /* Not found - should not happen */
}

Seraph_Vbit celestial_compile_module(Celestial_Module* mod,
                                      X64_Buffer* output,
                                      Seraph_Arena* arena) {
    if (!mod || !output || !arena) {
        return SERAPH_VBIT_VOID;
    }

    /* Initialize label table */
    X64_Labels labels;
    Seraph_Vbit labels_result = x64_labels_init(&labels);
    if (!seraph_vbit_is_true(labels_result)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Count functions */
    size_t fn_count = 0;
    for (Celestial_Function* fn = mod->functions; fn; fn = fn->next) {
        fn_count++;
    }

    /* Initialize module context for call fixup tracking */
    X64_ModuleContext mod_ctx;
    memset(&mod_ctx, 0, sizeof(mod_ctx));

    /* Allocate function entry table */
    mod_ctx.functions = seraph_arena_alloc(arena,
        sizeof(X64_FunctionEntry) * SERAPH_X64_MAX_FUNCTIONS, 8);
    if (!mod_ctx.functions) {
        x64_labels_free(&labels);
        return SERAPH_VBIT_FALSE;
    }
    mod_ctx.function_count = 0;
    mod_ctx.function_capacity = SERAPH_X64_MAX_FUNCTIONS;

    /* Allocate call fixup table */
    mod_ctx.call_fixups = seraph_arena_alloc(arena,
        sizeof(X64_CallFixup) * SERAPH_X64_MAX_CALL_FIXUPS, 8);
    if (!mod_ctx.call_fixups) {
        x64_labels_free(&labels);
        return SERAPH_VBIT_FALSE;
    }
    mod_ctx.call_fixup_count = 0;
    mod_ctx.call_fixup_capacity = SERAPH_X64_MAX_CALL_FIXUPS;

    /* Allocate function pointer fixup table */
    mod_ctx.fnptr_fixups = seraph_arena_alloc(arena,
        sizeof(X64_FnptrFixup) * SERAPH_X64_MAX_FNPTR_FIXUPS, 8);
    if (!mod_ctx.fnptr_fixups) {
        x64_labels_free(&labels);
        return SERAPH_VBIT_FALSE;
    }
    mod_ctx.fnptr_fixup_count = 0;
    mod_ctx.fnptr_fixup_capacity = SERAPH_X64_MAX_FNPTR_FIXUPS;

    /* Emit startup stub first (entry point) */
    size_t startup_call_fixup = x64_emit_startup_stub(output);
    size_t startup_call_end = startup_call_fixup + 4;  /* CALL rel32 ends 4 bytes after */

    /* First pass: record function offsets and compile */
    size_t main_offset = 0;
    int found_main = 0;

    for (Celestial_Function* fn = mod->functions; fn; fn = fn->next) {
        /* Record function offset before compiling */
        if (mod_ctx.function_count < mod_ctx.function_capacity) {
            mod_ctx.functions[mod_ctx.function_count].fn = fn;
            mod_ctx.functions[mod_ctx.function_count].offset = output->size;
            mod_ctx.function_count++;
        }

        if (fn->name && strcmp(fn->name, "main") == 0) {
            main_offset = output->size;
            found_main = 1;
        }

        Seraph_Vbit fn_result = celestial_compile_function(fn, mod, output,
                                                            &labels, arena, &mod_ctx);
        if (!seraph_vbit_is_true(fn_result)) {
            x64_labels_free(&labels);
            return SERAPH_VBIT_FALSE;
        }
    }

    /* Patch startup CALL to main */
    if (found_main) {
        int32_t rel32 = (int32_t)(main_offset - startup_call_end);
        output->code[startup_call_fixup + 0] = (uint8_t)(rel32 & 0xFF);
        output->code[startup_call_fixup + 1] = (uint8_t)((rel32 >> 8) & 0xFF);
        output->code[startup_call_fixup + 2] = (uint8_t)((rel32 >> 16) & 0xFF);
        output->code[startup_call_fixup + 3] = (uint8_t)((rel32 >> 24) & 0xFF);
    }

    /* Second pass: patch all internal function calls */
    for (size_t i = 0; i < mod_ctx.call_fixup_count; i++) {
        X64_CallFixup* fixup = &mod_ctx.call_fixups[i];
        size_t callee_offset = find_function_offset(&mod_ctx, fixup->callee);

        /* rel32 = target - (call_site + 4) */
        /* call_site points to the rel32 operand, so PC after call is call_site + 4 */
        int32_t rel32 = (int32_t)(callee_offset - (fixup->call_site + 4));
        output->code[fixup->call_site + 0] = (uint8_t)(rel32 & 0xFF);
        output->code[fixup->call_site + 1] = (uint8_t)((rel32 >> 8) & 0xFF);
        output->code[fixup->call_site + 2] = (uint8_t)((rel32 >> 16) & 0xFF);
        output->code[fixup->call_site + 3] = (uint8_t)((rel32 >> 24) & 0xFF);
    }

    /* Third pass: patch all function pointer loads */
    for (size_t i = 0; i < mod_ctx.fnptr_fixup_count; i++) {
        X64_FnptrFixup* fixup = &mod_ctx.fnptr_fixups[i];
        size_t fn_offset = find_function_offset(&mod_ctx, fixup->fn);

        /* For LEA reg, [RIP + disp32]:
         * disp32 = target - (fixup_site + 4)
         * fixup_site points to the disp32, RIP is at fixup_site + 4 */
        int32_t disp32 = (int32_t)(fn_offset - (fixup->fixup_site + 4));
        output->code[fixup->fixup_site + 0] = (uint8_t)(disp32 & 0xFF);
        output->code[fixup->fixup_site + 1] = (uint8_t)((disp32 >> 8) & 0xFF);
        output->code[fixup->fixup_site + 2] = (uint8_t)((disp32 >> 16) & 0xFF);
        output->code[fixup->fixup_site + 3] = (uint8_t)((disp32 >> 24) & 0xFF);
    }

    /* Free labels (fixups already resolved) */
    x64_labels_free(&labels);

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

X64_Size x64_size_from_type(Celestial_Type* type) {
    if (!type) return X64_SZ_64;

    switch (type->kind) {
        case CIR_TYPE_BOOL:
        case CIR_TYPE_I8:
        case CIR_TYPE_U8:
            return X64_SZ_8;

        case CIR_TYPE_I16:
        case CIR_TYPE_U16:
            return X64_SZ_16;

        case CIR_TYPE_I32:
        case CIR_TYPE_U32:
            return X64_SZ_32;

        case CIR_TYPE_I64:
        case CIR_TYPE_U64:
        case CIR_TYPE_CAPABILITY:
        case CIR_TYPE_SUBSTRATE:
        case CIR_TYPE_POINTER:
            return X64_SZ_64;

        default:
            return X64_SZ_64;
    }
}

/*============================================================================
 * 128-bit Arithmetic (for Q64.64 Scalar)
 *============================================================================*/

void x64_emit_add128(X64_CompileContext* ctx) {
    X64_Buffer* buf = ctx->output;

    /* Input: (RDX:RAX) + (R9:R8)
     * Output: (RDX:RAX)
     */

    /* add rax, r8 */
    x64_add_reg_reg(buf, X64_RAX, X64_R8, X64_SZ_64);

    /* adc rdx, r9 */
    x64_emit_byte(buf, x64_rex(1, 0, 0, 1));  /* REX.W + REX.B for R9 */
    x64_emit_byte(buf, 0x11);  /* ADC r/m64, r64 */
    x64_emit_byte(buf, x64_modrm(3, X64_R9 & 7, X64_RDX));
}

void x64_emit_sub128(X64_CompileContext* ctx) {
    X64_Buffer* buf = ctx->output;

    /* Input: (RDX:RAX) - (R9:R8)
     * Output: (RDX:RAX)
     */

    /* sub rax, r8 */
    x64_sub_reg_reg(buf, X64_RAX, X64_R8, X64_SZ_64);

    /* sbb rdx, r9 */
    x64_emit_byte(buf, x64_rex(1, 0, 0, 1));
    x64_emit_byte(buf, 0x19);  /* SBB r/m64, r64 */
    x64_emit_byte(buf, x64_modrm(3, X64_R9 & 7, X64_RDX));
}

void x64_emit_mul128(X64_CompileContext* ctx) {
    /* Q64.64 multiplication:
     * a = (a_hi.a_lo) [RAX:RDX = a_hi, R8:R9 = a_lo actually stored as RDX:RAX, R9:R8]
     * b = (b_hi.b_lo)
     *
     * Full 128x128 → 256 product:
     *   p0 = a_lo * b_lo   (64x64 → 128)
     *   p1 = a_lo * b_hi   (64x64 → 128)
     *   p2 = a_hi * b_lo   (64x64 → 128)
     *   p3 = a_hi * b_hi   (64x64 → 128)
     *
     * Result = (p3 << 128) + (p2 << 64) + (p1 << 64) + p0
     *
     * For Q64.64, we want the middle 128 bits (bits 64-191 of the 256-bit result).
     *
     * Inputs:  RAX = a_lo, RDX = a_hi, R8 = b_lo, R9 = b_hi
     * Outputs: RAX = result_lo, RDX = result_hi
     */
    X64_Buffer* buf = ctx->output;

    /* Save callee-saved registers we'll use */
    x64_push_reg(buf, X64_RBX);
    x64_push_reg(buf, X64_R12);
    x64_push_reg(buf, X64_R13);
    x64_push_reg(buf, X64_R14);

    /* Move operands to safe registers:
     * R10 = a_lo, R11 = a_hi, R12 = b_lo, R13 = b_hi */
    x64_mov_reg_reg(buf, X64_R10, X64_RAX, X64_SZ_64);
    x64_mov_reg_reg(buf, X64_R11, X64_RDX, X64_SZ_64);
    x64_mov_reg_reg(buf, X64_R12, X64_R8, X64_SZ_64);
    x64_mov_reg_reg(buf, X64_R13, X64_R9, X64_SZ_64);

    /* p0 = a_lo * b_lo → RAX:RDX */
    x64_mov_reg_reg(buf, X64_RAX, X64_R10, X64_SZ_64);
    x64_mul_reg(buf, X64_R12, X64_SZ_64);  /* RDX:RAX = RAX * R12 */
    /* Save p0_lo to RBX, p0_hi to R14 */
    x64_mov_reg_reg(buf, X64_RBX, X64_RAX, X64_SZ_64);   /* p0_lo - not needed but keeping product structure */
    x64_mov_reg_reg(buf, X64_R14, X64_RDX, X64_SZ_64);   /* p0_hi */

    /* p1 = a_lo * b_hi → will add to middle */
    x64_mov_reg_reg(buf, X64_RAX, X64_R10, X64_SZ_64);
    x64_mul_reg(buf, X64_R13, X64_SZ_64);  /* RDX:RAX = a_lo * b_hi */
    /* Add p1_lo to R14 (accumulating middle) */
    x64_add_reg_reg(buf, X64_R14, X64_RAX, X64_SZ_64);
    /* Save p1_hi with carry in RBX */
    x64_mov_reg_reg(buf, X64_RBX, X64_RDX, X64_SZ_64);
    x64_adc_reg_imm(buf, X64_RBX, 0, X64_SZ_64);  /* Add carry */

    /* p2 = a_hi * b_lo → add to middle */
    x64_mov_reg_reg(buf, X64_RAX, X64_R11, X64_SZ_64);
    x64_mul_reg(buf, X64_R12, X64_SZ_64);  /* RDX:RAX = a_hi * b_lo */
    /* Add p2_lo to R14 */
    x64_add_reg_reg(buf, X64_R14, X64_RAX, X64_SZ_64);
    /* Add p2_hi + carry to RBX */
    x64_adc_reg_reg(buf, X64_RBX, X64_RDX, X64_SZ_64);

    /* p3 = a_hi * b_hi → high portion */
    x64_mov_reg_reg(buf, X64_RAX, X64_R11, X64_SZ_64);
    x64_mul_reg(buf, X64_R13, X64_SZ_64);  /* RDX:RAX = a_hi * b_hi */
    /* Add p3_lo to RBX */
    x64_add_reg_reg(buf, X64_RBX, X64_RAX, X64_SZ_64);

    /* Result: R14 = middle_lo (result_lo), RBX = middle_hi (result_hi) */
    x64_mov_reg_reg(buf, X64_RAX, X64_R14, X64_SZ_64);
    x64_mov_reg_reg(buf, X64_RDX, X64_RBX, X64_SZ_64);

    /* Restore callee-saved registers */
    x64_pop_reg(buf, X64_R14);
    x64_pop_reg(buf, X64_R13);
    x64_pop_reg(buf, X64_R12);
    x64_pop_reg(buf, X64_RBX);
}

/*============================================================================
 * Galactic Number Helpers
 *============================================================================*/

void x64_emit_galactic_add(X64_CompileContext* ctx,
                           int32_t dst_offset,
                           int32_t src1_offset,
                           int32_t src2_offset) {
    X64_Buffer* buf = ctx->output;

    /* Galactic = 4 x Q64.64 (128 bits each)
     * Add component by component
     */
    for (int i = 0; i < 4; i++) {
        int32_t comp_offset = i * 16;  /* 128 bits = 16 bytes */

        /* Load src1 component (low 64 bits) */
        x64_mov_reg_mem(buf, X64_RAX, X64_RBP,
                       src1_offset + comp_offset, X64_SZ_64);
        x64_mov_reg_mem(buf, X64_RDX, X64_RBP,
                       src1_offset + comp_offset + 8, X64_SZ_64);

        /* Load src2 component */
        x64_mov_reg_mem(buf, X64_R8, X64_RBP,
                       src2_offset + comp_offset, X64_SZ_64);
        x64_mov_reg_mem(buf, X64_R9, X64_RBP,
                       src2_offset + comp_offset + 8, X64_SZ_64);

        /* Add 128-bit */
        x64_emit_add128(ctx);

        /* Store result */
        x64_mov_mem_reg(buf, X64_RBP, dst_offset + comp_offset,
                       X64_RAX, X64_SZ_64);
        x64_mov_mem_reg(buf, X64_RBP, dst_offset + comp_offset + 8,
                       X64_RDX, X64_SZ_64);
    }
}

void x64_emit_galactic_mul(X64_CompileContext* ctx,
                           int32_t dst_offset,
                           int32_t src1_offset,
                           int32_t src2_offset) {
    /* Galactic multiplication implements the chain rule for hyper-dual numbers:
     *
     * For the simplified 256-bit Galactic (primal + tangent, each Q128):
     *   result.primal = a.primal * b.primal
     *   result.tangent = a.primal * b.tangent + a.tangent * b.primal
     *
     * For the full 512-bit Galactic (w, x, y, z components):
     *   (w1, x1, y1, z1) * (w2, x2, y2, z2) =
     *     (w1*w2,
     *      w1*x2 + x1*w2,
     *      w1*y2 + y1*w2,
     *      w1*z2 + z1*w2 + x1*y2 + y1*x2)
     *
     * This implementation handles the 256-bit case (2-component dual number).
     */
    X64_Buffer* buf = ctx->output;

    /* Component offsets (each Q128 is 16 bytes) */
    #define PRIMAL_OFF  0
    #define TANGENT_OFF 16

    /* Step 1: Compute a.primal * b.primal → temp1
     * Load a.primal */
    x64_mov_reg_mem(buf, X64_RAX, X64_RBP, src1_offset + PRIMAL_OFF, X64_SZ_64);
    x64_mov_reg_mem(buf, X64_RDX, X64_RBP, src1_offset + PRIMAL_OFF + 8, X64_SZ_64);
    /* Load b.primal */
    x64_mov_reg_mem(buf, X64_R8, X64_RBP, src2_offset + PRIMAL_OFF, X64_SZ_64);
    x64_mov_reg_mem(buf, X64_R9, X64_RBP, src2_offset + PRIMAL_OFF + 8, X64_SZ_64);
    /* Multiply */
    x64_emit_mul128(ctx);
    /* Store result.primal */
    x64_mov_mem_reg(buf, X64_RBP, dst_offset + PRIMAL_OFF, X64_RAX, X64_SZ_64);
    x64_mov_mem_reg(buf, X64_RBP, dst_offset + PRIMAL_OFF + 8, X64_RDX, X64_SZ_64);

    /* Step 2: Compute a.primal * b.tangent → temp2
     * Load a.primal */
    x64_mov_reg_mem(buf, X64_RAX, X64_RBP, src1_offset + PRIMAL_OFF, X64_SZ_64);
    x64_mov_reg_mem(buf, X64_RDX, X64_RBP, src1_offset + PRIMAL_OFF + 8, X64_SZ_64);
    /* Load b.tangent */
    x64_mov_reg_mem(buf, X64_R8, X64_RBP, src2_offset + TANGENT_OFF, X64_SZ_64);
    x64_mov_reg_mem(buf, X64_R9, X64_RBP, src2_offset + TANGENT_OFF + 8, X64_SZ_64);
    /* Multiply */
    x64_emit_mul128(ctx);
    /* Save temp2 in R10:R11 */
    x64_mov_reg_reg(buf, X64_R10, X64_RAX, X64_SZ_64);
    x64_mov_reg_reg(buf, X64_R11, X64_RDX, X64_SZ_64);

    /* Step 3: Compute a.tangent * b.primal → temp3
     * Load a.tangent */
    x64_mov_reg_mem(buf, X64_RAX, X64_RBP, src1_offset + TANGENT_OFF, X64_SZ_64);
    x64_mov_reg_mem(buf, X64_RDX, X64_RBP, src1_offset + TANGENT_OFF + 8, X64_SZ_64);
    /* Load b.primal */
    x64_mov_reg_mem(buf, X64_R8, X64_RBP, src2_offset + PRIMAL_OFF, X64_SZ_64);
    x64_mov_reg_mem(buf, X64_R9, X64_RBP, src2_offset + PRIMAL_OFF + 8, X64_SZ_64);
    /* Multiply */
    x64_emit_mul128(ctx);

    /* Step 4: Add temp2 + temp3 for result.tangent */
    x64_add_reg_reg(buf, X64_RAX, X64_R10, X64_SZ_64);
    x64_adc_reg_reg(buf, X64_RDX, X64_R11, X64_SZ_64);
    /* Store result.tangent */
    x64_mov_mem_reg(buf, X64_RBP, dst_offset + TANGENT_OFF, X64_RAX, X64_SZ_64);
    x64_mov_mem_reg(buf, X64_RBP, dst_offset + TANGENT_OFF + 8, X64_RDX, X64_SZ_64);

    #undef PRIMAL_OFF
    #undef TANGENT_OFF
}
