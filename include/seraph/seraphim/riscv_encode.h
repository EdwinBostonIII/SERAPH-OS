/**
 * @file riscv_encode.h
 * @brief RISC-V (RV64) Instruction Encoder
 *
 * This module encodes RISC-V instructions for the SERAPH native compiler.
 * Targets RV64IMAC (Integer + Multiply + Atomics + Compressed).
 *
 * RISC-V has a simple, modular ISA with fixed 32-bit base instructions
 * and optional 16-bit compressed instructions.
 */

#ifndef SERAPH_SERAPHIM_RISCV_ENCODE_H
#define SERAPH_SERAPHIM_RISCV_ENCODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Register Definitions
 *============================================================================*/

/**
 * @brief RISC-V general-purpose registers
 */
typedef enum {
    /* Integer registers */
    RV_X0 = 0,   /**< Zero register (always 0) */
    RV_X1,       /**< Return address (ra) */
    RV_X2,       /**< Stack pointer (sp) */
    RV_X3,       /**< Global pointer (gp) */
    RV_X4,       /**< Thread pointer (tp) */
    RV_X5,       /**< Temporary (t0) */
    RV_X6,       /**< Temporary (t1) */
    RV_X7,       /**< Temporary (t2) */
    RV_X8,       /**< Saved/Frame pointer (s0/fp) */
    RV_X9,       /**< Saved (s1) */
    RV_X10,      /**< Arg/Return (a0) */
    RV_X11,      /**< Arg/Return (a1) */
    RV_X12,      /**< Argument (a2) */
    RV_X13,      /**< Argument (a3) */
    RV_X14,      /**< Argument (a4) */
    RV_X15,      /**< Argument (a5) */
    RV_X16,      /**< Argument (a6) */
    RV_X17,      /**< Argument (a7) */
    RV_X18,      /**< Saved (s2) */
    RV_X19,      /**< Saved (s3) */
    RV_X20,      /**< Saved (s4) */
    RV_X21,      /**< Saved (s5) */
    RV_X22,      /**< Saved (s6) */
    RV_X23,      /**< Saved (s7) */
    RV_X24,      /**< Saved (s8) */
    RV_X25,      /**< Saved (s9) */
    RV_X26,      /**< Saved (s10) */
    RV_X27,      /**< Saved (s11) */
    RV_X28,      /**< Temporary (t3) */
    RV_X29,      /**< Temporary (t4) */
    RV_X30,      /**< Temporary (t5) */
    RV_X31,      /**< Temporary (t6) */

    /* ABI names */
    RV_ZERO = RV_X0,
    RV_RA = RV_X1,
    RV_SP = RV_X2,
    RV_GP = RV_X3,
    RV_TP = RV_X4,
    RV_T0 = RV_X5,
    RV_T1 = RV_X6,
    RV_T2 = RV_X7,
    RV_FP = RV_X8,
    RV_S0 = RV_X8,
    RV_S1 = RV_X9,
    RV_A0 = RV_X10,
    RV_A1 = RV_X11,
    RV_A2 = RV_X12,
    RV_A3 = RV_X13,
    RV_A4 = RV_X14,
    RV_A5 = RV_X15,
    RV_A6 = RV_X16,
    RV_A7 = RV_X17,
    RV_S2 = RV_X18,
    RV_S3 = RV_X19,
    RV_S4 = RV_X20,
    RV_S5 = RV_X21,
    RV_S6 = RV_X22,
    RV_S7 = RV_X23,
    RV_S8 = RV_X24,
    RV_S9 = RV_X25,
    RV_S10 = RV_X26,
    RV_S11 = RV_X27,
    RV_T3 = RV_X28,
    RV_T4 = RV_X29,
    RV_T5 = RV_X30,
    RV_T6 = RV_X31,
} RV_Reg;

/*============================================================================
 * SERAPH ABI on RISC-V
 *============================================================================*/

#define RV_SUBSTRATE_REG  RV_S10   /**< Substrate context */
#define RV_CAPS_REG       RV_S11   /**< Capability context */

/*============================================================================
 * Instruction Buffer
 *============================================================================*/

/**
 * @brief Buffer for RISC-V instructions
 */
typedef struct {
    uint32_t* data;      /**< Instruction buffer */
    size_t    capacity;  /**< Total capacity in instructions */
    size_t    count;     /**< Current instruction count */
} RV_Buffer;

/**
 * @brief Initialize instruction buffer
 */
void rv_buffer_init(RV_Buffer* buf, void* memory, size_t capacity_bytes);

/**
 * @brief Emit a 32-bit instruction
 */
void rv_emit(RV_Buffer* buf, uint32_t instr);

/**
 * @brief Get current position (in instructions)
 */
size_t rv_buffer_pos(RV_Buffer* buf);

/**
 * @brief Patch instruction at position
 */
void rv_patch(RV_Buffer* buf, size_t pos, uint32_t instr);

/*============================================================================
 * R-Type Instructions (register-register)
 * Format: funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
 *============================================================================*/

/**
 * @brief ADD rd, rs1, rs2
 */
uint32_t rv_add(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief SUB rd, rs1, rs2
 */
uint32_t rv_sub(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief AND rd, rs1, rs2
 */
uint32_t rv_and(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief OR rd, rs1, rs2
 */
uint32_t rv_or(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief XOR rd, rs1, rs2
 */
uint32_t rv_xor(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief SLL rd, rs1, rs2 (shift left logical)
 */
uint32_t rv_sll(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief SRL rd, rs1, rs2 (shift right logical)
 */
uint32_t rv_srl(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief SRA rd, rs1, rs2 (shift right arithmetic)
 */
uint32_t rv_sra(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief SLT rd, rs1, rs2 (set if less than, signed)
 */
uint32_t rv_slt(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief SLTU rd, rs1, rs2 (set if less than, unsigned)
 */
uint32_t rv_sltu(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/*============================================================================
 * RV64 Word Instructions
 *============================================================================*/

/**
 * @brief ADDW rd, rs1, rs2 (32-bit add)
 */
uint32_t rv_addw(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief SUBW rd, rs1, rs2 (32-bit sub)
 */
uint32_t rv_subw(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/*============================================================================
 * M Extension (Multiply/Divide)
 *============================================================================*/

/**
 * @brief MUL rd, rs1, rs2
 */
uint32_t rv_mul(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief MULH rd, rs1, rs2 (high bits of signed multiply)
 */
uint32_t rv_mulh(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief DIV rd, rs1, rs2 (signed division)
 */
uint32_t rv_div(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief DIVU rd, rs1, rs2 (unsigned division)
 */
uint32_t rv_divu(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief REM rd, rs1, rs2 (signed remainder)
 */
uint32_t rv_rem(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/**
 * @brief REMU rd, rs1, rs2 (unsigned remainder)
 */
uint32_t rv_remu(RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/*============================================================================
 * I-Type Instructions (immediate)
 * Format: imm[31:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
 *============================================================================*/

/**
 * @brief ADDI rd, rs1, imm
 */
uint32_t rv_addi(RV_Reg rd, RV_Reg rs1, int16_t imm);

/**
 * @brief ANDI rd, rs1, imm
 */
uint32_t rv_andi(RV_Reg rd, RV_Reg rs1, int16_t imm);

/**
 * @brief ORI rd, rs1, imm
 */
uint32_t rv_ori(RV_Reg rd, RV_Reg rs1, int16_t imm);

/**
 * @brief XORI rd, rs1, imm
 */
uint32_t rv_xori(RV_Reg rd, RV_Reg rs1, int16_t imm);

/**
 * @brief SLTI rd, rs1, imm (set if less than immediate, signed)
 */
uint32_t rv_slti(RV_Reg rd, RV_Reg rs1, int16_t imm);

/**
 * @brief SLTIU rd, rs1, imm (set if less than immediate, unsigned)
 */
uint32_t rv_sltiu(RV_Reg rd, RV_Reg rs1, int16_t imm);

/**
 * @brief SLLI rd, rs1, shamt (shift left logical immediate)
 */
uint32_t rv_slli(RV_Reg rd, RV_Reg rs1, uint8_t shamt);

/**
 * @brief SRLI rd, rs1, shamt (shift right logical immediate)
 */
uint32_t rv_srli(RV_Reg rd, RV_Reg rs1, uint8_t shamt);

/**
 * @brief SRAI rd, rs1, shamt (shift right arithmetic immediate)
 */
uint32_t rv_srai(RV_Reg rd, RV_Reg rs1, uint8_t shamt);

/*============================================================================
 * Load Instructions
 *============================================================================*/

/**
 * @brief LD rd, offset(rs1) (load 64-bit)
 */
uint32_t rv_ld(RV_Reg rd, RV_Reg rs1, int16_t offset);

/**
 * @brief LW rd, offset(rs1) (load 32-bit signed)
 */
uint32_t rv_lw(RV_Reg rd, RV_Reg rs1, int16_t offset);

/**
 * @brief LWU rd, offset(rs1) (load 32-bit unsigned)
 */
uint32_t rv_lwu(RV_Reg rd, RV_Reg rs1, int16_t offset);

/**
 * @brief LH rd, offset(rs1) (load 16-bit signed)
 */
uint32_t rv_lh(RV_Reg rd, RV_Reg rs1, int16_t offset);

/**
 * @brief LHU rd, offset(rs1) (load 16-bit unsigned)
 */
uint32_t rv_lhu(RV_Reg rd, RV_Reg rs1, int16_t offset);

/**
 * @brief LB rd, offset(rs1) (load 8-bit signed)
 */
uint32_t rv_lb(RV_Reg rd, RV_Reg rs1, int16_t offset);

/**
 * @brief LBU rd, offset(rs1) (load 8-bit unsigned)
 */
uint32_t rv_lbu(RV_Reg rd, RV_Reg rs1, int16_t offset);

/*============================================================================
 * Store Instructions (S-Type)
 * Format: imm[11:5] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:0] | opcode[6:0]
 *============================================================================*/

/**
 * @brief SD rs2, offset(rs1) (store 64-bit)
 */
uint32_t rv_sd(RV_Reg rs2, RV_Reg rs1, int16_t offset);

/**
 * @brief SW rs2, offset(rs1) (store 32-bit)
 */
uint32_t rv_sw(RV_Reg rs2, RV_Reg rs1, int16_t offset);

/**
 * @brief SH rs2, offset(rs1) (store 16-bit)
 */
uint32_t rv_sh(RV_Reg rs2, RV_Reg rs1, int16_t offset);

/**
 * @brief SB rs2, offset(rs1) (store 8-bit)
 */
uint32_t rv_sb(RV_Reg rs2, RV_Reg rs1, int16_t offset);

/*============================================================================
 * Branch Instructions (B-Type)
 * Format: imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode
 *============================================================================*/

/**
 * @brief BEQ rs1, rs2, offset (branch if equal)
 */
uint32_t rv_beq(RV_Reg rs1, RV_Reg rs2, int16_t offset);

/**
 * @brief BNE rs1, rs2, offset (branch if not equal)
 */
uint32_t rv_bne(RV_Reg rs1, RV_Reg rs2, int16_t offset);

/**
 * @brief BLT rs1, rs2, offset (branch if less than, signed)
 */
uint32_t rv_blt(RV_Reg rs1, RV_Reg rs2, int16_t offset);

/**
 * @brief BGE rs1, rs2, offset (branch if greater or equal, signed)
 */
uint32_t rv_bge(RV_Reg rs1, RV_Reg rs2, int16_t offset);

/**
 * @brief BLTU rs1, rs2, offset (branch if less than, unsigned)
 */
uint32_t rv_bltu(RV_Reg rs1, RV_Reg rs2, int16_t offset);

/**
 * @brief BGEU rs1, rs2, offset (branch if greater or equal, unsigned)
 */
uint32_t rv_bgeu(RV_Reg rs1, RV_Reg rs2, int16_t offset);

/*============================================================================
 * Jump Instructions
 *============================================================================*/

/**
 * @brief JAL rd, offset (jump and link)
 * J-Type: imm[20|10:1|11|19:12] | rd | opcode
 */
uint32_t rv_jal(RV_Reg rd, int32_t offset);

/**
 * @brief JALR rd, rs1, offset (jump and link register)
 */
uint32_t rv_jalr(RV_Reg rd, RV_Reg rs1, int16_t offset);

/*============================================================================
 * Upper Immediate Instructions (U-Type)
 *============================================================================*/

/**
 * @brief LUI rd, imm (load upper immediate)
 */
uint32_t rv_lui(RV_Reg rd, int32_t imm);

/**
 * @brief AUIPC rd, imm (add upper immediate to PC)
 */
uint32_t rv_auipc(RV_Reg rd, int32_t imm);

/*============================================================================
 * Pseudo-Instructions
 *============================================================================*/

/**
 * @brief MV rd, rs (move register)
 */
uint32_t rv_mv(RV_Reg rd, RV_Reg rs);

/**
 * @brief LI rd, imm (load immediate - may emit multiple instructions)
 */
void rv_emit_li(RV_Buffer* buf, RV_Reg rd, int64_t imm);

/**
 * @brief NEG rd, rs (negate)
 */
uint32_t rv_neg(RV_Reg rd, RV_Reg rs);

/**
 * @brief NOT rd, rs (bitwise not)
 */
uint32_t rv_not(RV_Reg rd, RV_Reg rs);

/**
 * @brief SEQZ rd, rs (set if equal to zero)
 */
uint32_t rv_seqz(RV_Reg rd, RV_Reg rs);

/**
 * @brief SNEZ rd, rs (set if not equal to zero)
 */
uint32_t rv_snez(RV_Reg rd, RV_Reg rs);

/**
 * @brief J offset (unconditional jump)
 */
uint32_t rv_j(int32_t offset);

/**
 * @brief JR rs (jump to register)
 */
uint32_t rv_jr(RV_Reg rs);

/**
 * @brief RET (return from function)
 */
uint32_t rv_ret(void);

/**
 * @brief CALL offset (call far)
 */
void rv_emit_call(RV_Buffer* buf, int32_t offset);

/**
 * @brief NOP
 */
uint32_t rv_nop(void);

/*============================================================================
 * System Instructions
 *============================================================================*/

/**
 * @brief ECALL (environment call)
 */
uint32_t rv_ecall(void);

/**
 * @brief EBREAK (breakpoint)
 */
uint32_t rv_ebreak(void);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_RISCV_ENCODE_H */
