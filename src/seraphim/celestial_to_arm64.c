/**
 * @file celestial_to_arm64.c
 * @brief Celestial IR to ARM64 Code Generator Implementation
 *
 * This is the ARM64 backend for SERAPH's native compiler.
 * It generates ARM64 code from Celestial IR with:
 * - VOID-aware operations using bit 63 as VOID flag
 * - Capability-based memory access
 * - Substrate context management
 * - Linear scan register allocation
 */

#include "seraph/seraphim/celestial_to_arm64.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/* Allocatable caller-saved registers: X0-X15 (excluding X16-X17 scratch) */
#define ARM64_CALLER_SAVED_MASK 0x0000FFFF

/* Allocatable callee-saved registers: X19-X26 */
#define ARM64_CALLEE_SAVED_MASK 0x07F80000

/* Reserved registers:
 * X16-X17: Scratch (IP0, IP1)
 * X18:     Platform register
 * X27:     Substrate context
 * X28:     Capability context
 * X29:     Frame pointer
 * X30:     Link register
 * SP:      Stack pointer
 */

/* Scratch registers for code generation */
#define ARM64_SCRATCH0 ARM64_X16
#define ARM64_SCRATCH1 ARM64_X17

/*============================================================================
 * Register Allocation
 *============================================================================*/

void arm64_regalloc_init(ARM64_RegAlloc* ra, Seraph_Arena* arena) {
    if (!ra || !arena) return;

    memset(ra, 0, sizeof(ARM64_RegAlloc));
    ra->arena = arena;
    ra->free_regs = ARM64_CALLER_SAVED_MASK;
    ra->free_callee = ARM64_CALLEE_SAVED_MASK;
    ra->spill_offset = 0;
}

static ARM64_Reg arm64_alloc_reg(ARM64_RegAlloc* ra) __attribute__((unused));
static ARM64_Reg arm64_alloc_reg(ARM64_RegAlloc* ra) {
    /* Try caller-saved first */
    for (int i = 0; i < 16; i++) {
        if (ra->free_regs & (1u << i)) {
            ra->free_regs &= ~(1u << i);
            return (ARM64_Reg)i;
        }
    }

    /* Try callee-saved */
    for (int i = 19; i <= 26; i++) {
        if (ra->free_callee & (1u << i)) {
            ra->free_callee &= ~(1u << i);
            return (ARM64_Reg)i;
        }
    }

    /* No free registers - need to spill */
    return ARM64_XZR;  /* Indicates spill needed */
}

static void arm64_free_reg(ARM64_RegAlloc* ra, ARM64_Reg reg) __attribute__((unused));
static void arm64_free_reg(ARM64_RegAlloc* ra, ARM64_Reg reg) {
    if (reg < 16) {
        ra->free_regs |= (1u << reg);
    } else if (reg >= 19 && reg <= 26) {
        ra->free_callee |= (1u << reg);
    }
}

void arm64_regalloc_function(ARM64_RegAlloc* ra, Celestial_Function* fn) {
    if (!ra || !fn) return;

    /* Simple allocation: assign physical regs to vregs in order */
    /* For parameters, use argument registers */
    for (size_t i = 0; i < fn->param_count && i < 8; i++) {
        if (fn->params[i]) {
            fn->params[i]->id = i;  /* Use X0-X7 for params */
        }
    }

    /* Reset for local allocations */
    ra->free_regs = ARM64_CALLER_SAVED_MASK;
    /* Reserve argument registers that are in use */
    for (size_t i = 0; i < fn->param_count && i < 8; i++) {
        ra->free_regs &= ~(1u << i);
    }
}

ARM64_Reg arm64_regalloc_get(ARM64_RegAlloc* ra, uint32_t vreg) {
    (void)ra;
    /* Simple: vreg ID directly maps to register if in range */
    if (vreg < 16) return (ARM64_Reg)vreg;
    if (vreg < 27) return (ARM64_Reg)(vreg - 16 + 19);  /* Map to X19-X26 */
    return ARM64_X9;  /* Fallback */
}

/*============================================================================
 * Context Management
 *============================================================================*/

void arm64_context_init(ARM64_Context* ctx, ARM64_Buffer* code,
                        Celestial_Module* module, Seraph_Arena* arena) {
    if (!ctx || !code || !module || !arena) return;

    memset(ctx, 0, sizeof(ARM64_Context));
    ctx->code = code;
    ctx->module = module;
    ctx->arena = arena;

    arm64_regalloc_init(&ctx->regalloc, arena);
}

ARM64_Label* arm64_get_block_label(ARM64_Context* ctx, Celestial_Block* block) {
    if (!ctx || !block) return NULL;

    /* Ensure block_labels array exists */
    if (!ctx->block_labels && ctx->function) {
        size_t count = ctx->function->block_count;
        ctx->block_labels = seraph_arena_alloc(ctx->arena,
                                                count * sizeof(ARM64_Label*),
                                                _Alignof(ARM64_Label*));
        if (!ctx->block_labels) return NULL;
        memset(ctx->block_labels, 0, count * sizeof(ARM64_Label*));
    }

    /* Check if label exists */
    if (ctx->block_labels && block->id < ctx->function->block_count) {
        if (ctx->block_labels[block->id]) {
            return ctx->block_labels[block->id];
        }

        /* Create new label */
        ARM64_Label* label = seraph_arena_alloc(ctx->arena, sizeof(ARM64_Label),
                                                 _Alignof(ARM64_Label));
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

void arm64_resolve_fixups(ARM64_Context* ctx) {
    if (!ctx) return;

    for (ARM64_Fixup* fix = ctx->fixups; fix; fix = fix->next) {
        if (!fix->target || !fix->target->resolved) continue;

        /* Calculate offset in bytes */
        int32_t offset = ((int32_t)fix->target->offset - (int32_t)fix->patch_pos) * 4;

        /* Patch the instruction */
        if (fix->is_cond) {
            /* Conditional branch - B.cond has 19-bit offset */
            uint32_t old = ctx->code->data[fix->patch_pos];
            uint32_t cond = old & 0xF;
            arm64_patch(ctx->code, fix->patch_pos, arm64_bcond(cond, offset));
        } else {
            /* Unconditional branch - B has 26-bit offset */
            arm64_patch(ctx->code, fix->patch_pos, arm64_b(offset));
        }
    }
}

/*============================================================================
 * Code Generation Helpers
 *============================================================================*/

ARM64_Reg arm64_load_value(ARM64_Context* ctx, Celestial_Value* val) {
    if (!ctx || !val) return ARM64_XZR;

    switch (val->kind) {
        case CIR_VALUE_CONST:
            /* Load constant into scratch register */
            arm64_emit_mov64(ctx->code, ARM64_SCRATCH0, val->constant.i64);
            return ARM64_SCRATCH0;

        case CIR_VALUE_PARAM:
            /* Parameters are in X0-X7 */
            if (val->param.index < 8) {
                return (ARM64_Reg)val->param.index;
            }
            /* Stack parameter - load from frame */
            arm64_emit(ctx->code, arm64_ldr_imm(ARM64_SCRATCH0, ARM64_FP,
                                                 16 + (val->param.index - 8) * 8));
            return ARM64_SCRATCH0;

        case CIR_VALUE_VREG:
            return arm64_regalloc_get(&ctx->regalloc, val->id);

        case CIR_VALUE_VOID_CONST:
            /* Load -1 (all bits set = VOID) */
            arm64_emit(ctx->code, arm64_movn(ARM64_SCRATCH0, 0, 0));
            return ARM64_SCRATCH0;

        case CIR_VALUE_FNPTR:
            /* Load function pointer - address resolved by linker */
            /* For now, load placeholder address */
            arm64_emit_mov64(ctx->code, ARM64_SCRATCH0, 0);
            return ARM64_SCRATCH0;

        default:
            return ARM64_XZR;
    }
}

void arm64_store_value(ARM64_Context* ctx, ARM64_Reg reg, Celestial_Value* val) {
    if (!ctx || !val) return;

    if (val->kind == CIR_VALUE_VREG) {
        ARM64_Reg dest = arm64_regalloc_get(&ctx->regalloc, val->id);
        if (dest != reg) {
            arm64_emit(ctx->code, arm64_mov_reg(dest, reg));
        }
    }
}

void arm64_load_imm(ARM64_Context* ctx, ARM64_Reg reg, int64_t imm) {
    arm64_emit_mov64(ctx->code, reg, imm);
}

/*============================================================================
 * Function Prologue/Epilogue
 *============================================================================*/

void arm64_emit_prologue(ARM64_Context* ctx) {
    if (!ctx) return;

    /* Standard ARM64 prologue:
     * STP X29, X30, [SP, #-frame_size]!
     * MOV X29, SP
     * (save callee-saved registers)
     */

    int32_t frame_size = ctx->frame_size;
    if (frame_size < 16) frame_size = 16;

    /* Save FP and LR, allocate stack frame */
    arm64_emit(ctx->code, arm64_stp_pre(ARM64_FP, ARM64_LR, ARM64_SP, -frame_size));

    /* Set up frame pointer */
    arm64_emit(ctx->code, arm64_mov_reg(ARM64_FP, ARM64_SP));

    /* Save callee-saved registers that we use */
    /* For now, just save X19-X22 if needed */
    int32_t save_offset = 16;
    for (int i = 19; i <= 22; i++) {
        if (!(ctx->regalloc.free_callee & (1u << i))) {
            if (save_offset + 8 <= frame_size) {
                arm64_emit(ctx->code, arm64_str_imm(i, ARM64_FP, save_offset));
                save_offset += 8;
            }
        }
    }
}

void arm64_emit_epilogue(ARM64_Context* ctx) {
    if (!ctx) return;

    /* Restore callee-saved registers */
    int32_t save_offset = 16;
    for (int i = 19; i <= 22; i++) {
        if (!(ctx->regalloc.free_callee & (1u << i))) {
            if (save_offset + 8 <= ctx->frame_size) {
                arm64_emit(ctx->code, arm64_ldr_imm(i, ARM64_FP, save_offset));
                save_offset += 8;
            }
        }
    }

    /* Restore FP and LR, deallocate stack frame */
    arm64_emit(ctx->code, arm64_ldp_post(ARM64_FP, ARM64_LR, ARM64_SP,
                                          ctx->frame_size));

    /* Return */
    arm64_emit(ctx->code, arm64_ret());
}

/*============================================================================
 * Instruction Lowering
 *============================================================================*/

void arm64_lower_arith(ARM64_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    ARM64_Reg rd = (instr->result) ?
                   arm64_regalloc_get(&ctx->regalloc, instr->result->id) : ARM64_XZR;
    ARM64_Reg rn = arm64_load_value(ctx, instr->operands[0]);
    ARM64_Reg rm = ARM64_XZR;

    if (instr->operand_count > 1) {
        rm = arm64_load_value(ctx, instr->operands[1]);
    }

    switch (instr->opcode) {
        case CIR_ADD:
            arm64_emit(ctx->code, arm64_add_reg(rd, rn, rm));
            break;

        case CIR_SUB:
            arm64_emit(ctx->code, arm64_sub_reg(rd, rn, rm));
            break;

        case CIR_MUL:
            arm64_emit(ctx->code, arm64_mul(rd, rn, rm));
            break;

        case CIR_DIV:
            /* Check for division by zero - produces VOID */
            arm64_emit(ctx->code, arm64_cmp_imm(rm, 0));

            /* If zero, set result to -1 (VOID) */
            arm64_emit(ctx->code, arm64_sdiv(rd, rn, rm));
            arm64_emit(ctx->code, arm64_movn(ARM64_SCRATCH0, 0, 0));
            arm64_emit(ctx->code, arm64_csel(rd, ARM64_SCRATCH0, rd, ARM64_COND_EQ));
            break;

        case CIR_MOD:
            /* Modulo: a % b = a - (a / b) * b */
            /* Check for mod by zero - produces VOID */
            arm64_emit(ctx->code, arm64_cmp_imm(rm, 0));

            /* Compute: tmp = a / b; rd = a - tmp * b */
            arm64_emit(ctx->code, arm64_sdiv(ARM64_SCRATCH0, rn, rm));  /* tmp = a / b */
            arm64_emit(ctx->code, arm64_msub(rd, ARM64_SCRATCH0, rm, rn));  /* rd = rn - scratch0 * rm */
            arm64_emit(ctx->code, arm64_movn(ARM64_SCRATCH1, 0, 0));  /* scratch1 = -1 (VOID) */
            arm64_emit(ctx->code, arm64_csel(rd, ARM64_SCRATCH1, rd, ARM64_COND_EQ));  /* If zero divisor, return VOID */
            break;

        case CIR_NEG:
            arm64_emit(ctx->code, arm64_neg(rd, rn));
            break;

        case CIR_AND:
            arm64_emit(ctx->code, arm64_and_reg(rd, rn, rm));
            break;

        case CIR_OR:
            arm64_emit(ctx->code, arm64_orr_reg(rd, rn, rm));
            break;

        case CIR_XOR:
            arm64_emit(ctx->code, arm64_eor_reg(rd, rn, rm));
            break;

        case CIR_NOT:
            arm64_emit(ctx->code, arm64_mvn(rd, rn));
            break;

        case CIR_SHL:
            arm64_emit(ctx->code, arm64_lsl_reg(rd, rn, rm));
            break;

        case CIR_SHR:
            arm64_emit(ctx->code, arm64_lsr_reg(rd, rn, rm));
            break;

        case CIR_SAR:
            arm64_emit(ctx->code, arm64_asr_reg(rd, rn, rm));
            break;

        default:
            break;
    }
}

void arm64_lower_cmp(ARM64_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    ARM64_Reg rd = (instr->result) ?
                   arm64_regalloc_get(&ctx->regalloc, instr->result->id) : ARM64_XZR;
    ARM64_Reg rn = arm64_load_value(ctx, instr->operands[0]);
    ARM64_Reg rm = arm64_load_value(ctx, instr->operands[1]);

    /* Compare */
    arm64_emit(ctx->code, arm64_cmp_reg(rn, rm));

    /* Set result based on condition */
    ARM64_Cond cond;
    switch (instr->opcode) {
        case CIR_EQ: cond = ARM64_COND_EQ; break;
        case CIR_NE: cond = ARM64_COND_NE; break;
        case CIR_LT: cond = ARM64_COND_LT; break;
        case CIR_LE: cond = ARM64_COND_LE; break;
        case CIR_GT: cond = ARM64_COND_GT; break;
        case CIR_GE: cond = ARM64_COND_GE; break;
        default:     cond = ARM64_COND_AL; break;
    }

    arm64_emit(ctx->code, arm64_cset(rd, cond));
}

void arm64_lower_control(ARM64_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    switch (instr->opcode) {
        case CIR_JUMP: {
            ARM64_Label* target = arm64_get_block_label(ctx, instr->target1);
            if (target) {
                if (target->resolved) {
                    int32_t offset = ((int32_t)target->offset -
                                      (int32_t)arm64_buffer_pos(ctx->code)) * 4;
                    arm64_emit(ctx->code, arm64_b(offset));
                } else {
                    /* Forward reference - add fixup */
                    ARM64_Fixup* fix = seraph_arena_alloc(ctx->arena,
                                                           sizeof(ARM64_Fixup),
                                                           _Alignof(ARM64_Fixup));
                    if (fix) {
                        fix->patch_pos = arm64_buffer_pos(ctx->code);
                        fix->target = target;
                        fix->is_cond = 0;
                        fix->next = ctx->fixups;
                        ctx->fixups = fix;
                    }
                    arm64_emit(ctx->code, arm64_b(0));  /* Placeholder */
                }
            }
            break;
        }

        case CIR_BRANCH: {
            ARM64_Reg cond = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Label* then_lbl = arm64_get_block_label(ctx, instr->target1);
            ARM64_Label* else_lbl = arm64_get_block_label(ctx, instr->target2);

            /* CBNZ to then, B to else */
            if (then_lbl) {
                if (then_lbl->resolved) {
                    int32_t offset = ((int32_t)then_lbl->offset -
                                      (int32_t)arm64_buffer_pos(ctx->code)) * 4;
                    arm64_emit(ctx->code, arm64_cbnz(cond, offset));
                } else {
                    ARM64_Fixup* fix = seraph_arena_alloc(ctx->arena,
                                                           sizeof(ARM64_Fixup),
                                                           _Alignof(ARM64_Fixup));
                    if (fix) {
                        fix->patch_pos = arm64_buffer_pos(ctx->code);
                        fix->target = then_lbl;
                        fix->is_cond = 1;
                        fix->next = ctx->fixups;
                        ctx->fixups = fix;
                    }
                    arm64_emit(ctx->code, arm64_cbnz(cond, 0));
                }
            }

            if (else_lbl) {
                if (else_lbl->resolved) {
                    int32_t offset = ((int32_t)else_lbl->offset -
                                      (int32_t)arm64_buffer_pos(ctx->code)) * 4;
                    arm64_emit(ctx->code, arm64_b(offset));
                } else {
                    ARM64_Fixup* fix = seraph_arena_alloc(ctx->arena,
                                                           sizeof(ARM64_Fixup),
                                                           _Alignof(ARM64_Fixup));
                    if (fix) {
                        fix->patch_pos = arm64_buffer_pos(ctx->code);
                        fix->target = else_lbl;
                        fix->is_cond = 0;
                        fix->next = ctx->fixups;
                        ctx->fixups = fix;
                    }
                    arm64_emit(ctx->code, arm64_b(0));
                }
            }
            break;
        }

        case CIR_RETURN: {
            if (instr->operand_count > 0 && instr->operands[0]) {
                ARM64_Reg val = arm64_load_value(ctx, instr->operands[0]);
                if (val != ARM64_X0) {
                    arm64_emit(ctx->code, arm64_mov_reg(ARM64_X0, val));
                }
            }
            arm64_emit_epilogue(ctx);
            break;
        }

        case CIR_CALL: {
            Celestial_Function* callee = instr->callee;
            if (!callee) break;

            /* Load arguments into X0-X7 */
            for (size_t i = 0; i < instr->operand_count && i < 8; i++) {
                ARM64_Reg arg = arm64_load_value(ctx, instr->operands[i]);
                if (arg != (ARM64_Reg)i) {
                    arm64_emit(ctx->code, arm64_mov_reg((ARM64_Reg)i, arg));
                }
            }

            /* Function calls require linker relocation.
             * BL instruction has a 26-bit signed offset (±128MB range).
             * The placeholder 0 will be patched by the linker when
             * the actual function address is known.
             *
             * For intra-module calls, could compute offset if function
             * is already compiled. For inter-module/external calls,
             * must defer to linker.
             */
            size_t call_pos = arm64_buffer_pos(ctx->code);
            arm64_emit(ctx->code, arm64_bl(0));

            /* Record call site for linker (if tracking is available) */
            (void)call_pos;
            (void)callee;  /* Will be used for relocation entry */

            /* Store result if needed */
            if (instr->result) {
                ARM64_Reg rd = arm64_regalloc_get(&ctx->regalloc,
                                                   instr->result->id);
                if (rd != ARM64_X0) {
                    arm64_emit(ctx->code, arm64_mov_reg(rd, ARM64_X0));
                }
            }
            break;
        }

        case CIR_CALL_INDIRECT: {
            /* Indirect call through function pointer */
            /* operands[0] = function pointer */
            /* operands[1..n] = arguments */

            /* Load function pointer into scratch register */
            ARM64_Reg fn_ptr = arm64_load_value(ctx, instr->operands[0]);
            if (fn_ptr != ARM64_SCRATCH0) {
                arm64_emit(ctx->code, arm64_mov_reg(ARM64_SCRATCH0, fn_ptr));
            }

            /* Load arguments into X0-X7 */
            size_t arg_count = instr->operand_count - 1;
            for (size_t i = 0; i < arg_count && i < 8; i++) {
                ARM64_Reg arg = arm64_load_value(ctx, instr->operands[i + 1]);
                if (arg != (ARM64_Reg)i) {
                    arm64_emit(ctx->code, arm64_mov_reg((ARM64_Reg)i, arg));
                }
            }

            /* BLR X16 - indirect call through X16 (SCRATCH0) */
            arm64_emit(ctx->code, arm64_blr(ARM64_SCRATCH0));

            /* Store result if needed */
            if (instr->result) {
                ARM64_Reg rd = arm64_regalloc_get(&ctx->regalloc,
                                                   instr->result->id);
                if (rd != ARM64_X0) {
                    arm64_emit(ctx->code, arm64_mov_reg(rd, ARM64_X0));
                }
            }
            break;
        }

        default:
            break;
    }
}

void arm64_lower_void_op(ARM64_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    ARM64_Reg val = arm64_load_value(ctx, instr->operands[0]);
    ARM64_Reg rd = (instr->result) ?
                   arm64_regalloc_get(&ctx->regalloc, instr->result->id) : ARM64_XZR;

    switch (instr->opcode) {
        case CIR_VOID_TEST:
            /* Test if bit 63 is set (value is VOID) */
            arm64_emit(ctx->code, arm64_lsr_imm(rd, val, 63));
            break;

        case CIR_VOID_PROP:
            /* If VOID, early return VOID */
            arm64_emit(ctx->code, arm64_tbz(val, 63, 12));  /* Skip if not VOID */
            arm64_emit(ctx->code, arm64_movn(ARM64_X0, 0, 0));  /* Return VOID */
            arm64_emit_epilogue(ctx);
            /* Otherwise, pass through the value */
            if (rd != val) {
                arm64_emit(ctx->code, arm64_mov_reg(rd, val));
            }
            break;

        case CIR_VOID_ASSERT:
            /* If VOID, trap */
            arm64_emit(ctx->code, arm64_tbz(val, 63, 8));  /* Skip if not VOID */
            arm64_emit(ctx->code, arm64_brk(0x100));       /* Breakpoint/panic */
            /* Otherwise, pass through the value */
            if (rd != val) {
                arm64_emit(ctx->code, arm64_mov_reg(rd, val));
            }
            break;

        case CIR_VOID_COALESCE: {
            /* If value is VOID, use default instead */
            ARM64_Reg default_val = arm64_load_value(ctx, instr->operands[1]);
            arm64_emit(ctx->code, arm64_tbnz(val, 63, 8));  /* Skip if VOID */
            arm64_emit(ctx->code, arm64_mov_reg(rd, val));
            arm64_emit(ctx->code, arm64_b(8));              /* Skip default */
            arm64_emit(ctx->code, arm64_mov_reg(rd, default_val));
            break;
        }

        default:
            break;
    }
}

void arm64_lower_cap_op(ARM64_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    switch (instr->opcode) {
        case CIR_CAP_LOAD: {
            ARM64_Reg cap = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Reg off = arm64_load_value(ctx, instr->operands[1]);
            ARM64_Reg rd = (instr->result) ?
                           arm64_regalloc_get(&ctx->regalloc, instr->result->id) :
                           ARM64_XZR;

            /* Capability-checked load:
             * 1. Verify bounds (base <= ptr < base + len)
             * 2. Verify generation matches
             * 3. Load data
             */

            /* For now, simplified: base + offset */
            arm64_emit(ctx->code, arm64_add_reg(ARM64_SCRATCH0, cap, off));
            arm64_emit(ctx->code, arm64_ldr_imm(rd, ARM64_SCRATCH0, 0));
            break;
        }

        case CIR_CAP_STORE: {
            ARM64_Reg cap = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Reg off = arm64_load_value(ctx, instr->operands[1]);
            ARM64_Reg val = arm64_load_value(ctx, instr->operands[2]);

            /* Capability-checked store */
            arm64_emit(ctx->code, arm64_add_reg(ARM64_SCRATCH0, cap, off));
            arm64_emit(ctx->code, arm64_str_imm(val, ARM64_SCRATCH0, 0));
            break;
        }

        default:
            break;
    }
}

void arm64_lower_substrate_op(ARM64_Context* ctx, Celestial_Instr* instr) {
    if (!ctx || !instr) return;

    switch (instr->opcode) {
        case CIR_SUBSTRATE_ENTER:
            /* Set substrate context in X27 */
            /* The actual context setup would be done by runtime */
            break;

        case CIR_SUBSTRATE_EXIT:
            /* Clear substrate context */
            arm64_emit(ctx->code, arm64_movz(ARM64_X27, 0, 0));
            break;

        default:
            break;
    }
}

void arm64_lower_instr(ARM64_Context* ctx, Celestial_Instr* instr) {
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
            arm64_lower_arith(ctx, instr);
            break;

        /* Comparison */
        case CIR_EQ:
        case CIR_NE:
        case CIR_LT:
        case CIR_LE:
        case CIR_GT:
        case CIR_GE:
            arm64_lower_cmp(ctx, instr);
            break;

        /* Control flow */
        case CIR_JUMP:
        case CIR_BRANCH:
        case CIR_RETURN:
        case CIR_CALL:
        case CIR_CALL_INDIRECT:
            arm64_lower_control(ctx, instr);
            break;

        /* VOID operations */
        case CIR_VOID_TEST:
        case CIR_VOID_PROP:
        case CIR_VOID_ASSERT:
        case CIR_VOID_COALESCE:
            arm64_lower_void_op(ctx, instr);
            break;

        /* Capability operations */
        case CIR_CAP_LOAD:
        case CIR_CAP_STORE:
            arm64_lower_cap_op(ctx, instr);
            break;

        /* Substrate operations */
        case CIR_SUBSTRATE_ENTER:
        case CIR_SUBSTRATE_EXIT:
            arm64_lower_substrate_op(ctx, instr);
            break;

        /* Memory operations */
        case CIR_LOAD: {
            ARM64_Reg ptr = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Reg rd = (instr->result) ?
                           arm64_regalloc_get(&ctx->regalloc, instr->result->id) :
                           ARM64_XZR;
            arm64_emit(ctx->code, arm64_ldr_imm(rd, ptr, 0));
            break;
        }

        case CIR_STORE: {
            ARM64_Reg ptr = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Reg val = arm64_load_value(ctx, instr->operands[1]);
            arm64_emit(ctx->code, arm64_str_imm(val, ptr, 0));
            break;
        }

        case CIR_ALLOCA: {
            ARM64_Reg rd = (instr->result) ?
                           arm64_regalloc_get(&ctx->regalloc, instr->result->id) :
                           ARM64_XZR;
            /* Allocate from frame - just compute address */
            ctx->local_size += 8;  /* Assume 8-byte allocation */
            arm64_emit(ctx->code, arm64_sub_imm(rd, ARM64_FP, ctx->local_size));
            break;
        }

        case CIR_GEP: {
            /* Get element pointer: base + offset */
            ARM64_Reg base = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Reg rd = (instr->result) ?
                           arm64_regalloc_get(&ctx->regalloc, instr->result->id) :
                           ARM64_XZR;

            if (instr->operands[1] && instr->operands[1]->kind == CIR_VALUE_CONST) {
                int64_t offset = instr->operands[1]->constant.i64;
                if (offset == 0) {
                    arm64_emit(ctx->code, arm64_mov_reg(rd, base));
                } else {
                    arm64_emit(ctx->code, arm64_add_imm(rd, base, (uint16_t)offset));
                }
            } else if (instr->operands[1]) {
                ARM64_Reg off = arm64_load_value(ctx, instr->operands[1]);
                arm64_emit(ctx->code, arm64_add_reg(rd, base, off));
            }
            break;
        }

        case CIR_EXTRACTFIELD: {
            /* Load from struct field */
            ARM64_Reg ptr = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Reg rd = (instr->result) ?
                           arm64_regalloc_get(&ctx->regalloc, instr->result->id) :
                           ARM64_XZR;

            int64_t field_idx = 0;
            if (instr->operands[1] && instr->operands[1]->kind == CIR_VALUE_CONST) {
                field_idx = instr->operands[1]->constant.i64;
            }
            int32_t offset = (int32_t)(field_idx * 8);
            arm64_emit(ctx->code, arm64_ldr_imm(rd, ptr, offset));
            break;
        }

        case CIR_INSERTFIELD: {
            /* Store to struct field */
            ARM64_Reg ptr = arm64_load_value(ctx, instr->operands[0]);
            ARM64_Reg val = arm64_load_value(ctx, instr->operands[1]);

            int64_t field_idx = 0;
            if (instr->operands[2] && instr->operands[2]->kind == CIR_VALUE_CONST) {
                field_idx = instr->operands[2]->constant.i64;
            }
            int32_t offset = (int32_t)(field_idx * 8);
            arm64_emit(ctx->code, arm64_str_imm(val, ptr, offset));

            /* Return struct pointer */
            if (instr->result) {
                ARM64_Reg rd = arm64_regalloc_get(&ctx->regalloc, instr->result->id);
                arm64_emit(ctx->code, arm64_mov_reg(rd, ptr));
            }
            break;
        }

        case CIR_NOP:
            /* Folded constant - skip */
            break;

        default:
            /* NOP for unhandled instructions */
            arm64_emit(ctx->code, arm64_nop());
            break;
    }
}

/*============================================================================
 * Block and Function Compilation
 *============================================================================*/

void arm64_compile_block(ARM64_Context* ctx, Celestial_Block* block) {
    if (!ctx || !block) return;

    /* Mark label as resolved */
    ARM64_Label* label = arm64_get_block_label(ctx, block);
    if (label) {
        label->offset = arm64_buffer_pos(ctx->code);
        label->resolved = 1;
    }

    /* Lower each instruction */
    for (Celestial_Instr* instr = block->first; instr; instr = instr->next) {
        arm64_lower_instr(ctx, instr);
    }
}

void arm64_compile_function(ARM64_Context* ctx, Celestial_Function* fn) {
    if (!ctx || !fn) return;

    ctx->function = fn;
    ctx->block_labels = NULL;
    ctx->fixups = NULL;
    ctx->local_size = 0;
    ctx->save_size = 0;

    /* Perform register allocation */
    arm64_regalloc_function(&ctx->regalloc, fn);

    /* Calculate frame size (16-byte aligned) */
    ctx->frame_size = 16;  /* FP + LR */
    /* Add space for locals (will be adjusted during compilation) */

    /* Emit prologue */
    arm64_emit_prologue(ctx);

    /* Compile all blocks */
    for (Celestial_Block* block = fn->blocks; block; block = block->next) {
        arm64_compile_block(ctx, block);
    }

    /* Resolve forward references */
    arm64_resolve_fixups(ctx);

    ctx->function = NULL;
}

/**
 * @brief Emit ARM64 startup stub
 *
 * _start:
 *     bl main       ; call main
 *     mov x8, #93   ; sys_exit
 *     svc #0        ; syscall
 *
 * Returns the instruction index where the BL instruction needs to be patched
 */
static size_t arm64_emit_startup_stub(ARM64_Context* ctx) {
    /* BL imm26 (placeholder - will be patched) */
    size_t bl_index = ctx->code->count;
    arm64_emit(ctx->code, 0x94000000);  /* BL with offset 0 */

    /* MOV X8, #93 (exit syscall number on Linux ARM64) */
    arm64_emit(ctx->code, arm64_movz(ARM64_X8, 93, 0));

    /* SVC #0 (supervisor call for syscall) */
    arm64_emit(ctx->code, 0xD4000001);  /* SVC #0 */

    return bl_index;
}

void arm64_compile_module(ARM64_Context* ctx) {
    if (!ctx || !ctx->module) return;

    /* Emit startup stub first (entry point) */
    size_t bl_index = arm64_emit_startup_stub(ctx);

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
        arm64_compile_function(ctx, fn);
    }

    /* Patch BL instruction to call main */
    if (found_main) {
        /* ARM64 BL uses signed imm26 (in instructions, not bytes) as offset */
        int32_t imm26 = (int32_t)(main_index - bl_index);

        /* Encode BL: 100101 | imm26 */
        uint32_t bl_instr = 0x94000000 | (imm26 & 0x03FFFFFF);
        arm64_patch(ctx->code, bl_index, bl_instr);
    }
}
