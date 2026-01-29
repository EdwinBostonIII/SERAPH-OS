;===============================================================================
; @file idt.asm
; @brief MC23: The Void Interceptor - Assembly Interrupt Stubs
;
; SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
;
; This file contains the low-level assembly stubs that serve as entry points
; for all interrupt vectors. These stubs:
;   1. Save all registers (create Seraph_InterruptFrame)
;   2. Push the vector number
;   3. Push a dummy error code (for vectors that don't push one)
;   4. Call the C dispatcher (seraph_int_dispatch)
;   5. Restore registers
;   6. Return from interrupt (IRETQ)
;
; CALLING CONVENTION:
;
; When an interrupt occurs, the CPU pushes (in order):
;   - SS, RSP (if privilege change)
;   - RFLAGS
;   - CS
;   - RIP
;   - Error code (for some exceptions)
;
; We need to create a consistent stack frame for the C handler.
;
; EXCEPTIONS WITH ERROR CODES:
;   8  (#DF) - Double Fault
;   10 (#TS) - Invalid TSS
;   11 (#NP) - Segment Not Present
;   12 (#SS) - Stack-Segment Fault
;   13 (#GP) - General Protection Fault
;   14 (#PF) - Page Fault
;   17 (#AC) - Alignment Check
;   21 (#CP) - Control Protection
;   29 (#VC) - VMM Communication
;   30 (#SX) - Security Exception
;===============================================================================

bits 64
section .text

;===============================================================================
; External C function
;===============================================================================
extern seraph_int_dispatch

;===============================================================================
; Common interrupt handler - called after stub pushes vector and error code
;===============================================================================
seraph_isr_common:
    ; Save all general purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp       ; Note: we skip RSP since it's already in the frame
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to interrupt frame as first argument (RDI in System V ABI)
    mov rdi, rsp

    ; Save frame pointer to callee-saved register BEFORE alignment
    ; CRITICAL: RDI is caller-saved (can be clobbered by C function)
    ;           R12 is callee-saved (preserved across function calls)
    ; If we don't do this, the C function may clobber RDI and we lose
    ; our ability to restore RSP, causing a triple fault.
    mov r12, rsp

    ; Ensure 16-byte stack alignment before call (ABI requirement)
    ; RSP should be 16-byte aligned after call pushes return address
    ; Our frame is 184 bytes (23 * 8), which is not 16-byte aligned
    ; We need to align it
    and rsp, ~0xF

    ; Call C dispatcher
    call seraph_int_dispatch

    ; Restore RSP from callee-saved R12 (NOT RDI which may be clobbered)
    mov rsp, r12

    ; Restore all general purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Remove vector and error code from stack
    add rsp, 16

    ; Return from interrupt
    iretq

;===============================================================================
; Macro for exception stubs WITHOUT error code
; Pushes dummy error code (0) then vector number
;===============================================================================
%macro ISR_NO_ERROR 1
global seraph_isr_stub_%1
seraph_isr_stub_%1:
    push qword 0            ; Dummy error code
    push qword %1           ; Vector number
    jmp seraph_isr_common
%endmacro

;===============================================================================
; Macro for exception stubs WITH error code
; CPU already pushed error code, just push vector number
;===============================================================================
%macro ISR_WITH_ERROR 1
global seraph_isr_stub_%1
seraph_isr_stub_%1:
    push qword %1           ; Vector number (error code already on stack)
    jmp seraph_isr_common
%endmacro

;===============================================================================
; Exception stubs (vectors 0-31)
;===============================================================================

; #DE - Divide Error (no error code)
ISR_NO_ERROR 0

; #DB - Debug Exception (no error code)
ISR_NO_ERROR 1

; NMI - Non-Maskable Interrupt (no error code)
ISR_NO_ERROR 2

; #BP - Breakpoint (no error code)
ISR_NO_ERROR 3

; #OF - Overflow (no error code)
ISR_NO_ERROR 4

; #BR - Bound Range Exceeded (no error code)
ISR_NO_ERROR 5

; #UD - Invalid Opcode (no error code)
ISR_NO_ERROR 6

; #NM - Device Not Available (no error code)
ISR_NO_ERROR 7

; #DF - Double Fault (HAS error code)
ISR_WITH_ERROR 8

; Coprocessor Segment Overrun - legacy (no error code)
ISR_NO_ERROR 9

; #TS - Invalid TSS (HAS error code)
ISR_WITH_ERROR 10

; #NP - Segment Not Present (HAS error code)
ISR_WITH_ERROR 11

; #SS - Stack-Segment Fault (HAS error code)
ISR_WITH_ERROR 12

; #GP - General Protection Fault (HAS error code)
ISR_WITH_ERROR 13

; #PF - Page Fault (HAS error code)
ISR_WITH_ERROR 14

; Reserved
ISR_NO_ERROR 15

; #MF - x87 Floating-Point Exception (no error code)
ISR_NO_ERROR 16

; #AC - Alignment Check (HAS error code)
ISR_WITH_ERROR 17

; #MC - Machine Check (no error code)
ISR_NO_ERROR 18

; #XM - SIMD Floating-Point Exception (no error code)
ISR_NO_ERROR 19

; #VE - Virtualization Exception (no error code)
ISR_NO_ERROR 20

; #CP - Control Protection Exception (HAS error code)
ISR_WITH_ERROR 21

; Reserved (22-27)
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27

; Hypervisor Injection Exception (no error code)
ISR_NO_ERROR 28

; #VC - VMM Communication Exception (HAS error code)
ISR_WITH_ERROR 29

; #SX - Security Exception (HAS error code)
ISR_WITH_ERROR 30

; Reserved
ISR_NO_ERROR 31

;===============================================================================
; IRQ stubs (vectors 32-47)
; Hardware IRQs never have error codes
;===============================================================================

ISR_NO_ERROR 32   ; IRQ0  - Timer
ISR_NO_ERROR 33   ; IRQ1  - Keyboard
ISR_NO_ERROR 34   ; IRQ2  - Cascade
ISR_NO_ERROR 35   ; IRQ3  - COM2
ISR_NO_ERROR 36   ; IRQ4  - COM1
ISR_NO_ERROR 37   ; IRQ5  - LPT2
ISR_NO_ERROR 38   ; IRQ6  - Floppy
ISR_NO_ERROR 39   ; IRQ7  - LPT1 / Spurious
ISR_NO_ERROR 40   ; IRQ8  - RTC
ISR_NO_ERROR 41   ; IRQ9  - ACPI
ISR_NO_ERROR 42   ; IRQ10 - Open
ISR_NO_ERROR 43   ; IRQ11 - Open
ISR_NO_ERROR 44   ; IRQ12 - PS/2 Mouse
ISR_NO_ERROR 45   ; IRQ13 - FPU
ISR_NO_ERROR 46   ; IRQ14 - Primary ATA
ISR_NO_ERROR 47   ; IRQ15 - Secondary ATA / Spurious

;===============================================================================
; Generic stub for software interrupts (48+)
; This is a placeholder - higher vectors would need specific stubs
;===============================================================================
global seraph_isr_stub_generic
seraph_isr_stub_generic:
    push qword 0            ; Dummy error code
    push qword 255          ; Use 255 as "unknown" vector
    jmp seraph_isr_common

;===============================================================================
; Helper functions for MSVC compatibility (no inline asm in x64)
;===============================================================================

; Load IDT
global _seraph_lidt
_seraph_lidt:
    lidt [rdi]
    ret

; Enable interrupts
global _seraph_sti
_seraph_sti:
    sti
    ret

; Disable interrupts
global _seraph_cli
_seraph_cli:
    cli
    ret

; Get RFLAGS
global _seraph_get_flags
_seraph_get_flags:
    pushfq
    pop rax
    ret

; Save flags and disable interrupts
global _seraph_save_disable
_seraph_save_disable:
    pushfq
    pop rax
    cli
    ret

; Restore flags
global _seraph_restore_flags
_seraph_restore_flags:
    push rdi
    popfq
    ret

; Get CR2 (page fault address)
global _seraph_get_cr2
_seraph_get_cr2:
    mov rax, cr2
    ret

; Port I/O for MSVC
global _seraph_outb
_seraph_outb:
    ; RDI = port (uint16_t), RSI = value (uint8_t)
    mov dx, di
    mov al, sil
    out dx, al
    ret

global _seraph_inb
_seraph_inb:
    ; RDI = port (uint16_t)
    mov dx, di
    xor eax, eax
    in al, dx
    ret

global _seraph_io_wait
_seraph_io_wait:
    xor eax, eax
    out 0x80, al
    ret
