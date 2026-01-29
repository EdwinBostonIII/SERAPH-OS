/**
 * @file test_main.c
 * @brief Main test runner for SERAPH OS
 *
 * Runs all test suites and reports results.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test suite declarations - Phase 1: Foundation Layer */
extern void run_void_tests(void);
extern void run_vbit_tests(void);
extern void run_bits_tests(void);
extern void run_semantic_byte_tests(void);
extern void run_integer_tests(void);
extern void run_q128_tests(void);
extern void run_galactic_tests(void);
/* Note: Galactic scheduler tests run as separate executable */

/* Test suite declarations - Phase 2: Memory Safety */
extern void run_capability_tests(void);
extern void run_chronon_tests(void);
extern void run_arena_tests(void);

/* Test suite declarations - Phase 3: Graphics Foundation */
extern void run_glyph_tests(void);

/* Test suite declarations - Phase 4: Process Model */
extern void run_sovereign_tests(void);

/* Test suite declarations - Phase 5: UI System */
extern void run_surface_tests(void);

/* Test suite declarations - Phase 6: IPC */
extern void run_whisper_tests(void);

/* Test suite declarations - Phase 7: Threading */
extern void test_strand(void);

/* Test suite declarations - Phase 8: Persistent Storage */
extern void run_atlas_tests(void);

/* Test suite declarations - Phase 9: Distributed Memory */
extern void run_aether_tests(void);

/* Test suite declarations - Phase 10: Seraphim Compiler */
extern void run_seraphim_lexer_tests(void);
extern void run_seraphim_parser_tests(void);
extern void run_seraphim_types_tests(void);
extern void run_seraphim_effects_tests(void);
/* Note: proofs and codegen tests run as separate executables */
#ifdef SERAPH_INCLUDE_COMPILER_FULL_TESTS
extern void run_seraphim_proofs_tests(void);
extern void run_seraphim_codegen_tests(void);
#endif

/* Test suite declarations - Phase 11: Integration Tests
 * Note: Integration tests are designed to run as standalone executables.
 * They can be run via CTest: ctest -R integration
 * Or directly: ./test_integration_memory, ./test_integration_system, etc.
 */
#ifdef SERAPH_INCLUDE_INTEGRATION_TESTS
extern void run_integration_memory_tests(void);
extern void run_integration_interrupts_tests(void);
extern void run_integration_compiler_tests(void);
extern void run_integration_drivers_tests(void);
extern void run_integration_system_tests(void);
#endif

/* Test suite declarations - Phase 12: PRISM Hypervisor Extensions */
#ifdef SERAPH_INCLUDE_PRISM_TESTS
extern void run_resonance_tests(void);
extern void run_hive_tests(void);
extern void run_entropic_tests(void);
extern void run_akashic_tests(void);
#endif

/*============================================================================
 * Main Entry Point
 *============================================================================*/

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("     SERAPH Operating System Tests     \n");
    printf("     Phase 1: Foundation Layer         \n");
    printf("========================================\n");

    /* Check for specific test suite argument */
    const char* suite = NULL;
    if (argc > 1) {
        suite = argv[1];
    }

    int suites_run = 0;
    int suites_passed = 0;

    /* Run test suites */

    if (!suite || strcmp(suite, "void") == 0) {
        run_void_tests();
        suites_run++;
        suites_passed++;  /* Would have exited on failure */
    }

    if (!suite || strcmp(suite, "vbit") == 0) {
        run_vbit_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "bits") == 0) {
        run_bits_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "sbyte") == 0) {
        run_semantic_byte_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "integers") == 0) {
        run_integer_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "q128") == 0) {
        run_q128_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "galactic") == 0) {
        run_galactic_tests();
        suites_run++;
        suites_passed++;
    }

    /* Note: galactic_sched tests run as separate executable */

    /* Phase 2: Memory Safety */
    if (!suite || strcmp(suite, "capability") == 0) {
        run_capability_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "chronon") == 0) {
        run_chronon_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "arena") == 0) {
        run_arena_tests();
        suites_run++;
        suites_passed++;
    }

    /* Phase 3: Graphics Foundation */
    if (!suite || strcmp(suite, "glyph") == 0) {
        run_glyph_tests();
        suites_run++;
        suites_passed++;
    }

    /* Phase 4: Process Model */
    if (!suite || strcmp(suite, "sovereign") == 0) {
        run_sovereign_tests();
        suites_run++;
        suites_passed++;
    }

    /* Phase 5: UI System */
    if (!suite || strcmp(suite, "surface") == 0) {
        run_surface_tests();
        suites_run++;
        suites_passed++;
    }

    /* Phase 6: IPC */
    if (!suite || strcmp(suite, "whisper") == 0) {
        run_whisper_tests();
        suites_run++;
        suites_passed++;
    }

    /* Phase 7: Threading */
    if (!suite || strcmp(suite, "strand") == 0) {
        test_strand();
        suites_run++;
        suites_passed++;
    }

    /* Phase 8: Persistent Storage */
    if (!suite || strcmp(suite, "atlas") == 0) {
        run_atlas_tests();
        suites_run++;
        suites_passed++;
    }

    /* Phase 9: Distributed Memory */
    if (!suite || strcmp(suite, "aether") == 0) {
        run_aether_tests();
        suites_run++;
        suites_passed++;
    }

    /* Phase 10: Seraphim Compiler */
    if (!suite || strcmp(suite, "seraphim") == 0 || strcmp(suite, "lexer") == 0) {
        run_seraphim_lexer_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "seraphim") == 0 || strcmp(suite, "parser") == 0) {
        run_seraphim_parser_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "seraphim") == 0 || strcmp(suite, "types") == 0) {
        run_seraphim_types_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "seraphim") == 0 || strcmp(suite, "effects") == 0) {
        run_seraphim_effects_tests();
        suites_run++;
        suites_passed++;
    }

#ifdef SERAPH_INCLUDE_COMPILER_FULL_TESTS
    if (!suite || strcmp(suite, "seraphim") == 0 || strcmp(suite, "proofs") == 0) {
        run_seraphim_proofs_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "seraphim") == 0 || strcmp(suite, "codegen") == 0) {
        run_seraphim_codegen_tests();
        suites_run++;
        suites_passed++;
    }
#endif

    /* Phase 11: Integration Tests
     * Note: Run via CTest or standalone executables.
     * To include in main test runner, define SERAPH_INCLUDE_INTEGRATION_TESTS
     */
#ifdef SERAPH_INCLUDE_INTEGRATION_TESTS
    if (!suite || strcmp(suite, "integration") == 0 || strcmp(suite, "memory_int") == 0) {
        run_integration_memory_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "integration") == 0 || strcmp(suite, "interrupts_int") == 0) {
        run_integration_interrupts_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "integration") == 0 || strcmp(suite, "compiler_int") == 0) {
        run_integration_compiler_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "integration") == 0 || strcmp(suite, "drivers_int") == 0) {
        run_integration_drivers_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "integration") == 0 || strcmp(suite, "system_int") == 0) {
        run_integration_system_tests();
        suites_run++;
        suites_passed++;
    }
#endif /* SERAPH_INCLUDE_INTEGRATION_TESTS */

    /* Phase 12: PRISM Hypervisor Extensions
     * Note: Run via CTest or standalone executables.
     * To include in main test runner, define SERAPH_INCLUDE_PRISM_TESTS
     */
#ifdef SERAPH_INCLUDE_PRISM_TESTS
    if (!suite || strcmp(suite, "prism") == 0 || strcmp(suite, "resonance") == 0) {
        run_resonance_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "prism") == 0 || strcmp(suite, "hive") == 0) {
        run_hive_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "prism") == 0 || strcmp(suite, "entropic") == 0) {
        run_entropic_tests();
        suites_run++;
        suites_passed++;
    }

    if (!suite || strcmp(suite, "prism") == 0 || strcmp(suite, "akashic") == 0) {
        run_akashic_tests();
        suites_run++;
        suites_passed++;
    }
#endif /* SERAPH_INCLUDE_PRISM_TESTS */

    /* Summary */
    printf("\n========================================\n");
    printf("     Test Summary                      \n");
    printf("========================================\n");
    printf("Test suites run: %d\n", suites_run);
    printf("Test suites passed: %d\n", suites_passed);

    if (suites_passed == suites_run) {
        printf("\n*** ALL TESTS PASSED ***\n\n");
        return 0;
    } else {
        printf("\n*** SOME TESTS FAILED ***\n\n");
        return 1;
    }
}
