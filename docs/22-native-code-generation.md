# MC27: Native Code Generation - Multi-Architecture Backend

## Overview

SERAPH's native compiler generates machine code for three architectures without external toolchains:

- **x86-64** (x64): Primary desktop/server target
- **ARM64** (AArch64): Mobile and Apple Silicon
- **RISC-V** (RV64IMAC): Open hardware target

This is **Not Invented Here** in action: no LLVM, no GCC, no external assemblers. Pure SERAPH.

## Plain English Explanation

When you run `seraphic hello.srph`, the compiler:

1. Parses your code into an AST
2. Converts AST to Celestial IR (architecture-neutral)
3. Lowers Celestial IR to machine code bytes
4. Wraps the bytes in an ELF executable

Step 3 is what this document covers. Each architecture has:
- An **encoder** that knows how to format instructions
- A **lowering** module that translates IR to native instructions

## Architecture: x86-64 Backend

### x64_encode.c - The Instruction Encoder

The encoder produces raw x86-64 bytes. Key concepts:

#### REX Prefix
```c
uint8_t x64_rex(uint8_t W, uint8_t R, uint8_t X, uint8_t B) {
    return 0x40 | (W << 3) | (R << 2) | (X << 1) | B;
}
```
- **W**: 64-bit operand size
- **R**: Extend ModR/M reg field to access R8-R15
- **X**: Extend SIB index field
- **B**: Extend ModR/M r/m or SIB base field

#### ModR/M Byte
```c
uint8_t x64_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    return (mod << 6) | ((reg & 7) << 3) | (rm & 7);
}
```
- **mod**: 00=memory, 01=mem+disp8, 10=mem+disp32, 11=register
- **reg**: Register or opcode extension
- **rm**: Register/memory operand

#### SIB Byte (for complex addressing)
```c
uint8_t x64_sib(uint8_t scale, uint8_t index, uint8_t base) {
    return (scale << 6) | ((index & 7) << 3) | (base & 7);
}
```

### Register Mapping

```c
typedef enum {
    X64_RAX = 0,  X64_RCX = 1,  X64_RDX = 2,  X64_RBX = 3,
    X64_RSP = 4,  X64_RBP = 5,  X64_RSI = 6,  X64_RDI = 7,
    X64_R8  = 8,  X64_R9  = 9,  X64_R10 = 10, X64_R11 = 11,
    X64_R12 = 12, X64_R13 = 13, X64_R14 = 14, X64_R15 = 15,
    X64_NONE = 255
} X64_Reg;
```

### Calling Convention (System V AMD64 ABI)

Arguments: RDI, RSI, RDX, RCX, R8, R9 (integers)
Return value: RAX
Caller-saved: RAX, RCX, RDX, RSI, RDI, R8-R11
Callee-saved: RBX, RBP, R12-R15

### Key Encoding Functions

```c
/* Move immediate to register */
void x64_mov_reg_imm64(X64_Buffer* buf, X64_Reg reg, uint64_t imm);

/* Move register to register */
void x64_mov_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/* Add register to register */
void x64_add_reg_reg(X64_Buffer* buf, X64_Reg dst, X64_Reg src, X64_Size size);

/* Compare register to immediate */
void x64_cmp_reg_imm(X64_Buffer* buf, X64_Reg reg, int32_t imm, X64_Size size);

/* Conditional jump */
void x64_jcc(X64_Buffer* buf, X64_Condition cc, int32_t offset);

/* Call relative */
void x64_call_rel32(X64_Buffer* buf, int32_t offset);

/* UD2 - Used for SERAPH VOID panic */
void x64_ud2(X64_Buffer* buf);
```

### Label Management

```c
typedef struct {
    uint32_t id;
    size_t   offset;      /* Position in code buffer (SIZE_MAX if undefined) */
} X64_Label_Entry;

uint32_t x64_label_create(X64_Labels* labels);
void x64_label_define(X64_Labels* labels, X64_Buffer* buf, uint32_t label_id);
void x64_label_resolve_all(X64_Labels* labels, X64_Buffer* buf);
```

Fixups are recorded when jumps reference undefined labels, then resolved after all code is emitted.

### VOID Checking Sequences

The x64 backend generates specific sequences for VOID operations:

#### VOID Test (bt rax, 63)
```asm
; Test if value in RAX is VOID (bit 63 set = sign bit)
bt   rax, 63       ; Test bit 63
jc   is_void       ; Jump if carry (bit was set)
```

#### VOID Assert (panic if VOID)
```asm
bt   rax, 63       ; Test bit 63
jnc  not_void      ; Jump if not carry
ud2                ; Undefined instruction = panic
not_void:
```

#### VOID Propagation (return VOID from function)
```asm
bt   rax, 63       ; Test bit 63
jnc  not_void      ; Jump if not VOID
; Load VOID constant
mov  rax, 0xFFFFFFFFFFFFFFFF
; Fall through to epilogue
```

## Architecture: ARM64 Backend

### arm64_encode.c - The Instruction Encoder

ARM64 uses fixed-width 32-bit instructions. Much simpler than x86!

#### Instruction Formats

```c
/* Data processing - immediate */
uint32_t arm64_dp_imm(uint8_t sf, uint8_t op, uint8_t s,
                       uint16_t imm12, uint8_t rn, uint8_t rd);

/* Data processing - register */
uint32_t arm64_dp_reg(uint8_t sf, uint8_t op, uint8_t s,
                       uint8_t rm, uint8_t rn, uint8_t rd);

/* Load/Store with unsigned offset */
uint32_t arm64_ldst_imm(uint8_t size, uint8_t v, uint8_t opc,
                         uint16_t imm12, uint8_t rn, uint8_t rt);
```

### Register Naming

```c
typedef enum {
    ARM64_X0 = 0, ARM64_X1, ARM64_X2, ..., ARM64_X30,
    ARM64_XZR = 31,   /* Zero register */
    ARM64_SP = 31,    /* Stack pointer (context-dependent) */
    ARM64_LR = 30,    /* Link register (X30) */
} ARM64_Reg;
```

### 64-bit Immediate Loading

ARM64 can only encode limited immediates directly. For full 64-bit values:

```c
void arm64_mov_imm64(ARM64_Buffer* buf, ARM64_Reg rd, uint64_t imm) {
    /* MOVZ for lowest 16 bits, MOVK for higher chunks */
    arm64_movz(buf, rd, imm & 0xFFFF, 0);
    if (imm > 0xFFFF)
        arm64_movk(buf, rd, (imm >> 16) & 0xFFFF, 1);
    if (imm > 0xFFFFFFFF)
        arm64_movk(buf, rd, (imm >> 32) & 0xFFFF, 2);
    if (imm > 0xFFFFFFFFFFFF)
        arm64_movk(buf, rd, (imm >> 48) & 0xFFFF, 3);
}
```

### Conditional Operations

ARM64 uses condition codes for conditional select:

```c
/* CSEL - Conditional select */
uint32_t arm64_csel(ARM64_Reg rd, ARM64_Reg rn, ARM64_Reg rm,
                     ARM64_Cond cond);

/* CSET - Set register to 1 if condition true, 0 otherwise */
uint32_t arm64_cset(ARM64_Reg rd, ARM64_Cond cond);
```

### Key Encoding Functions

```c
/* ADD immediate */
void arm64_add_imm(ARM64_Buffer* buf, ARM64_Reg rd, ARM64_Reg rn,
                    uint16_t imm, uint8_t shift);

/* SUB register */
void arm64_sub_reg(ARM64_Buffer* buf, ARM64_Reg rd, ARM64_Reg rn,
                    ARM64_Reg rm);

/* Load register */
void arm64_ldr(ARM64_Buffer* buf, ARM64_Reg rt, ARM64_Reg rn,
                int16_t offset);

/* Branch to label */
void arm64_b(ARM64_Buffer* buf, int32_t offset);

/* Branch with link (call) */
void arm64_bl(ARM64_Buffer* buf, int32_t offset);

/* Return */
void arm64_ret(ARM64_Buffer* buf);

/* System call */
void arm64_svc(ARM64_Buffer* buf, uint16_t imm);

/* Breakpoint (for VOID panic) */
void arm64_brk(ARM64_Buffer* buf, uint16_t imm);
```

## Architecture: RISC-V Backend

### riscv_encode.c - The Instruction Encoder

RISC-V also uses 32-bit instructions (in the base integer ISA). We target RV64IMAC:
- RV64I: Base 64-bit integer
- M: Multiply/Divide extension
- A: Atomic operations
- C: Compressed instructions (optional, not used)

#### Instruction Formats

```c
/* R-Type: register-register operations */
uint32_t rv_r_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                    uint8_t rs1, uint8_t rs2, uint8_t funct7);

/* I-Type: immediate operations */
uint32_t rv_i_type(uint8_t opcode, uint8_t rd, uint8_t funct3,
                    uint8_t rs1, int16_t imm);

/* S-Type: stores */
uint32_t rv_s_type(uint8_t opcode, uint8_t funct3,
                    uint8_t rs1, uint8_t rs2, int16_t imm);

/* B-Type: branches */
uint32_t rv_b_type(uint8_t opcode, uint8_t funct3,
                    uint8_t rs1, uint8_t rs2, int16_t imm);

/* J-Type: jumps */
uint32_t rv_j_type(uint8_t opcode, uint8_t rd, int32_t imm);

/* U-Type: upper immediate */
uint32_t rv_u_type(uint8_t opcode, uint8_t rd, int32_t imm);
```

### Register Naming

```c
typedef enum {
    RV_ZERO = 0,   /* Always zero */
    RV_RA = 1,     /* Return address */
    RV_SP = 2,     /* Stack pointer */
    RV_GP = 3,     /* Global pointer */
    RV_TP = 4,     /* Thread pointer */
    RV_T0 = 5, RV_T1 = 6, RV_T2 = 7,   /* Temporaries */
    RV_S0 = 8, RV_S1 = 9,              /* Saved registers */
    RV_A0 = 10, RV_A1 = 11, ..., RV_A7 = 17,  /* Arguments */
    RV_S2 = 18, ..., RV_S11 = 27,      /* More saved */
    RV_T3 = 28, ..., RV_T6 = 31,       /* More temporaries */
} RV_Reg;
```

### 64-bit Immediate Loading

RISC-V uses LUI/AUIPC plus ADDI/ORI:

```c
void rv_li(RV_Buffer* buf, RV_Reg rd, int64_t imm) {
    /* For values > 32 bits, need multiple instructions */
    if (fits_i32(imm)) {
        rv_lui(buf, rd, (imm + 0x800) >> 12);
        rv_addi(buf, rd, rd, imm & 0xFFF);
    } else {
        /* Full 64-bit sequence */
        rv_lui(buf, rd, (imm >> 32 + 0x800) >> 12);
        rv_addi(buf, rd, rd, (imm >> 32) & 0xFFF);
        rv_slli(buf, rd, rd, 12);
        rv_ori(buf, rd, rd, (imm >> 20) & 0xFFF);
        rv_slli(buf, rd, rd, 12);
        rv_ori(buf, rd, rd, (imm >> 8) & 0xFFF);
        rv_slli(buf, rd, rd, 8);
        rv_ori(buf, rd, rd, imm & 0xFF);
    }
}
```

### M Extension (Multiply/Divide)

```c
void rv_mul(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, RV_Reg rs2);
void rv_mulh(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, RV_Reg rs2);
void rv_div(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, RV_Reg rs2);
void rv_divu(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, RV_Reg rs2);
void rv_rem(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, RV_Reg rs2);
void rv_remu(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, RV_Reg rs2);
```

### Key Encoding Functions

```c
/* ADD */
void rv_add(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, RV_Reg rs2);

/* ADDI (add immediate) */
void rv_addi(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, int16_t imm);

/* Load doubleword */
void rv_ld(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, int16_t offset);

/* Store doubleword */
void rv_sd(RV_Buffer* buf, RV_Reg rs1, RV_Reg rs2, int16_t offset);

/* Branch if equal */
void rv_beq(RV_Buffer* buf, RV_Reg rs1, RV_Reg rs2, int16_t offset);

/* Jump and link (call) */
void rv_jal(RV_Buffer* buf, RV_Reg rd, int32_t offset);

/* Jump and link register (indirect call/return) */
void rv_jalr(RV_Buffer* buf, RV_Reg rd, RV_Reg rs1, int16_t offset);

/* Environment call (syscall) */
void rv_ecall(RV_Buffer* buf);

/* Environment break (for VOID panic) */
void rv_ebreak(RV_Buffer* buf);
```

## IR Lowering (celestial_to_*.c)

Each backend has a lowering module that translates Celestial IR to native instructions.

### Common Pattern: Lowering Arithmetic

```c
Seraph_Vbit x64_lower_arithmetic(X64_CompileContext* ctx, Celestial_Instr* instr) {
    X64_Buffer* buf = ctx->output;

    /* Load operands */
    x64_load_value(ctx, instr->operands[0], X64_RAX);
    x64_load_value(ctx, instr->operands[1], X64_RCX);

    switch (instr->opcode) {
        case CIR_ADD:
            x64_add_reg_reg(buf, X64_RAX, X64_RCX, X64_SZ_64);
            break;
        case CIR_SUB:
            x64_sub_reg_reg(buf, X64_RAX, X64_RCX, X64_SZ_64);
            break;
        case CIR_MUL:
            x64_imul_reg_reg(buf, X64_RAX, X64_RCX, X64_SZ_64);
            break;
        case CIR_DIV:
            /* Must handle division by zero → VOID */
            x64_emit_void_div(ctx, instr);
            break;
        ...
    }

    /* Store result */
    if (instr->result) {
        x64_store_value(ctx, X64_RAX, instr->result);
    }

    return SERAPH_VBIT_TRUE;
}
```

### Common Pattern: Lowering Control Flow

```c
Seraph_Vbit x64_lower_control_flow(X64_CompileContext* ctx, Celestial_Instr* instr) {
    switch (instr->opcode) {
        case CIR_JUMP:
            x64_jmp_label(ctx->output, ctx->labels,
                          get_or_create_block_label(ctx, instr->target1));
            break;

        case CIR_BRANCH:
            /* Load condition into RAX */
            x64_load_value(ctx, instr->operands[0], X64_RAX);
            /* Compare to TRUE (1) */
            x64_cmp_reg_imm(ctx->output, X64_RAX, 1, X64_SZ_8);
            /* Jump if equal */
            x64_jcc_label(ctx->output, X64_CC_E, ctx->labels,
                          get_or_create_block_label(ctx, instr->target1));
            /* Fall through to else */
            x64_jmp_label(ctx->output, ctx->labels,
                          get_or_create_block_label(ctx, instr->target2));
            break;

        case CIR_CALL:
            /* Push arguments, emit CALL, pop arguments */
            ...
            break;

        case CIR_RETURN:
            /* Load return value to RAX, emit epilogue */
            if (instr->operands[0])
                x64_load_value(ctx, instr->operands[0], X64_RAX);
            x64_emit_epilogue(ctx);
            break;
    }
}
```

### Register Allocation

The backend uses a simple linear scan register allocator:

1. Compute live intervals for each virtual register
2. Assign physical registers greedily
3. Spill to stack when registers are exhausted

```c
typedef struct {
    uint32_t vreg_id;
    X64_Reg  phys_reg;     /* Assigned physical register or X64_NONE */
    int32_t  spill_offset; /* Stack offset if spilled */
    uint32_t start, end;   /* Live interval */
} X64_Interval;
```

## Function Prologue/Epilogue

### x64 Prologue
```asm
push rbp
mov  rbp, rsp
sub  rsp, frame_size    ; Allocate locals
; Save callee-saved registers if used
```

### x64 Epilogue
```asm
; Restore callee-saved registers
add  rsp, frame_size
pop  rbp
ret
```

### ARM64 Prologue
```asm
stp  x29, x30, [sp, #-16]!    ; Push frame pointer and link register
mov  x29, sp                   ; Set frame pointer
sub  sp, sp, #frame_size      ; Allocate locals
```

### ARM64 Epilogue
```asm
add  sp, sp, #frame_size
ldp  x29, x30, [sp], #16      ; Pop frame pointer and link register
ret
```

### RISC-V Prologue
```asm
addi sp, sp, -frame_size
sd   ra, offset(sp)           ; Save return address
sd   s0, offset(sp)           ; Save frame pointer
addi s0, sp, frame_size       ; Set frame pointer
```

### RISC-V Epilogue
```asm
ld   ra, offset(sp)           ; Restore return address
ld   s0, offset(sp)           ; Restore frame pointer
addi sp, sp, frame_size
ret
```

## VOID Panic Instruction

Each architecture has a panic instruction used for VOID assertion failures:

| Architecture | Instruction | Description |
|-------------|-------------|-------------|
| x86-64 | `ud2` | Undefined instruction, triggers #UD |
| ARM64 | `brk #0` | Breakpoint, triggers debug exception |
| RISC-V | `ebreak` | Environment break, triggers exception |

## Compilation Flow

```
celestial_compile_function(fn, mod, output, labels, arena, mod_ctx)
    │
    ├── x64_regalloc_init()           # Initialize register allocator
    ├── x64_allocate_registers()       # Assign registers to vregs
    ├── x64_emit_prologue()           # Function prologue
    │
    ├── for each block:
    │       x64_label_define()         # Define block label
    │       for each instruction:
    │           x64_lower_instruction()
    │               ├── x64_lower_arithmetic()
    │               ├── x64_lower_bitwise()
    │               ├── x64_lower_comparison()
    │               ├── x64_lower_control_flow()
    │               ├── x64_lower_void_op()
    │               ├── x64_lower_capability_op()
    │               ├── x64_lower_memory_op()
    │               └── x64_lower_conversion()
    │
    └── x64_label_resolve_all()        # Patch jump offsets
```

## Call Fixups

Function calls are initially emitted with placeholder offsets:

```c
/* CALL rel32 with placeholder */
x64_emit_byte(buf, 0xE8);
size_t fixup_offset = buf->size;
x64_emit_dword(buf, 0);  /* Placeholder */

/* Record fixup for later */
fixup->call_site = fixup_offset;
fixup->callee = callee;
```

After all functions are compiled:

```c
for (size_t i = 0; i < mod_ctx->call_fixup_count; i++) {
    X64_CallFixup* fixup = &mod_ctx->call_fixups[i];
    int32_t offset = target_offset - (fixup->call_site + 4);
    patch_dword(buf, fixup->call_site, offset);
}
```

## Summary

SERAPH's native code generation is a complete, self-contained compilation backend:

1. **No external dependencies**: Pure C, no LLVM, no GCC
2. **Multi-architecture**: x64, ARM64, RISC-V from the same IR
3. **SERAPH-aware**: VOID checking, capability bounds, effect tracking
4. **Production-quality**: Proper calling conventions, register allocation, fixups

The code is designed for clarity over optimization - it generates correct code that respects all SERAPH invariants. Future work may add optimization passes, but correctness comes first.

## Source Files

| File | Description |
|------|-------------|
| `src/seraphim/celestial_to_x64.c` | IR lowering to x86-64 |
| `src/seraphim/celestial_to_arm64.c` | IR lowering to ARM64 |
| `src/seraphim/celestial_to_riscv.c` | IR lowering to RISC-V |
| `src/seraphim/x64_encode.c` | x86-64 instruction encoding |
| `src/seraphim/arm64_encode.c` | ARM64 instruction encoding |
| `src/seraphim/riscv_encode.c` | RISC-V instruction encoding |
| `include/seraph/seraphim/x64_encode.h` | x86-64 encoder API |
| `include/seraph/seraphim/arm64_encode.h` | ARM64 encoder API |
| `include/seraph/seraphim/riscv_encode.h` | RISC-V encoder API |
