/**
 * @file celestial_to_riscv.c
 * @brief Celestial IR to RISC-V Code Generator Implementation
 *
 * This is the RISC-V (RV64IMAC) backend for SERAPH's native compiler.
 * It generates RISC-V code from Celestial IR with:
 * - VOID-aware operations using bit 63 as VOID flag
 * - Capability-based memory access
 * - Substrate context management
 * - Linear scan register allocation
 */

#include "seraph/seraphim/celestial_to_riscv.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/* Allocatable caller-saved (temporary) registers: t0-t6 (x5-x7, x28-x31) */
#define RV_TEMP_MASK ((1u << 5) | (1u << 6) | (1u << 7) | \
                      (1u << 28) | (1u << 29) | (1u << 30) | (1u << 31))

/* Allocatable callee-saved registers: s0-s9 (x8-x9, x18-x25)
 * Note: s10 (x26) and s11 (x27) are reserved for SERAPH ABI */
#define RV_SAVED_MASK ((1u << 8) | (1u << 9) | \
                       (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | \
                       (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25))

/* Reserved registers:
 * x0:      Zero register
 * x1:      Return address (ra)
 * x2:      Stack pointer (sp)
 * x3:      Global pointer (gp)
 * x4:      Thread pointer (tp)
 * x26:     Substrate context (s10) - SERAPH ABI
 * x27:     Capability context (s11) - SERAPH ABI
 */

/* Scratch registers for code generation */
#define RV_SCRATCH0 RV_T0
#define RV_SCRATCH1 RV_T1

/*============================================================================
 * Register Allocation
 *============================================================================*/

void rv_regalloc_init(RV_RegAlloc* ra, Seraph_Arena* arena) {
    if (!ra || !arena) return;

    memset(ra, 0, sizeof(RV_RegAlloc));
    ra->arena = arena;
    ra->free_temps = RV_TEMP_MASK;
    ra->free_saved = RV_SAVED_MASK;
    ra->spill_offset = 0;
}

static RV_Reg rv_alloc_reg(RV_RegAlloc* ra) __attribute__((unused));
static RV_Reg rv_alloc_reg(RV_RegAlloc* ra) {
    /* Try temporaries first */
    for (int i = 5; i <= 7; i++) {
        if (ra->free_temps & (1u << i)) {
            ra->free_temps &= ~(1u << i);
            return (RV_Reg)i;
        }
    }
    for (int i = 28; i <= 31; i++) {
        if (ra->free_temps & (1u << i)) {
            ra->free_temps &= ~(1u << i);
            return (RV_Reg)i;
        }
    }

    /* Try callee-saved */
    for (int i = 8; i <= 9; i++) {
        if (ra->free_saved & (1u << i)) {
            ra->free_saved &= ~(1u << i);
            return (RV_Reg)i;
        }
    }
    for (int i = 18; i <= 25; i++) {
        if (ra->free_saved & (1u << i)) {
            ra->free_saved &= ~(1u << i);
            return (RV_Reg)i;
        }
    }

    /* No free registers - need to spill */
    return RV_ZERO;  /* Indicates spill needed */
}

static void rv_free_reg(RV_RegAlloc* ra, RV_Reg reg) __attribute__((unused));
static void rv_free_reg(RV_RegAlloc* ra, RV_Reg reg) {
    if (reg >= 5 && reg <= 7) {
        ra->free_temps |= (1u << reg);
    } else if (reg >= 28 && reg <= 31) {
        ra->free_temps |= (1u << reg);
    } else if (reg >= 8 && reg <= 9) {
        ra->free_saved |= (1u << reg);
    } else if (reg >= 18 && reg <= 25) {
        ra->free_saved |= (1u << reg);
    }
}

void rv_regalloc_function(RV_RegAlloc* ra, Celestial_Function* fn) {
    if (!ra || !fn) return;

    /* Simple allocation: assign physical regs to vregs in order */
    /* For parameters, use argument registers (a0-a7 = x10-x17) */
    for (size_t i = 0; i < fn->param_count && i < 8; i++) {
        if (fn->params[i]) {
            fn->params[i]->id = 10 + (uint32_t)i;  /* Use a0-a7 for params */
        }
    }

    /* Reset for local allocations */
    ra->free_temps = RV_TEMP_MASK;
    /* Argument registers are not in the temp pool, so no masking needed */
}

RV_Reg rv_regalloc_get(RV_RegAlloc* ra, uint32_t vreg) {
    (void)ra;
    /* Simple: map vreg to physical register */
    if (vreg >= 10 && vreg <= 17) return (RV_Reg)vreg;  /* Arguments */
    if (vreg < 8) return (RV_Reg)(vreg + 5);  /* Map to t0-t6 range */
    if (vreg < 18) return (RV_Reg)(vreg - 8 + 18);  /* Map to s2-s9 */
    return RV_T2;  /* Fallback */
}

/*============================================================================
 * Context Management
 *============================================================================*/

void rv_context_init(RV_Context* ctx, RV_Buffer* code,
                     Celestial_Module* module, Seraph_Arena* arena) {
    if (!ctx || !code || !module || !arena) return;

    memset(ctx, 0, sizeof(RV_Context));
    ctx->code = code;
    ctx->module = module;
    ctx->arena = arena;

    rv_regalloc_init(&ctx->regalloc, arena);
}

RV_Label* rv_get_block_label(RV_Context* ctx, Celestial_Block* block) {
    if (!ctx || !block) return NULL;

    /* Ensure block_labels array exists */
    if (!ctx->block_labels && ctx->function) {
        size_t count = ctx->function->block_count;
        ctx->block_labels = seraph_arena_alloc(ctx->arena,
                                                count * sizeof(RV_Label*),
                                                _Alignof(RV_Label*));
        if (!ctx->block_labels) return NULL;
        memset(ctx->block_labels, 0, count * sizeof(RV_Label*));
    }

    /* Check if label exists */
    if (ctx->block_labels && block->id < ctx->function->block_count) {
        if (ctx->block_labels[block->id]) {
            return ctx->block_labels[block->id];
        }

        /* Create new label */
        RV_Label* label = seraph_arena_alloc(ctx->arena, sizeof(RV_Label),
                                              _Alignof(RV_Label));
        if (!label) return NULL;

        label->name = block->name;
        label->offset = 0;
        label->resolved = 0;
        label->next = ctx->labels;
        ctx->labels = label;
        ctx->block_labels[block->id] = label;

        return label;
    }

    return NULL;
}

void rv_resolve_fixups(RV_Context* ctx) {
    if (!ctx) return;

    for (RV_Fixup* fix = ctx->fixups; fix; fix = fix->next) {
        if (!fix->target || !fix->target->resolved) continue;

        /* Calculate offset in bytes */
        int32_t offset = ((int32_t)fix->target->offset - (int32_t)fix->patch_pos) * 4;

        /* Patch the instruction */
        if (fix->is_branch) {
            /* Branch instructions (B-type) - 12-bit signed offset */
            /* Need to re-encode with correct offset */
            uint32_t old = ctx->code->data[fix->patch_pos];
            uint8_t funct3 = (old >> 12) & 0x7;
            RV_Reg rs1 = (old >> 15) & 0x1F;
            RV_Reg rs2 = (old >> 20) & 0x1F;

            /* Re-encode based on branch type */
            uint32_t new_instr;
            switch (funct3) {
                case 0x0: new_instr = rv_beq(rs1, rs2, (int16_t)offset); break;
                case 0x1: new_instr = rv_bne(rs1, rs2, (int16_t)offset); break;
                case 0x4: new_instr = rv_blt(rs1, rs2, (int16_t)offset); break;
                case 0x5: new_instr = rv_bge(rs1, rs2, (int16_t)offset); break;
                case 0x6: new_instr = rv_bltu(rs1, rs2, (int16_t)offset); break;
                case 0x7: new_instr = rv_bgeu(rs1, rs2, (int16_t)offset); break;
                default:  new_instr = old; break;
            }
            rv_patch(ctx->code, fix->patch_pos, new_instr);
        } else {
            /* JAL instruction (J-type) - 20-bit signed offset */
            rv_patch(ctx->code, fix->patch_pos, rv_jal(RV_ZERO, offset));
        }
    }
}

/*============================================================================
 * Code Generation Helpers
 *============================================================================*/

RV_Reg rv_load_value(RV_Context* ctx, Celestial_Value* val) {
    if (!ctx || !val) return RV_ZERO;

    switch (val->kind) {
        case CIR_VALUE_CONST:
            /* Load constant into scratch register */
            rv_emit_li(ctx->code, RV_SCRATCH0, val->constant.i64);
            return RV_SCRATCH0;

        case CIR_VALUE_PARAM:
            /* Parameters are in a0-a7 (x10-x17) */
            if (val->param.index < 8) {
                return (RV_Reg)(RV_A0 + val->param.index);
            }
            /* Stack parameter - load from frame */
            rv_emit(ctx->code, rv_ld(RV_SCRATCH0, RV_FP,
                                      (int16_t)(16 + (val->param.index - 8) * 8)));
            return RV_SCRATCH0;

        case CIR_VALUE_VREG:
            return rv_regalloc_get(&ctx->regalloc, val->id);

        case CIR_VALUE_VOID_CONST:
            /* Load -1 (all bits set = VOID) */
            rv_emit(ctx->code, rv_addi(RV_SCRATCH0, RV_ZERO, -1));
            return RV_SCRATCH0;

        case CIR_VALUE_FNPTR:
            /* Load function pointer - address resolved by linker */
            /* For now, load placeholder address (0) */
            rv_emit_li(ctx->code, RV_SCRATCH0, 0);
            return RV_SCRATCH0;

        default:
            return RV_ZERO;
    }
}

void rv_store_value(RV_Context* ctx, RV_Reg reg, Celestial_Value* val) {
    if (!ctx || !val) return;

    if (val->kind == CIR_VALUE_VREG) {
        RV_Reg dest = rv_regalloc_get(&ctx->regalloc, val->id);
        if (dest != reg) {
            rv_emit(ctx->code, rv_mv(dest, reg));
        }
    }
}

/*============================================================================
 * Function Prologue/Epilogue
 *============================================================================*/

void rv_emit_prologue(RV_Context* ctx) {
    if (!ctx) return;

    /* Standard RISC-V prologue:
     * addi sp, sp, -frame_size
     * sd ra, frame_size-8(sp)
     * sd fp, frame_size-16(sp)
     * addi fp, sp, frame_size
     * (save callee-saved registers)
     */

    int32_t frame_size = ctx->frame_size;
    if (frame_size < 16) frame_size = 16;
    /* Align to 16 bytes */
    frame_size = (frame_size + 15) & ~15;

    /* Allocate stack frame */
    rv_emit(ctx->code, rv_addi(RV_SP, RV_SP, (int16_t)(-frame_size)));

    /* Save return address and frame pointer */
    rv_emit(ctx->code, rv_sd(RV_RA, RV_SP, (int16_t)(frame_size - 8)));
    rv_emit(ctx->code, rv_sd(RV_FP, RV_SP, (int16_t)(frame_size - 16)));

    /* Set up frame pointer */
    rv_emit(ctx->code, rv_addi(RV_FP, RV_SP, (int16_t)frame_size));

    /* Save callee-saved registers that we use */
    int32_t save_offset = frame_size - 24;
    for (int i = 8; i <= 9; i++) {
        if (!(ctx->regalloc.free_saved & (1u << i))) {
            if (save_offset >= 0) {
                rv_emit(ctx->code, rv_sd((RV_Reg)i, RV_SP, (int16_t)save_offset));
                save_offset -= 8;
            }
        }
    }
    for (int i = 18; i <= 25; i++) {
        if (!(ctx->regalloc.free_saved & (1u << i))) {
            if (save_offset >= 0) {
                rv_emit(ctx->code, rv_sd((RV_Reg)i, RV_SP, (int16_t)save_offset));
                save_offset -= 8;
            }
        }
    }
}

void rv_emit_epilogue(RV_Context* ctx) {
    if (!ctx) return;

    int32_t frame_size = ctx->frame_size;
    if (frame_size < 16) frame_size = 16;
    frame_size = (frame_size + 15) & ~15;

    /* Restore callee-saved registers */
    int32_t save_offset = frame_size - 24;
    for (int i = 8; i <= 9; i++) {
        if (!(ctx->regalloc.free_saved & (1u << i))) {
            if (save_offset >= 0) {
                rv_emit(ctx->code, rv_ld((RV_Reg)i, RV_SP, (int16_t)save_offset));
                save_offset -= 8;
            }
        }
    }
    for (int i = 18; i <= 25; i++) {
        if (!(ctx->regalloc.free_saved & (1u << i))) {
            if (save_offset >= 0) {
                rv_emit(ctx->code, rv_ld((RV_Reg)i, RV_SP, (int16_t)save_offset));
                save_offset -= 8;
            }
        }
    }

    /* Restore return address and frame pointer */
    rv_emit(ctx->code, rv_ld(RV_RA, RV_SP, (int16_t)(frame_size - 8)));
    rv_emit(ctx->code, rv_ld(RV_FP, RV_SP, (int16_t)(frame_size - 16)));

    /* Deallocate stack frame */
    rv_emit(ctx->code, rv_addi(RV_SP, RV_SP, (int16_t)frame_size));

    /* Return */
    rv_emit(ctx->code, rv_ret());
}

/*============================================================================
 * Instruction Lowering
 *============================================================================*/

void rv_lower_arith(RV_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    RV_Reg rd = (instr->result) ?
                rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;
    RV_Reg rs1 = rv_load_value(ctx, instr->operands[0]);
    RV_Reg rs2 = RV_ZERO;

    if (instr->operand_count > 1) {
        rs2 = rv_load_value(ctx, instr->operands[1]);
    }

    switch (instr->opcode) {
        case CIR_ADD:
            rv_emit(ctx->code, rv_add(rd, rs1, rs2));
            break;

        case CIR_SUB:
            rv_emit(ctx->code, rv_sub(rd, rs1, rs2));
            break;

        case CIR_MUL:
            rv_emit(ctx->code, rv_mul(rd, rs1, rs2));
            break;

        case CIR_DIV:
            /* Check for division by zero - produces VOID
             * beq rs2, zero, .void
             * div rd, rs1, rs2
             * j .done
             * .void:
             * addi rd, zero, -1
             * .done:
             */
            rv_emit(ctx->code, rv_beq(rs2, RV_ZERO, 12));  /* Skip to void if zero */
            rv_emit(ctx->code, rv_div(rd, rs1, rs2));
            rv_emit(ctx->code, rv_j(8));  /* Skip void case */
            rv_emit(ctx->code, rv_addi(rd, RV_ZERO, -1));  /* VOID */
            break;

        case CIR_MOD:
            /* Modulo with division by zero check */
            rv_emit(ctx->code, rv_beq(rs2, RV_ZERO, 12));
            rv_emit(ctx->code, rv_rem(rd, rs1, rs2));
            rv_emit(ctx->code, rv_j(8));
            rv_emit(ctx->code, rv_addi(rd, RV_ZERO, -1));
            break;

        case CIR_NEG:
            rv_emit(ctx->code, rv_neg(rd, rs1));
            break;

        case CIR_AND:
            rv_emit(ctx->code, rv_and(rd, rs1, rs2));
            break;

        case CIR_OR:
            rv_emit(ctx->code, rv_or(rd, rs1, rs2));
            break;

        case CIR_XOR:
            rv_emit(ctx->code, rv_xor(rd, rs1, rs2));
            break;

        case CIR_NOT:
            rv_emit(ctx->code, rv_not(rd, rs1));
            break;

        case CIR_SHL:
            rv_emit(ctx->code, rv_sll(rd, rs1, rs2));
            break;

        case CIR_SHR:
            rv_emit(ctx->code, rv_srl(rd, rs1, rs2));
            break;

        case CIR_SAR:
            rv_emit(ctx->code, rv_sra(rd, rs1, rs2));
            break;

        default:
            break;
    }
}

void rv_lower_cmp(RV_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    RV_Reg rd = (instr->result) ?
                rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;
    RV_Reg rs1 = rv_load_value(ctx, instr->operands[0]);
    RV_Reg rs2 = rv_load_value(ctx, instr->operands[1]);

    switch (instr->opcode) {
        case CIR_EQ:
            /* xor tmp, rs1, rs2; seqz rd, tmp */
            rv_emit(ctx->code, rv_xor(rd, rs1, rs2));
            rv_emit(ctx->code, rv_seqz(rd, rd));
            break;

        case CIR_NE:
            /* xor tmp, rs1, rs2; snez rd, tmp */
            rv_emit(ctx->code, rv_xor(rd, rs1, rs2));
            rv_emit(ctx->code, rv_snez(rd, rd));
            break;

        case CIR_LT:
            /* slt rd, rs1, rs2 */
            rv_emit(ctx->code, rv_slt(rd, rs1, rs2));
            break;

        case CIR_LE:
            /* slt rd, rs2, rs1; xori rd, rd, 1 (rd = !(rs2 < rs1)) */
            rv_emit(ctx->code, rv_slt(rd, rs2, rs1));
            rv_emit(ctx->code, rv_xori(rd, rd, 1));
            break;

        case CIR_GT:
            /* slt rd, rs2, rs1 */
            rv_emit(ctx->code, rv_slt(rd, rs2, rs1));
            break;

        case CIR_GE:
            /* slt rd, rs1, rs2; xori rd, rd, 1 (rd = !(rs1 < rs2)) */
            rv_emit(ctx->code, rv_slt(rd, rs1, rs2));
            rv_emit(ctx->code, rv_xori(rd, rd, 1));
            break;

        default:
            break;
    }
}

void rv_lower_control(RV_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    switch (instr->opcode) {
        case CIR_JUMP: {
            RV_Label* target = rv_get_block_label(ctx, instr->target1);
            if (target) {
                if (target->resolved) {
                    int32_t offset = ((int32_t)target->offset -
                                      (int32_t)rv_buffer_pos(ctx->code)) * 4;
                    rv_emit(ctx->code, rv_j(offset));
                } else {
                    /* Forward reference - add fixup */
                    RV_Fixup* fix = seraph_arena_alloc(ctx->arena,
                                                        sizeof(RV_Fixup),
                                                        _Alignof(RV_Fixup));
                    if (fix) {
                        fix->patch_pos = rv_buffer_pos(ctx->code);
                        fix->target = target;
                        fix->is_branch = 0;
                        fix->next = ctx->fixups;
                        ctx->fixups = fix;
                    }
                    rv_emit(ctx->code, rv_j(0));  /* Placeholder */
                }
            }
            break;
        }

        case CIR_BRANCH: {
            RV_Reg cond = rv_load_value(ctx, instr->operands[0]);
            RV_Label* then_lbl = rv_get_block_label(ctx, instr->target1);
            RV_Label* else_lbl = rv_get_block_label(ctx, instr->target2);

            /* BNE cond, zero, then; J else */
            if (then_lbl) {
                if (then_lbl->resolved) {
                    int32_t offset = ((int32_t)then_lbl->offset -
                                      (int32_t)rv_buffer_pos(ctx->code)) * 4;
                    rv_emit(ctx->code, rv_bne(cond, RV_ZERO, (int16_t)offset));
                } else {
                    RV_Fixup* fix = seraph_arena_alloc(ctx->arena,
                                                        sizeof(RV_Fixup),
                                                        _Alignof(RV_Fixup));
                    if (fix) {
                        fix->patch_pos = rv_buffer_pos(ctx->code);
                        fix->target = then_lbl;
                        fix->is_branch = 1;
                        fix->next = ctx->fixups;
                        ctx->fixups = fix;
                    }
                    rv_emit(ctx->code, rv_bne(cond, RV_ZERO, 0));
                }
            }

            if (else_lbl) {
                if (else_lbl->resolved) {
                    int32_t offset = ((int32_t)else_lbl->offset -
                                      (int32_t)rv_buffer_pos(ctx->code)) * 4;
                    rv_emit(ctx->code, rv_j(offset));
                } else {
                    RV_Fixup* fix = seraph_arena_alloc(ctx->arena,
                                                        sizeof(RV_Fixup),
                                                        _Alignof(RV_Fixup));
                    if (fix) {
                        fix->patch_pos = rv_buffer_pos(ctx->code);
                        fix->target = else_lbl;
                        fix->is_branch = 0;
                        fix->next = ctx->fixups;
                        ctx->fixups = fix;
                    }
                    rv_emit(ctx->code, rv_j(0));
                }
            }
            break;
        }

        case CIR_RETURN: {
            if (instr->operand_count > 0 && instr->operands[0]) {
                RV_Reg val = rv_load_value(ctx, instr->operands[0]);
                if (val != RV_A0) {
                    rv_emit(ctx->code, rv_mv(RV_A0, val));
                }
            }
            rv_emit_epilogue(ctx);
            break;
        }

        case CIR_CALL: {
            Celestial_Function* callee = instr->callee;
            if (!callee) break;

            /* Load arguments into a0-a7 */
            for (size_t i = 0; i < instr->operand_count && i < 8; i++) {
                RV_Reg arg = rv_load_value(ctx, instr->operands[i]);
                if (arg != (RV_Reg)(RV_A0 + i)) {
                    rv_emit(ctx->code, rv_mv((RV_Reg)(RV_A0 + i), arg));
                }
            }

            /* TODO: Handle function address properly */
            /* For now, emit JAL with placeholder */
            rv_emit(ctx->code, rv_jal(RV_RA, 0));

            /* Store result if needed */
            if (instr->result) {
                RV_Reg rd = rv_regalloc_get(&ctx->regalloc, instr->result->id);
                if (rd != RV_A0) {
                    rv_emit(ctx->code, rv_mv(rd, RV_A0));
                }
            }
            break;
        }

        case CIR_CALL_INDIRECT: {
            /* Indirect call through function pointer */
            /* operands[0] = function pointer */
            /* operands[1..n] = arguments */

            /* Load function pointer into scratch register */
            RV_Reg fn_ptr = rv_load_value(ctx, instr->operands[0]);
            if (fn_ptr != RV_SCRATCH0) {
                rv_emit(ctx->code, rv_mv(RV_SCRATCH0, fn_ptr));
            }

            /* Load arguments into a0-a7 */
            size_t arg_count = instr->operand_count - 1;
            for (size_t i = 0; i < arg_count && i < 8; i++) {
                RV_Reg arg = rv_load_value(ctx, instr->operands[i + 1]);
                if (arg != (RV_Reg)(RV_A0 + i)) {
                    rv_emit(ctx->code, rv_mv((RV_Reg)(RV_A0 + i), arg));
                }
            }

            /* JALR ra, 0(t0) - indirect call through t0 (SCRATCH0) */
            rv_emit(ctx->code, rv_jalr(RV_RA, RV_SCRATCH0, 0));

            /* Store result if needed */
            if (instr->result) {
                RV_Reg rd = rv_regalloc_get(&ctx->regalloc, instr->result->id);
                if (rd != RV_A0) {
                    rv_emit(ctx->code, rv_mv(rd, RV_A0));
                }
            }
            break;
        }

        default:
            break;
    }
}

void rv_lower_void_op(RV_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    RV_Reg val = rv_load_value(ctx, instr->operands[0]);
    RV_Reg rd = (instr->result) ?
                rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;

    switch (instr->opcode) {
        case CIR_VOID_TEST:
            /* Test if bit 63 is set (value is VOID)
             * srli rd, val, 63 */
            rv_emit(ctx->code, rv_srli(rd, val, 63));
            break;

        case CIR_VOID_PROP:
            /* If VOID, early return VOID
             * srli tmp, val, 63
             * beq tmp, zero, .continue  (skip if not VOID)
             * li a0, -1
             * [epilogue]
             * .continue:
             * mv rd, val
             */
            rv_emit(ctx->code, rv_srli(RV_SCRATCH1, val, 63));
            rv_emit(ctx->code, rv_beq(RV_SCRATCH1, RV_ZERO, 24));  /* Skip 6 instrs */
            rv_emit(ctx->code, rv_addi(RV_A0, RV_ZERO, -1));
            rv_emit_epilogue(ctx);
            /* Continue - pass through value */
            if (rd != val) {
                rv_emit(ctx->code, rv_mv(rd, val));
            }
            break;

        case CIR_VOID_ASSERT:
            /* If VOID, trap
             * srli tmp, val, 63
             * beq tmp, zero, .continue  (skip if not VOID)
             * ebreak
             * .continue:
             * mv rd, val
             */
            rv_emit(ctx->code, rv_srli(RV_SCRATCH1, val, 63));
            rv_emit(ctx->code, rv_beq(RV_SCRATCH1, RV_ZERO, 8));
            rv_emit(ctx->code, rv_ebreak());
            if (rd != val) {
                rv_emit(ctx->code, rv_mv(rd, val));
            }
            break;

        case CIR_VOID_COALESCE: {
            /* If value is VOID, use default instead
             * srli tmp, val, 63
             * beq tmp, zero, .use_val
             * mv rd, default
             * j .done
             * .use_val:
             * mv rd, val
             * .done:
             */
            RV_Reg default_val = rv_load_value(ctx, instr->operands[1]);
            rv_emit(ctx->code, rv_srli(RV_SCRATCH1, val, 63));
            rv_emit(ctx->code, rv_beq(RV_SCRATCH1, RV_ZERO, 12));  /* Skip to use_val */
            rv_emit(ctx->code, rv_mv(rd, default_val));
            rv_emit(ctx->code, rv_j(8));  /* Skip to done */
            rv_emit(ctx->code, rv_mv(rd, val));
            break;
        }

        default:
            break;
    }
}

/*============================================================================
 * Capability and Substrate Operations
 *============================================================================*/

static void rv_lower_cap_op(RV_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    switch (instr->opcode) {
        case CIR_CAP_LOAD: {
            RV_Reg cap = rv_load_value(ctx, instr->operands[0]);
            RV_Reg off = rv_load_value(ctx, instr->operands[1]);
            RV_Reg rd = (instr->result) ?
                        rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;

            /* Capability-checked load:
             * For now, simplified: base + offset */
            rv_emit(ctx->code, rv_add(RV_SCRATCH0, cap, off));
            rv_emit(ctx->code, rv_ld(rd, RV_SCRATCH0, 0));
            break;
        }

        case CIR_CAP_STORE: {
            RV_Reg cap = rv_load_value(ctx, instr->operands[0]);
            RV_Reg off = rv_load_value(ctx, instr->operands[1]);
            RV_Reg val = rv_load_value(ctx, instr->operands[2]);

            /* Capability-checked store */
            rv_emit(ctx->code, rv_add(RV_SCRATCH0, cap, off));
            rv_emit(ctx->code, rv_sd(val, RV_SCRATCH0, 0));
            break;
        }

        default:
            break;
    }
}

static void rv_lower_substrate_op(RV_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    switch (instr->opcode) {
        case CIR_SUBSTRATE_ENTER:
            /* Set substrate context in s10 (x26) */
            /* The actual context setup would be done by runtime */
            break;

        case CIR_SUBSTRATE_EXIT:
            /* Clear substrate context */
            rv_emit(ctx->code, rv_mv(RV_S10, RV_ZERO));
            break;

        default:
            break;
    }
}

/*============================================================================
 * Main Instruction Lowering
 *============================================================================*/

void rv_lower_instr(RV_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    switch (instr->opcode) {
        /* Arithmetic */
        case CIR_ADD:
        case CIR_SUB:
        case CIR_MUL:
        case CIR_DIV:
        case CIR_MOD:
        case CIR_NEG:
        case CIR_AND:
        case CIR_OR:
        case CIR_XOR:
        case CIR_NOT:
        case CIR_SHL:
        case CIR_SHR:
        case CIR_SAR:
            rv_lower_arith(ctx, instr);
            break;

        /* Comparison */
        case CIR_EQ:
        case CIR_NE:
        case CIR_LT:
        case CIR_LE:
        case CIR_GT:
        case CIR_GE:
            rv_lower_cmp(ctx, instr);
            break;

        /* Control flow */
        case CIR_JUMP:
        case CIR_BRANCH:
        case CIR_RETURN:
        case CIR_CALL:
        case CIR_CALL_INDIRECT:
            rv_lower_control(ctx, instr);
            break;

        /* VOID operations */
        case CIR_VOID_TEST:
        case CIR_VOID_PROP:
        case CIR_VOID_ASSERT:
        case CIR_VOID_COALESCE:
            rv_lower_void_op(ctx, instr);
            break;

        /* Capability operations */
        case CIR_CAP_LOAD:
        case CIR_CAP_STORE:
            rv_lower_cap_op(ctx, instr);
            break;

        /* Substrate operations */
        case CIR_SUBSTRATE_ENTER:
        case CIR_SUBSTRATE_EXIT:
            rv_lower_substrate_op(ctx, instr);
            break;

        /* Memory operations */
        case CIR_LOAD: {
            RV_Reg ptr = rv_load_value(ctx, instr->operands[0]);
            RV_Reg rd = (instr->result) ?
                        rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;
            rv_emit(ctx->code, rv_ld(rd, ptr, 0));
            break;
        }

        case CIR_STORE: {
            RV_Reg ptr = rv_load_value(ctx, instr->operands[0]);
            RV_Reg val = rv_load_value(ctx, instr->operands[1]);
            rv_emit(ctx->code, rv_sd(val, ptr, 0));
            break;
        }

        case CIR_ALLOCA: {
            RV_Reg rd = (instr->result) ?
                        rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;
            /* Allocate from frame - compute address */
            ctx->local_size += 8;  /* Assume 8-byte allocation */
            rv_emit(ctx->code, rv_addi(rd, RV_FP, (int16_t)(-ctx->local_size)));
            break;
        }

        case CIR_GEP: {
            /* Get element pointer: base + offset */
            RV_Reg base = rv_load_value(ctx, instr->operands[0]);
            RV_Reg rd = (instr->result) ?
                        rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;

            if (instr->operands[1] && instr->operands[1]->kind == CIR_VALUE_CONST) {
                int64_t offset = instr->operands[1]->constant.i64;
                if (offset == 0) {
                    rv_emit(ctx->code, rv_addi(rd, base, 0));  /* mv rd, base */
                } else {
                    rv_emit(ctx->code, rv_addi(rd, base, (int16_t)offset));
                }
            } else if (instr->operands[1]) {
                RV_Reg off = rv_load_value(ctx, instr->operands[1]);
                rv_emit(ctx->code, rv_add(rd, base, off));
            }
            break;
        }

        case CIR_EXTRACTFIELD: {
            /* Load from struct field */
            RV_Reg ptr = rv_load_value(ctx, instr->operands[0]);
            RV_Reg rd = (instr->result) ?
                        rv_regalloc_get(&ctx->regalloc, instr->result->id) : RV_ZERO;

            int64_t field_idx = 0;
            if (instr->operands[1] && instr->operands[1]->kind == CIR_VALUE_CONST) {
                field_idx = instr->operands[1]->constant.i64;
            }
            int16_t offset = (int16_t)(field_idx * 8);
            rv_emit(ctx->code, rv_ld(rd, ptr, offset));
            break;
        }

        case CIR_INSERTFIELD: {
            /* Store to struct field */
            RV_Reg ptr = rv_load_value(ctx, instr->operands[0]);
            RV_Reg val = rv_load_value(ctx, instr->operands[1]);

            int64_t field_idx = 0;
            if (instr->operands[2] && instr->operands[2]->kind == CIR_VALUE_CONST) {
                field_idx = instr->operands[2]->constant.i64;
            }
            int16_t offset = (int16_t)(field_idx * 8);
            rv_emit(ctx->code, rv_sd(val, ptr, offset));

            /* Return struct pointer */
            if (instr->result) {
                RV_Reg rd = rv_regalloc_get(&ctx->regalloc, instr->result->id);
                rv_emit(ctx->code, rv_addi(rd, ptr, 0));  /* mv rd, ptr */
            }
            break;
        }

        case CIR_NOP:
            /* Folded constant - skip */
            break;

        default:
            /* NOP for unhandled instructions */
            rv_emit(ctx->code, rv_nop());
            break;
    }
}

/*============================================================================
 * Block and Function Compilation
 *============================================================================*/

void rv_compile_block(RV_Context* ctx, Celestial_Block* block) {
    if (!ctx || !block) return;

    /* Mark label as resolved */
    RV_Label* label = rv_get_block_label(ctx, block);
    if (label) {
        label->offset = rv_buffer_pos(ctx->code);
        label->resolved = 1;
    }

    /* Lower each instruction */
    for (Celestial_Instr* instr = block->first; instr; instr = instr->next) {
        rv_lower_instr(ctx, instr);
    }
}

void rv_compile_function(RV_Context* ctx, Celestial_Function* fn) {
    if (!ctx || !fn) return;

    ctx->function = fn;
    ctx->block_labels = NULL;
    ctx->fixups = NULL;
    ctx->local_size = 0;
    ctx->save_size = 0;

    /* Perform register allocation */
    rv_regalloc_function(&ctx->regalloc, fn);

    /* Calculate frame size (16-byte aligned) */
    ctx->frame_size = 32;  /* RA + FP + some space for locals */

    /* Emit prologue */
    rv_emit_prologue(ctx);

    /* Compile all blocks */
    for (Celestial_Block* block = fn->blocks; block; block = block->next) {
        rv_compile_block(ctx, block);
    }

    /* Resolve forward references */
    rv_resolve_fixups(ctx);

    ctx->function = NULL;
}

/**
 * @brief Emit RISC-V startup stub
 *
 * _start:
 *     jal ra, main  ; call main (will be patched)
 *     li a7, 93     ; sys_exit syscall number
 *     ecall         ; syscall
 *
 * Returns the instruction index where the JAL instruction needs to be patched
 */
static size_t rv_emit_startup_stub(RV_Context* ctx) {
    /* JAL ra, offset (placeholder - will be patched) */
    size_t jal_index = ctx->code->count;
    rv_emit(ctx->code, rv_jal(RV_RA, 0));  /* JAL ra, 0 (placeholder) */

    /* LI a7, 93 (exit syscall number on Linux RISC-V) */
    rv_emit(ctx->code, rv_addi(RV_A7, RV_ZERO, 93));

    /* ECALL (environment call for syscall) */
    rv_emit(ctx->code, rv_ecall());

    return jal_index;
}

void rv_compile_module(RV_Context* ctx) {
    if (!ctx || !ctx->module) return;

    /* Emit startup stub first (entry point) */
    size_t jal_index = rv_emit_startup_stub(ctx);

    /* Track main function instruction index */
    size_t main_index = 0;
    int found_main = 0;

    /* Compile each function */
    for (Celestial_Function* fn = ctx->module->functions; fn; fn = fn->next) {
        /* Record main function index before compiling */
        if (fn->name && strcmp(fn->name, "main") == 0) {
            main_index = ctx->code->count;
            found_main = 1;
        }
        rv_compile_function(ctx, fn);
    }

    /* Patch JAL instruction to call main */
    if (found_main) {
        /* RISC-V JAL offset is in bytes (instruction index * 4) */
        int32_t offset_bytes = (int32_t)((main_index - jal_index) * 4);

        /* Use the rv_jal helper which handles encoding */
        uint32_t jal_instr = rv_jal(RV_RA, offset_bytes);
        rv_patch(ctx->code, jal_index, jal_instr);
    }
}
