; =============================================================================
; context_switch.asm - CPU Context Save/Restore for SERAPH
;
; MC13/27: The Pulse - Preemptive Scheduler
;
; Implements the core context switching routines in x86-64 assembly.
; These routines save and restore the complete CPU state for cooperative
; and preemptive multitasking.
;
; Build with: nasm -f elf64 context_switch.asm -o context_switch.o
; =============================================================================

[BITS 64]

; Export symbols
global seraph_context_save
global seraph_context_restore
global seraph_context_switch
global seraph_context_save_fpu
global seraph_context_restore_fpu
global seraph_context_init
global seraph_context_init_kernel

; Context structure offsets (must match context.h)
%define CTX_R15      0x00
%define CTX_R14      0x08
%define CTX_R13      0x10
%define CTX_R12      0x18
%define CTX_RBX      0x20
%define CTX_RBP      0x28
%define CTX_R11      0x30
%define CTX_R10      0x38
%define CTX_R9       0x40
%define CTX_R8       0x48
%define CTX_RAX      0x50
%define CTX_RCX      0x58
%define CTX_RDX      0x60
%define CTX_RSI      0x68
%define CTX_RDI      0x70
%define CTX_RIP      0x78
%define CTX_CS       0x80
%define CTX_RFLAGS   0x88
%define CTX_RSP      0x90
%define CTX_SS       0x98
%define CTX_CR3      0xA0
%define CTX_FPU      0xA8
%define CTX_FPU_VALID (CTX_FPU + 512)
%define CTX_GEN      (CTX_FPU_VALID + 1)

; Segment selectors
%define KERNEL_CS    0x08
%define KERNEL_DS    0x10
%define USER_CS      0x1B
%define USER_DS      0x23

; =============================================================================
; seraph_context_save
;
; Saves the current CPU context into the provided structure.
; This is a cooperative save - we save all callee-saved registers,
; the return address (as RIP), and the current stack pointer.
;
; Input: RDI = pointer to Seraph_CPU_Context
; Output: Returns 0
; =============================================================================
section .text
seraph_context_save:
    ; Save callee-saved registers
    mov     [rdi + CTX_R15], r15
    mov     [rdi + CTX_R14], r14
    mov     [rdi + CTX_R13], r13
    mov     [rdi + CTX_R12], r12
    mov     [rdi + CTX_RBX], rbx
    mov     [rdi + CTX_RBP], rbp

    ; Save caller-saved registers (optional for cooperative, needed for preemptive)
    mov     [rdi + CTX_R11], r11
    mov     [rdi + CTX_R10], r10
    mov     [rdi + CTX_R9],  r9
    mov     [rdi + CTX_R8],  r8
    mov     [rdi + CTX_RAX], rax
    mov     [rdi + CTX_RCX], rcx
    mov     [rdi + CTX_RDX], rdx
    mov     [rdi + CTX_RSI], rsi
    ; RDI holds the context pointer, save after we're done using it
    push    rdi
    pop     rax
    mov     [rdi + CTX_RDI], rax

    ; Save return address as RIP (where we'll resume)
    mov     rax, [rsp]          ; Return address on stack
    mov     [rdi + CTX_RIP], rax

    ; Save stack pointer (pointing past return address)
    lea     rax, [rsp + 8]
    mov     [rdi + CTX_RSP], rax

    ; Save flags
    pushfq
    pop     rax
    mov     [rdi + CTX_RFLAGS], rax

    ; Save segment registers (kernel mode values)
    mov     rax, cs
    mov     [rdi + CTX_CS], rax
    mov     rax, ss
    mov     [rdi + CTX_SS], rax

    ; Save CR3 (page table)
    mov     rax, cr3
    mov     [rdi + CTX_CR3], rax

    ; Mark FPU state as not saved (caller can call seraph_context_save_fpu)
    mov     byte [rdi + CTX_FPU_VALID], 0

    ; Increment generation counter
    inc     qword [rdi + CTX_GEN]

    ; Return 0 to indicate this is the save path
    xor     eax, eax
    ret

; =============================================================================
; seraph_context_restore
;
; Restores CPU context from the provided structure.
; This function does NOT return - execution continues at the saved RIP.
;
; Input: RDI = pointer to Seraph_CPU_Context
; =============================================================================
seraph_context_restore:
    ; Check if we need to switch page tables
    mov     rax, [rdi + CTX_CR3]
    test    rax, rax
    jz      .skip_cr3
    mov     rcx, cr3
    cmp     rax, rcx
    je      .skip_cr3
    mov     cr3, rax
.skip_cr3:

    ; Restore FPU state if valid
    cmp     byte [rdi + CTX_FPU_VALID], 0
    je      .skip_fpu
    fxrstor [rdi + CTX_FPU]
.skip_fpu:

    ; Restore callee-saved registers
    mov     r15, [rdi + CTX_R15]
    mov     r14, [rdi + CTX_R14]
    mov     r13, [rdi + CTX_R13]
    mov     r12, [rdi + CTX_R12]
    mov     rbx, [rdi + CTX_RBX]
    mov     rbp, [rdi + CTX_RBP]

    ; Restore caller-saved registers
    mov     r11, [rdi + CTX_R11]
    mov     r10, [rdi + CTX_R10]
    mov     r9,  [rdi + CTX_R9]
    mov     r8,  [rdi + CTX_R8]
    mov     rax, [rdi + CTX_RAX]
    mov     rcx, [rdi + CTX_RCX]
    mov     rdx, [rdi + CTX_RDX]
    mov     rsi, [rdi + CTX_RSI]

    ; Load new stack pointer
    mov     rsp, [rdi + CTX_RSP]

    ; Push return address onto new stack
    push    qword [rdi + CTX_RIP]

    ; Restore flags
    push    qword [rdi + CTX_RFLAGS]
    popfq

    ; Restore RDI last (since we're using it for context pointer)
    mov     rdi, [rdi + CTX_RDI]

    ; Return to saved RIP
    ret

; =============================================================================
; seraph_context_switch
;
; Atomically saves current context and restores a new one.
; This is the core context switch operation.
;
; Input: RDI = pointer to old context (where to save current state)
;        RSI = pointer to new context (what to restore)
; =============================================================================
seraph_context_switch:
    ; ===== SAVE CURRENT CONTEXT =====

    ; Save callee-saved registers
    mov     [rdi + CTX_R15], r15
    mov     [rdi + CTX_R14], r14
    mov     [rdi + CTX_R13], r13
    mov     [rdi + CTX_R12], r12
    mov     [rdi + CTX_RBX], rbx
    mov     [rdi + CTX_RBP], rbp

    ; Save caller-saved registers
    mov     [rdi + CTX_R11], r11
    mov     [rdi + CTX_R10], r10
    mov     [rdi + CTX_R9],  r9
    mov     [rdi + CTX_R8],  r8
    mov     [rdi + CTX_RAX], rax
    mov     [rdi + CTX_RCX], rcx
    mov     [rdi + CTX_RDX], rdx
    ; Save RSI (new context pointer) after we're done using it
    mov     rax, rsi
    mov     [rdi + CTX_RSI], rax
    ; Save original RDI value (though it's the old context pointer, save it anyway)
    mov     [rdi + CTX_RDI], rdi

    ; Save return address as RIP
    mov     rax, [rsp]
    mov     [rdi + CTX_RIP], rax

    ; Save stack pointer
    lea     rax, [rsp + 8]
    mov     [rdi + CTX_RSP], rax

    ; Save flags
    pushfq
    pop     rax
    mov     [rdi + CTX_RFLAGS], rax

    ; Save segment registers
    mov     rax, cs
    mov     [rdi + CTX_CS], rax
    mov     rax, ss
    mov     [rdi + CTX_SS], rax

    ; Save CR3
    mov     rax, cr3
    mov     [rdi + CTX_CR3], rax

    ; Mark FPU as not saved (lazy FPU switching)
    mov     byte [rdi + CTX_FPU_VALID], 0

    ; Increment generation
    inc     qword [rdi + CTX_GEN]

    ; ===== RESTORE NEW CONTEXT =====

    ; Check if we need to switch page tables
    mov     rax, [rsi + CTX_CR3]
    test    rax, rax
    jz      .switch_skip_cr3
    mov     rcx, cr3
    cmp     rax, rcx
    je      .switch_skip_cr3
    mov     cr3, rax
.switch_skip_cr3:

    ; Restore FPU if valid
    cmp     byte [rsi + CTX_FPU_VALID], 0
    je      .switch_skip_fpu
    fxrstor [rsi + CTX_FPU]
.switch_skip_fpu:

    ; Restore callee-saved registers
    mov     r15, [rsi + CTX_R15]
    mov     r14, [rsi + CTX_R14]
    mov     r13, [rsi + CTX_R13]
    mov     r12, [rsi + CTX_R12]
    mov     rbx, [rsi + CTX_RBX]
    mov     rbp, [rsi + CTX_RBP]

    ; Restore caller-saved registers
    mov     r11, [rsi + CTX_R11]
    mov     r10, [rsi + CTX_R10]
    mov     r9,  [rsi + CTX_R9]
    mov     r8,  [rsi + CTX_R8]
    mov     rax, [rsi + CTX_RAX]
    mov     rcx, [rsi + CTX_RCX]
    mov     rdx, [rsi + CTX_RDX]

    ; Load new stack
    mov     rsp, [rsi + CTX_RSP]

    ; Push new return address
    push    qword [rsi + CTX_RIP]

    ; Restore flags
    push    qword [rsi + CTX_RFLAGS]
    popfq

    ; Restore RDI and RSI
    mov     rdi, [rsi + CTX_RDI]
    mov     rsi, [rsi + CTX_RSI]

    ; Return to new context
    ret

; =============================================================================
; seraph_context_save_fpu
;
; Saves the FPU/SSE state to the context.
;
; Input: RDI = pointer to Seraph_CPU_Context
; =============================================================================
seraph_context_save_fpu:
    fxsave  [rdi + CTX_FPU]
    mov     byte [rdi + CTX_FPU_VALID], 1
    ret

; =============================================================================
; seraph_context_restore_fpu
;
; Restores FPU/SSE state from the context.
;
; Input: RDI = pointer to Seraph_CPU_Context
; =============================================================================
seraph_context_restore_fpu:
    cmp     byte [rdi + CTX_FPU_VALID], 0
    je      .no_restore
    fxrstor [rdi + CTX_FPU]
.no_restore:
    ret

; =============================================================================
; seraph_context_init
;
; Initialize a context for a new thread.
;
; Input: RDI = pointer to Seraph_CPU_Context
;        RSI = entry point function
;        RDX = stack top
;        RCX = argument
;        R8  = CR3 (0 to use current)
; =============================================================================
seraph_context_init:
    ; Clear the context first
    push    rdi
    push    rcx
    mov     rcx, 0xA8           ; Size up to FPU area
    xor     eax, eax
.clear_loop:
    mov     byte [rdi], al
    inc     rdi
    loop    .clear_loop
    pop     rcx
    pop     rdi

    ; Set up for user mode (can be overridden later)
    mov     qword [rdi + CTX_CS], USER_CS
    mov     qword [rdi + CTX_SS], USER_DS

    ; Set entry point
    mov     [rdi + CTX_RIP], rsi

    ; Set up stack (aligned, with space for return address)
    sub     rdx, 8
    and     rdx, ~0xF           ; 16-byte align
    mov     [rdi + CTX_RSP], rdx

    ; Set argument (passed in RDI in System V ABI)
    mov     [rdi + CTX_RDI], rcx

    ; Set CR3
    test    r8, r8
    jnz     .use_provided_cr3
    mov     r8, cr3             ; Use current if none provided
.use_provided_cr3:
    mov     [rdi + CTX_CR3], r8

    ; Set default flags (interrupts enabled)
    mov     rax, 0x202          ; IF set, reserved bit set
    mov     [rdi + CTX_RFLAGS], rax

    ; Clear FPU valid flag
    mov     byte [rdi + CTX_FPU_VALID], 0

    ; Initialize generation counter
    mov     qword [rdi + CTX_GEN], 1

    ret

; =============================================================================
; seraph_context_init_kernel
;
; Initialize a context for a kernel thread.
;
; Input: RDI = pointer to Seraph_CPU_Context
;        RSI = entry point function
;        RDX = stack top
;        RCX = argument
; =============================================================================
seraph_context_init_kernel:
    ; Use the regular init
    xor     r8, r8              ; Use current CR3
    call    seraph_context_init

    ; Override segment selectors for kernel mode
    mov     qword [rdi + CTX_CS], KERNEL_CS
    mov     qword [rdi + CTX_SS], KERNEL_DS

    ret

; =============================================================================
; End of file
; =============================================================================
