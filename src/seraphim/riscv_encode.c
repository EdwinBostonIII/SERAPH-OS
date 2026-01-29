/**
 * @file riscv_encode.c
 * @brief RISC-V Instruction Encoder Implementation
 *
 * Encodes RV64IMAC instructions for the SERAPH native compiler.
 */

#include "seraph/seraphim/riscv_encode.h"
#include <string.h>

/*============================================================================
 * Buffer Operations
 *============================================================================*/

void rv_buffer_init(RV_Buffer* buf, void* memory, size_t capacity_bytes) {
    if (!buf || !memory) return;

    buf->data = (uint32_t*)memory;
    buf->capacity = capacity_bytes / sizeof(uint32_t);
    buf->count = 0;
}

void rv_emit(RV_Buffer* buf, uint32_t instr) {
    if (!buf || buf->count >= buf->capacity) return;
    buf->data[buf->count++] = instr;
}

size_t rv_buffer_pos(RV_Buffer* buf) {
    return buf ? buf->count : 0;
}

void rv_patch(RV_Buffer* buf, size_t pos, uint32_t instr) {
    if (buf && pos < buf->count) {
        buf->data[pos] = instr;
    }
}

/*============================================================================
 * Opcode Constants
 *============================================================================*/

#define RV_OP_LOAD      0x03
#define RV_OP_MISC_MEM  0x0F
#define RV_OP_IMM       0x13
#define RV_OP_AUIPC     0x17
#define RV_OP_IMM_32    0x1B
#define RV_OP_STORE     0x23
#define RV_OP_REG       0x33
#define RV_OP_LUI       0x37
#define RV_OP_REG_32    0x3B
#define RV_OP_BRANCH    0x63
#define RV_OP_JALR      0x67
#define RV_OP_JAL       0x6F
#define RV_OP_SYSTEM    0x73

/*============================================================================
 * R-Type Instruction Encoding
 * funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
 *============================================================================*/

static inline uint32_t rv_rtype(uint8_t funct7, RV_Reg rs2, RV_Reg rs1,
                                 uint8_t funct3, RV_Reg rd, uint8_t opcode) {
    return ((uint32_t)(funct7 & 0x7F) << 25) |
           ((uint32_t)(rs2 & 0x1F) << 20) |
           ((uint32_t)(rs1 & 0x1F) << 15) |
           ((uint32_t)(funct3 & 0x7) << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           (opcode & 0x7F);
}

uint32_t rv_add(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x0, rd, RV_OP_REG);
}

uint32_t rv_sub(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x20, rs2, rs1, 0x0, rd, RV_OP_REG);
}

uint32_t rv_and(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x7, rd, RV_OP_REG);
}

uint32_t rv_or(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x6, rd, RV_OP_REG);
}

uint32_t rv_xor(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x4, rd, RV_OP_REG);
}

uint32_t rv_sll(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x1, rd, RV_OP_REG);
}

uint32_t rv_srl(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x5, rd, RV_OP_REG);
}

uint32_t rv_sra(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x20, rs2, rs1, 0x5, rd, RV_OP_REG);
}

uint32_t rv_slt(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x2, rd, RV_OP_REG);
}

uint32_t rv_sltu(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x3, rd, RV_OP_REG);
}

uint32_t rv_addw(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x00, rs2, rs1, 0x0, rd, RV_OP_REG_32);
}

uint32_t rv_subw(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x20, rs2, rs1, 0x0, rd, RV_OP_REG_32);
}

/*============================================================================
 * M Extension (Multiply/Divide)
 *============================================================================*/

uint32_t rv_mul(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x01, rs2, rs1, 0x0, rd, RV_OP_REG);
}

uint32_t rv_mulh(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x01, rs2, rs1, 0x1, rd, RV_OP_REG);
}

uint32_t rv_div(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x01, rs2, rs1, 0x4, rd, RV_OP_REG);
}

uint32_t rv_divu(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x01, rs2, rs1, 0x5, rd, RV_OP_REG);
}

uint32_t rv_rem(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x01, rs2, rs1, 0x6, rd, RV_OP_REG);
}

uint32_t rv_remu(RV_Reg rd, RV_Reg rs1, RV_Reg rs2) {
    return rv_rtype(0x01, rs2, rs1, 0x7, rd, RV_OP_REG);
}

/*============================================================================
 * I-Type Instruction Encoding
 * imm[31:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
 *============================================================================*/

static inline uint32_t rv_itype(int16_t imm, RV_Reg rs1, uint8_t funct3,
                                 RV_Reg rd, uint8_t opcode) {
    return ((uint32_t)(imm & 0xFFF) << 20) |
           ((uint32_t)(rs1 & 0x1F) << 15) |
           ((uint32_t)(funct3 & 0x7) << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           (opcode & 0x7F);
}

uint32_t rv_addi(RV_Reg rd, RV_Reg rs1, int16_t imm) {
    return rv_itype(imm, rs1, 0x0, rd, RV_OP_IMM);
}

uint32_t rv_andi(RV_Reg rd, RV_Reg rs1, int16_t imm) {
    return rv_itype(imm, rs1, 0x7, rd, RV_OP_IMM);
}

uint32_t rv_ori(RV_Reg rd, RV_Reg rs1, int16_t imm) {
    return rv_itype(imm, rs1, 0x6, rd, RV_OP_IMM);
}

uint32_t rv_xori(RV_Reg rd, RV_Reg rs1, int16_t imm) {
    return rv_itype(imm, rs1, 0x4, rd, RV_OP_IMM);
}

uint32_t rv_slti(RV_Reg rd, RV_Reg rs1, int16_t imm) {
    return rv_itype(imm, rs1, 0x2, rd, RV_OP_IMM);
}

uint32_t rv_sltiu(RV_Reg rd, RV_Reg rs1, int16_t imm) {
    return rv_itype(imm, rs1, 0x3, rd, RV_OP_IMM);
}

uint32_t rv_slli(RV_Reg rd, RV_Reg rs1, uint8_t shamt) {
    /* For RV64, shamt is 6 bits */
    return ((uint32_t)(shamt & 0x3F) << 20) |
           ((uint32_t)(rs1 & 0x1F) << 15) |
           (0x1 << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           RV_OP_IMM;
}

uint32_t rv_srli(RV_Reg rd, RV_Reg rs1, uint8_t shamt) {
    return ((uint32_t)(shamt & 0x3F) << 20) |
           ((uint32_t)(rs1 & 0x1F) << 15) |
           (0x5 << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           RV_OP_IMM;
}

uint32_t rv_srai(RV_Reg rd, RV_Reg rs1, uint8_t shamt) {
    return (0x10 << 26) |
           ((uint32_t)(shamt & 0x3F) << 20) |
           ((uint32_t)(rs1 & 0x1F) << 15) |
           (0x5 << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           RV_OP_IMM;
}

/*============================================================================
 * Load Instructions
 *============================================================================*/

uint32_t rv_ld(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x3, rd, RV_OP_LOAD);
}

uint32_t rv_lw(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x2, rd, RV_OP_LOAD);
}

uint32_t rv_lwu(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x6, rd, RV_OP_LOAD);
}

uint32_t rv_lh(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x1, rd, RV_OP_LOAD);
}

uint32_t rv_lhu(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x5, rd, RV_OP_LOAD);
}

uint32_t rv_lb(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x0, rd, RV_OP_LOAD);
}

uint32_t rv_lbu(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x4, rd, RV_OP_LOAD);
}

/*============================================================================
 * S-Type Instruction Encoding (Store)
 * imm[11:5] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:0] | opcode[6:0]
 *============================================================================*/

static inline uint32_t rv_stype(int16_t imm, RV_Reg rs2, RV_Reg rs1,
                                 uint8_t funct3, uint8_t opcode) {
    uint32_t imm_hi = (imm >> 5) & 0x7F;
    uint32_t imm_lo = imm & 0x1F;
    return (imm_hi << 25) |
           ((uint32_t)(rs2 & 0x1F) << 20) |
           ((uint32_t)(rs1 & 0x1F) << 15) |
           ((uint32_t)(funct3 & 0x7) << 12) |
           (imm_lo << 7) |
           (opcode & 0x7F);
}

uint32_t rv_sd(RV_Reg rs2, RV_Reg rs1, int16_t offset) {
    return rv_stype(offset, rs2, rs1, 0x3, RV_OP_STORE);
}

uint32_t rv_sw(RV_Reg rs2, RV_Reg rs1, int16_t offset) {
    return rv_stype(offset, rs2, rs1, 0x2, RV_OP_STORE);
}

uint32_t rv_sh(RV_Reg rs2, RV_Reg rs1, int16_t offset) {
    return rv_stype(offset, rs2, rs1, 0x1, RV_OP_STORE);
}

uint32_t rv_sb(RV_Reg rs2, RV_Reg rs1, int16_t offset) {
    return rv_stype(offset, rs2, rs1, 0x0, RV_OP_STORE);
}

/*============================================================================
 * B-Type Instruction Encoding (Branch)
 * imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode
 *============================================================================*/

static inline uint32_t rv_btype(int16_t offset, RV_Reg rs2, RV_Reg rs1,
                                 uint8_t funct3, uint8_t opcode) {
    /* Offset is in bytes, instruction format uses bits [12:1] */
    uint32_t imm12 = (offset >> 12) & 0x1;
    uint32_t imm11 = (offset >> 11) & 0x1;
    uint32_t imm10_5 = (offset >> 5) & 0x3F;
    uint32_t imm4_1 = (offset >> 1) & 0xF;

    return (imm12 << 31) |
           (imm10_5 << 25) |
           ((uint32_t)(rs2 & 0x1F) << 20) |
           ((uint32_t)(rs1 & 0x1F) << 15) |
           ((uint32_t)(funct3 & 0x7) << 12) |
           (imm4_1 << 8) |
           (imm11 << 7) |
           (opcode & 0x7F);
}

uint32_t rv_beq(RV_Reg rs1, RV_Reg rs2, int16_t offset) {
    return rv_btype(offset, rs2, rs1, 0x0, RV_OP_BRANCH);
}

uint32_t rv_bne(RV_Reg rs1, RV_Reg rs2, int16_t offset) {
    return rv_btype(offset, rs2, rs1, 0x1, RV_OP_BRANCH);
}

uint32_t rv_blt(RV_Reg rs1, RV_Reg rs2, int16_t offset) {
    return rv_btype(offset, rs2, rs1, 0x4, RV_OP_BRANCH);
}

uint32_t rv_bge(RV_Reg rs1, RV_Reg rs2, int16_t offset) {
    return rv_btype(offset, rs2, rs1, 0x5, RV_OP_BRANCH);
}

uint32_t rv_bltu(RV_Reg rs1, RV_Reg rs2, int16_t offset) {
    return rv_btype(offset, rs2, rs1, 0x6, RV_OP_BRANCH);
}

uint32_t rv_bgeu(RV_Reg rs1, RV_Reg rs2, int16_t offset) {
    return rv_btype(offset, rs2, rs1, 0x7, RV_OP_BRANCH);
}

/*============================================================================
 * J-Type Instruction Encoding (Jump)
 * imm[20|10:1|11|19:12] | rd | opcode
 *============================================================================*/

uint32_t rv_jal(RV_Reg rd, int32_t offset) {
    /* Offset is in bytes */
    uint32_t imm20 = (offset >> 20) & 0x1;
    uint32_t imm19_12 = (offset >> 12) & 0xFF;
    uint32_t imm11 = (offset >> 11) & 0x1;
    uint32_t imm10_1 = (offset >> 1) & 0x3FF;

    return (imm20 << 31) |
           (imm10_1 << 21) |
           (imm11 << 20) |
           (imm19_12 << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           RV_OP_JAL;
}

uint32_t rv_jalr(RV_Reg rd, RV_Reg rs1, int16_t offset) {
    return rv_itype(offset, rs1, 0x0, rd, RV_OP_JALR);
}

/*============================================================================
 * U-Type Instruction Encoding
 * imm[31:12] | rd | opcode
 *============================================================================*/

uint32_t rv_lui(RV_Reg rd, int32_t imm) {
    return ((uint32_t)(imm & 0xFFFFF) << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           RV_OP_LUI;
}

uint32_t rv_auipc(RV_Reg rd, int32_t imm) {
    return ((uint32_t)(imm & 0xFFFFF) << 12) |
           ((uint32_t)(rd & 0x1F) << 7) |
           RV_OP_AUIPC;
}

/*============================================================================
 * Pseudo-Instructions
 *============================================================================*/

uint32_t rv_mv(RV_Reg rd, RV_Reg rs) {
    return rv_addi(rd, rs, 0);
}

void rv_emit_li(RV_Buffer* buf, RV_Reg rd, int64_t imm) {
    /* Load 64-bit immediate */
    if (imm >= -2048 && imm < 2048) {
        /* Fits in 12-bit immediate */
        rv_emit(buf, rv_addi(rd, RV_ZERO, (int16_t)imm));
    } else if ((imm & 0xFFF) == 0 && imm >= -0x80000000LL && imm < 0x80000000LL) {
        /* Upper 20 bits only */
        rv_emit(buf, rv_lui(rd, (int32_t)(imm >> 12)));
    } else if (imm >= -0x80000000LL && imm < 0x80000000LL) {
        /* Fits in 32 bits */
        int32_t hi = (int32_t)(imm >> 12);
        int16_t lo = imm & 0xFFF;
        /* Sign-extend correction */
        if (lo < 0) hi++;
        rv_emit(buf, rv_lui(rd, hi));
        rv_emit(buf, rv_addi(rd, rd, lo));
    } else {
        /* Need full 64-bit sequence */
        /* Build in chunks using shifts and adds */
        int64_t val = imm;
        int shift = 0;

        /* Find highest non-zero chunk */
        while (shift < 48 && (val >> (48 - shift)) == 0) {
            shift += 16;
        }

        /* Load first chunk */
        int16_t chunk = (val >> (48 - shift)) & 0xFFFF;
        if (chunk >= -2048 && chunk < 2048) {
            rv_emit(buf, rv_addi(rd, RV_ZERO, chunk));
        } else {
            rv_emit(buf, rv_lui(rd, (chunk >> 12) + ((chunk & 0x800) ? 1 : 0)));
            if (chunk & 0xFFF) {
                rv_emit(buf, rv_addi(rd, rd, chunk & 0xFFF));
            }
        }

        /* Add remaining chunks */
        shift += 16;
        while (shift <= 48) {
            chunk = (val >> (48 - shift)) & 0xFFFF;
            rv_emit(buf, rv_slli(rd, rd, 16));
            if (chunk != 0) {
                rv_emit(buf, rv_ori(rd, rd, chunk));
            }
            shift += 16;
        }
    }
}

uint32_t rv_neg(RV_Reg rd, RV_Reg rs) {
    return rv_sub(rd, RV_ZERO, rs);
}

uint32_t rv_not(RV_Reg rd, RV_Reg rs) {
    return rv_xori(rd, rs, -1);
}

uint32_t rv_seqz(RV_Reg rd, RV_Reg rs) {
    return rv_sltiu(rd, rs, 1);
}

uint32_t rv_snez(RV_Reg rd, RV_Reg rs) {
    return rv_sltu(rd, RV_ZERO, rs);
}

uint32_t rv_j(int32_t offset) {
    return rv_jal(RV_ZERO, offset);
}

uint32_t rv_jr(RV_Reg rs) {
    return rv_jalr(RV_ZERO, rs, 0);
}

uint32_t rv_ret(void) {
    return rv_jalr(RV_ZERO, RV_RA, 0);
}

void rv_emit_call(RV_Buffer* buf, int32_t offset) {
    /* For near calls, use JAL */
    if (offset >= -0x100000 && offset < 0x100000) {
        rv_emit(buf, rv_jal(RV_RA, offset));
    } else {
        /* Far call: AUIPC + JALR */
        int32_t hi = offset >> 12;
        int32_t lo = offset & 0xFFF;
        if (lo >= 0x800) hi++;  /* Sign-extend correction */
        rv_emit(buf, rv_auipc(RV_T1, hi));
        rv_emit(buf, rv_jalr(RV_RA, RV_T1, lo));
    }
}

uint32_t rv_nop(void) {
    return rv_addi(RV_ZERO, RV_ZERO, 0);
}

/*============================================================================
 * System Instructions
 *============================================================================*/

uint32_t rv_ecall(void) {
    return 0x00000073;
}

uint32_t rv_ebreak(void) {
    return 0x00100073;
}
