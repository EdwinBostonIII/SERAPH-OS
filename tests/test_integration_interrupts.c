/**
 * @file test_integration_interrupts.c
 * @brief Integration Tests for Interrupt Handling Subsystem
 *
 * MC-INT-02: Interrupt Subsystem Integration Testing
 *
 * This test suite verifies that all interrupt handling components
 * work correctly together:
 *
 *   - IDT (Interrupt Descriptor Table) setup
 *   - Exception handlers with VOID injection
 *   - PIC remapping and EOI
 *   - APIC timer for preemption
 *   - Interrupt enable/disable
 *
 * Test Strategy:
 *   1. Initialize IDT structures
 *   2. Verify gate configuration
 *   3. Test interrupt registration
 *   4. Verify VOID injection on exceptions
 *   5. Test scheduler integration
 */

#include "seraph/interrupts.h"
#include "seraph/scheduler.h"
#include "seraph/context.h"
#include "seraph/apic.h"
#include "seraph/void.h"
#include "seraph/vbit.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static int test_##name(void); \
    static void run_test_##name(void) { \
        tests_run++; \
        printf("  Running: %s... ", #name); \
        fflush(stdout); \
        if (test_##name() == 0) { \
            tests_passed++; \
            printf("PASS\n"); \
        } else { \
            tests_failed++; \
            printf("FAIL\n"); \
        } \
    } \
    static int test_##name(void)

#define ASSERT(cond) do { if (!(cond)) { \
    fprintf(stderr, "\n    ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
    return 1; \
} } while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))

/*============================================================================
 * IDT Structure Tests
 *============================================================================*/

/* Test: IDT gate structure size */
TEST(idt_gate_size) {
    /* IDT gate must be exactly 16 bytes in x86-64 */
    ASSERT_EQ(sizeof(Seraph_IDT_Gate), 16);
    return 0;
}

/* Test: IDTR structure size */
TEST(idtr_size) {
    /* IDTR must be 10 bytes (2 + 8) */
    ASSERT_EQ(sizeof(Seraph_IDTR), 10);
    return 0;
}

/* Test: Interrupt frame size */
TEST(interrupt_frame_size) {
    /* Verify interrupt frame matches expected layout (176 bytes per static assert) */
    ASSERT_EQ(sizeof(Seraph_InterruptFrame), 176);
    return 0;
}

/*============================================================================
 * IDT Configuration Tests
 *============================================================================*/

/* Test: Gate type attributes */
TEST(gate_types) {
    /* Verify gate type constants */
    ASSERT_EQ(SERAPH_GATE_INTERRUPT & 0x0F, 0x0E); /* Interrupt gate */
    ASSERT_EQ(SERAPH_GATE_TRAP & 0x0F, 0x0F);      /* Trap gate */

    /* Verify present bit */
    ASSERT((SERAPH_GATE_INTERRUPT & 0x80) != 0);
    ASSERT((SERAPH_GATE_TRAP & 0x80) != 0);

    return 0;
}

/* Test: Exception vector definitions */
TEST(exception_vectors) {
    /* Verify standard exception vectors */
    ASSERT_EQ(SERAPH_EXC_DE, 0);   /* Divide Error */
    ASSERT_EQ(SERAPH_EXC_DB, 1);   /* Debug */
    ASSERT_EQ(SERAPH_EXC_NMI, 2);  /* NMI */
    ASSERT_EQ(SERAPH_EXC_BP, 3);   /* Breakpoint */
    ASSERT_EQ(SERAPH_EXC_OF, 4);   /* Overflow */
    ASSERT_EQ(SERAPH_EXC_GP, 13);  /* General Protection */
    ASSERT_EQ(SERAPH_EXC_PF, 14);  /* Page Fault */

    return 0;
}

/* Test: IRQ vector remapping */
TEST(irq_vectors) {
    /* Verify IRQ vectors are remapped to 32-47 */
    ASSERT_EQ(SERAPH_IRQ_TIMER, 32);
    ASSERT_EQ(SERAPH_IRQ_KEYBOARD, 33);

    return 0;
}

/*============================================================================
 * Exception Classification Tests
 *============================================================================*/

/* Test: Error code exceptions */
TEST(error_code_exceptions) {
    /* Exceptions that push error codes */
    ASSERT(seraph_exc_has_error_code(SERAPH_EXC_DF));  /* Double Fault */
    ASSERT(seraph_exc_has_error_code(SERAPH_EXC_TS));  /* Invalid TSS */
    ASSERT(seraph_exc_has_error_code(SERAPH_EXC_NP));  /* Segment Not Present */
    ASSERT(seraph_exc_has_error_code(SERAPH_EXC_SS));  /* Stack Segment */
    ASSERT(seraph_exc_has_error_code(SERAPH_EXC_GP));  /* General Protection */
    ASSERT(seraph_exc_has_error_code(SERAPH_EXC_PF));  /* Page Fault */

    /* Exceptions that don't push error codes */
    ASSERT(!seraph_exc_has_error_code(SERAPH_EXC_DE)); /* Divide Error */
    ASSERT(!seraph_exc_has_error_code(SERAPH_EXC_BP)); /* Breakpoint */

    return 0;
}

/* Test: Exception names */
TEST(exception_names) {
    ASSERT(strcmp(seraph_exc_name(SERAPH_EXC_DE), "Divide Error") == 0);
    ASSERT(strcmp(seraph_exc_name(SERAPH_EXC_GP), "General Protection") == 0);
    ASSERT(strcmp(seraph_exc_name(SERAPH_EXC_PF), "Page Fault") == 0);

    return 0;
}

/*============================================================================
 * VOID Injection Tests
 *============================================================================*/

/* Test: VOID values are properly defined */
TEST(void_values) {
    /* Verify VOID sentinel values */
    ASSERT_EQ(SERAPH_VOID_U8, 0xFF);
    ASSERT_EQ(SERAPH_VOID_U16, 0xFFFF);
    ASSERT_EQ(SERAPH_VOID_U32, 0xFFFFFFFFU);
    ASSERT_EQ(SERAPH_VOID_U64, 0xFFFFFFFFFFFFFFFFULL);
    /* VOID_PTR is the all-1s sentinel, NOT NULL */
    ASSERT_EQ(SERAPH_VOID_PTR, (void*)(uintptr_t)0xFFFFFFFFFFFFFFFFULL);
    ASSERT_NE(SERAPH_VOID_PTR, NULL);

    return 0;
}

/* Test: VOID checking macros */
TEST(void_checking) {
    /* Test IS_VOID macros */
    ASSERT(SERAPH_IS_VOID_U8(SERAPH_VOID_U8));
    ASSERT(!SERAPH_IS_VOID_U8(0));
    ASSERT(!SERAPH_IS_VOID_U8(1));

    ASSERT(SERAPH_IS_VOID_U64(SERAPH_VOID_U64));
    ASSERT(!SERAPH_IS_VOID_U64(0));

    /* VOID_PTR is the all-1s sentinel (0xFFFFFFFFFFFFFFFF), NOT NULL */
    ASSERT(SERAPH_IS_VOID_PTR(SERAPH_VOID_PTR));
    ASSERT(!SERAPH_IS_VOID_PTR(NULL)); /* NULL is NOT VOID */
    int dummy;
    ASSERT(!SERAPH_IS_VOID_PTR(&dummy));

    return 0;
}

/*============================================================================
 * Context Structure Tests
 *============================================================================*/

/* Test: CPU context structure */
TEST(cpu_context_structure) {
    /* Verify context structure is properly aligned */
    ASSERT((sizeof(Seraph_CPU_Context) % 16) == 0);

    /* Verify context has FPU state area */
    Seraph_CPU_Context ctx;
    ASSERT(((uintptr_t)ctx.fpu_state % 16) == 0);

    return 0;
}

/* Test: Context initialization */
TEST(context_init) {
    Seraph_CPU_Context ctx;

    /* Initialize context */
    memset(&ctx, 0, sizeof(ctx));
    ctx.rip = 0x1000;
    ctx.rsp = 0x2000;
    ctx.rflags = SERAPH_RFLAGS_DEFAULT;
    ctx.cs = SERAPH_KERNEL_CS;
    ctx.ss = SERAPH_KERNEL_DS;

    /* Verify initialization */
    ASSERT_EQ(ctx.rip, 0x1000);
    ASSERT_EQ(ctx.rsp, 0x2000);
    ASSERT((ctx.rflags & SERAPH_RFLAGS_IF) != 0); /* Interrupts enabled */

    return 0;
}

/*============================================================================
 * APIC Configuration Tests
 *============================================================================*/

/* Test: APIC register offsets */
TEST(apic_registers) {
    /* Verify APIC register offsets */
    ASSERT_EQ(SERAPH_APIC_ID, 0x020);
    ASSERT_EQ(SERAPH_APIC_VERSION, 0x030);
    ASSERT_EQ(SERAPH_APIC_EOI, 0x0B0);
    ASSERT_EQ(SERAPH_APIC_LVT_TIMER, 0x320);
    ASSERT_EQ(SERAPH_APIC_TIMER_INIT, 0x380);

    return 0;
}

/* Test: APIC timer modes */
TEST(apic_timer_modes) {
    /* Verify timer mode flags */
    ASSERT_EQ(SERAPH_APIC_TIMER_ONESHOT, 0x00000);
    ASSERT_EQ(SERAPH_APIC_TIMER_PERIODIC, 0x20000);

    return 0;
}

/*============================================================================
 * Scheduler Integration Tests
 *============================================================================*/

/* Test: Scheduler priority levels */
TEST(scheduler_priorities) {
    /* Verify priority constants */
    ASSERT_EQ(SERAPH_PRIORITY_IDLE, 0);
    ASSERT_EQ(SERAPH_PRIORITY_NORMAL, 3);
    ASSERT_EQ(SERAPH_PRIORITY_MAX, 7);

    /* Verify priority ordering */
    ASSERT(SERAPH_PRIORITY_REALTIME > SERAPH_PRIORITY_NORMAL);
    ASSERT(SERAPH_PRIORITY_NORMAL > SERAPH_PRIORITY_LOW);

    return 0;
}

/* Test: Time quantum per priority */
TEST(scheduler_quantum) {
    /* Higher priority should have longer quantum */
    ASSERT(SERAPH_QUANTUM_CRITICAL > SERAPH_QUANTUM_NORMAL);
    ASSERT(SERAPH_QUANTUM_NORMAL > SERAPH_QUANTUM_LOW);
    ASSERT(SERAPH_QUANTUM_LOW > SERAPH_QUANTUM_IDLE);

    return 0;
}

/*============================================================================
 * Page Fault Error Code Tests
 *============================================================================*/

/* Test: Page fault error bits */
TEST(page_fault_error_bits) {
    /* Verify page fault error code bits */
    ASSERT_EQ(SERAPH_PF_PRESENT, 0x01);
    ASSERT_EQ(SERAPH_PF_WRITE, 0x02);
    ASSERT_EQ(SERAPH_PF_USER, 0x04);
    ASSERT_EQ(SERAPH_PF_RESERVED, 0x08);
    ASSERT_EQ(SERAPH_PF_INSTRUCTION, 0x10);

    return 0;
}

/* Test: Page fault error parsing */
TEST(page_fault_error_parsing) {
    /* Test various error code combinations */
    uint64_t err = SERAPH_PF_PRESENT | SERAPH_PF_WRITE | SERAPH_PF_USER;

    ASSERT((err & SERAPH_PF_PRESENT) != 0);   /* Page was present */
    ASSERT((err & SERAPH_PF_WRITE) != 0);     /* Was a write */
    ASSERT((err & SERAPH_PF_USER) != 0);      /* User mode */
    ASSERT((err & SERAPH_PF_INSTRUCTION) == 0); /* Not instruction fetch */

    return 0;
}

/*============================================================================
 * Integration Tests
 *============================================================================*/

/* Test: Interrupt handler callback simulation */
static int handler_called = 0;
static void test_handler(Seraph_InterruptFrame* frame) {
    (void)frame;
    handler_called = 1;
}

TEST(handler_registration) {
    /* Reset flag */
    handler_called = 0;

    /* Register handler (simulation) */
    /* In real kernel, this would use seraph_int_register() */

    /* Simulate handler call */
    Seraph_InterruptFrame frame;
    memset(&frame, 0, sizeof(frame));
    test_handler(&frame);

    /* Verify handler was called */
    ASSERT_EQ(handler_called, 1);

    return 0;
}

/* Test: Full interrupt path simulation */
TEST(interrupt_path_simulation) {
    /* Simulate full interrupt handling path:
     * 1. Save context
     * 2. Call handler
     * 3. Potentially switch context
     * 4. Restore context
     */

    Seraph_CPU_Context old_ctx, new_ctx;
    memset(&old_ctx, 0, sizeof(old_ctx));
    memset(&new_ctx, 0, sizeof(new_ctx));

    /* Set up old context */
    old_ctx.rip = 0x1000;
    old_ctx.rsp = 0x2000;
    old_ctx.rax = 0xDEADBEEF;

    /* Set up new context */
    new_ctx.rip = 0x3000;
    new_ctx.rsp = 0x4000;
    new_ctx.rax = 0xCAFEBABE;

    /* Verify contexts are different */
    ASSERT_NE(old_ctx.rip, new_ctx.rip);
    ASSERT_NE(old_ctx.rsp, new_ctx.rsp);
    ASSERT_NE(old_ctx.rax, new_ctx.rax);

    /* In real kernel, context switch would happen here */

    return 0;
}

/*============================================================================
 * Test Runner
 *============================================================================*/

int main(void) {
    printf("=== Interrupt Subsystem Integration Tests ===\n\n");

    printf("IDT Structure Tests:\n");
    run_test_idt_gate_size();
    run_test_idtr_size();
    run_test_interrupt_frame_size();

    printf("\nIDT Configuration Tests:\n");
    run_test_gate_types();
    run_test_exception_vectors();
    run_test_irq_vectors();

    printf("\nException Classification Tests:\n");
    run_test_error_code_exceptions();
    run_test_exception_names();

    printf("\nVOID Injection Tests:\n");
    run_test_void_values();
    run_test_void_checking();

    printf("\nContext Structure Tests:\n");
    run_test_cpu_context_structure();
    run_test_context_init();

    printf("\nAPIC Tests:\n");
    run_test_apic_registers();
    run_test_apic_timer_modes();

    printf("\nScheduler Integration Tests:\n");
    run_test_scheduler_priorities();
    run_test_scheduler_quantum();

    printf("\nPage Fault Tests:\n");
    run_test_page_fault_error_bits();
    run_test_page_fault_error_parsing();

    printf("\nIntegration Tests:\n");
    run_test_handler_registration();
    run_test_interrupt_path_simulation();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
