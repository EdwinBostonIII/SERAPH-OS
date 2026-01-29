/**
 * @file interrupts.h
 * @brief MC23: The Void Interceptor - IDT and Interrupt Handling
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * The Void Interceptor transforms hardware exceptions into VOID semantics.
 * Instead of crashing, exceptions propagate as VOID values through the
 * computational graph, enabling graceful degradation.
 *
 * CORE PRINCIPLES:
 *
 *   1. EXCEPTIONS BECOME VOID: Division by zero returns VOID, not SIGFPE.
 *      Page faults return VOID for unmapped memory, not SIGSEGV.
 *
 *   2. SOVEREIGN ISOLATION: Each Sovereign has its own exception context.
 *      A #GP in one Sovereign cannot affect another.
 *
 *   3. VOID ARCHAEOLOGY: All exceptions are recorded in the causality
 *      tracking system for debugging.
 *
 *   4. GRACEFUL DEGRADATION: Where possible, the system continues
 *      execution with VOID values rather than crashing.
 *
 * EXCEPTION CATEGORIES:
 *
 *   RECOVERABLE: #DE (div/0), #OF, #BR - inject VOID and continue
 *   FATAL: #GP, #DF, #MC - terminate the Sovereign
 *   ROUTABLE: #PF - route to VMM for demand paging
 */

#ifndef SERAPH_INTERRUPTS_H
#define SERAPH_INTERRUPTS_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * IDT Constants
 *============================================================================*/

/** Number of IDT entries (256 possible interrupt vectors) */
#define SERAPH_IDT_ENTRIES 256

/** Number of exception vectors (0-31) */
#define SERAPH_EXCEPTION_COUNT 32

/** Kernel code segment selector (standard x86-64 value) */
#define SERAPH_KERNEL_CS 0x08

/*============================================================================
 * IDT Gate Descriptor (64-bit mode)
 *============================================================================*/

/**
 * @brief IDT gate descriptor for x86-64 long mode
 *
 * Layout (16 bytes total):
 *   [0-1]   offset_low:  Lower 16 bits of handler address
 *   [2-3]   selector:    Code segment selector
 *   [4]     ist:         Interrupt Stack Table index (bits 0-2)
 *   [5]     type_attr:   Gate type and attributes
 *   [6-7]   offset_mid:  Middle 16 bits of handler address
 *   [8-11]  offset_high: Upper 32 bits of handler address
 *   [12-15] reserved:    Must be zero
 */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;      /**< Offset bits 0-15 */
    uint16_t selector;        /**< Code segment selector */
    uint8_t  ist;             /**< Interrupt Stack Table offset (0 = don't switch) */
    uint8_t  type_attr;       /**< Type and attributes */
    uint16_t offset_mid;      /**< Offset bits 16-31 */
    uint32_t offset_high;     /**< Offset bits 32-63 */
    uint32_t reserved;        /**< Reserved, must be 0 */
} Seraph_IDT_Gate;

_Static_assert(sizeof(Seraph_IDT_Gate) == 16, "IDT gate must be 16 bytes");

/*============================================================================
 * IDTR Register Format
 *============================================================================*/

/**
 * @brief IDTR register format for LIDT instruction
 *
 * The limit is one less than the size of the IDT in bytes.
 * For 256 entries: limit = 256 * 16 - 1 = 4095
 */
typedef struct __attribute__((packed)) {
    uint16_t limit;           /**< Size of IDT minus 1 */
    uint64_t base;            /**< Linear address of IDT */
} Seraph_IDTR;

_Static_assert(sizeof(Seraph_IDTR) == 10, "IDTR must be 10 bytes");

/*============================================================================
 * Interrupt Frame
 *============================================================================*/

/**
 * @brief CPU state pushed during interrupt/exception
 *
 * This structure represents the full CPU context at the time of an interrupt.
 * The order matters: it matches what our assembly stubs push.
 *
 * Stack layout (from bottom to top, lower addresses first):
 *   - Registers pushed by our stub (r15-rax, vector, error_code)
 *   - Context pushed by CPU (rip, cs, rflags, rsp, ss)
 */
typedef struct __attribute__((packed)) {
    /* Pushed by common stub (in reverse order of push instructions) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

    /* Pushed by vector-specific stub */
    uint64_t vector;          /**< Interrupt vector number */
    uint64_t error_code;      /**< Error code (0 if none) */

    /* Pushed by CPU */
    uint64_t rip;             /**< Instruction pointer at time of interrupt */
    uint64_t cs;              /**< Code segment */
    uint64_t rflags;          /**< CPU flags */
    uint64_t rsp;             /**< Stack pointer (if privilege change) */
    uint64_t ss;              /**< Stack segment (if privilege change) */
} Seraph_InterruptFrame;

_Static_assert(sizeof(Seraph_InterruptFrame) == 176, "InterruptFrame size mismatch");

/*============================================================================
 * Exception Vector Definitions
 *============================================================================*/

/** @name CPU Exception Vectors (0-31) */
/**@{*/
#define SERAPH_EXC_DE    0    /**< #DE Divide Error (recoverable -> VOID) */
#define SERAPH_EXC_DB    1    /**< #DB Debug Exception */
#define SERAPH_EXC_NMI   2    /**< NMI Non-Maskable Interrupt */
#define SERAPH_EXC_BP    3    /**< #BP Breakpoint (INT3) */
#define SERAPH_EXC_OF    4    /**< #OF Overflow (INTO instruction) */
#define SERAPH_EXC_BR    5    /**< #BR Bound Range Exceeded */
#define SERAPH_EXC_UD    6    /**< #UD Invalid Opcode */
#define SERAPH_EXC_NM    7    /**< #NM Device Not Available (FPU) */
#define SERAPH_EXC_DF    8    /**< #DF Double Fault (fatal) */
#define SERAPH_EXC_CSO   9    /**< Coprocessor Segment Overrun (legacy) */
#define SERAPH_EXC_TS   10    /**< #TS Invalid TSS */
#define SERAPH_EXC_NP   11    /**< #NP Segment Not Present */
#define SERAPH_EXC_SS   12    /**< #SS Stack-Segment Fault */
#define SERAPH_EXC_GP   13    /**< #GP General Protection (usually fatal) */
#define SERAPH_EXC_PF   14    /**< #PF Page Fault (routable to VMM) */
#define SERAPH_EXC_RES  15    /**< Reserved */
#define SERAPH_EXC_MF   16    /**< #MF x87 Floating-Point Exception */
#define SERAPH_EXC_AC   17    /**< #AC Alignment Check */
#define SERAPH_EXC_MC   18    /**< #MC Machine Check (fatal) */
#define SERAPH_EXC_XM   19    /**< #XM SIMD Floating-Point Exception */
#define SERAPH_EXC_VE   20    /**< #VE Virtualization Exception */
#define SERAPH_EXC_CP   21    /**< #CP Control Protection Exception */
/* 22-27 Reserved */
#define SERAPH_EXC_HV   28    /**< Hypervisor Injection Exception */
#define SERAPH_EXC_VC   29    /**< VMM Communication Exception */
#define SERAPH_EXC_SX   30    /**< Security Exception */
/* 31 Reserved */
/**@}*/

/*============================================================================
 * Exception Helper Functions
 *============================================================================*/

/**
 * @brief Check if an exception pushes an error code
 *
 * Only certain exceptions push an error code onto the stack:
 * #DF, #TS, #NP, #SS, #GP, #PF, #AC, #CP, #VC, #SX
 *
 * @param vector Exception vector number
 * @return true if exception pushes error code
 */
static inline bool seraph_exc_has_error_code(uint8_t vector) {
    switch (vector) {
        case SERAPH_EXC_DF:   /* Double Fault */
        case SERAPH_EXC_TS:   /* Invalid TSS */
        case SERAPH_EXC_NP:   /* Segment Not Present */
        case SERAPH_EXC_SS:   /* Stack Segment Fault */
        case SERAPH_EXC_GP:   /* General Protection */
        case SERAPH_EXC_PF:   /* Page Fault */
        case SERAPH_EXC_AC:   /* Alignment Check */
        case SERAPH_EXC_CP:   /* Control Protection */
        case SERAPH_EXC_VC:   /* VMM Communication */
        case SERAPH_EXC_SX:   /* Security Exception */
            return true;
        default:
            return false;
    }
}

/**
 * @brief Get human-readable exception name
 *
 * @param vector Exception vector number
 * @return String name of exception, or "Unknown" if not recognized
 */
static inline const char* seraph_exc_name(uint8_t vector) {
    static const char* names[] = {
        "Divide Error",           /* 0 */
        "Debug",                  /* 1 */
        "NMI",                    /* 2 */
        "Breakpoint",             /* 3 */
        "Overflow",               /* 4 */
        "Bound Range",            /* 5 */
        "Invalid Opcode",         /* 6 */
        "Device Not Available",   /* 7 */
        "Double Fault",           /* 8 */
        "Coprocessor Segment",    /* 9 */
        "Invalid TSS",            /* 10 */
        "Segment Not Present",    /* 11 */
        "Stack Segment",          /* 12 */
        "General Protection",     /* 13 */
        "Page Fault",             /* 14 */
        "Reserved",               /* 15 */
        "x87 FPU Error",          /* 16 */
        "Alignment Check",        /* 17 */
        "Machine Check",          /* 18 */
        "SIMD FPU Error",         /* 19 */
        "Virtualization",         /* 20 */
        "Control Protection",     /* 21 */
    };
    if (vector < sizeof(names) / sizeof(names[0])) {
        return names[vector];
    }
    return "Unknown";
}

/*============================================================================
 * IRQ Definitions
 *============================================================================*/

/** @name Hardware IRQ Vectors (remapped to 32-47) */
/**@{*/
#define SERAPH_IRQ_BASE     32  /**< Base vector for hardware IRQs */
#define SERAPH_IRQ_COUNT    16  /**< Number of legacy IRQs */

#define SERAPH_IRQ_TIMER    (SERAPH_IRQ_BASE + 0)   /**< PIT Timer (IRQ0) */
#define SERAPH_IRQ_KEYBOARD (SERAPH_IRQ_BASE + 1)   /**< Keyboard (IRQ1) */
#define SERAPH_IRQ_CASCADE  (SERAPH_IRQ_BASE + 2)   /**< Cascade (IRQ2) */
#define SERAPH_IRQ_COM2     (SERAPH_IRQ_BASE + 3)   /**< COM2 (IRQ3) */
#define SERAPH_IRQ_COM1     (SERAPH_IRQ_BASE + 4)   /**< COM1 (IRQ4) */
#define SERAPH_IRQ_LPT2     (SERAPH_IRQ_BASE + 5)   /**< LPT2 (IRQ5) */
#define SERAPH_IRQ_FLOPPY   (SERAPH_IRQ_BASE + 6)   /**< Floppy (IRQ6) */
#define SERAPH_IRQ_LPT1     (SERAPH_IRQ_BASE + 7)   /**< LPT1 / Spurious (IRQ7) */
#define SERAPH_IRQ_RTC      (SERAPH_IRQ_BASE + 8)   /**< RTC (IRQ8) */
#define SERAPH_IRQ_ACPI     (SERAPH_IRQ_BASE + 9)   /**< ACPI (IRQ9) */
#define SERAPH_IRQ_OPEN1    (SERAPH_IRQ_BASE + 10)  /**< Open (IRQ10) */
#define SERAPH_IRQ_OPEN2    (SERAPH_IRQ_BASE + 11)  /**< Open (IRQ11) */
#define SERAPH_IRQ_MOUSE    (SERAPH_IRQ_BASE + 12)  /**< PS/2 Mouse (IRQ12) */
#define SERAPH_IRQ_FPU      (SERAPH_IRQ_BASE + 13)  /**< FPU (IRQ13) */
#define SERAPH_IRQ_ATA1     (SERAPH_IRQ_BASE + 14)  /**< Primary ATA (IRQ14) */
#define SERAPH_IRQ_ATA2     (SERAPH_IRQ_BASE + 15)  /**< Secondary ATA (IRQ15) */
/**@}*/

/*============================================================================
 * Gate Type Attributes
 *============================================================================*/

/** @name Gate Type Constants */
/**@{*/
/**
 * Gate type_attr format:
 *   Bit 7:     P (Present)
 *   Bits 6-5:  DPL (Descriptor Privilege Level)
 *   Bit 4:     0 (must be 0 for interrupt/trap gates)
 *   Bits 3-0:  Type (0xE = Interrupt Gate, 0xF = Trap Gate)
 */
#define SERAPH_GATE_INTERRUPT 0x8E  /**< P=1, DPL=0, Type=Interrupt Gate */
#define SERAPH_GATE_TRAP      0x8F  /**< P=1, DPL=0, Type=Trap Gate */
#define SERAPH_GATE_USER_INT  0xEE  /**< P=1, DPL=3, Type=Interrupt Gate */
#define SERAPH_GATE_USER_TRAP 0xEF  /**< P=1, DPL=3, Type=Trap Gate */
/**@}*/

/*============================================================================
 * Handler Types
 *============================================================================*/

/**
 * @brief Interrupt handler function type
 *
 * Handlers receive the full interrupt frame and can inspect/modify
 * the saved register state. The frame is on the stack, so modifications
 * affect the return context.
 *
 * @param frame Pointer to saved CPU state
 */
typedef void (*Seraph_Interrupt_Handler)(Seraph_InterruptFrame* frame);

/*============================================================================
 * Exception Information
 *============================================================================*/

/**
 * @brief Exception classification for handling strategy
 */
typedef enum {
    SERAPH_EXC_CLASS_BENIGN,      /**< Informational, resume normally */
    SERAPH_EXC_CLASS_RECOVERABLE, /**< Can inject VOID and continue */
    SERAPH_EXC_CLASS_ROUTABLE,    /**< Route to subsystem (e.g., VMM) */
    SERAPH_EXC_CLASS_FATAL,       /**< Must terminate Sovereign */
    SERAPH_EXC_CLASS_IGNORED      /**< Reserved/unused vector */
} Seraph_Exception_Class;

/**
 * @brief Detailed exception information
 */
typedef struct {
    uint8_t vector;               /**< Exception vector number */
    const char* name;             /**< Human-readable name (e.g., "Divide Error") */
    const char* mnemonic;         /**< Intel mnemonic (e.g., "#DE") */
    bool has_error_code;          /**< Does CPU push error code? */
    Seraph_Exception_Class class; /**< How to handle this exception */
} Seraph_Exception_Info;

/**
 * @brief Get information about an exception vector
 *
 * @param vector Exception vector number (0-31)
 * @return Exception info, or NULL if vector >= 32
 */
const Seraph_Exception_Info* seraph_exception_info(uint8_t vector);

/*============================================================================
 * IDT Management API
 *============================================================================*/

/**
 * @brief Initialize the Interrupt Descriptor Table
 *
 * Sets up the IDT with default handlers for all 256 vectors:
 *   - Vectors 0-31: Exception handlers
 *   - Vectors 32-47: Hardware IRQ handlers (remapped from PIC)
 *   - Vectors 48-255: Software interrupt handlers
 *
 * Also remaps the 8259 PIC to avoid conflicts with CPU exceptions.
 */
void seraph_idt_init(void);

/**
 * @brief Set an IDT gate entry
 *
 * @param vector Interrupt vector number (0-255)
 * @param handler Pointer to handler function
 * @param type_attr Gate type and attributes (e.g., SERAPH_GATE_INTERRUPT)
 * @param ist Interrupt Stack Table index (0 = no stack switch)
 */
void seraph_idt_set_gate(uint8_t vector, void* handler, uint8_t type_attr, uint8_t ist);

/**
 * @brief Load the IDT into the CPU
 *
 * Issues the LIDT instruction to load our IDT into the CPU's IDTR register.
 * This must be called after seraph_idt_init() to activate the IDT.
 */
void seraph_idt_load(void);

/**
 * @brief Get the current IDT base address
 *
 * @return Pointer to the IDT array
 */
Seraph_IDT_Gate* seraph_idt_get(void);

/*============================================================================
 * Interrupt Handler Registration API
 *============================================================================*/

/**
 * @brief Register a handler for a specific interrupt vector
 *
 * The registered handler will be called from the dispatcher when
 * the specified interrupt occurs.
 *
 * @param vector Interrupt vector number (0-255)
 * @param handler Handler function (NULL to unregister)
 * @return Previous handler for this vector, or NULL if none
 */
Seraph_Interrupt_Handler seraph_int_register(uint8_t vector, Seraph_Interrupt_Handler handler);

/**
 * @brief Get the currently registered handler for a vector
 *
 * @param vector Interrupt vector number
 * @return Registered handler, or NULL if none
 */
Seraph_Interrupt_Handler seraph_int_get_handler(uint8_t vector);

/**
 * @brief Central interrupt dispatcher
 *
 * Called by the assembly stub for all interrupts. Routes to the
 * registered handler or performs default handling.
 *
 * @param frame Pointer to saved CPU state
 */
void seraph_int_dispatch(Seraph_InterruptFrame* frame);

/*============================================================================
 * Interrupt Control API
 *============================================================================*/

/**
 * @brief Enable interrupts (STI)
 */
void seraph_int_enable(void);

/**
 * @brief Disable interrupts (CLI)
 */
void seraph_int_disable(void);

/**
 * @brief Check if interrupts are currently enabled
 *
 * @return true if interrupts are enabled, false otherwise
 */
bool seraph_int_enabled(void);

/**
 * @brief Save interrupt state and disable
 *
 * @return Previous interrupt state (for seraph_int_restore)
 */
uint64_t seraph_int_save_disable(void);

/**
 * @brief Restore previously saved interrupt state
 *
 * @param flags Value returned from seraph_int_save_disable
 */
void seraph_int_restore(uint64_t flags);

/*============================================================================
 * PIC Management API
 *============================================================================*/

/**
 * @brief Initialize and remap the 8259 PICs
 *
 * Remaps IRQ 0-7 to vectors 32-39 (PIC1) and
 * IRQ 8-15 to vectors 40-47 (PIC2).
 *
 * This avoids conflicts with CPU exception vectors 0-31.
 */
void seraph_pic_init(void);

/**
 * @brief Send End-of-Interrupt signal to PIC
 *
 * Must be called at the end of every IRQ handler.
 *
 * @param irq The IRQ number (0-15)
 */
void seraph_pic_eoi(uint8_t irq);

/**
 * @brief Mask (disable) a specific IRQ
 *
 * @param irq The IRQ number to mask (0-15)
 */
void seraph_pic_mask(uint8_t irq);

/**
 * @brief Unmask (enable) a specific IRQ
 *
 * @param irq The IRQ number to unmask (0-15)
 */
void seraph_pic_unmask(uint8_t irq);

/**
 * @brief Disable all IRQs by masking them
 */
void seraph_pic_disable_all(void);

/**
 * @brief Get the currently masked IRQs
 *
 * @return 16-bit mask (bit n = 1 means IRQ n is masked)
 */
uint16_t seraph_pic_get_mask(void);

/**
 * @brief Set the IRQ mask
 *
 * @param mask 16-bit mask (bit n = 1 to mask IRQ n)
 */
void seraph_pic_set_mask(uint16_t mask);

/*============================================================================
 * Page Fault Handling (for VMM integration)
 *============================================================================*/

/**
 * @brief Page fault error code bits
 */
#ifndef SERAPH_PF_ERROR_BITS_DEFINED
#define SERAPH_PF_ERROR_BITS_DEFINED
typedef enum {
    SERAPH_PF_PRESENT   = (1 << 0),  /**< Page was present */
    SERAPH_PF_WRITE     = (1 << 1),  /**< Write access */
    SERAPH_PF_USER      = (1 << 2),  /**< User mode access */
    SERAPH_PF_RESERVED  = (1 << 3),  /**< Reserved bit violation */
    SERAPH_PF_FETCH     = (1 << 4),  /**< Instruction fetch */
    SERAPH_PF_PK        = (1 << 5),  /**< Protection key violation */
    SERAPH_PF_SS        = (1 << 6),  /**< Shadow stack access */
    SERAPH_PF_SGX       = (1 << 15), /**< SGX-related fault */
} Seraph_PF_Error_Bits;

/** Aliases for compatibility with vmm.h names */
#define SERAPH_PF_PROTECTION SERAPH_PF_PK
#define SERAPH_PF_SHADOW SERAPH_PF_SS

/** Alias for SERAPH_PF_FETCH for backward compatibility */
#define SERAPH_PF_INSTRUCTION SERAPH_PF_FETCH
#endif /* SERAPH_PF_ERROR_BITS_DEFINED */

/**
 * @brief Page fault handler callback type
 *
 * @param fault_addr The virtual address that caused the fault (from CR2)
 * @param error_code The page fault error code
 * @param frame The interrupt frame
 * @return SERAPH_VBIT_TRUE if handled, SERAPH_VBIT_FALSE if not, SERAPH_VBIT_VOID on error
 */
typedef Seraph_Vbit (*Seraph_PF_Handler)(uint64_t fault_addr, uint64_t error_code,
                                          Seraph_InterruptFrame* frame);

/**
 * @brief Register a page fault handler
 *
 * The VMM registers its handler here to intercept page faults for
 * demand paging, copy-on-write, etc.
 *
 * @param handler The page fault handler (NULL to unregister)
 * @return Previous handler
 */
Seraph_PF_Handler seraph_pf_register(Seraph_PF_Handler handler);

/**
 * @brief Get the fault address from CR2
 *
 * @return The linear address that caused the most recent page fault
 */
uint64_t seraph_get_cr2(void);

/*============================================================================
 * Debugging and Statistics
 *============================================================================*/

/**
 * @brief Interrupt statistics
 */
typedef struct {
    uint64_t total_interrupts;    /**< Total interrupts handled */
    uint64_t exception_count[32]; /**< Count per exception vector */
    uint64_t irq_count[16];       /**< Count per IRQ */
    uint64_t spurious_count;      /**< Spurious interrupts */
    uint64_t void_injections;     /**< Times VOID was injected for recovery */
    uint64_t sovereign_kills;     /**< Sovereigns terminated due to exceptions */
} Seraph_Int_Stats;

/**
 * @brief Get interrupt statistics
 *
 * @return Pointer to statistics structure
 */
const Seraph_Int_Stats* seraph_int_stats(void);

/**
 * @brief Reset interrupt statistics
 */
void seraph_int_stats_reset(void);

/**
 * @brief Dump interrupt frame for debugging
 *
 * @param frame The interrupt frame to dump
 */
void seraph_int_dump_frame(const Seraph_InterruptFrame* frame);

/**
 * @brief Get exception name string
 *
 * @param vector Exception vector (0-31)
 * @return Human-readable name, or "Unknown" if invalid
 */
const char* seraph_exception_name(uint8_t vector);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_INTERRUPTS_H */
