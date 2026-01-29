/**
 * @file interrupts.c
 * @brief MC23: The Void Interceptor - Exception and IRQ Handlers
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module implements the SERAPH interrupt handling philosophy:
 * exceptions that can be recovered from inject VOID values into
 * the computational stream rather than crashing the system.
 *
 * VOID INJECTION STRATEGY:
 *
 *   #DE (Divide Error):
 *     - Set RAX = VOID_U64 (all 1s)
 *     - Set RDX = VOID_U64 (for 128-bit result)
 *     - Advance RIP past the faulting instruction
 *     - Resume execution - the VOID will propagate naturally
 *
 *   #OF (Overflow), #BR (Bound Range):
 *     - Similar: inject VOID into result register
 *     - Advance RIP and continue
 *
 *   #GP (General Protection):
 *     - Cannot recover safely - terminate the Sovereign
 *     - Record full context for debugging
 *
 *   #PF (Page Fault):
 *     - Route to VMM for demand paging
 *     - If VMM cannot handle, inject VOID
 *     - If no VMM registered, terminate Sovereign
 */

#include "seraph/interrupts.h"
#include "seraph/void.h"
#include <stdio.h>
#include <string.h>

/*============================================================================
 * Exception Information Table
 *============================================================================*/

/** Exception information for vectors 0-31 */
static const Seraph_Exception_Info g_exception_info[32] = {
    [SERAPH_EXC_DE]  = { 0,  "Divide Error",            "#DE", false, SERAPH_EXC_CLASS_RECOVERABLE },
    [SERAPH_EXC_DB]  = { 1,  "Debug",                   "#DB", false, SERAPH_EXC_CLASS_BENIGN },
    [SERAPH_EXC_NMI] = { 2,  "Non-Maskable Interrupt",  "NMI", false, SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_BP]  = { 3,  "Breakpoint",              "#BP", false, SERAPH_EXC_CLASS_BENIGN },
    [SERAPH_EXC_OF]  = { 4,  "Overflow",                "#OF", false, SERAPH_EXC_CLASS_RECOVERABLE },
    [SERAPH_EXC_BR]  = { 5,  "Bound Range Exceeded",    "#BR", false, SERAPH_EXC_CLASS_RECOVERABLE },
    [SERAPH_EXC_UD]  = { 6,  "Invalid Opcode",          "#UD", false, SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_NM]  = { 7,  "Device Not Available",    "#NM", false, SERAPH_EXC_CLASS_BENIGN },
    [SERAPH_EXC_DF]  = { 8,  "Double Fault",            "#DF", true,  SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_CSO] = { 9,  "Coprocessor Overrun",     "",    false, SERAPH_EXC_CLASS_IGNORED },
    [SERAPH_EXC_TS]  = { 10, "Invalid TSS",             "#TS", true,  SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_NP]  = { 11, "Segment Not Present",     "#NP", true,  SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_SS]  = { 12, "Stack-Segment Fault",     "#SS", true,  SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_GP]  = { 13, "General Protection",      "#GP", true,  SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_PF]  = { 14, "Page Fault",              "#PF", true,  SERAPH_EXC_CLASS_ROUTABLE },
    [SERAPH_EXC_RES] = { 15, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
    [SERAPH_EXC_MF]  = { 16, "x87 FPU Error",           "#MF", false, SERAPH_EXC_CLASS_RECOVERABLE },
    [SERAPH_EXC_AC]  = { 17, "Alignment Check",         "#AC", true,  SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_MC]  = { 18, "Machine Check",           "#MC", false, SERAPH_EXC_CLASS_FATAL },
    [SERAPH_EXC_XM]  = { 19, "SIMD FP Exception",       "#XM", false, SERAPH_EXC_CLASS_RECOVERABLE },
    [SERAPH_EXC_VE]  = { 20, "Virtualization",          "#VE", false, SERAPH_EXC_CLASS_ROUTABLE },
    [SERAPH_EXC_CP]  = { 21, "Control Protection",      "#CP", true,  SERAPH_EXC_CLASS_FATAL },
    [22]             = { 22, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
    [23]             = { 23, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
    [24]             = { 24, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
    [25]             = { 25, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
    [26]             = { 26, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
    [27]             = { 27, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
    [SERAPH_EXC_HV]  = { 28, "Hypervisor Injection",    "",    false, SERAPH_EXC_CLASS_ROUTABLE },
    [SERAPH_EXC_VC]  = { 29, "VMM Communication",       "#VC", true,  SERAPH_EXC_CLASS_ROUTABLE },
    [SERAPH_EXC_SX]  = { 30, "Security Exception",      "#SX", true,  SERAPH_EXC_CLASS_FATAL },
    [31]             = { 31, "Reserved",                "",    false, SERAPH_EXC_CLASS_IGNORED },
};

/*============================================================================
 * Handler Registration Table
 *============================================================================*/

/** Registered interrupt handlers (NULL = use default) */
static Seraph_Interrupt_Handler g_handlers[SERAPH_IDT_ENTRIES] = {0};

/** Page fault handler for VMM integration */
static Seraph_PF_Handler g_pf_handler = NULL;

/*============================================================================
 * Statistics
 *============================================================================*/

static Seraph_Int_Stats g_stats = {0};

/*============================================================================
 * Exception Information API
 *============================================================================*/

const Seraph_Exception_Info* seraph_exception_info(uint8_t vector) {
    if (vector >= 32) {
        return NULL;
    }
    return &g_exception_info[vector];
}

const char* seraph_exception_name(uint8_t vector) {
    if (vector >= 32) {
        return "Unknown";
    }
    return g_exception_info[vector].name;
}

/*============================================================================
 * Handler Registration API
 *============================================================================*/

Seraph_Interrupt_Handler seraph_int_register(uint8_t vector, Seraph_Interrupt_Handler handler) {
    Seraph_Interrupt_Handler old = g_handlers[vector];
    g_handlers[vector] = handler;
    return old;
}

Seraph_Interrupt_Handler seraph_int_get_handler(uint8_t vector) {
    return g_handlers[vector];
}

Seraph_PF_Handler seraph_pf_register(Seraph_PF_Handler handler) {
    Seraph_PF_Handler old = g_pf_handler;
    g_pf_handler = handler;
    return old;
}

/*============================================================================
 * Default Exception Handlers
 *============================================================================*/

/**
 * @brief Get instruction length for skipping faulting instruction
 *
 * This is a simplified decoder - in a real implementation, you'd
 * need a proper x86-64 instruction decoder. For now, we assume
 * common instruction lengths.
 *
 * DIV/IDIV: 2-3 bytes typically (opcode + ModRM + optional SIB/disp)
 */
static size_t get_instruction_length(uint8_t* rip) {
    /* Simple heuristic for DIV/IDIV instructions:
     *   F6 /6 = DIV r/m8
     *   F7 /6 = DIV r/m16/32/64
     *   F6 /7 = IDIV r/m8
     *   F7 /7 = IDIV r/m16/32/64
     *
     * The ModRM byte follows, and may have SIB and displacement.
     * For register operands, it's typically just opcode + ModRM = 2 bytes.
     * With REX prefix: 3 bytes.
     */

    uint8_t* p = rip;

    /* Check for REX prefix (0x40-0x4F) */
    if ((*p & 0xF0) == 0x40) {
        p++;  /* Skip REX */
    }

    /* Check for DIV/IDIV opcode */
    if (*p == 0xF6 || *p == 0xF7) {
        p++;  /* Skip opcode */

        /* Parse ModRM */
        uint8_t modrm = *p++;
        uint8_t mod = (modrm >> 6) & 0x03;
        uint8_t rm = modrm & 0x07;

        /* Check for SIB byte */
        if (mod != 0x03 && rm == 0x04) {
            p++;  /* Skip SIB */
        }

        /* Check for displacement */
        if (mod == 0x01) {
            p++;  /* 8-bit displacement */
        } else if (mod == 0x02) {
            p += 4;  /* 32-bit displacement */
        } else if (mod == 0x00 && rm == 0x05) {
            p += 4;  /* RIP-relative 32-bit displacement */
        }
    } else {
        /* Unknown instruction - default to 2 bytes */
        p += 2;
    }

    return (size_t)(p - rip);
}

/**
 * @brief Handle recoverable divide error by injecting VOID
 */
static void handle_divide_error(Seraph_InterruptFrame* frame) {
    /* Record the VOID occurrence for archaeology */
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_DIV_ZERO, 0,
                       frame->rax, frame->rdx, "#DE exception");

    /* Inject VOID into result registers:
     * DIV/IDIV write to RAX (quotient) and RDX (remainder) */
    frame->rax = SERAPH_VOID_U64;
    frame->rdx = SERAPH_VOID_U64;

    /* Advance RIP past the faulting instruction */
    size_t inst_len = get_instruction_length((uint8_t*)frame->rip);
    frame->rip += inst_len;

    g_stats.void_injections++;
}

/**
 * @brief Handle overflow by injecting VOID
 */
static void handle_overflow(Seraph_InterruptFrame* frame) {
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_OVERFLOW, 0,
                       frame->rax, 0, "#OF exception");

    frame->rax = SERAPH_VOID_U64;

    /* INTO is a 1-byte instruction */
    frame->rip += 1;

    g_stats.void_injections++;
}

/**
 * @brief Handle bound range exceeded by injecting VOID
 */
static void handle_bound_range(Seraph_InterruptFrame* frame) {
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_OUT_OF_BOUNDS, 0,
                       frame->rax, 0, "#BR exception");

    frame->rax = SERAPH_VOID_U64;

    /* BOUND is typically 2-6 bytes; use decoder */
    frame->rip += 2;  /* Simplified */

    g_stats.void_injections++;
}

/**
 * @brief Handle x87 FPU error by injecting VOID
 */
static void handle_fpu_error(Seraph_InterruptFrame* frame) {
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_OVERFLOW, 0,
                       0, 0, "#MF x87 FPU exception");

    /* Clear the FPU exception */
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("fnclex");
#endif

    /* FPU state is complex; for now, just mark the exception */
    g_stats.void_injections++;

    (void)frame;
}

/**
 * @brief Handle SIMD FP error by injecting VOID
 */
static void handle_simd_error(Seraph_InterruptFrame* frame) {
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_OVERFLOW, 0,
                       0, 0, "#XM SIMD exception");

    /* Read MXCSR to get specific error */
    uint32_t mxcsr = 0;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    /* Clear exception flags */
    mxcsr &= ~0x3F;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
#endif

    g_stats.void_injections++;

    (void)frame;
}

/**
 * @brief Handle page fault
 */
static void handle_page_fault(Seraph_InterruptFrame* frame) {
    uint64_t fault_addr = seraph_get_cr2();
    uint64_t error_code = frame->error_code;

    /* Try the registered VMM handler first */
    if (g_pf_handler != NULL) {
        Seraph_Vbit result = g_pf_handler(fault_addr, error_code, frame);
        if (seraph_vbit_is_true(result)) {
            /* VMM handled it (demand paging, COW, etc.) - resume */
            return;
        }
    }

    /* VMM couldn't handle it - this is fatal for the Sovereign */
    fprintf(stderr, "SERAPH FATAL: Unhandled page fault\n");
    fprintf(stderr, "  Fault address: 0x%016llx\n", (unsigned long long)fault_addr);
    fprintf(stderr, "  Error code: 0x%llx", (unsigned long long)error_code);
    if (error_code & SERAPH_PF_PRESENT) fprintf(stderr, " PRESENT");
    if (error_code & SERAPH_PF_WRITE) fprintf(stderr, " WRITE");
    if (error_code & SERAPH_PF_USER) fprintf(stderr, " USER");
    if (error_code & SERAPH_PF_FETCH) fprintf(stderr, " FETCH");
    fprintf(stderr, "\n");

    seraph_int_dump_frame(frame);

    g_stats.sovereign_kills++;

    /* In a real kernel, we'd terminate the Sovereign here.
     * For now, we hang (in kernel) or abort (in userspace simulation). */
#ifdef SERAPH_KERNEL
    while (1) { __asm__ volatile("hlt"); }
#else
    /* Record in VOID tracking before exit */
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_NULL_PTR, 0,
                       fault_addr, error_code, "fatal page fault");
#endif
}

/**
 * @brief Handle fatal exception - terminate Sovereign
 */
static void handle_fatal_exception(Seraph_InterruptFrame* frame) {
    const Seraph_Exception_Info* info = seraph_exception_info((uint8_t)frame->vector);

    fprintf(stderr, "SERAPH FATAL: %s (%s)\n",
            info ? info->name : "Unknown",
            info ? info->mnemonic : "");
    fprintf(stderr, "  Error code: 0x%llx\n", (unsigned long long)frame->error_code);

    seraph_int_dump_frame(frame);

    g_stats.sovereign_kills++;

    /* Record in VOID tracking */
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_UNKNOWN, 0,
                       frame->vector, frame->error_code,
                       info ? info->name : "fatal exception");

#ifdef SERAPH_KERNEL
    while (1) { __asm__ volatile("hlt"); }
#endif
}

/*============================================================================
 * Default IRQ Handlers
 *============================================================================*/

/**
 * @brief Handle timer IRQ (IRQ0)
 */
static void handle_timer_irq(Seraph_InterruptFrame* frame) {
    (void)frame;

    /* Would update Chronon, schedule next Strand, etc. */
    g_stats.irq_count[0]++;

    /* Send EOI */
    seraph_pic_eoi(0);
}

/**
 * @brief Handle keyboard IRQ (IRQ1)
 */
static void handle_keyboard_irq(Seraph_InterruptFrame* frame) {
    (void)frame;

    /* Read scancode from port 0x60 to clear the interrupt */
#if defined(__GNUC__) || defined(__clang__)
    uint8_t scancode;
    __asm__ volatile("inb $0x60, %0" : "=a"(scancode));
    (void)scancode;  /* Would dispatch to input handler */
#endif

    g_stats.irq_count[1]++;
    seraph_pic_eoi(1);
}

/**
 * @brief Handle spurious IRQ (IRQ7 or IRQ15)
 */
static void handle_spurious_irq(Seraph_InterruptFrame* frame) {
    (void)frame;

    /* Don't send EOI for spurious IRQs */
    g_stats.spurious_count++;
}

/**
 * @brief Generic IRQ handler
 */
static void handle_generic_irq(Seraph_InterruptFrame* frame) {
    uint8_t irq = (uint8_t)(frame->vector - SERAPH_IRQ_BASE);

    if (irq < 16) {
        g_stats.irq_count[irq]++;
    }

    /* Send EOI */
    seraph_pic_eoi(irq);
}

/*============================================================================
 * Central Interrupt Dispatcher
 *============================================================================*/

/**
 * @brief Central interrupt dispatcher
 *
 * Called from assembly stub for all interrupts.
 */
void seraph_int_dispatch(Seraph_InterruptFrame* frame) {
    uint64_t vector = frame->vector;

    g_stats.total_interrupts++;

    /* Check for registered handler first */
    if (vector < SERAPH_IDT_ENTRIES && g_handlers[vector] != NULL) {
        g_handlers[vector](frame);
        return;
    }

    /* Default handling based on vector type */

    /* Exceptions (0-31) */
    if (vector < 32) {
        g_stats.exception_count[vector]++;

        const Seraph_Exception_Info* info = seraph_exception_info((uint8_t)vector);

        switch (info->class) {
            case SERAPH_EXC_CLASS_RECOVERABLE:
                switch (vector) {
                    case SERAPH_EXC_DE:
                        handle_divide_error(frame);
                        return;
                    case SERAPH_EXC_OF:
                        handle_overflow(frame);
                        return;
                    case SERAPH_EXC_BR:
                        handle_bound_range(frame);
                        return;
                    case SERAPH_EXC_MF:
                        handle_fpu_error(frame);
                        return;
                    case SERAPH_EXC_XM:
                        handle_simd_error(frame);
                        return;
                    default:
                        /* Shouldn't reach here */
                        break;
                }
                break;

            case SERAPH_EXC_CLASS_ROUTABLE:
                if (vector == SERAPH_EXC_PF) {
                    handle_page_fault(frame);
                    return;
                }
                /* Other routable exceptions fall through to fatal */
                break;

            case SERAPH_EXC_CLASS_BENIGN:
                /* Debug, breakpoint - just return */
                return;

            case SERAPH_EXC_CLASS_FATAL:
            case SERAPH_EXC_CLASS_IGNORED:
            default:
                break;
        }

        /* Fatal or unhandled exception */
        handle_fatal_exception(frame);
        return;
    }

    /* Hardware IRQs (32-47) */
    if (vector >= SERAPH_IRQ_BASE && vector < SERAPH_IRQ_BASE + SERAPH_IRQ_COUNT) {
        uint8_t irq = (uint8_t)(vector - SERAPH_IRQ_BASE);

        switch (irq) {
            case 0:
                handle_timer_irq(frame);
                return;
            case 1:
                handle_keyboard_irq(frame);
                return;
            case 7:
            case 15:
                /* Check for spurious interrupt */
                handle_spurious_irq(frame);
                return;
            default:
                handle_generic_irq(frame);
                return;
        }
    }

    /* Software interrupts (48+) - no default handling */
}

/*============================================================================
 * Statistics API
 *============================================================================*/

const Seraph_Int_Stats* seraph_int_stats(void) {
    return &g_stats;
}

void seraph_int_stats_reset(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}

/*============================================================================
 * Debug Helpers
 *============================================================================*/

void seraph_int_dump_frame(const Seraph_InterruptFrame* frame) {
    fprintf(stderr, "=== Interrupt Frame ===\n");
    fprintf(stderr, "Vector: %llu  Error Code: 0x%llx\n",
            (unsigned long long)frame->vector,
            (unsigned long long)frame->error_code);
    fprintf(stderr, "RIP: 0x%016llx  CS: 0x%04llx  RFLAGS: 0x%016llx\n",
            (unsigned long long)frame->rip,
            (unsigned long long)frame->cs,
            (unsigned long long)frame->rflags);
    fprintf(stderr, "RSP: 0x%016llx  SS: 0x%04llx\n",
            (unsigned long long)frame->rsp,
            (unsigned long long)frame->ss);
    fprintf(stderr, "RAX: 0x%016llx  RBX: 0x%016llx  RCX: 0x%016llx\n",
            (unsigned long long)frame->rax,
            (unsigned long long)frame->rbx,
            (unsigned long long)frame->rcx);
    fprintf(stderr, "RDX: 0x%016llx  RSI: 0x%016llx  RDI: 0x%016llx\n",
            (unsigned long long)frame->rdx,
            (unsigned long long)frame->rsi,
            (unsigned long long)frame->rdi);
    fprintf(stderr, "RBP: 0x%016llx  R8:  0x%016llx  R9:  0x%016llx\n",
            (unsigned long long)frame->rbp,
            (unsigned long long)frame->r8,
            (unsigned long long)frame->r9);
    fprintf(stderr, "R10: 0x%016llx  R11: 0x%016llx  R12: 0x%016llx\n",
            (unsigned long long)frame->r10,
            (unsigned long long)frame->r11,
            (unsigned long long)frame->r12);
    fprintf(stderr, "R13: 0x%016llx  R14: 0x%016llx  R15: 0x%016llx\n",
            (unsigned long long)frame->r13,
            (unsigned long long)frame->r14,
            (unsigned long long)frame->r15);
    fprintf(stderr, "=======================\n");
}
