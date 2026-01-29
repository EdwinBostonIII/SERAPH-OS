/**
 * @file arm64_encode.h
 * @brief ARM64 (AArch64) Instruction Encoder
 *
 * This module encodes ARM64 instructions for the SERAPH native compiler.
 * ARM64 is a RISC architecture with fixed 32-bit instruction widths.
 *
 * Key features:
 * - 31 general-purpose registers (X0-X30)
 * - Separate SP (stack pointer) and LR (link register = X30)
 * - Fixed-width 32-bit instructions
 * - Flexible addressing modes
 */

#ifndef SERAPH_SERAPHIM_ARM64_ENCODE_H
#define SERAPH_SERAPHIM_ARM64_ENCODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Register Definitions
 *============================================================================*/

/**
 * @brief ARM64 general-purpose registers
 */
typedef enum {
    /* General purpose */
    ARM64_X0 = 0, ARM64_X1, ARM64_X2, ARM64_X3,
    ARM64_X4, ARM64_X5, ARM64_X6, ARM64_X7,
    ARM64_X8, ARM64_X9, ARM64_X10, ARM64_X11,
    ARM64_X12, ARM64_X13, ARM64_X14, ARM64_X15,
    ARM64_X16, ARM64_X17, ARM64_X18, ARM64_X19,
    ARM64_X20, ARM64_X21, ARM64_X22, ARM64_X23,
    ARM64_X24, ARM64_X25, ARM64_X26, ARM64_X27,
    ARM64_X28, ARM64_X29, ARM64_X30,

    /* Special */
    ARM64_SP = 31,    /**< Stack pointer */
    ARM64_XZR = 31,   /**< Zero register (same encoding as SP) */

    /* 32-bit aliases */
    ARM64_W0 = 0, ARM64_W1, ARM64_W2, ARM64_W3,
    ARM64_W4, ARM64_W5, ARM64_W6, ARM64_W7,

    /* Aliases for ABI */
    ARM64_FP = 29,    /**< Frame pointer (X29) */
    ARM64_LR = 30,    /**< Link register (X30) */
} ARM64_Reg;

/**
 * @brief Condition codes for ARM64
 */
typedef enum {
    ARM64_COND_EQ = 0,   /**< Equal */
    ARM64_COND_NE = 1,   /**< Not equal */
    ARM64_COND_CS = 2,   /**< Carry set / unsigned >= */
    ARM64_COND_CC = 3,   /**< Carry clear / unsigned < */
    ARM64_COND_MI = 4,   /**< Minus / negative */
    ARM64_COND_PL = 5,   /**< Plus / positive or zero */
    ARM64_COND_VS = 6,   /**< Overflow */
    ARM64_COND_VC = 7,   /**< No overflow */
    ARM64_COND_HI = 8,   /**< Unsigned > */
    ARM64_COND_LS = 9,   /**< Unsigned <= */
    ARM64_COND_GE = 10,  /**< Signed >= */
    ARM64_COND_LT = 11,  /**< Signed < */
    ARM64_COND_GT = 12,  /**< Signed > */
    ARM64_COND_LE = 13,  /**< Signed <= */
    ARM64_COND_AL = 14,  /**< Always */
    ARM64_COND_NV = 15,  /**< Never */
} ARM64_Cond;

/**
 * @brief Shift types
 */
typedef enum {
    ARM64_SHIFT_LSL = 0, /**< Logical shift left */
    ARM64_SHIFT_LSR = 1, /**< Logical shift right */
    ARM64_SHIFT_ASR = 2, /**< Arithmetic shift right */
    ARM64_SHIFT_ROR = 3, /**< Rotate right */
} ARM64_Shift;

/*============================================================================
 * SERAPH ABI on ARM64
 *============================================================================*/

#define ARM64_SUBSTRATE_REG  ARM64_X27  /**< Substrate context */
#define ARM64_CAPS_REG       ARM64_X28  /**< Capability context */

/* Argument registers */
#define ARM64_ARG0  ARM64_X0
#define ARM64_ARG1  ARM64_X1
#define ARM64_ARG2  ARM64_X2
#define ARM64_ARG3  ARM64_X3
#define ARM64_ARG4  ARM64_X4
#define ARM64_ARG5  ARM64_X5
#define ARM64_ARG6  ARM64_X6
#define ARM64_ARG7  ARM64_X7

/* Return value register */
#define ARM64_RET   ARM64_X0

/*============================================================================
 * Instruction Buffer
 *============================================================================*/

/**
 * @brief Buffer for ARM64 instructions
 */
typedef struct {
    uint32_t* data;      /**< Instruction buffer (32-bit aligned) */
    size_t    capacity;  /**< Total capacity in instructions */
    size_t    count;     /**< Current instruction count */
} ARM64_Buffer;

/**
 * @brief Initialize instruction buffer
 */
void arm64_buffer_init(ARM64_Buffer* buf, void* memory, size_t capacity_bytes);

/**
 * @brief Emit a 32-bit instruction
 */
void arm64_emit(ARM64_Buffer* buf, uint32_t instr);

/**
 * @brief Get current position (in instructions)
 */
size_t arm64_buffer_pos(ARM64_Buffer* buf);

/**
 * @brief Patch instruction at position
 */
void arm64_patch(ARM64_Buffer* buf, size_t pos, uint32_t instr);

/*============================================================================
 * Data Processing (Immediate)
 *============================================================================*/

/**
 * @brief ADD Rd, Rn, #imm12 (64-bit)
 */
uint32_t arm64_add_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12);

/**
 * @brief ADD Wd, Wn, #imm12 (32-bit)
 */
uint32_t arm64_addw_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12);

/**
 * @brief SUB Rd, Rn, #imm12 (64-bit)
 */
uint32_t arm64_sub_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12);

/**
 * @brief SUB Wd, Wn, #imm12 (32-bit)
 */
uint32_t arm64_subw_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12);

/**
 * @brief CMP Rn, #imm12 (SUBS with XZR destination)
 */
uint32_t arm64_cmp_imm(ARM64_Reg rn, uint16_t imm12);

/**
 * @brief CMN Rn, #imm12 (ADDS with XZR destination)
 */
uint32_t arm64_cmn_imm(ARM64_Reg rn, uint16_t imm12);

/*============================================================================
 * Data Processing (Register)
 *============================================================================*/

/**
 * @brief ADD Rd, Rn, Rm (64-bit)
 */
uint32_t arm64_add_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief SUB Rd, Rn, Rm (64-bit)
 */
uint32_t arm64_sub_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief MUL Rd, Rn, Rm (64-bit)
 */
uint32_t arm64_mul(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief SDIV Rd, Rn, Rm (64-bit signed division)
 */
uint32_t arm64_sdiv(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief UDIV Rd, Rn, Rm (64-bit unsigned division)
 */
uint32_t arm64_udiv(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief MSUB Rd, Rn, Rm, Ra (Rd = Ra - Rn * Rm)
 */
uint32_t arm64_msub(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm, ARM64_Reg ra);

/**
 * @brief NEG Rd, Rm (SUB Rd, XZR, Rm)
 */
uint32_t arm64_neg(ARM64_Reg rd, ARM64_Reg rm);

/**
 * @brief CMP Rn, Rm (SUBS with XZR destination)
 */
uint32_t arm64_cmp_reg(ARM64_Reg rn, ARM64_Reg rm);

/*============================================================================
 * Logical (Register)
 *============================================================================*/

/**
 * @brief AND Rd, Rn, Rm
 */
uint32_t arm64_and_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief ORR Rd, Rn, Rm
 */
uint32_t arm64_orr_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief EOR Rd, Rn, Rm (XOR)
 */
uint32_t arm64_eor_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief MVN Rd, Rm (bitwise NOT)
 */
uint32_t arm64_mvn(ARM64_Reg rd, ARM64_Reg rm);

/*============================================================================
 * Shift/Rotate
 *============================================================================*/

/**
 * @brief LSL Rd, Rn, Rm
 */
uint32_t arm64_lsl_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief LSR Rd, Rn, Rm
 */
uint32_t arm64_lsr_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief ASR Rd, Rn, Rm
 */
uint32_t arm64_asr_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm);

/**
 * @brief LSL Rd, Rn, #shift
 */
uint32_t arm64_lsl_imm(ARM64_Reg rd, ARM64_Reg rn, uint8_t shift);

/**
 * @brief LSR Rd, Rn, #shift
 */
uint32_t arm64_lsr_imm(ARM64_Reg rd, ARM64_Reg rn, uint8_t shift);

/**
 * @brief ASR Rd, Rn, #shift
 */
uint32_t arm64_asr_imm(ARM64_Reg rd, ARM64_Reg rn, uint8_t shift);

/*============================================================================
 * Move
 *============================================================================*/

/**
 * @brief MOV Rd, Rm (via ORR with XZR)
 */
uint32_t arm64_mov_reg(ARM64_Reg rd, ARM64_Reg rm);

/**
 * @brief MOV Rd, #imm16 (MOVZ - move wide with zero)
 */
uint32_t arm64_movz(ARM64_Reg rd, uint16_t imm16, uint8_t shift);

/**
 * @brief MOVK Rd, #imm16, LSL #shift (move wide with keep)
 */
uint32_t arm64_movk(ARM64_Reg rd, uint16_t imm16, uint8_t shift);

/**
 * @brief MOVN Rd, #imm16, LSL #shift (move wide negated)
 */
uint32_t arm64_movn(ARM64_Reg rd, uint16_t imm16, uint8_t shift);

/**
 * @brief Load a 64-bit immediate into register
 */
void arm64_emit_mov64(ARM64_Buffer* buf, ARM64_Reg rd, int64_t imm);

/*============================================================================
 * Conditional Select
 *============================================================================*/

/**
 * @brief CSEL Rd, Rn, Rm, cond (conditional select)
 */
uint32_t arm64_csel(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm, ARM64_Cond cond);

/**
 * @brief CSET Rd, cond (conditional set)
 */
uint32_t arm64_cset(ARM64_Reg rd, ARM64_Cond cond);

/**
 * @brief CSETM Rd, cond (conditional set mask)
 */
uint32_t arm64_csetm(ARM64_Reg rd, ARM64_Cond cond);

/*============================================================================
 * Load/Store
 *============================================================================*/

/**
 * @brief LDR Rt, [Rn, #imm12] (load 64-bit)
 */
uint32_t arm64_ldr_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset);

/**
 * @brief LDRW Wt, [Rn, #imm12] (load 32-bit)
 */
uint32_t arm64_ldrw_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset);

/**
 * @brief LDRB Wt, [Rn, #imm12] (load byte)
 */
uint32_t arm64_ldrb_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset);

/**
 * @brief STR Rt, [Rn, #imm12] (store 64-bit)
 */
uint32_t arm64_str_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset);

/**
 * @brief STRW Wt, [Rn, #imm12] (store 32-bit)
 */
uint32_t arm64_strw_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset);

/**
 * @brief STRB Wt, [Rn, #imm12] (store byte)
 */
uint32_t arm64_strb_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset);

/**
 * @brief STP Rt1, Rt2, [Rn, #imm7]! (store pair pre-index)
 */
uint32_t arm64_stp_pre(ARM64_Reg rt1, ARM64_Reg rt2, ARM64_Reg rn, int16_t offset);

/**
 * @brief LDP Rt1, Rt2, [Rn], #imm7 (load pair post-index)
 */
uint32_t arm64_ldp_post(ARM64_Reg rt1, ARM64_Reg rt2, ARM64_Reg rn, int16_t offset);

/*============================================================================
 * Branch
 *============================================================================*/

/**
 * @brief B #offset (unconditional branch)
 */
uint32_t arm64_b(int32_t offset);

/**
 * @brief BL #offset (branch and link)
 */
uint32_t arm64_bl(int32_t offset);

/**
 * @brief B.cond #offset (conditional branch)
 */
uint32_t arm64_bcond(ARM64_Cond cond, int32_t offset);

/**
 * @brief BR Rn (branch to register)
 */
uint32_t arm64_br(ARM64_Reg rn);

/**
 * @brief BLR Rn (branch and link to register)
 */
uint32_t arm64_blr(ARM64_Reg rn);

/**
 * @brief RET (return - BR X30)
 */
uint32_t arm64_ret(void);

/**
 * @brief CBZ Rt, #offset (compare and branch if zero)
 */
uint32_t arm64_cbz(ARM64_Reg rt, int32_t offset);

/**
 * @brief CBNZ Rt, #offset (compare and branch if not zero)
 */
uint32_t arm64_cbnz(ARM64_Reg rt, int32_t offset);

/**
 * @brief TBZ Rt, #bit, #offset (test bit and branch if zero)
 */
uint32_t arm64_tbz(ARM64_Reg rt, uint8_t bit, int32_t offset);

/**
 * @brief TBNZ Rt, #bit, #offset (test bit and branch if not zero)
 */
uint32_t arm64_tbnz(ARM64_Reg rt, uint8_t bit, int32_t offset);

/*============================================================================
 * System
 *============================================================================*/

/**
 * @brief NOP
 */
uint32_t arm64_nop(void);

/**
 * @brief SVC #imm16 (supervisor call)
 */
uint32_t arm64_svc(uint16_t imm);

/**
 * @brief BRK #imm16 (breakpoint)
 */
uint32_t arm64_brk(uint16_t imm);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_ARM64_ENCODE_H */
