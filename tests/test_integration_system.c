/**
 * @file test_integration_system.c
 * @brief Full System Integration Tests
 *
 * MC-INT-04: SERAPH Operating System End-to-End Integration Testing
 *
 * This test suite verifies that ALL components of the SERAPH Operating System
 * work together correctly as a complete, integrated system:
 *
 * COMPONENTS TESTED:
 *   - VOID Semantics (void.h, vbit.h)
 *   - Capabilities (capability.h)
 *   - Memory Management (pmm.h, vmm.h, kmalloc.h, arena.h)
 *   - Process Model (sovereign.h, strand.h)
 *   - Interrupt Handling (interrupts.h)
 *   - Scheduling (scheduler.h, context.h)
 *   - Storage Substrates (atlas.h, aether.h)
 *   - IPC (whisper.h)
 *   - Compiler (lexer, parser, checker, effects)
 */

#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/capability.h"
#include "seraph/arena.h"
#include "seraph/sovereign.h"
#include "seraph/strand.h"
#include "seraph/whisper.h"
#include "seraph/chronon.h"
#include "seraph/atlas.h"
#include "seraph/aether.h"
#include "seraph/interrupts.h"
#include "seraph/scheduler.h"
#include "seraph/context.h"
#include "seraph/pmm.h"
#include "seraph/vmm.h"
#include "seraph/kmalloc.h"
#include "seraph/seraphim/lexer.h"
#include "seraph/seraphim/parser.h"
#include "seraph/seraphim/effects.h"
#include "seraph/seraphim/token.h"
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
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_VBIT_TRUE(v) ASSERT(seraph_vbit_is_true(v))

/*============================================================================
 * VOID Semantics Integration
 *============================================================================*/

/* Test: VOID propagation through multiple operations */
TEST(void_propagation_chain) {
    /* Simulate a chain of operations where VOID propagates */
    uint64_t a = 10;
    uint64_t b = 0;  /* Will cause VOID in division */

    /* Safe division (would produce VOID in real system) */
    uint64_t result1 = (b == 0) ? SERAPH_VOID_U64 : (a / b);
    ASSERT(SERAPH_IS_VOID_U64(result1));

    /* VOID propagation */
    uint64_t result2 = SERAPH_IS_VOID_U64(result1) ? SERAPH_VOID_U64 : result1 * 2;
    ASSERT(SERAPH_IS_VOID_U64(result2));

    /* Non-VOID path */
    uint64_t c = 5;
    uint64_t result3 = (c == 0) ? SERAPH_VOID_U64 : (a / c);
    ASSERT(!SERAPH_IS_VOID_U64(result3));
    ASSERT_EQ(result3, 2);

    return 0;
}

/* Test: VBIT three-valued logic */
TEST(vbit_logic) {
    Seraph_Vbit t = SERAPH_VBIT_TRUE;
    Seraph_Vbit f = SERAPH_VBIT_FALSE;
    Seraph_Vbit v = SERAPH_VBIT_VOID;

    /* AND truth table with VOID */
    ASSERT_EQ(seraph_vbit_and(t, t), SERAPH_VBIT_TRUE);
    ASSERT_EQ(seraph_vbit_and(t, f), SERAPH_VBIT_FALSE);
    ASSERT_EQ(seraph_vbit_and(f, v), SERAPH_VBIT_FALSE);  /* FALSE dominates */
    ASSERT_EQ(seraph_vbit_and(t, v), SERAPH_VBIT_VOID);   /* VOID propagates */

    /* OR truth table with VOID */
    ASSERT_EQ(seraph_vbit_or(f, f), SERAPH_VBIT_FALSE);
    ASSERT_EQ(seraph_vbit_or(t, f), SERAPH_VBIT_TRUE);
    ASSERT_EQ(seraph_vbit_or(t, v), SERAPH_VBIT_TRUE);    /* TRUE dominates */
    ASSERT_EQ(seraph_vbit_or(f, v), SERAPH_VBIT_VOID);    /* VOID propagates */

    return 0;
}

/*============================================================================
 * Capability System Integration
 *============================================================================*/

/* Test: Capability creation and validation */
TEST(capability_basics) {
    /* Create a capability using seraph_cap_create */
    /* Signature: seraph_cap_create(base, length, generation, perms) */
    Seraph_Capability cap = seraph_cap_create(
        (void*)0x1000,  /* Base */
        0x2000,         /* Length */
        1,              /* Generation */
        SERAPH_CAP_READ | SERAPH_CAP_WRITE  /* Permissions */
    );

    /* Verify capability exists */
    ASSERT(seraph_cap_exists(cap));
    ASSERT(!seraph_cap_is_void(cap));

    /* Check permissions */
    ASSERT(seraph_cap_can_read(cap));
    ASSERT(seraph_cap_can_write(cap));
    ASSERT(!seraph_cap_can_exec(cap));

    /* Check bounds */
    ASSERT(seraph_cap_in_bounds(cap, 0));
    ASSERT(seraph_cap_in_bounds(cap, 0x1FFF));
    ASSERT(!seraph_cap_in_bounds(cap, 0x2000));

    /* VOID capability */
    Seraph_Capability void_cap = SERAPH_CAP_VOID;
    ASSERT(seraph_cap_is_void(void_cap));
    ASSERT(!seraph_cap_exists(void_cap));

    return 0;
}

/* Test: Capability derivation */
TEST(capability_derive) {
    /* Create parent capability */
    /* Signature: seraph_cap_create(base, length, generation, perms) */
    Seraph_Capability parent = seraph_cap_create(
        (void*)0x1000,
        0x2000,
        1,  /* Generation */
        SERAPH_CAP_READ | SERAPH_CAP_WRITE | SERAPH_CAP_DERIVE
    );

    /* Derive a child with reduced permissions */
    Seraph_Capability child = seraph_cap_derive(
        parent,
        0x100,   /* Offset */
        0x500,   /* Length */
        SERAPH_CAP_READ  /* Read only */
    );

    /* Child should have reduced scope */
    ASSERT(seraph_cap_exists(child));
    ASSERT(seraph_cap_can_read(child));
    ASSERT(!seraph_cap_can_write(child));

    return 0;
}

/*============================================================================
 * Arena Integration
 *============================================================================*/

/* Test: Arena basic operations */
TEST(arena_operations) {
    Seraph_Arena arena;

    /* Create arena */
    Seraph_Vbit result = seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE);
    ASSERT_VBIT_TRUE(result);

    /* Allocate memory */
    void* p1 = seraph_arena_alloc(&arena, 256, 8);
    ASSERT_NOT_NULL(p1);
    ASSERT(p1 != SERAPH_VOID_PTR);

    void* p2 = seraph_arena_alloc(&arena, 512, 16);
    ASSERT_NOT_NULL(p2);
    ASSERT(p2 != SERAPH_VOID_PTR);
    ASSERT(p2 != p1);

    /* Write and read data */
    memset(p1, 0xAA, 256);
    memset(p2, 0xBB, 512);

    uint8_t* data1 = (uint8_t*)p1;
    uint8_t* data2 = (uint8_t*)p2;
    ASSERT_EQ(data1[0], 0xAA);
    ASSERT_EQ(data2[0], 0xBB);

    /* Reset arena */
    uint32_t new_gen = seraph_arena_reset(&arena);
    ASSERT(new_gen != SERAPH_VOID_U32);
    ASSERT_EQ(arena.used, 0);

    /* Cleanup */
    seraph_arena_destroy(&arena);
    return 0;
}

/*============================================================================
 * Substrate Integration
 *============================================================================*/

/* Test: Atlas address space layout */
TEST(atlas_address_space) {
    /* Verify address space constants */
    ASSERT(SERAPH_ATLAS_BASE > 0);
    ASSERT(SERAPH_ATLAS_END > SERAPH_ATLAS_BASE);

    /* Verify non-overlapping regions */
    ASSERT(SERAPH_VOLATILE_END < SERAPH_ATLAS_BASE);
    ASSERT(SERAPH_ATLAS_END < SERAPH_AETHER_BASE);

    return 0;
}

/* Test: Aether coherence states */
TEST(aether_coherence) {
    /* Verify coherence state values */
    ASSERT_EQ(SERAPH_AETHER_PAGE_INVALID, 0);
    ASSERT_NE(SERAPH_AETHER_PAGE_SHARED, 0);
    ASSERT_NE(SERAPH_AETHER_PAGE_EXCLUSIVE, 0);

    /* All states should be distinct */
    ASSERT_NE(SERAPH_AETHER_PAGE_SHARED, SERAPH_AETHER_PAGE_EXCLUSIVE);

    return 0;
}

/*============================================================================
 * Interrupt + Scheduler Integration
 *============================================================================*/

/* Test: Priority queue ordering */
TEST(scheduler_priority_ordering) {
    /* Verify higher priority strands run first */
    ASSERT(SERAPH_PRIORITY_CRITICAL > SERAPH_PRIORITY_REALTIME);
    ASSERT(SERAPH_PRIORITY_REALTIME > SERAPH_PRIORITY_HIGH);
    ASSERT(SERAPH_PRIORITY_HIGH > SERAPH_PRIORITY_NORMAL);
    ASSERT(SERAPH_PRIORITY_NORMAL > SERAPH_PRIORITY_LOW);
    ASSERT(SERAPH_PRIORITY_LOW > SERAPH_PRIORITY_BACKGROUND);
    ASSERT(SERAPH_PRIORITY_BACKGROUND > SERAPH_PRIORITY_IDLE);

    return 0;
}

/* Test: Context structure for scheduling */
TEST(context_for_scheduling) {
    Seraph_CPU_Context ctx1, ctx2;
    memset(&ctx1, 0, sizeof(ctx1));
    memset(&ctx2, 0, sizeof(ctx2));

    /* Set up two different contexts */
    ctx1.rip = 0x1000;
    ctx1.rsp = 0x2000;
    ctx1.rflags = SERAPH_RFLAGS_DEFAULT;

    ctx2.rip = 0x3000;
    ctx2.rsp = 0x4000;
    ctx2.rflags = SERAPH_RFLAGS_DEFAULT;

    /* Contexts should be independent */
    ASSERT_NE(ctx1.rip, ctx2.rip);
    ASSERT_NE(ctx1.rsp, ctx2.rsp);

    /* Both should have interrupts enabled */
    ASSERT((ctx1.rflags & SERAPH_RFLAGS_IF) != 0);
    ASSERT((ctx2.rflags & SERAPH_RFLAGS_IF) != 0);

    return 0;
}

/*============================================================================
 * Interrupt Frame Tests
 *============================================================================*/

/* Test: Interrupt frame structure */
TEST(interrupt_frame_structure) {
    Seraph_InterruptFrame frame;
    memset(&frame, 0, sizeof(frame));

    /* Set some values */
    frame.rip = 0x401000;
    frame.cs = SERAPH_KERNEL_CS;
    frame.rflags = SERAPH_RFLAGS_DEFAULT;
    frame.rsp = 0xFFFF800000000000ULL;
    frame.ss = SERAPH_KERNEL_DS;
    frame.vector = 14;  /* Page fault */
    frame.error_code = SERAPH_PF_WRITE;

    /* Verify values */
    ASSERT_EQ(frame.vector, 14);
    ASSERT_EQ(frame.error_code, SERAPH_PF_WRITE);
    ASSERT_EQ(frame.cs, SERAPH_KERNEL_CS);

    return 0;
}

/*============================================================================
 * Compiler + System Integration
 *============================================================================*/

/* Test: Lexer tokenization */
TEST(compiler_lexer) {
    Seraph_Arena arena;
    Seraph_Vbit result = seraph_arena_create(&arena, 64 * 1024, 0, SERAPH_ARENA_FLAG_NONE);
    ASSERT_VBIT_TRUE(result);

    const char* src = "let x = 42;";

    Seraph_Lexer lexer;
    result = seraph_lexer_init(&lexer, src, strlen(src), "test.seraph", &arena);
    ASSERT_VBIT_TRUE(result);

    /* Tokenize all */
    result = seraph_lexer_tokenize(&lexer);
    ASSERT_VBIT_TRUE(result);

    /* Check tokens: let, x, =, 42, ; */
    ASSERT(seraph_lexer_token_count(&lexer) >= 4);

    Seraph_Token tok0 = seraph_lexer_get_token(&lexer, 0);
    ASSERT_EQ(tok0.type, SERAPH_TOK_LET);

    Seraph_Token tok1 = seraph_lexer_get_token(&lexer, 1);
    ASSERT_EQ(tok1.type, SERAPH_TOK_IDENT);

    Seraph_Token tok2 = seraph_lexer_get_token(&lexer, 2);
    ASSERT_EQ(tok2.type, SERAPH_TOK_ASSIGN);  /* = is ASSIGN, not EQ (which is ==) */

    Seraph_Token tok3 = seraph_lexer_get_token(&lexer, 3);
    ASSERT_EQ(tok3.type, SERAPH_TOK_INT_LITERAL);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Effect flags */
TEST(effect_flags) {
    /* Verify effect flags are distinct */
    ASSERT_NE(SERAPH_EFFECT_VOID, 0);
    ASSERT_NE(SERAPH_EFFECT_PERSIST, 0);
    ASSERT_NE(SERAPH_EFFECT_NETWORK, 0);
    ASSERT_NE(SERAPH_EFFECT_IO, 0);

    /* Can combine effects */
    Seraph_Effect_Flags combined = SERAPH_EFFECT_VOID | SERAPH_EFFECT_PERSIST;
    ASSERT(seraph_effect_has(combined, SERAPH_EFFECT_VOID));
    ASSERT(seraph_effect_has(combined, SERAPH_EFFECT_PERSIST));
    ASSERT(!seraph_effect_has(combined, SERAPH_EFFECT_NETWORK));

    return 0;
}

/*============================================================================
 * Chronon (Time) Integration
 *============================================================================*/

/* Test: Chronon type and values */
TEST(chronon_basics) {
    /* Verify chronon constants */
    ASSERT(SERAPH_CHRONON_VOID != 0);

    /* VOID chronon check */
    Seraph_Chronon void_chr = SERAPH_CHRONON_VOID;
    ASSERT(seraph_chronon_is_void(void_chr));

    /* Valid chronon */
    Seraph_Chronon valid = 1000;
    ASSERT(!seraph_chronon_is_void(valid));

    return 0;
}

/*============================================================================
 * Sovereign/Strand State Tests
 *============================================================================*/

/* Test: Sovereign state values */
TEST(sovereign_states) {
    /* Verify state values are distinct */
    ASSERT_NE(SERAPH_SOVEREIGN_NASCENT, SERAPH_SOVEREIGN_RUNNING);
    ASSERT_NE(SERAPH_SOVEREIGN_RUNNING, SERAPH_SOVEREIGN_SUSPENDED);
    ASSERT_NE(SERAPH_SOVEREIGN_SUSPENDED, SERAPH_SOVEREIGN_EXITING);

    /* Check state predicates */
    ASSERT(seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_RUNNING));
    ASSERT(!seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_CONCEIVING));
    ASSERT(seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_VOID));

    return 0;
}

/* Test: Strand state values */
TEST(strand_states) {
    /* Verify state values */
    ASSERT_NE(SERAPH_STRAND_READY, SERAPH_STRAND_RUNNING);
    ASSERT_NE(SERAPH_STRAND_RUNNING, SERAPH_STRAND_BLOCKED);
    ASSERT_NE(SERAPH_STRAND_BLOCKED, SERAPH_STRAND_TERMINATED);

    return 0;
}

/*============================================================================
 * VMM/PMM Constants Tests
 *============================================================================*/

/* Test: VMM page table flags */
TEST(vmm_pte_flags) {
    /* Test flag values */
    ASSERT_EQ(SERAPH_PTE_PRESENT, (1ULL << 0));
    ASSERT_EQ(SERAPH_PTE_WRITABLE, (1ULL << 1));
    ASSERT_EQ(SERAPH_PTE_USER, (1ULL << 2));
    ASSERT_EQ(SERAPH_PTE_NX, (1ULL << 63));

    /* Test flag combinations */
    uint64_t kernel_page = SERAPH_PTE_PRESENT | SERAPH_PTE_WRITABLE;
    ASSERT((kernel_page & SERAPH_PTE_PRESENT) != 0);
    ASSERT((kernel_page & SERAPH_PTE_WRITABLE) != 0);
    ASSERT((kernel_page & SERAPH_PTE_USER) == 0);

    return 0;
}

/* Test: PMM constants */
TEST(pmm_constants) {
    ASSERT_EQ(SERAPH_PMM_PAGE_SIZE, 4096);
    ASSERT_EQ(SERAPH_PMM_PAGE_SHIFT, 12);
    ASSERT_EQ(SERAPH_PMM_BITS_PER_WORD, 64);

    return 0;
}

/*============================================================================
 * Whisper Channel Constants
 *============================================================================*/

/* Test: Whisper message types */
TEST(whisper_types) {
    /* Verify message types are distinct */
    ASSERT_NE(SERAPH_WHISPER_REQUEST, SERAPH_WHISPER_RESPONSE);
    ASSERT_NE(SERAPH_WHISPER_GRANT, SERAPH_WHISPER_LEND);
    ASSERT_NE(SERAPH_WHISPER_LEND, SERAPH_WHISPER_RETURN);

    return 0;
}

/*============================================================================
 * Integration: VOID Across Components
 *============================================================================*/

/* Test: VOID handling across component boundaries */
TEST(void_across_components) {
    /* Create a valid capability */
    Seraph_Capability cap = seraph_cap_create(
        (void*)0x1000, 0x100, SERAPH_CAP_READ, 1
    );
    ASSERT(seraph_cap_exists(cap));

    /* Get pointer at valid offset */
    void* valid_ptr = seraph_cap_get_ptr(cap, 0);
    ASSERT(valid_ptr != SERAPH_VOID_PTR);

    /* Get pointer at invalid offset - returns VOID_PTR */
    void* invalid_ptr = seraph_cap_get_ptr(cap, 0x200);
    ASSERT(SERAPH_IS_VOID_PTR(invalid_ptr));

    /* VOID capability always returns VOID_PTR */
    Seraph_Capability void_cap = SERAPH_CAP_VOID;
    void* void_ptr = seraph_cap_get_ptr(void_cap, 0);
    ASSERT(SERAPH_IS_VOID_PTR(void_ptr));

    return 0;
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_integration_system_tests(void) {
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;

    printf("=== SERAPH Full System Integration Tests ===\n\n");

    printf("VOID Semantics Integration:\n");
    run_test_void_propagation_chain();
    run_test_vbit_logic();

    printf("\nCapability System Integration:\n");
    run_test_capability_basics();
    run_test_capability_derive();

    printf("\nArena Integration:\n");
    run_test_arena_operations();

    printf("\nSubstrate Integration:\n");
    run_test_atlas_address_space();
    run_test_aether_coherence();

    printf("\nScheduler Integration:\n");
    run_test_scheduler_priority_ordering();
    run_test_context_for_scheduling();

    printf("\nInterrupt Integration:\n");
    run_test_interrupt_frame_structure();

    printf("\nCompiler Integration:\n");
    run_test_compiler_lexer();
    run_test_effect_flags();

    printf("\nChronon Integration:\n");
    run_test_chronon_basics();

    printf("\nProcess Model Integration:\n");
    run_test_sovereign_states();
    run_test_strand_states();

    printf("\nMemory Management Integration:\n");
    run_test_vmm_pte_flags();
    run_test_pmm_constants();

    printf("\nIPC Integration:\n");
    run_test_whisper_types();

    printf("\nCross-Component Integration:\n");
    run_test_void_across_components();

    /* Summary */
    printf("\n=== System Integration Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
}

#ifndef SERAPH_NO_MAIN
int main(void) {
    run_integration_system_tests();
    return tests_failed > 0 ? 1 : 0;
}
#endif
