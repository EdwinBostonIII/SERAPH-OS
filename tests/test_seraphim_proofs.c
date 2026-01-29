/**
 * @file test_seraphim_proofs.c
 * @brief Unit Tests for Seraphim Proof Generation System
 *
 * MC-TEST-26A: Seraphim Proof System Testing
 *
 * This test suite verifies the proof generation and verification system:
 *
 *   - Proof kind enumeration and constants
 *   - Proof status enumeration and constants
 *   - Proof table initialization and operations
 *   - Bounds proof generation
 *   - VOID handling proof generation
 *   - Effect proof generation
 *   - Permission proof generation
 *   - Type safety proof generation
 *   - Proof verification and counting
 */

#include "seraph/seraphim/proofs.h"
#include "seraph/arena.h"
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
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)

/*============================================================================
 * Proof Kind Tests
 *============================================================================*/

/* Test: Proof kind enumeration values */
TEST(proof_kind_values) {
    /* Verify proof kind constants */
    ASSERT_EQ(SERAPH_PROOF_BOUNDS, 0x01);
    ASSERT_EQ(SERAPH_PROOF_VOID, 0x02);
    ASSERT_EQ(SERAPH_PROOF_EFFECT, 0x03);
    ASSERT_EQ(SERAPH_PROOF_PERMISSION, 0x04);
    ASSERT_EQ(SERAPH_PROOF_GENERATION, 0x05);
    ASSERT_EQ(SERAPH_PROOF_SUBSTRATE, 0x06);
    ASSERT_EQ(SERAPH_PROOF_TYPE, 0x07);
    ASSERT_EQ(SERAPH_PROOF_INIT, 0x08);
    ASSERT_EQ(SERAPH_PROOF_OVERFLOW, 0x09);
    ASSERT_EQ(SERAPH_PROOF_NULL, 0x0A);
    ASSERT_EQ(SERAPH_PROOF_INVARIANT, 0x0B);
    ASSERT_EQ(SERAPH_PROOF_TERMINATION, 0x0C);
    ASSERT_EQ(SERAPH_PROOF_VOID_KIND, 0xFF);

    return 0;
}

/* Test: Proof kind names */
TEST(proof_kind_names) {
    /* Verify proof kind name resolution */
    const char* name;

    name = seraph_proof_kind_name(SERAPH_PROOF_BOUNDS);
    ASSERT_NOT_NULL(name);
    ASSERT(strcmp(name, "BOUNDS") == 0 || strstr(name, "bounds") != NULL ||
           strstr(name, "Bounds") != NULL);

    name = seraph_proof_kind_name(SERAPH_PROOF_VOID);
    ASSERT_NOT_NULL(name);

    name = seraph_proof_kind_name(SERAPH_PROOF_EFFECT);
    ASSERT_NOT_NULL(name);

    return 0;
}

/*============================================================================
 * Proof Status Tests
 *============================================================================*/

/* Test: Proof status enumeration values */
TEST(proof_status_values) {
    ASSERT_EQ(SERAPH_PROOF_STATUS_PROVEN, 0x01);
    ASSERT_EQ(SERAPH_PROOF_STATUS_ASSUMED, 0x02);
    ASSERT_EQ(SERAPH_PROOF_STATUS_RUNTIME, 0x03);
    ASSERT_EQ(SERAPH_PROOF_STATUS_FAILED, 0x04);
    ASSERT_EQ(SERAPH_PROOF_STATUS_SKIPPED, 0x05);

    return 0;
}

/* Test: Proof status names */
TEST(proof_status_names) {
    const char* name;

    name = seraph_proof_status_name(SERAPH_PROOF_STATUS_PROVEN);
    ASSERT_NOT_NULL(name);

    name = seraph_proof_status_name(SERAPH_PROOF_STATUS_FAILED);
    ASSERT_NOT_NULL(name);

    return 0;
}

/*============================================================================
 * Proof Structure Tests
 *============================================================================*/

/* Test: Proof structure layout */
TEST(proof_structure) {
    Seraph_Proof proof;
    memset(&proof, 0, sizeof(proof));

    /* Set up a bounds proof */
    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    proof.loc.line = 42;
    proof.loc.column = 10;
    proof.description = "Array access within bounds";
    proof.bounds.array_size = 100;
    proof.bounds.index_min = 0;
    proof.bounds.index_max = 50;

    ASSERT_EQ(proof.kind, SERAPH_PROOF_BOUNDS);
    ASSERT_EQ(proof.status, SERAPH_PROOF_STATUS_PROVEN);
    ASSERT_EQ(proof.loc.line, 42);
    ASSERT_EQ(proof.bounds.array_size, 100);
    ASSERT_EQ(proof.bounds.index_max, 50);

    return 0;
}

/* Test: Effect proof structure */
TEST(effect_proof_structure) {
    Seraph_Proof proof;
    memset(&proof, 0, sizeof(proof));

    proof.kind = SERAPH_PROOF_EFFECT;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    proof.effects.required_effects = 0x03;  /* VOID | PERSIST */
    proof.effects.allowed_effects = 0x07;   /* VOID | PERSIST | NETWORK */

    ASSERT_EQ(proof.effects.required_effects, 0x03);
    ASSERT_EQ(proof.effects.allowed_effects, 0x07);
    /* Required effects should be subset of allowed */
    ASSERT((proof.effects.required_effects & proof.effects.allowed_effects) ==
           proof.effects.required_effects);

    return 0;
}

/* Test: Permission proof structure */
TEST(permission_proof_structure) {
    Seraph_Proof proof;
    memset(&proof, 0, sizeof(proof));

    proof.kind = SERAPH_PROOF_PERMISSION;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    proof.permissions.required_perms = 0x03;  /* Read | Write */
    proof.permissions.granted_perms = 0x07;   /* Read | Write | Execute */

    /* Required should be subset of granted */
    ASSERT((proof.permissions.required_perms & proof.permissions.granted_perms) ==
           proof.permissions.required_perms);

    return 0;
}

/*============================================================================
 * Proof Table Tests
 *============================================================================*/

/* Test: Proof table initialization */
TEST(proof_table_init) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));

    Seraph_Vbit result = seraph_proof_table_init(&table, &arena);
    ASSERT(seraph_vbit_is_true(result));
    ASSERT_EQ(table.count, 0);
    ASSERT_EQ(table.proven_count, 0);
    ASSERT_EQ(table.runtime_count, 0);
    ASSERT_EQ(table.failed_count, 0);
    ASSERT_EQ(table.arena, &arena);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Adding proofs to table */
TEST(proof_table_add) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    /* Add a proof manually */
    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    proof.description = "Test bounds proof";

    seraph_proof_add(&table, proof);
    ASSERT_EQ(table.count, 1);

    /* Add another proof */
    proof.kind = SERAPH_PROOF_VOID;
    proof.status = SERAPH_PROOF_STATUS_RUNTIME;
    seraph_proof_add(&table, proof);
    ASSERT_EQ(table.count, 2);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Adding bounds proofs */
TEST(proof_table_add_bounds) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 10, .column = 5 };
    seraph_proof_add_bounds(&table, loc, 100, 0, 50, SERAPH_PROOF_STATUS_PROVEN);

    ASSERT_EQ(table.count, 1);
    ASSERT_NOT_NULL(table.proofs);
    ASSERT_EQ(table.proofs->kind, SERAPH_PROOF_BOUNDS);
    ASSERT_EQ(table.proofs->bounds.array_size, 100);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Adding VOID proofs */
TEST(proof_table_add_void) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 20, .column = 15 };
    seraph_proof_add_void(&table, loc, "VOID propagated via ??",
                           SERAPH_PROOF_STATUS_PROVEN);

    ASSERT_EQ(table.count, 1);
    ASSERT_NOT_NULL(table.proofs);
    ASSERT_EQ(table.proofs->kind, SERAPH_PROOF_VOID);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Adding effect proofs */
TEST(proof_table_add_effect) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 30, .column = 1 };
    seraph_proof_add_effect(&table, loc, 0x01, 0x03, SERAPH_PROOF_STATUS_PROVEN);

    ASSERT_EQ(table.count, 1);
    ASSERT_NOT_NULL(table.proofs);
    ASSERT_EQ(table.proofs->kind, SERAPH_PROOF_EFFECT);
    ASSERT_EQ(table.proofs->effects.required_effects, 0x01);
    ASSERT_EQ(table.proofs->effects.allowed_effects, 0x03);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Adding permission proofs */
TEST(proof_table_add_permission) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 40, .column = 20 };
    seraph_proof_add_permission(&table, loc, 0x01, 0x03, SERAPH_PROOF_STATUS_PROVEN);

    ASSERT_EQ(table.count, 1);
    ASSERT_NOT_NULL(table.proofs);
    ASSERT_EQ(table.proofs->kind, SERAPH_PROOF_PERMISSION);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Adding type proofs */
TEST(proof_table_add_type) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 50, .column = 8 };
    seraph_proof_add_type(&table, loc, "u32 fits in u64",
                           SERAPH_PROOF_STATUS_PROVEN);

    ASSERT_EQ(table.count, 1);
    ASSERT_NOT_NULL(table.proofs);
    ASSERT_EQ(table.proofs->kind, SERAPH_PROOF_TYPE);

    seraph_arena_destroy(&arena);
    return 0;
}

/*============================================================================
 * Proof Counting Tests
 *============================================================================*/

/* Test: Count proofs by status */
TEST(proof_count_by_status) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 1, .column = 1 };

    /* Add mix of proven and runtime proofs */
    seraph_proof_add_bounds(&table, loc, 100, 0, 50, SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_bounds(&table, loc, 100, 0, 150, SERAPH_PROOF_STATUS_RUNTIME);
    seraph_proof_add_void(&table, loc, "test", SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_void(&table, loc, "test2", SERAPH_PROOF_STATUS_FAILED);

    ASSERT_EQ(table.count, 4);

    size_t proven = seraph_proof_count_by_status(&table, SERAPH_PROOF_STATUS_PROVEN);
    size_t runtime = seraph_proof_count_by_status(&table, SERAPH_PROOF_STATUS_RUNTIME);
    size_t failed = seraph_proof_count_by_status(&table, SERAPH_PROOF_STATUS_FAILED);

    ASSERT_EQ(proven, 2);
    ASSERT_EQ(runtime, 1);
    ASSERT_EQ(failed, 1);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Count proofs by kind */
TEST(proof_count_by_kind) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 1, .column = 1 };

    /* Add different kinds of proofs */
    seraph_proof_add_bounds(&table, loc, 100, 0, 50, SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_bounds(&table, loc, 200, 0, 100, SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_void(&table, loc, "test", SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_effect(&table, loc, 0x01, 0x03, SERAPH_PROOF_STATUS_PROVEN);

    ASSERT_EQ(table.count, 4);

    size_t bounds = seraph_proof_count_by_kind(&table, SERAPH_PROOF_BOUNDS);
    size_t void_proofs = seraph_proof_count_by_kind(&table, SERAPH_PROOF_VOID);
    size_t effect = seraph_proof_count_by_kind(&table, SERAPH_PROOF_EFFECT);
    size_t permission = seraph_proof_count_by_kind(&table, SERAPH_PROOF_PERMISSION);

    ASSERT_EQ(bounds, 2);
    ASSERT_EQ(void_proofs, 1);
    ASSERT_EQ(effect, 1);
    ASSERT_EQ(permission, 0);

    seraph_arena_destroy(&arena);
    return 0;
}

/*============================================================================
 * Proof Verification Tests
 *============================================================================*/

/* Test: Verify all proofs - all pass */
TEST(proof_verify_all_pass) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 1, .column = 1 };

    /* All proofs are proven */
    seraph_proof_add_bounds(&table, loc, 100, 0, 50, SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_void(&table, loc, "test", SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_effect(&table, loc, 0x01, 0x03, SERAPH_PROOF_STATUS_PROVEN);

    int result = seraph_proof_verify_all(&table);
    ASSERT_EQ(result, 1);  /* All pass */

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Verify all proofs - some fail */
TEST(proof_verify_all_fail) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 1, .column = 1 };

    /* Mix of proven and failed */
    seraph_proof_add_bounds(&table, loc, 100, 0, 50, SERAPH_PROOF_STATUS_PROVEN);
    seraph_proof_add_bounds(&table, loc, 100, 0, 200, SERAPH_PROOF_STATUS_FAILED);

    int result = seraph_proof_verify_all(&table);
    ASSERT_EQ(result, 0);  /* Some failed */

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Verify empty table */
TEST(proof_verify_empty) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    /* Empty table should verify successfully */
    int result = seraph_proof_verify_all(&table);
    ASSERT_EQ(result, 1);

    seraph_arena_destroy(&arena);
    return 0;
}

/*============================================================================
 * Proof Linked List Tests
 *============================================================================*/

/* Test: Proof linked list traversal */
TEST(proof_linked_list) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 16 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    Seraph_Source_Loc loc = { .line = 1, .column = 1 };

    /* Add multiple proofs */
    seraph_proof_add_bounds(&table, loc, 100, 0, 50, SERAPH_PROOF_STATUS_PROVEN);
    loc.line = 2;
    seraph_proof_add_void(&table, loc, "test", SERAPH_PROOF_STATUS_PROVEN);
    loc.line = 3;
    seraph_proof_add_effect(&table, loc, 0x01, 0x03, SERAPH_PROOF_STATUS_PROVEN);

    /* Count by traversing linked list */
    int count = 0;
    Seraph_Proof* current = table.proofs;
    while (current != NULL) {
        count++;
        current = current->next;
    }

    ASSERT_EQ(count, 3);
    ASSERT_EQ(table.count, 3);

    seraph_arena_destroy(&arena);
    return 0;
}

/*============================================================================
 * Integration Tests
 *============================================================================*/

/* Test: Complete proof workflow */
TEST(complete_proof_workflow) {
    Seraph_Arena arena;
    Seraph_Proof_Table table;

    ASSERT(seraph_vbit_is_true(seraph_arena_create(&arena, 64 * 1024, 0, SERAPH_ARENA_FLAG_NONE)));
    ASSERT(seraph_vbit_is_true(seraph_proof_table_init(&table, &arena)));

    /* Simulate a realistic proof generation scenario */
    Seraph_Source_Loc loc;

    /* Function with array access */
    loc.line = 10; loc.column = 5;
    seraph_proof_add_bounds(&table, loc, 1000, 0, 999, SERAPH_PROOF_STATUS_PROVEN);

    /* VOID propagation */
    loc.line = 15; loc.column = 12;
    seraph_proof_add_void(&table, loc, "Division result propagated",
                           SERAPH_PROOF_STATUS_PROVEN);

    /* Effect verification for pure function */
    loc.line = 5; loc.column = 1;
    seraph_proof_add_effect(&table, loc, 0x00, 0x00, SERAPH_PROOF_STATUS_PROVEN);

    /* Capability permission check */
    loc.line = 25; loc.column = 8;
    seraph_proof_add_permission(&table, loc, 0x01, 0x03, SERAPH_PROOF_STATUS_PROVEN);

    /* Type safety */
    loc.line = 30; loc.column = 10;
    seraph_proof_add_type(&table, loc, "i32 narrowing checked",
                           SERAPH_PROOF_STATUS_RUNTIME);

    /* Verify statistics */
    ASSERT_EQ(table.count, 5);
    ASSERT_EQ(seraph_proof_count_by_status(&table, SERAPH_PROOF_STATUS_PROVEN), 4);
    ASSERT_EQ(seraph_proof_count_by_status(&table, SERAPH_PROOF_STATUS_RUNTIME), 1);

    seraph_arena_destroy(&arena);
    return 0;
}

/*============================================================================
 * Test Runner
 *============================================================================*/

/**
 * @brief Run all proof tests (wrapper for test_main.c)
 */
void run_seraphim_proofs_tests(void) {
    printf("=== Seraphim Proof System Tests ===\n\n");

    printf("Proof Kind Tests:\n");
    run_test_proof_kind_values();
    run_test_proof_kind_names();

    printf("\nProof Status Tests:\n");
    run_test_proof_status_values();
    run_test_proof_status_names();

    printf("\nProof Structure Tests:\n");
    run_test_proof_structure();
    run_test_effect_proof_structure();
    run_test_permission_proof_structure();

    printf("\nProof Table Tests:\n");
    run_test_proof_table_init();
    run_test_proof_table_add();
    run_test_proof_table_add_bounds();
    run_test_proof_table_add_void();
    run_test_proof_table_add_effect();
    run_test_proof_table_add_permission();
    run_test_proof_table_add_type();

    printf("\nProof Counting Tests:\n");
    run_test_proof_count_by_status();
    run_test_proof_count_by_kind();

    printf("\nProof Verification Tests:\n");
    run_test_proof_verify_all_pass();
    run_test_proof_verify_all_fail();
    run_test_proof_verify_empty();

    printf("\nProof Linked List Tests:\n");
    run_test_proof_linked_list();

    printf("\nIntegration Tests:\n");
    run_test_complete_proof_workflow();

    /* Summary */
    printf("\n=== Proof Tests Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
}

#ifndef SERAPH_NO_MAIN
int main(void) {
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;

    printf("=== Seraphim Proof System Tests ===\n\n");

    printf("Proof Kind Tests:\n");
    run_test_proof_kind_values();
    run_test_proof_kind_names();

    printf("\nProof Status Tests:\n");
    run_test_proof_status_values();
    run_test_proof_status_names();

    printf("\nProof Structure Tests:\n");
    run_test_proof_structure();
    run_test_effect_proof_structure();
    run_test_permission_proof_structure();

    printf("\nProof Table Tests:\n");
    run_test_proof_table_init();
    run_test_proof_table_add();
    run_test_proof_table_add_bounds();
    run_test_proof_table_add_void();
    run_test_proof_table_add_effect();
    run_test_proof_table_add_permission();
    run_test_proof_table_add_type();

    printf("\nProof Counting Tests:\n");
    run_test_proof_count_by_status();
    run_test_proof_count_by_kind();

    printf("\nProof Verification Tests:\n");
    run_test_proof_verify_all_pass();
    run_test_proof_verify_all_fail();
    run_test_proof_verify_empty();

    printf("\nProof Linked List Tests:\n");
    run_test_proof_linked_list();

    printf("\nIntegration Tests:\n");
    run_test_complete_proof_workflow();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
#endif /* SERAPH_NO_MAIN */
