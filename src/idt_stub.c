/**
 * @file idt_stub.c
 * @brief SERAPH IDT - Stub Implementation for Testing
 *
 * This file provides stub implementations of the IDT functions
 * that are normally implemented in assembly. These stubs allow the
 * test suite to link properly without requiring actual hardware.
 *
 * NOTE: These stubs are NOT suitable for production use.
 */

#include "seraph/interrupts.h"
#include <string.h>

/*============================================================================
 * Global IDT State (Stub)
 *============================================================================*/

static Seraph_IDT_Gate g_idt[SERAPH_IDT_ENTRIES];
static Seraph_IDTR g_idtr;
static Seraph_Interrupt_Handler g_handlers[SERAPH_IDT_ENTRIES];
static Seraph_Int_Stats g_int_stats;

/*============================================================================
 * IDT Management (Stubs)
 *============================================================================*/

void seraph_idt_init(void) {
    memset(g_idt, 0, sizeof(g_idt));
    memset(g_handlers, 0, sizeof(g_handlers));
    memset(&g_int_stats, 0, sizeof(g_int_stats));

    g_idtr.limit = sizeof(g_idt) - 1;
    g_idtr.base = (uint64_t)g_idt;
}

void seraph_idt_set_gate(uint8_t vector, void* handler, uint8_t type_attr, uint8_t ist) {
    uint64_t addr = (uint64_t)handler;

    g_idt[vector].offset_low = addr & 0xFFFF;
    g_idt[vector].selector = SERAPH_KERNEL_CS;
    g_idt[vector].ist = ist & 0x07;
    g_idt[vector].type_attr = type_attr;
    g_idt[vector].offset_mid = (addr >> 16) & 0xFFFF;
    g_idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    g_idt[vector].reserved = 0;
}

void seraph_idt_load(void) {
    /* Stub - would normally execute LIDT instruction */
}

Seraph_IDT_Gate* seraph_idt_get(void) {
    return g_idt;
}

/*============================================================================
 * Interrupt Handler Registration (Stubs)
 *============================================================================*/

Seraph_Interrupt_Handler seraph_int_register(uint8_t vector, Seraph_Interrupt_Handler handler) {
    Seraph_Interrupt_Handler old = g_handlers[vector];
    g_handlers[vector] = handler;
    return old;
}

Seraph_Interrupt_Handler seraph_int_get_handler(uint8_t vector) {
    return g_handlers[vector];
}

void seraph_int_dispatch(Seraph_InterruptFrame* frame) {
    if (frame == NULL) return;

    g_int_stats.total_interrupts++;

    uint8_t vector = (uint8_t)frame->vector;

    if (vector < 32) {
        g_int_stats.exception_count[vector]++;
    } else if (vector < 48) {
        g_int_stats.irq_count[vector - 32]++;
    }

    if (g_handlers[vector] != NULL) {
        g_handlers[vector](frame);
    }
}

/*============================================================================
 * Interrupt Control (Stubs)
 *============================================================================*/

static bool g_interrupts_enabled = false;

void seraph_int_enable(void) {
    g_interrupts_enabled = true;
}

void seraph_int_disable(void) {
    g_interrupts_enabled = false;
}

bool seraph_int_enabled(void) {
    return g_interrupts_enabled;
}

uint64_t seraph_int_save_disable(void) {
    uint64_t old = g_interrupts_enabled ? 0x200 : 0;
    g_interrupts_enabled = false;
    return old;
}

void seraph_int_restore(uint64_t flags) {
    g_interrupts_enabled = (flags & 0x200) != 0;
}

/*============================================================================
 * PIC Management (Stubs)
 *============================================================================*/

static uint16_t g_pic_mask = 0xFFFF;

void seraph_pic_init(void) {
    g_pic_mask = 0xFFFF;
}

void seraph_pic_eoi(uint8_t irq) {
    (void)irq;
    /* Stub - would normally write to PIC */
}

void seraph_pic_mask(uint8_t irq) {
    if (irq < 16) {
        g_pic_mask |= (1 << irq);
    }
}

void seraph_pic_unmask(uint8_t irq) {
    if (irq < 16) {
        g_pic_mask &= ~(1 << irq);
    }
}

void seraph_pic_disable_all(void) {
    g_pic_mask = 0xFFFF;
}

uint16_t seraph_pic_get_mask(void) {
    return g_pic_mask;
}

void seraph_pic_set_mask(uint16_t mask) {
    g_pic_mask = mask;
}

/*============================================================================
 * Page Fault Handler (Stub)
 *============================================================================*/

static Seraph_PF_Handler g_pf_handler = NULL;

Seraph_PF_Handler seraph_pf_register(Seraph_PF_Handler handler) {
    Seraph_PF_Handler old = g_pf_handler;
    g_pf_handler = handler;
    return old;
}

uint64_t seraph_get_cr2(void) {
    /* Stub - would normally read CR2 register */
    return 0;
}

/*============================================================================
 * Statistics (Stubs)
 *============================================================================*/

const Seraph_Int_Stats* seraph_int_stats(void) {
    return &g_int_stats;
}

void seraph_int_stats_reset(void) {
    memset(&g_int_stats, 0, sizeof(g_int_stats));
}

void seraph_int_dump_frame(const Seraph_InterruptFrame* frame) {
    (void)frame;
    /* Stub - would normally print frame contents */
}

const char* seraph_exception_name(uint8_t vector) {
    return seraph_exc_name(vector);
}

/*============================================================================
 * Exception Info (Stub)
 *============================================================================*/

static const Seraph_Exception_Info g_exception_info[] = {
    { 0, "Divide Error", "#DE", false, SERAPH_EXC_CLASS_RECOVERABLE },
    { 1, "Debug", "#DB", false, SERAPH_EXC_CLASS_BENIGN },
    { 2, "NMI", "NMI", false, SERAPH_EXC_CLASS_FATAL },
    { 3, "Breakpoint", "#BP", false, SERAPH_EXC_CLASS_BENIGN },
    { 4, "Overflow", "#OF", false, SERAPH_EXC_CLASS_RECOVERABLE },
    { 5, "Bound Range", "#BR", false, SERAPH_EXC_CLASS_RECOVERABLE },
    { 6, "Invalid Opcode", "#UD", false, SERAPH_EXC_CLASS_FATAL },
    { 7, "Device Not Available", "#NM", false, SERAPH_EXC_CLASS_BENIGN },
    { 8, "Double Fault", "#DF", true, SERAPH_EXC_CLASS_FATAL },
    { 9, "Coprocessor Segment", "#CSO", false, SERAPH_EXC_CLASS_FATAL },
    {10, "Invalid TSS", "#TS", true, SERAPH_EXC_CLASS_FATAL },
    {11, "Segment Not Present", "#NP", true, SERAPH_EXC_CLASS_FATAL },
    {12, "Stack Segment", "#SS", true, SERAPH_EXC_CLASS_FATAL },
    {13, "General Protection", "#GP", true, SERAPH_EXC_CLASS_FATAL },
    {14, "Page Fault", "#PF", true, SERAPH_EXC_CLASS_ROUTABLE },
};

const Seraph_Exception_Info* seraph_exception_info(uint8_t vector) {
    if (vector < sizeof(g_exception_info) / sizeof(g_exception_info[0])) {
        return &g_exception_info[vector];
    }
    return NULL;
}
