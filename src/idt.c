/**
 * @file idt.c
 * @brief MC23: Interrupt Descriptor Table Setup
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module manages the x86-64 Interrupt Descriptor Table (IDT).
 * The IDT maps interrupt vectors (0-255) to handler functions.
 *
 * ARCHITECTURE NOTES:
 *
 * In x86-64 long mode, each IDT entry is 16 bytes:
 *   - 8 bytes for handler address (split across entry)
 *   - 2 bytes for code segment selector
 *   - 1 byte for IST (Interrupt Stack Table) offset
 *   - 1 byte for type/attributes
 *   - 4 bytes reserved
 *
 * The type_attr byte format:
 *   Bit 7: Present (P)
 *   Bits 6-5: DPL (Descriptor Privilege Level)
 *   Bit 4: 0 (must be 0 for system descriptors)
 *   Bits 3-0: Type
 *     0xE = 64-bit Interrupt Gate (clears IF on entry)
 *     0xF = 64-bit Trap Gate (preserves IF)
 */

#include "seraph/interrupts.h"
#include <string.h>

/*============================================================================
 * External Assembly Stubs
 *
 * These are defined in idt.asm and provide the low-level entry points
 * for each interrupt vector.
 *============================================================================*/

/* Exception stubs (vectors 0-31) */
extern void seraph_isr_stub_0(void);
extern void seraph_isr_stub_1(void);
extern void seraph_isr_stub_2(void);
extern void seraph_isr_stub_3(void);
extern void seraph_isr_stub_4(void);
extern void seraph_isr_stub_5(void);
extern void seraph_isr_stub_6(void);
extern void seraph_isr_stub_7(void);
extern void seraph_isr_stub_8(void);
extern void seraph_isr_stub_9(void);
extern void seraph_isr_stub_10(void);
extern void seraph_isr_stub_11(void);
extern void seraph_isr_stub_12(void);
extern void seraph_isr_stub_13(void);
extern void seraph_isr_stub_14(void);
extern void seraph_isr_stub_15(void);
extern void seraph_isr_stub_16(void);
extern void seraph_isr_stub_17(void);
extern void seraph_isr_stub_18(void);
extern void seraph_isr_stub_19(void);
extern void seraph_isr_stub_20(void);
extern void seraph_isr_stub_21(void);
extern void seraph_isr_stub_22(void);
extern void seraph_isr_stub_23(void);
extern void seraph_isr_stub_24(void);
extern void seraph_isr_stub_25(void);
extern void seraph_isr_stub_26(void);
extern void seraph_isr_stub_27(void);
extern void seraph_isr_stub_28(void);
extern void seraph_isr_stub_29(void);
extern void seraph_isr_stub_30(void);
extern void seraph_isr_stub_31(void);

/* IRQ stubs (vectors 32-47) */
extern void seraph_isr_stub_32(void);
extern void seraph_isr_stub_33(void);
extern void seraph_isr_stub_34(void);
extern void seraph_isr_stub_35(void);
extern void seraph_isr_stub_36(void);
extern void seraph_isr_stub_37(void);
extern void seraph_isr_stub_38(void);
extern void seraph_isr_stub_39(void);
extern void seraph_isr_stub_40(void);
extern void seraph_isr_stub_41(void);
extern void seraph_isr_stub_42(void);
extern void seraph_isr_stub_43(void);
extern void seraph_isr_stub_44(void);
extern void seraph_isr_stub_45(void);
extern void seraph_isr_stub_46(void);
extern void seraph_isr_stub_47(void);

/* Software interrupt stubs (48+) - we'll use a generic handler */
extern void seraph_isr_stub_generic(void);

/*============================================================================
 * IDT Data Structures
 *============================================================================*/

/** The IDT itself - aligned to 16 bytes for performance */
static Seraph_IDT_Gate g_idt[SERAPH_IDT_ENTRIES] __attribute__((aligned(16)));

/** IDTR value for LIDT instruction */
static Seraph_IDTR g_idtr;

/** Stub table for easy lookup during initialization */
static void* g_isr_stubs[48] = {
    seraph_isr_stub_0,  seraph_isr_stub_1,  seraph_isr_stub_2,  seraph_isr_stub_3,
    seraph_isr_stub_4,  seraph_isr_stub_5,  seraph_isr_stub_6,  seraph_isr_stub_7,
    seraph_isr_stub_8,  seraph_isr_stub_9,  seraph_isr_stub_10, seraph_isr_stub_11,
    seraph_isr_stub_12, seraph_isr_stub_13, seraph_isr_stub_14, seraph_isr_stub_15,
    seraph_isr_stub_16, seraph_isr_stub_17, seraph_isr_stub_18, seraph_isr_stub_19,
    seraph_isr_stub_20, seraph_isr_stub_21, seraph_isr_stub_22, seraph_isr_stub_23,
    seraph_isr_stub_24, seraph_isr_stub_25, seraph_isr_stub_26, seraph_isr_stub_27,
    seraph_isr_stub_28, seraph_isr_stub_29, seraph_isr_stub_30, seraph_isr_stub_31,
    seraph_isr_stub_32, seraph_isr_stub_33, seraph_isr_stub_34, seraph_isr_stub_35,
    seraph_isr_stub_36, seraph_isr_stub_37, seraph_isr_stub_38, seraph_isr_stub_39,
    seraph_isr_stub_40, seraph_isr_stub_41, seraph_isr_stub_42, seraph_isr_stub_43,
    seraph_isr_stub_44, seraph_isr_stub_45, seraph_isr_stub_46, seraph_isr_stub_47,
};

/*============================================================================
 * IDT Management Implementation
 *============================================================================*/

/**
 * @brief Set an IDT gate entry
 */
void seraph_idt_set_gate(uint8_t vector, void* handler, uint8_t type_attr, uint8_t ist) {
    uintptr_t addr = (uintptr_t)handler;

    g_idt[vector].offset_low  = (uint16_t)(addr & 0xFFFF);
    g_idt[vector].selector    = SERAPH_KERNEL_CS;
    g_idt[vector].ist         = ist & 0x07;  /* IST is only 3 bits */
    g_idt[vector].type_attr   = type_attr;
    g_idt[vector].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    g_idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    g_idt[vector].reserved    = 0;
}

/**
 * @brief Load the IDT into the CPU
 */
void seraph_idt_load(void) {
    /* Set up IDTR */
    g_idtr.limit = sizeof(g_idt) - 1;
    g_idtr.base  = (uint64_t)&g_idt[0];

    /* Load the IDT using LIDT instruction */
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("lidt %0" : : "m"(g_idtr));
#elif defined(_MSC_VER)
    /* MSVC doesn't support inline assembly in x64 mode.
     * This would need to be in a separate .asm file for MSVC.
     * For now, we provide a stub that can be linked later. */
    extern void _seraph_lidt(void* idtr);
    _seraph_lidt(&g_idtr);
#endif
}

/**
 * @brief Get the current IDT base address
 */
Seraph_IDT_Gate* seraph_idt_get(void) {
    return g_idt;
}

/**
 * @brief Initialize the IDT
 */
void seraph_idt_init(void) {
    /* Clear the IDT */
    memset(g_idt, 0, sizeof(g_idt));

    /* Set up exception handlers (vectors 0-31) */
    for (int i = 0; i < 32; i++) {
        /*
         * Exceptions that push error codes: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30
         * Our stubs handle this by pushing a dummy 0 for others.
         *
         * Use trap gates for exceptions so we can see the exact state.
         * Use interrupt gates for hardware IRQs to prevent nested IRQs.
         */
        uint8_t type_attr;

        /* Double Fault (#DF) should use IST 1 for a known-good stack */
        uint8_t ist = (i == SERAPH_EXC_DF) ? 1 : 0;

        /* Breakpoint (#BP) is callable from user mode (DPL=3) */
        if (i == SERAPH_EXC_BP || i == SERAPH_EXC_OF) {
            type_attr = SERAPH_GATE_USER_TRAP;
        } else {
            type_attr = SERAPH_GATE_TRAP;
        }

        seraph_idt_set_gate((uint8_t)i, g_isr_stubs[i], type_attr, ist);
    }

    /* Set up IRQ handlers (vectors 32-47) */
    for (int i = 32; i < 48; i++) {
        /* IRQs use interrupt gates to clear IF (prevent nesting by default) */
        seraph_idt_set_gate((uint8_t)i, g_isr_stubs[i], SERAPH_GATE_INTERRUPT, 0);
    }

    /* Initialize the PIC (remap IRQs to vectors 32-47) */
    seraph_pic_init();

    /* Load the IDT into the CPU */
    seraph_idt_load();
}

/*============================================================================
 * Interrupt Enable/Disable
 *============================================================================*/

/**
 * @brief Enable interrupts
 */
void seraph_int_enable(void) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("sti");
#elif defined(_MSC_VER)
    extern void _seraph_sti(void);
    _seraph_sti();
#endif
}

/**
 * @brief Disable interrupts
 */
void seraph_int_disable(void) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("cli");
#elif defined(_MSC_VER)
    extern void _seraph_cli(void);
    _seraph_cli();
#endif
}

/**
 * @brief Check if interrupts are enabled
 */
bool seraph_int_enabled(void) {
    uint64_t flags;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(
        "pushfq\n\t"
        "pop %0"
        : "=r"(flags)
    );
#elif defined(_MSC_VER)
    extern uint64_t _seraph_get_flags(void);
    flags = _seraph_get_flags();
#else
    flags = 0;
#endif
    /* Bit 9 is the IF (Interrupt Flag) */
    return (flags & (1ULL << 9)) != 0;
}

/**
 * @brief Save interrupt state and disable
 */
uint64_t seraph_int_save_disable(void) {
    uint64_t flags;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
#elif defined(_MSC_VER)
    extern uint64_t _seraph_save_disable(void);
    flags = _seraph_save_disable();
#else
    flags = 0;
#endif
    return flags;
}

/**
 * @brief Restore previously saved interrupt state
 */
void seraph_int_restore(uint64_t flags) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(
        "push %0\n\t"
        "popfq"
        :
        : "r"(flags)
        : "memory", "cc"
    );
#elif defined(_MSC_VER)
    extern void _seraph_restore_flags(uint64_t);
    _seraph_restore_flags(flags);
#endif
}

/**
 * @brief Get CR2 (page fault address)
 */
uint64_t seraph_get_cr2(void) {
    uint64_t cr2;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(
        "mov %%cr2, %0"
        : "=r"(cr2)
    );
#elif defined(_MSC_VER)
    extern uint64_t _seraph_get_cr2(void);
    cr2 = _seraph_get_cr2();
#else
    cr2 = 0;
#endif
    return cr2;
}
