/**
 * @file arm64_encode.c
 * @brief ARM64 Instruction Encoder Implementation
 *
 * Encodes ARM64 (AArch64) instructions for the SERAPH native compiler.
 * All instructions are 32-bit fixed width.
 */

#include "seraph/seraphim/arm64_encode.h"
#include <string.h>

/*============================================================================
 * Buffer Operations
 *============================================================================*/

void arm64_buffer_init(ARM64_Buffer* buf, void* memory, size_t capacity_bytes) {
    if (!buf || !memory) return;

    buf->data = (uint32_t*)memory;
    buf->capacity = capacity_bytes / sizeof(uint32_t);
    buf->count = 0;
}

void arm64_emit(ARM64_Buffer* buf, uint32_t instr) {
    if (!buf || buf->count >= buf->capacity) return;
    buf->data[buf->count++] = instr;
}

size_t arm64_buffer_pos(ARM64_Buffer* buf) {
    return buf ? buf->count : 0;
}

void arm64_patch(ARM64_Buffer* buf, size_t pos, uint32_t instr) {
    if (buf && pos < buf->count) {
        buf->data[pos] = instr;
    }
}

/*============================================================================
 * Encoding Helpers
 *============================================================================*/

/* 64-bit size field */
#define SF_64 (1u << 31)
/* 32-bit size field */
#define SF_32 (0u << 31)

/*============================================================================
 * Data Processing (Immediate)
 *============================================================================*/

uint32_t arm64_add_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12) {
    /* ADD Xd, Xn, #imm12: 1|00|10001|00|imm12|Rn|Rd */
    return SF_64 | (0x11 << 24) | ((imm12 & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_addw_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12) {
    /* ADD Wd, Wn, #imm12: 0|00|10001|00|imm12|Rn|Rd */
    return SF_32 | (0x11 << 24) | ((imm12 & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_sub_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12) {
    /* SUB Xd, Xn, #imm12: 1|10|10001|00|imm12|Rn|Rd */
    return SF_64 | (0x51 << 24) | ((imm12 & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_subw_imm(ARM64_Reg rd, ARM64_Reg rn, uint16_t imm12) {
    /* SUB Wd, Wn, #imm12: 0|10|10001|00|imm12|Rn|Rd */
    return SF_32 | (0x51 << 24) | ((imm12 & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_cmp_imm(ARM64_Reg rn, uint16_t imm12) {
    /* CMP Xn, #imm12: SUBS XZR, Xn, #imm12 */
    return SF_64 | (0x71 << 24) | ((imm12 & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (ARM64_XZR & 0x1F);
}

uint32_t arm64_cmn_imm(ARM64_Reg rn, uint16_t imm12) {
    /* CMN Xn, #imm12: ADDS XZR, Xn, #imm12 */
    return SF_64 | (0x31 << 24) | ((imm12 & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (ARM64_XZR & 0x1F);
}

/*============================================================================
 * Data Processing (Register)
 *============================================================================*/

uint32_t arm64_add_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* ADD Xd, Xn, Xm: 1|00|01011|00|0|Rm|000000|Rn|Rd */
    return SF_64 | (0x0B << 24) | ((rm & 0x1F) << 16) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_sub_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* SUB Xd, Xn, Xm: 1|10|01011|00|0|Rm|000000|Rn|Rd */
    return SF_64 | (0x4B << 24) | ((rm & 0x1F) << 16) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_mul(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* MUL Xd, Xn, Xm: 1|00|11011|000|Rm|0|11111|Rn|Rd */
    return SF_64 | (0x1B << 24) | ((rm & 0x1F) << 16) |
           (0x1F << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_sdiv(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* SDIV Xd, Xn, Xm: 1|00|11010110|Rm|00001|1|Rn|Rd */
    return SF_64 | (0x1AC << 21) | ((rm & 0x1F) << 16) |
           (0x03 << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_udiv(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* UDIV Xd, Xn, Xm: 1|00|11010110|Rm|00001|0|Rn|Rd */
    return SF_64 | (0x1AC << 21) | ((rm & 0x1F) << 16) |
           (0x02 << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_msub(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm, ARM64_Reg ra) {
    /* MSUB Xd, Xn, Xm, Xa: Rd = Ra - Rn * Rm
     * 1|00|11011|000|Rm|1|Ra|Rn|Rd */
    return SF_64 | (0x1B << 24) | ((rm & 0x1F) << 16) |
           (1 << 15) | ((ra & 0x1F) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_neg(ARM64_Reg rd, ARM64_Reg rm) {
    /* NEG Xd, Xm: SUB Xd, XZR, Xm */
    return arm64_sub_reg(rd, ARM64_XZR, rm);
}

uint32_t arm64_cmp_reg(ARM64_Reg rn, ARM64_Reg rm) {
    /* CMP Xn, Xm: SUBS XZR, Xn, Xm */
    return SF_64 | (0x6B << 24) | ((rm & 0x1F) << 16) |
           ((rn & 0x1F) << 5) | (ARM64_XZR & 0x1F);
}

/*============================================================================
 * Logical (Register)
 *============================================================================*/

uint32_t arm64_and_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* AND Xd, Xn, Xm: 1|00|01010|00|0|Rm|000000|Rn|Rd */
    return SF_64 | (0x0A << 24) | ((rm & 0x1F) << 16) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_orr_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* ORR Xd, Xn, Xm: 1|01|01010|00|0|Rm|000000|Rn|Rd */
    return SF_64 | (0x2A << 24) | ((rm & 0x1F) << 16) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_eor_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* EOR Xd, Xn, Xm: 1|10|01010|00|0|Rm|000000|Rn|Rd */
    return SF_64 | (0x4A << 24) | ((rm & 0x1F) << 16) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_mvn(ARM64_Reg rd, ARM64_Reg rm) {
    /* MVN Xd, Xm: ORN Xd, XZR, Xm */
    return SF_64 | (0x2A << 24) | (1 << 21) | ((rm & 0x1F) << 16) |
           ((ARM64_XZR & 0x1F) << 5) | (rd & 0x1F);
}

/*============================================================================
 * Shift/Rotate
 *============================================================================*/

uint32_t arm64_lsl_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* LSLV Xd, Xn, Xm: 1|00|11010110|Rm|00100|0|Rn|Rd */
    return SF_64 | (0x1AC << 21) | ((rm & 0x1F) << 16) |
           (0x08 << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_lsr_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* LSRV Xd, Xn, Xm: 1|00|11010110|Rm|00101|0|Rn|Rd */
    return SF_64 | (0x1AC << 21) | ((rm & 0x1F) << 16) |
           (0x09 << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_asr_reg(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm) {
    /* ASRV Xd, Xn, Xm: 1|00|11010110|Rm|00101|1|Rn|Rd */
    return SF_64 | (0x1AC << 21) | ((rm & 0x1F) << 16) |
           (0x0A << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_lsl_imm(ARM64_Reg rd, ARM64_Reg rn, uint8_t shift) {
    /* LSL via UBFM: 1|10|100110|1|immr|imms|Rn|Rd
     * For LSL #n, immr = -n mod 64, imms = 63 - n */
    uint8_t immr = (64 - shift) & 0x3F;
    uint8_t imms = 63 - shift;
    return SF_64 | (0xD3 << 24) | (1 << 22) |
           ((immr & 0x3F) << 16) | ((imms & 0x3F) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_lsr_imm(ARM64_Reg rd, ARM64_Reg rn, uint8_t shift) {
    /* LSR via UBFM: 1|10|100110|1|shift|63|Rn|Rd */
    return SF_64 | (0xD3 << 24) | (1 << 22) |
           ((shift & 0x3F) << 16) | (0x3F << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_asr_imm(ARM64_Reg rd, ARM64_Reg rn, uint8_t shift) {
    /* ASR via SBFM: 1|00|100110|1|shift|63|Rn|Rd */
    return SF_64 | (0x93 << 24) | (1 << 22) |
           ((shift & 0x3F) << 16) | (0x3F << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

/*============================================================================
 * Move
 *============================================================================*/

uint32_t arm64_mov_reg(ARM64_Reg rd, ARM64_Reg rm) {
    /* MOV Xd, Xm: ORR Xd, XZR, Xm */
    return arm64_orr_reg(rd, ARM64_XZR, rm);
}

uint32_t arm64_movz(ARM64_Reg rd, uint16_t imm16, uint8_t shift) {
    /* MOVZ Xd, #imm16, LSL #shift: 1|10|100101|hw|imm16|Rd */
    uint8_t hw = shift / 16;  /* 0, 1, 2, or 3 */
    return SF_64 | (0xD2 << 23) | ((hw & 0x3) << 21) |
           ((imm16 & 0xFFFF) << 5) | (rd & 0x1F);
}

uint32_t arm64_movk(ARM64_Reg rd, uint16_t imm16, uint8_t shift) {
    /* MOVK Xd, #imm16, LSL #shift: 1|11|100101|hw|imm16|Rd */
    uint8_t hw = shift / 16;
    return SF_64 | (0xF2 << 23) | ((hw & 0x3) << 21) |
           ((imm16 & 0xFFFF) << 5) | (rd & 0x1F);
}

uint32_t arm64_movn(ARM64_Reg rd, uint16_t imm16, uint8_t shift) {
    /* MOVN Xd, #imm16, LSL #shift: 1|00|100101|hw|imm16|Rd */
    uint8_t hw = shift / 16;
    return SF_64 | (0x92 << 23) | ((hw & 0x3) << 21) |
           ((imm16 & 0xFFFF) << 5) | (rd & 0x1F);
}

void arm64_emit_mov64(ARM64_Buffer* buf, ARM64_Reg rd, int64_t imm) {
    /* Load 64-bit immediate using MOVZ + MOVK sequence */
    uint64_t uimm = (uint64_t)imm;

    /* Emit MOVZ for first chunk */
    arm64_emit(buf, arm64_movz(rd, uimm & 0xFFFF, 0));

    /* Emit MOVK for remaining chunks if needed */
    if ((uimm >> 16) & 0xFFFF) {
        arm64_emit(buf, arm64_movk(rd, (uimm >> 16) & 0xFFFF, 16));
    }
    if ((uimm >> 32) & 0xFFFF) {
        arm64_emit(buf, arm64_movk(rd, (uimm >> 32) & 0xFFFF, 32));
    }
    if ((uimm >> 48) & 0xFFFF) {
        arm64_emit(buf, arm64_movk(rd, (uimm >> 48) & 0xFFFF, 48));
    }
}

/*============================================================================
 * Conditional Select
 *============================================================================*/

uint32_t arm64_csel(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm, ARM64_Cond cond) {
    /* CSEL Xd, Xn, Xm, cond: 1|00|11010100|Rm|cond|00|Rn|Rd */
    return SF_64 | (0x1A << 24) | (1 << 23) | ((rm & 0x1F) << 16) |
           ((cond & 0xF) << 12) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_cset(ARM64_Reg rd, ARM64_Cond cond) {
    /* CSET Xd, cond: CSINC Xd, XZR, XZR, invert(cond) */
    ARM64_Cond inv_cond = cond ^ 1;  /* Invert condition */
    return SF_64 | (0x1A << 24) | (1 << 23) | ((ARM64_XZR & 0x1F) << 16) |
           ((inv_cond & 0xF) << 12) | (1 << 10) |
           ((ARM64_XZR & 0x1F) << 5) | (rd & 0x1F);
}

uint32_t arm64_csetm(ARM64_Reg rd, ARM64_Cond cond) {
    /* CSETM Xd, cond: CSINV Xd, XZR, XZR, invert(cond) */
    ARM64_Cond inv_cond = cond ^ 1;
    return SF_64 | (0x5A << 24) | (1 << 23) | ((ARM64_XZR & 0x1F) << 16) |
           ((inv_cond & 0xF) << 12) |
           ((ARM64_XZR & 0x1F) << 5) | (rd & 0x1F);
}

/*============================================================================
 * Load/Store
 *============================================================================*/

uint32_t arm64_ldr_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset) {
    /* LDR Xt, [Xn, #offset]: 11|111|00|01|0|1|imm12|Rn|Rt */
    /* For unsigned offset (imm12 must be positive, scaled by 8) */
    uint16_t scaled = (offset >= 0) ? (offset / 8) : 0;
    return 0xF9400000 | ((scaled & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rt & 0x1F);
}

uint32_t arm64_ldrw_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset) {
    /* LDR Wt, [Xn, #offset]: 10|111|00|01|0|1|imm12|Rn|Rt */
    uint16_t scaled = (offset >= 0) ? (offset / 4) : 0;
    return 0xB9400000 | ((scaled & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rt & 0x1F);
}

uint32_t arm64_ldrb_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset) {
    /* LDRB Wt, [Xn, #offset]: 00|111|00|01|0|1|imm12|Rn|Rt */
    uint16_t uoffset = (offset >= 0) ? offset : 0;
    return 0x39400000 | ((uoffset & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rt & 0x1F);
}

uint32_t arm64_str_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset) {
    /* STR Xt, [Xn, #offset]: 11|111|00|00|0|1|imm12|Rn|Rt */
    uint16_t scaled = (offset >= 0) ? (offset / 8) : 0;
    return 0xF9000000 | ((scaled & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rt & 0x1F);
}

uint32_t arm64_strw_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset) {
    /* STR Wt, [Xn, #offset]: 10|111|00|00|0|1|imm12|Rn|Rt */
    uint16_t scaled = (offset >= 0) ? (offset / 4) : 0;
    return 0xB9000000 | ((scaled & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rt & 0x1F);
}

uint32_t arm64_strb_imm(ARM64_Reg rt, ARM64_Reg rn, int16_t offset) {
    /* STRB Wt, [Xn, #offset]: 00|111|00|00|0|1|imm12|Rn|Rt */
    uint16_t uoffset = (offset >= 0) ? offset : 0;
    return 0x39000000 | ((uoffset & 0xFFF) << 10) |
           ((rn & 0x1F) << 5) | (rt & 0x1F);
}

uint32_t arm64_stp_pre(ARM64_Reg rt1, ARM64_Reg rt2, ARM64_Reg rn, int16_t offset) {
    /* STP Xt1, Xt2, [Xn, #offset]!: 10|101|0|011|1|imm7|Rt2|Rn|Rt1 */
    int8_t scaled = offset / 8;
    return 0xA9800000 | ((scaled & 0x7F) << 15) |
           ((rt2 & 0x1F) << 10) | ((rn & 0x1F) << 5) | (rt1 & 0x1F);
}

uint32_t arm64_ldp_post(ARM64_Reg rt1, ARM64_Reg rt2, ARM64_Reg rn, int16_t offset) {
    /* LDP Xt1, Xt2, [Xn], #offset: 10|101|0|001|1|imm7|Rt2|Rn|Rt1 */
    int8_t scaled = offset / 8;
    return 0xA8C00000 | ((scaled & 0x7F) << 15) |
           ((rt2 & 0x1F) << 10) | ((rn & 0x1F) << 5) | (rt1 & 0x1F);
}

/*============================================================================
 * Branch
 *============================================================================*/

uint32_t arm64_b(int32_t offset) {
    /* B #offset: 0|00101|imm26 */
    /* Offset is in instructions (4-byte aligned) */
    int32_t imm26 = offset / 4;
    return 0x14000000 | (imm26 & 0x3FFFFFF);
}

uint32_t arm64_bl(int32_t offset) {
    /* BL #offset: 1|00101|imm26 */
    int32_t imm26 = offset / 4;
    return 0x94000000 | (imm26 & 0x3FFFFFF);
}

uint32_t arm64_bcond(ARM64_Cond cond, int32_t offset) {
    /* B.cond #offset: 0101010|0|imm19|0|cond */
    int32_t imm19 = offset / 4;
    return 0x54000000 | ((imm19 & 0x7FFFF) << 5) | (cond & 0xF);
}

uint32_t arm64_br(ARM64_Reg rn) {
    /* BR Xn: 1101011|0|0|00|11111|0000|00|Rn|00000 */
    return 0xD61F0000 | ((rn & 0x1F) << 5);
}

uint32_t arm64_blr(ARM64_Reg rn) {
    /* BLR Xn: 1101011|0|0|01|11111|0000|00|Rn|00000 */
    return 0xD63F0000 | ((rn & 0x1F) << 5);
}

uint32_t arm64_ret(void) {
    /* RET: BR X30 */
    return arm64_br(ARM64_LR);
}

uint32_t arm64_cbz(ARM64_Reg rt, int32_t offset) {
    /* CBZ Xt, #offset: 1|011010|0|imm19|Rt */
    int32_t imm19 = offset / 4;
    return 0xB4000000 | ((imm19 & 0x7FFFF) << 5) | (rt & 0x1F);
}

uint32_t arm64_cbnz(ARM64_Reg rt, int32_t offset) {
    /* CBNZ Xt, #offset: 1|011010|1|imm19|Rt */
    int32_t imm19 = offset / 4;
    return 0xB5000000 | ((imm19 & 0x7FFFF) << 5) | (rt & 0x1F);
}

uint32_t arm64_tbz(ARM64_Reg rt, uint8_t bit, int32_t offset) {
    /* TBZ Rt, #bit, #offset: b5|011011|0|b40|imm14|Rt */
    int32_t imm14 = offset / 4;
    uint8_t b5 = (bit >> 5) & 1;
    uint8_t b40 = bit & 0x1F;
    return ((b5 << 31) | 0x36000000 | ((b40 & 0x1F) << 19) |
            ((imm14 & 0x3FFF) << 5) | (rt & 0x1F));
}

uint32_t arm64_tbnz(ARM64_Reg rt, uint8_t bit, int32_t offset) {
    /* TBNZ Rt, #bit, #offset: b5|011011|1|b40|imm14|Rt */
    int32_t imm14 = offset / 4;
    uint8_t b5 = (bit >> 5) & 1;
    uint8_t b40 = bit & 0x1F;
    return ((b5 << 31) | 0x37000000 | ((b40 & 0x1F) << 19) |
            ((imm14 & 0x3FFF) << 5) | (rt & 0x1F));
}

/*============================================================================
 * System
 *============================================================================*/

uint32_t arm64_nop(void) {
    return 0xD503201F;
}

uint32_t arm64_svc(uint16_t imm) {
    /* SVC #imm: 11010100|000|imm16|000|01 */
    return 0xD4000001 | ((imm & 0xFFFF) << 5);
}

uint32_t arm64_brk(uint16_t imm) {
    /* BRK #imm: 11010100|001|imm16|000|00 */
    return 0xD4200000 | ((imm & 0xFFFF) << 5);
}
