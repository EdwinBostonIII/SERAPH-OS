/**
 * @file test_integration_memory.c
 * @brief Integration Tests for Memory Management Subsystem
 *
 * MC-INT-01: Memory Subsystem Integration Testing
 *
 * This test suite verifies that all memory management components
 * work correctly together:
 *
 *   - PMM (Physical Memory Manager) structures and constants
 *   - VMM (Virtual Memory Manager) page table flags
 *   - kmalloc interface definitions
 *   - Arena allocator integration
 */

#include "seraph/pmm.h"
#include "seraph/vmm.h"
#include "seraph/kmalloc.h"
#include "seraph/arena.h"
#include "seraph/void.h"
#include "seraph/boot.h"
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
 * PMM Tests
 *============================================================================*/

/* Test: PMM page size constant */
TEST(pmm_page_size) {
    ASSERT_EQ(SERAPH_PMM_PAGE_SIZE, 4096);
    return 0;
}

/* Test: PMM structure can be created */
TEST(pmm_structure) {
    Seraph_PMM pmm;
    memset(&pmm, 0, sizeof(pmm));

    pmm.total_pages = 1000;
    pmm.free_pages = 500;
    pmm.base_address = 0x100000;

    ASSERT_EQ(pmm.total_pages, 1000);
    ASSERT_EQ(pmm.free_pages, 500);
    ASSERT_EQ(pmm.base_address, 0x100000);

    return 0;
}

/*============================================================================
 * VMM Tests
 *============================================================================*/

/* Test: VMM page table entry flags */
TEST(vmm_pte_flags) {
    ASSERT_EQ(SERAPH_PTE_PRESENT, (1ULL << 0));
    ASSERT_EQ(SERAPH_PTE_WRITABLE, (1ULL << 1));
    ASSERT_EQ(SERAPH_PTE_USER, (1ULL << 2));
    ASSERT_EQ(SERAPH_PTE_NOCACHE, (1ULL << 4));
    ASSERT_EQ(SERAPH_PTE_HUGE, (1ULL << 7));
    ASSERT_EQ(SERAPH_PTE_GLOBAL, (1ULL << 8));
    ASSERT_EQ(SERAPH_PTE_NX, (1ULL << 63));

    return 0;
}

/* Test: VMM address space layout */
TEST(vmm_address_layout) {
    ASSERT_EQ(SERAPH_VOLATILE_BASE, 0x0000000000000000ULL);
    ASSERT(SERAPH_VOLATILE_END > SERAPH_VOLATILE_BASE);
    ASSERT(SERAPH_ATLAS_BASE > SERAPH_VOLATILE_END);
    ASSERT(SERAPH_ATLAS_END > SERAPH_ATLAS_BASE);
    ASSERT(SERAPH_AETHER_BASE > SERAPH_ATLAS_END);
    ASSERT(SERAPH_AETHER_END > SERAPH_AETHER_BASE);
    ASSERT(SERAPH_KERNEL_BASE > 0);

    return 0;
}

/* Test: VMM structure */
TEST(vmm_structure) {
    Seraph_VMM vmm;
    memset(&vmm, 0, sizeof(vmm));

    vmm.pml4_phys = 0x1000000;
    ASSERT_EQ(vmm.pml4_phys, 0x1000000);

    return 0;
}

/*============================================================================
 * kmalloc Tests
 *============================================================================*/

/* Test: Slab size classes using SERAPH_KMALLOC_SIZE_CLASS macro */
TEST(kmalloc_size_classes) {
    /* Test the size class macro generates correct sizes */
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(0), 16);
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(1), 32);
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(2), 64);
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(3), 128);
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(4), 256);
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(5), 512);
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(6), 1024);
    ASSERT_EQ(SERAPH_KMALLOC_SIZE_CLASS(7), 2048);

    return 0;
}

/* Test: Slab structure */
TEST(kmalloc_slab_structure) {
    Seraph_Slab slab;
    memset(&slab, 0, sizeof(slab));

    slab.object_size = 64;
    slab.object_count = 100;
    slab.free_count = 50;

    ASSERT_EQ(slab.object_size, 64);
    ASSERT_EQ(slab.object_count, 100);
    ASSERT_EQ(slab.free_count, 50);

    return 0;
}

/*============================================================================
 * Arena Tests
 *============================================================================*/

/* Test: Arena structure */
TEST(arena_structure) {
    Seraph_Arena arena;
    memset(&arena, 0, sizeof(arena));

    ASSERT_EQ(arena.memory, NULL);
    ASSERT_EQ(arena.capacity, 0);
    ASSERT_EQ(arena.used, 0);

    return 0;
}

/* Test: Arena allocation with actual API */
TEST(arena_basic_alloc) {
    Seraph_Arena arena;
    memset(&arena, 0, sizeof(arena));

    /* Use seraph_arena_create to properly initialize */
    Seraph_Vbit result = seraph_arena_create(&arena, 4096, 0, SERAPH_ARENA_FLAG_NONE);

    /* Check if creation succeeded */
    if (!seraph_vbit_is_true(result)) {
        return 1;
    }

    /* Allocate from arena using actual API (arena, size, align) */
    void* p1 = seraph_arena_alloc(&arena, 64, 8);
    ASSERT(p1 != NULL);
    ASSERT(p1 != SERAPH_VOID_PTR);
    ASSERT(arena.used >= 64);

    void* p2 = seraph_arena_alloc(&arena, 128, 16);
    ASSERT(p2 != NULL);
    ASSERT(p2 != SERAPH_VOID_PTR);
    ASSERT(p2 != p1);

    uint32_t new_gen = seraph_arena_reset(&arena);
    ASSERT(new_gen != SERAPH_VOID_U32);
    ASSERT_EQ(arena.used, 0);

    seraph_arena_destroy(&arena);
    return 0;
}

/* Test: Arena array allocation */
TEST(arena_array_alloc) {
    Seraph_Arena arena;
    memset(&arena, 0, sizeof(arena));

    /* Use seraph_arena_create to properly initialize */
    Seraph_Vbit result = seraph_arena_create(&arena, 8192, 0, SERAPH_ARENA_FLAG_NONE);

    if (!seraph_vbit_is_true(result)) {
        return 1;
    }

    /* seraph_arena_alloc_array(arena, elem_size, count, align) */
    uint64_t* arr = (uint64_t*)seraph_arena_alloc_array(&arena, sizeof(uint64_t), 100, 8);
    ASSERT(arr != NULL);
    ASSERT(arr != SERAPH_VOID_PTR);

    for (int i = 0; i < 100; i++) {
        arr[i] = (uint64_t)i * 2;
    }

    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(arr[i], (uint64_t)i * 2);
    }

    seraph_arena_destroy(&arena);
    return 0;
}

/*============================================================================
 * Boot Info Tests
 *============================================================================*/

/* Test: Boot info magic */
TEST(boot_info_magic) {
    ASSERT_EQ(SERAPH_BOOT_MAGIC, 0x5345524150484254ULL);
    ASSERT_EQ(SERAPH_BOOT_VERSION, 1);
    return 0;
}

/* Test: Boot info structure */
TEST(boot_info_structure) {
    Seraph_BootInfo boot_info;
    memset(&boot_info, 0, sizeof(boot_info));

    boot_info.magic = SERAPH_BOOT_MAGIC;
    boot_info.version = SERAPH_BOOT_VERSION;
    boot_info.framebuffer_base = 0xFD000000;
    boot_info.fb_width = 1920;
    boot_info.fb_height = 1080;

    ASSERT_EQ(boot_info.magic, SERAPH_BOOT_MAGIC);
    ASSERT_EQ(boot_info.version, SERAPH_BOOT_VERSION);
    ASSERT_EQ(boot_info.fb_width, 1920);
    ASSERT_EQ(boot_info.fb_height, 1080);

    return 0;
}

/*============================================================================
 * VOID Integration Tests
 *============================================================================*/

/* Test: VOID values in memory context */
TEST(void_memory_values) {
    ASSERT_EQ(SERAPH_VOID_U8, 0xFF);
    ASSERT_EQ(SERAPH_VOID_U16, 0xFFFF);
    ASSERT_EQ(SERAPH_VOID_U32, 0xFFFFFFFFU);
    ASSERT_EQ(SERAPH_VOID_U64, 0xFFFFFFFFFFFFFFFFULL);

    ASSERT(SERAPH_IS_VOID_U64(SERAPH_VOID_U64));
    ASSERT(!SERAPH_IS_VOID_U64(0));
    ASSERT(!SERAPH_IS_VOID_U64(42));

    return 0;
}

/*============================================================================
 * Cross-Component Tests
 *============================================================================*/

/* Test: PTE flag combinations */
TEST(pte_flag_combinations) {
    uint64_t kernel_data = SERAPH_PTE_PRESENT | SERAPH_PTE_WRITABLE | SERAPH_PTE_GLOBAL;
    ASSERT((kernel_data & SERAPH_PTE_PRESENT) != 0);
    ASSERT((kernel_data & SERAPH_PTE_WRITABLE) != 0);
    ASSERT((kernel_data & SERAPH_PTE_USER) == 0);

    uint64_t user_page = SERAPH_PTE_PRESENT | SERAPH_PTE_USER | SERAPH_PTE_NX;
    ASSERT((user_page & SERAPH_PTE_USER) != 0);
    ASSERT((user_page & SERAPH_PTE_NX) != 0);

    return 0;
}

/* Test: Address space region membership */
TEST(address_region_membership) {
    /* Volatile region: starts at 0, so just check it's within bounds */
    uint64_t volatile_addr = SERAPH_VOLATILE_BASE + 0x1000;
    ASSERT(volatile_addr <= SERAPH_VOLATILE_END);

    /* Atlas region */
    uint64_t atlas_addr = SERAPH_ATLAS_BASE + 0x1000;
    ASSERT(atlas_addr >= SERAPH_ATLAS_BASE);
    ASSERT(atlas_addr <= SERAPH_ATLAS_END);

    /* Aether region */
    uint64_t aether_addr = SERAPH_AETHER_BASE + 0x1000;
    ASSERT(aether_addr >= SERAPH_AETHER_BASE);
    ASSERT(aether_addr <= SERAPH_AETHER_END);

    /* Verify non-overlapping regions */
    ASSERT(SERAPH_VOLATILE_END < SERAPH_ATLAS_BASE);
    ASSERT(SERAPH_ATLAS_END < SERAPH_AETHER_BASE);

    return 0;
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_integration_memory_tests(void) {
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;

    printf("=== Memory Subsystem Integration Tests ===\n\n");

    printf("PMM Tests:\n");
    run_test_pmm_page_size();
    run_test_pmm_structure();

    printf("\nVMM Tests:\n");
    run_test_vmm_pte_flags();
    run_test_vmm_address_layout();
    run_test_vmm_structure();

    printf("\nkmalloc Tests:\n");
    run_test_kmalloc_size_classes();
    run_test_kmalloc_slab_structure();

    printf("\nArena Tests:\n");
    run_test_arena_structure();
    run_test_arena_basic_alloc();
    run_test_arena_array_alloc();

    printf("\nBoot Info Tests:\n");
    run_test_boot_info_magic();
    run_test_boot_info_structure();

    printf("\nVOID Integration Tests:\n");
    run_test_void_memory_values();

    printf("\nCross-Component Tests:\n");
    run_test_pte_flag_combinations();
    run_test_address_region_membership();

    printf("\n=== Memory Integration Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
}

#ifndef SERAPH_NO_MAIN
int main(void) {
    run_integration_memory_tests();
    return tests_failed > 0 ? 1 : 0;
}
#endif
