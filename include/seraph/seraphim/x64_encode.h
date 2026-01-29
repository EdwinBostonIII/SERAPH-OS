/**
 * @file x64_encode.h
 * @brief MC27: x86-64 Instruction Encoder
 *
 * Native x86-64 machine code generation for Seraphim.
 * This is the foundation for SERAPH's compiler independence.
 *
 * x86-64 Instruction Format:
 *   [Prefixes] [REX] [Opcode] [ModR/M] [SIB] [Displacement] [Immediate]
 *
 * Register Encoding (System V AMD64 ABI):
 *   RAX=0  RCX=1  RDX=2  RBX=3  RSP=4  RBP=5  RSI=6  RDI=7
 *   R8=8   R9=9   R10=10 R11=11 R12=12 R13=13 R14=14 R15=15
 *
 * Calling Convention (arguments):
 *   Integer: RDI, RSI, RDX, RCX, R8, R9, then stack
 *   Return:  RAX (integer), XMM0 (float)
 *
 * Callee-saved: RBX, RBP, R12-R15
 * Caller-saved: RAX, RCX, RDX, RSI, RDI, R8-R11
 */

#ifndef SERAPH_SERAPHIM_X64_ENCODE_H
#define SERAPH_SERAPHIM_X64_ENCODE_H

#include <stdint.h>
#include <stddef.h>
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Register Definitions
 *============================================================================*/

/**
 * @brief x86-64 General Purpose Registers
 */
typedef enum {
    X64_RAX = 0,  X64_RCX = 1,  X64_RDX = 2,  X64_RBX = 3,
    X64_RSP = 4,  X64_RBP = 5,  X64_RSI = 6,  X64_RDI = 7,
    X64_R8  = 8,  X64_R9  = 9,  X64_R10 = 10, X64_R11 = 11,
    X64_R12 = 12, X64_R13 = 13, X64_R14 = 14, X64_R15 = 15,

    /* 32-bit versions (same encoding, no REX.W) */
    X64_EAX = 0,  X64_ECX = 1,  X64_EDX = 2,  X64_EBX = 3,
    X64_ESP = 4,  X64_EBP = 5,  X64_ESI = 6,  X64_EDI = 7,
    X64_R8D = 8,  X64_R9D = 9,  X64_R10D = 10, X64_R11D = 11,
    X64_R12D = 12, X64_R13D = 13, X64_R14D = 14, X64_R15D = 15,

    /* No register (for memory-only operands) */
    X64_NONE = -1,
} X64_Reg;

/**
 * @brief Register argument order (System V ABI)
 */
static const X64_Reg X64_ARG_REGS[] = {
    X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9
};
#define X64_ARG_REG_COUNT 6

/**
 * @brief Callee-saved registers
 */
static const X64_Reg X64_CALLEE_SAVED[] = {
    X64_RBX, X64_RBP, X64_R12, X64_R13, X64_R14, X64_R15
};
#define X64_CALLEE_SAVED_COUNT 6

/*============================================================================
 * Operand Sizes
 *============================================================================*/

typedef enum {
    X64_SZ_8  = 1,   /**< Byte */
    X64_SZ_16 = 2,   /**< Word */
    X64_SZ_32 = 4,   /**< Doubleword */
    X64_SZ_64 = 8,   /**< Quadword */
} X64_Size;

/*============================================================================
 * Condition Codes
 *============================================================================*/

/**
 * @brief x86-64 Condition Codes (for Jcc, CMOVcc, SETcc)
 */
typedef enum {
    X64_CC_O   = 0x0,  /**< Overflow */
    X64_CC_NO  = 0x1,  /**< Not Overflow */
    X64_CC_B   = 0x2,  /**< Below (CF=1) */
    X64_CC_C   = 0x2,  /**< Carry (CF=1) */
    X64_CC_NAE = 0x2,  /**< Not Above or Equal */
    X64_CC_AE  = 0x3,  /**< Above or Equal (CF=0) */
    X64_CC_NB  = 0x3,  /**< Not Below */
    X64_CC_NC  = 0x3,  /**< Not Carry */
    X64_CC_E   = 0x4,  /**< Equal (ZF=1) */
    X64_CC_Z   = 0x4,  /**< Zero */
    X64_CC_NE  = 0x5,  /**< Not Equal (ZF=0) */
    X64_CC_NZ  = 0x5,  /**< Not Zero */
    X64_CC_BE  = 0x6,  /**< Below or Equal */
    X64_CC_NA  = 0x6,  /**< Not Above */
    X64_CC_A   = 0x7,  /**< Above */
    X64_CC_NBE = 0x7,  /**< Not Below or Equal */
    X64_CC_S   = 0x8,  /**< Sign (SF=1) */
    X64_CC_NS  = 0x9,  /**< Not Sign */
    X64_CC_P   = 0xA,  /**< Parity (PF=1) */
    X64_CC_PE  = 0xA,  /**< Parity Even */
    X64_CC_NP  = 0xB,  /**< Not Parity */
    X64_CC_PO  = 0xB,  /**< Parity Odd */
    X64_CC_L   = 0xC,  /**< Less (signed) */
    X64_CC_NGE = 0xC,  /**< Not Greater or Equal */
    X64_CC_GE  = 0xD,  /**< Greater or Equal (signed) */
    X64_CC_NL  = 0xD,  /**< Not Less */
    X64_CC_LE  = 0xE,  /**< Less or Equal (signed) */
    X64_CC_NG  = 0xE,  /**< Not Greater */
    X64_CC_G   = 0xF,  /**< Greater (signed) */
    X64_CC_NLE = 0xF,  /**< Not Less or Equal */
} X64_Condition;

/*============================================================================
 * Instruction Buffer
 *============================================================================*/

/**
 * @brief Machine code buffer
 */
typedef struct {
    uint8_t*  code;          /**< Code buffer */
    size_t    size;          /**< Current size (bytes written) */
    size_t    capacity;      /**< Buffer capacity */

    /* Relocation tracking */
    struct {
        size_t   offset;     /**< Offset in code buffer */
        uint32_t symbol_id;  /**< Symbol being referenced */
        int8_t   type;       /**< Relocation type */
    }* relocs;
    size_t    reloc_count;
    size_t    reloc_capacity;
} X64_Buffer;

/*============================================================================
 * Label Management
 *============================================================================*/

/**
 * @brief Forward reference (unresolved jump)
 */
typedef struct {
    size_t   patch_offset;   /**< Where to patch in code */
    uint32_t label_id;       /**< Target label ID */
    int8_t   size;           /**< Size of displacement (1, 2, or 4) */
} X64_Fixup;

/**
 * @brief Label definition
 */
typedef struct {
    uint32_t id;             /**< Label ID */
    size_t   offset;         /**< Offset in code (or SIZE_MAX if undefined) */
} X64_Label;

/**
 * @brief Label table
 */
typedef struct {
    X64_Label* labels;
    size_t     count;
    size_t     capacity;

    X64_Fixup* fixups;
    size_t     fixup_count;
    size_t     fixup_capacity;

    uint32_t   next_id;      /**< Next label ID to allocate */
} X64_Labels;

/*============================================================================
 * Buffer Management
 *============================================================================*/

/**
 * @brief Initialize instruction buffer
 *
 * @param buf Buffer to initialize
 * @param initial_capacity Initial capacity in bytes
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_buf_init(X64_Buffer* buf, size_t initial_capacity);

/**
 * @brief Free instruction buffer
 *
 * @param buf Buffer to free
 */
void x64_buf_free(X64_Buffer* buf);

/**
 * @brief Ensure buffer has space for n more bytes
 *
 * @param buf The buffer
 * @param n Bytes needed
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_buf_reserve(X64_Buffer* buf, size_t n);

/**
 * @brief Write byte to buffer
 *
 * @param buf The buffer
 * @param b Byte to write
 */
void x64_emit_byte(X64_Buffer* buf, uint8_t b);

/**
 * @brief Write word (2 bytes, little-endian) to buffer
 *
 * @param buf The buffer
 * @param w Word to write
 */
void x64_emit_word(X64_Buffer* buf, uint16_t w);

/**
 * @brief Write dword (4 bytes, little-endian) to buffer
 *
 * @param buf The buffer
 * @param d Dword to write
 */
void x64_emit_dword(X64_Buffer* buf, uint32_t d);

/**
 * @brief Write qword (8 bytes, little-endian) to buffer
 *
 * @param buf The buffer
 * @param q Qword to write
 */
void x64_emit_qword(X64_Buffer* buf, uint64_t q);

/*============================================================================
 * Label Management
 *============================================================================*/

/**
 * @brief Initialize label table
 *
 * @param labels Label table to initialize
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_labels_init(X64_Labels* labels);

/**
 * @brief Free label table
 *
 * @param labels Label table to free
 */
void x64_labels_free(X64_Labels* labels);

/**
 * @brief Create a new label (undefined)
 *
 * @param labels Label table
 * @return Label ID, or UINT32_MAX on error
 */
uint32_t x64_label_create(X64_Labels* labels);

/**
 * @brief Define a label at current position
 *
 * @param labels Label table
 * @param buf Code buffer
 * @param label_id Label to define
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_label_define(X64_Labels* labels, X64_Buffer* buf, uint32_t label_id);

/**
 * @brief Add fixup for forward reference
 *
 * @param labels Label table
 * @param buf Code buffer
 * @param label_id Target label
 * @param size Displacement size (1, 2, or 4)
 * @return VBIT_TRUE on success
 */
Seraph_Vbit x64_label_fixup(X64_Labels* labels, X64_Buffer* buf,
                            uint32_t label_id, int8_t size);

/**
 * @brief Resolve all fixups
 *
 * @param labels Label table
 * @param buf Code buffer
 * @return VBIT_TRUE on success (all labels defined)
 */
Seraph_Vbit x64_labels_resolve(X64_Labels* labels, X64_Buffer* buf);

/*============================================================================
 * REX Prefix Helpers
 *============================================================================*/

/**
 * @brief Determine if register requires REX prefix
 */
static inline int x64_needs_rex(X64_Reg reg) {
    return reg >= X64_R8;
}

/**
 * @brief Build REX prefix
 *
 * @param w 1 for 64-bit operand size
 * @param r Extension of ModR/M reg field
 * @param x Extension of SIB index field
 * @param b Extension of ModR/M r/m, SIB base, or opcode reg
 * @return REX byte
 */
static inline uint8_t x64_rex(int w, int r, int x, int b) {
    return 0x40 | (w ? 0x08 : 0) | (r ? 0x04 : 0) | (x ? 0x02 : 0) | (b ? 0x01 : 0);
}

/**
 * @brief Build ModR/M byte
 *
 * @param mod Addressing mode (0-3)
 * @param reg Register or opcode extension (low 3 bits)
 * @param rm Register/memory operand (low 3 bits)
 * @return ModR/M byte
 */
static inline uint8_t x64_modrm(int mod, int reg, int rm) {
    return (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7));
}

/**
 * @brief Build SIB byte
 *
 * @param scale Scale factor (0=1, 1=2, 2=4, 3=8)
 * @param index Index register (low 3 bits)
 * @param base Base register (low 3 bits)
 * @return SIB byte
 */
static inline uint8_t x64_sib(int scale, int index, int base) {
    return (uint8_t)(((scale & 3) << 6) | ((index & 7) << 3) | (base & 7));
}

/*============================================================================
 * Core Instruction Encoders
 *============================================================================*/

/**
 * @brief Emit MOV reg64, imm64 (movabs)
 *
 * @param buf Code buffer
 * @param dst Destination register
 * @param imm 64-bit immediate value
 */
void x64_mov_reg_imm64(X64_Buffer* buf, X64_Reg dst, uint64_t imm);

/**
 * @brief Emit MOV reg64, imm32 (sign-extended)
 *
 * @param buf Code buffer
 * @param dst Destination register
 * @param imm 32-bit immediate (sign-extended to 64)
 */
void x64_mov_reg_imm32(X64_Buffer* buf, X64_Reg dst, int32_t imm);

/**
 * @brief Emit MOV reg, reg
 *
 * @param buf Code buffer
 * @param dst Destination register
 * @param src Source register
 * @param size Operand size (4 or 8)
 */
void x64_mov_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit MOV reg, [base + disp]
 *
 * @param buf Code buffer
 * @param dst Destination register
 * @param base Base register
 * @param disp Displacement
 * @param size Operand size
 */
void x64_mov_reg_mem(X64_Buffer* buf, X64_Reg dst, X64_Reg base,
                     int32_t disp, X64_Size size);

/**
 * @brief Emit MOV [base + disp], reg
 *
 * @param buf Code buffer
 * @param base Base register
 * @param disp Displacement
 * @param src Source register
 * @param size Operand size
 */
void x64_mov_mem_reg(X64_Buffer* buf, X64_Reg base, int32_t disp,
                     X64_Reg src, X64_Size size);

/**
 * @brief Emit MOV [base + disp], imm32
 *
 * @param buf Code buffer
 * @param base Base register
 * @param disp Displacement
 * @param imm Immediate value
 * @param size Operand size (1, 2, or 4)
 */
void x64_mov_mem_imm(X64_Buffer* buf, X64_Reg base, int32_t disp,
                     int32_t imm, X64_Size size);

/*============================================================================
 * Arithmetic Instructions
 *============================================================================*/

/**
 * @brief Emit ADD reg, reg
 */
void x64_add_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit ADD reg, imm32
 */
void x64_add_reg_imm(X64_Buffer* buf, X64_Reg dst, int32_t imm, X64_Size size);

/**
 * @brief Emit SUB reg, reg
 */
void x64_sub_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit SUB reg, imm32
 */
void x64_sub_reg_imm(X64_Buffer* buf, X64_Reg dst, int32_t imm, X64_Size size);

/**
 * @brief Emit IMUL reg, reg
 */
void x64_imul_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit IMUL reg, imm32
 */
void x64_imul_reg_imm(X64_Buffer* buf, X64_Reg dst, X64_Reg src,
                       int32_t imm, X64_Size size);

/**
 * @brief Emit IDIV reg (RDX:RAX / reg -> RAX, remainder -> RDX)
 */
void x64_idiv_reg(X64_Buffer* buf, X64_Reg divisor, X64_Size size);

/**
 * @brief Emit MUL reg (unsigned: RDX:RAX = RAX * reg)
 */
void x64_mul_reg(X64_Buffer* buf, X64_Reg src, X64_Size size);

/**
 * @brief Emit ADD reg, [base + disp]
 */
void x64_add_reg_mem(X64_Buffer* buf, X64_Reg dst, X64_Reg base,
                     int32_t disp, X64_Size size);

/**
 * @brief Emit ADC reg, reg (add with carry)
 */
void x64_adc_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit ADC reg, [base + disp] (add with carry from memory)
 */
void x64_adc_reg_mem(X64_Buffer* buf, X64_Reg dst, X64_Reg base,
                     int32_t disp, X64_Size size);

/**
 * @brief Emit ADC reg, imm32 (add with carry immediate)
 */
void x64_adc_reg_imm(X64_Buffer* buf, X64_Reg dst, int32_t imm, X64_Size size);

/**
 * @brief Emit MOVSXD reg64, reg32 (sign-extend 32-bit to 64-bit)
 */
void x64_movsxd(X64_Buffer* buf, X64_Reg dst, X64_Reg src);

/**
 * @brief Emit CQO (sign-extend RAX into RDX:RAX for division)
 */
void x64_cqo(X64_Buffer* buf);

/**
 * @brief Emit CDQ (sign-extend EAX into EDX:EAX)
 */
void x64_cdq(X64_Buffer* buf);

/**
 * @brief Emit NEG reg (two's complement)
 */
void x64_neg_reg(X64_Buffer* buf, X64_Reg reg, X64_Size size);

/*============================================================================
 * Bitwise Instructions
 *============================================================================*/

/**
 * @brief Emit AND reg, reg
 */
void x64_and_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit AND reg, imm32
 */
void x64_and_reg_imm(X64_Buffer* buf, X64_Reg dst, int32_t imm, X64_Size size);

/**
 * @brief Emit OR reg, reg
 */
void x64_or_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit XOR reg, reg
 */
void x64_xor_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit NOT reg
 */
void x64_not_reg(X64_Buffer* buf, X64_Reg reg, X64_Size size);

/**
 * @brief Emit SHL reg, CL
 */
void x64_shl_reg_cl(X64_Buffer* buf, X64_Reg reg, X64_Size size);

/**
 * @brief Emit SHL reg, imm8
 */
void x64_shl_reg_imm(X64_Buffer* buf, X64_Reg reg, uint8_t imm, X64_Size size);

/**
 * @brief Emit SHR reg, CL
 */
void x64_shr_reg_cl(X64_Buffer* buf, X64_Reg reg, X64_Size size);

/**
 * @brief Emit SHR reg, imm8
 */
void x64_shr_reg_imm(X64_Buffer* buf, X64_Reg reg, uint8_t imm, X64_Size size);

/**
 * @brief Emit SAR reg, CL (arithmetic shift right)
 */
void x64_sar_reg_cl(X64_Buffer* buf, X64_Reg reg, X64_Size size);

/**
 * @brief Emit SAR reg, imm8
 */
void x64_sar_reg_imm(X64_Buffer* buf, X64_Reg reg, uint8_t imm, X64_Size size);

/*============================================================================
 * Comparison and Test
 *============================================================================*/

/**
 * @brief Emit CMP reg, reg
 */
void x64_cmp_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit CMP reg, imm32
 */
void x64_cmp_reg_imm(X64_Buffer* buf, X64_Reg reg, int32_t imm, X64_Size size);

/**
 * @brief Emit TEST reg, reg
 */
void x64_test_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/**
 * @brief Emit SETcc reg8 (set byte based on condition)
 */
void x64_setcc(X64_Buffer* buf, X64_Condition cc, X64_Reg reg);

/**
 * @brief Emit CMOVcc reg, reg (conditional move)
 */
void x64_cmovcc(X64_Buffer* buf, X64_Condition cc, X64_Reg dst,
                X64_Reg src, X64_Size size);

/*============================================================================
 * Control Flow
 *============================================================================*/

/**
 * @brief Emit JMP rel32 (near jump)
 *
 * @param buf Code buffer
 * @param labels Label table
 * @param label_id Target label
 */
void x64_jmp_label(X64_Buffer* buf, X64_Labels* labels, uint32_t label_id);

/**
 * @brief Emit Jcc rel32 (conditional jump)
 *
 * @param buf Code buffer
 * @param cc Condition code
 * @param labels Label table
 * @param label_id Target label
 */
void x64_jcc_label(X64_Buffer* buf, X64_Condition cc,
                   X64_Labels* labels, uint32_t label_id);

/**
 * @brief Emit JMP reg (indirect jump)
 */
void x64_jmp_reg(X64_Buffer* buf, X64_Reg reg);

/**
 * @brief Emit CALL rel32
 *
 * @param buf Code buffer
 * @param labels Label table
 * @param label_id Target label
 */
void x64_call_label(X64_Buffer* buf, X64_Labels* labels, uint32_t label_id);

/**
 * @brief Emit CALL reg (indirect call)
 */
void x64_call_reg(X64_Buffer* buf, X64_Reg reg);

/**
 * @brief Emit RET
 */
void x64_ret(X64_Buffer* buf);

/**
 * @brief Emit RET imm16 (return and pop bytes)
 */
void x64_ret_imm(X64_Buffer* buf, uint16_t pop_bytes);

/*============================================================================
 * Stack Operations
 *============================================================================*/

/**
 * @brief Emit PUSH reg64
 */
void x64_push_reg(X64_Buffer* buf, X64_Reg reg);

/**
 * @brief Emit PUSH imm32 (sign-extended to 64)
 */
void x64_push_imm(X64_Buffer* buf, int32_t imm);

/**
 * @brief Emit POP reg64
 */
void x64_pop_reg(X64_Buffer* buf, X64_Reg reg);

/**
 * @brief Emit ENTER (create stack frame)
 */
void x64_enter(X64_Buffer* buf, uint16_t frame_size, uint8_t nesting_level);

/**
 * @brief Emit LEAVE (destroy stack frame)
 */
void x64_leave(X64_Buffer* buf);

/*============================================================================
 * Misc Instructions
 *============================================================================*/

/**
 * @brief Emit NOP
 */
void x64_nop(X64_Buffer* buf);

/**
 * @brief Emit multi-byte NOP (for alignment)
 *
 * @param buf Code buffer
 * @param count Number of NOP bytes
 */
void x64_nop_n(X64_Buffer* buf, int count);

/**
 * @brief Emit LEA reg, [base + disp]
 */
void x64_lea(X64_Buffer* buf, X64_Reg dst, X64_Reg base, int32_t disp);

/**
 * @brief Emit LEA reg, [base + index*scale + disp]
 */
void x64_lea_sib(X64_Buffer* buf, X64_Reg dst, X64_Reg base,
                 X64_Reg index, int scale, int32_t disp);

/**
 * @brief Emit MOVZX (zero-extend)
 */
void x64_movzx(X64_Buffer* buf, X64_Reg dst, X64_Reg src,
               X64_Size dst_size, X64_Size src_size);

/**
 * @brief Emit MOVSX (sign-extend)
 */
void x64_movsx(X64_Buffer* buf, X64_Reg dst, X64_Reg src,
               X64_Size dst_size, X64_Size src_size);

/**
 * @brief Emit INT 3 (breakpoint)
 */
void x64_int3(X64_Buffer* buf);

/**
 * @brief Emit SYSCALL
 */
void x64_syscall(X64_Buffer* buf);

/**
 * @brief Emit HLT
 */
void x64_hlt(X64_Buffer* buf);

/**
 * @brief Emit CLI (clear interrupts)
 */
void x64_cli(X64_Buffer* buf);

/**
 * @brief Emit STI (set interrupts)
 */
void x64_sti(X64_Buffer* buf);

/**
 * @brief Emit UD2 (undefined instruction - for SERAPH VOID panic)
 */
void x64_ud2(X64_Buffer* buf);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_X64_ENCODE_H */
