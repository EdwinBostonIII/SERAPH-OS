/**
 * @file test_atlas.c
 * @brief Tests for MC27: Atlas - The Single-Level Store
 */

#include "seraph/atlas.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define unlink(path) DeleteFileA(path)
#else
#include <unistd.h>
#endif

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;
static int test_failed_flag = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name) do { \
    printf("  " #name "... "); \
    tests_run++; \
    test_failed_flag = 0; \
    name(); \
    if (!test_failed_flag) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        test_failed_flag = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_TRUE(x) ASSERT(x)
#define ASSERT_FALSE(x) ASSERT(!(x))
#define ASSERT_NOT_NULL(x) ASSERT((x) != NULL)
#define ASSERT_NULL(x) ASSERT((x) == NULL)

/* Test file path */
static const char* TEST_PATH = "test_atlas.dat";
static const char* TEST_PATH_2 = "test_atlas_2.dat";

/*
 * Note: Static storage was removed - tests use local variables with 8MB stack.
 * In the actual SERAPH kernel, Atlas instances are allocated via kmalloc or
 * placed in Sovereign arenas, not on the stack.
 */

/* Clean up test files */
static void cleanup_test_files(void) {
    unlink(TEST_PATH);
    unlink(TEST_PATH_2);
}

/*============================================================================
 * Initialization Tests
 *============================================================================*/

TEST(test_atlas_init_new) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    Seraph_Vbit result = seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_TRUE(seraph_atlas_is_valid(&atlas));
    ASSERT_NOT_NULL(atlas.base);
    ASSERT_TRUE(atlas.initialized);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_init_default) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    Seraph_Vbit result = seraph_atlas_init_default(&atlas);

    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_TRUE(seraph_atlas_is_valid(&atlas));

    seraph_atlas_destroy(&atlas);
    unlink("seraph_atlas.dat");
}

TEST(test_atlas_init_null_params) {
    Seraph_Atlas atlas;

    /* NULL atlas */
    Seraph_Vbit result = seraph_atlas_init(NULL, TEST_PATH, 1024 * 1024);
    ASSERT_TRUE(seraph_vbit_is_void(result));

    /* NULL path */
    result = seraph_atlas_init(&atlas, NULL, 1024 * 1024);
    ASSERT_TRUE(seraph_vbit_is_void(result));
}

TEST(test_atlas_init_existing) {
    cleanup_test_files();

    /* Create new Atlas */
    Seraph_Atlas atlas1;
    seraph_atlas_init(&atlas1, TEST_PATH, 1024 * 1024);

    /* Write some data */
    void* ptr = seraph_atlas_alloc(&atlas1, 100);
    ASSERT_NOT_NULL(ptr);
    memset(ptr, 0x42, 100);
    seraph_atlas_set_root(&atlas1, ptr);
    seraph_atlas_sync(&atlas1);
    seraph_atlas_destroy(&atlas1);

    /* Reopen existing Atlas */
    Seraph_Atlas atlas2;
    Seraph_Vbit result = seraph_atlas_init(&atlas2, TEST_PATH, 0);

    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_TRUE(seraph_atlas_is_valid(&atlas2));

    /* Verify data survived */
    void* root = seraph_atlas_get_root(&atlas2);
    ASSERT_NOT_NULL(root);
    ASSERT_EQ(((uint8_t*)root)[0], 0x42);

    seraph_atlas_destroy(&atlas2);
    cleanup_test_files();
}

TEST(test_atlas_destroy_null) {
    /* Should not crash */
    seraph_atlas_destroy(NULL);

    Seraph_Atlas atlas;
    memset(&atlas, 0, sizeof(atlas));
    seraph_atlas_destroy(&atlas);  /* Should handle uninitialized */
}

/*============================================================================
 * Genesis Tests
 *============================================================================*/

TEST(test_atlas_genesis_magic) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(&atlas);
    ASSERT_NOT_NULL(genesis);
    ASSERT_EQ(genesis->magic, SERAPH_ATLAS_MAGIC);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_genesis_version) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(&atlas);
    ASSERT_NOT_NULL(genesis);
    ASSERT_EQ(genesis->version, SERAPH_ATLAS_VERSION);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_genesis_null) {
    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(NULL);
    ASSERT_NULL(genesis);

    Seraph_Atlas invalid = {0};
    genesis = seraph_atlas_genesis(&invalid);
    ASSERT_NULL(genesis);
}

TEST(test_atlas_root_set_get) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    /* Initially no root */
    void* root = seraph_atlas_get_root(&atlas);
    ASSERT_NULL(root);

    /* Allocate and set root */
    void* data = seraph_atlas_alloc(&atlas, 64);
    ASSERT_NOT_NULL(data);

    Seraph_Vbit result = seraph_atlas_set_root(&atlas, data);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    /* Get root */
    root = seraph_atlas_get_root(&atlas);
    ASSERT_EQ(root, data);

    /* Clear root */
    result = seraph_atlas_set_root(&atlas, NULL);
    ASSERT_TRUE(seraph_vbit_is_true(result));
    root = seraph_atlas_get_root(&atlas);
    ASSERT_NULL(root);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

/*============================================================================
 * Allocation Tests
 *============================================================================*/

TEST(test_atlas_alloc_basic) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr = seraph_atlas_alloc(&atlas, 100);
    ASSERT_NOT_NULL(ptr);
    ASSERT_TRUE(seraph_atlas_contains(&atlas, ptr));

    /* Should be able to write to it */
    memset(ptr, 0xAB, 100);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_alloc_multiple) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr1 = seraph_atlas_alloc(&atlas, 100);
    void* ptr2 = seraph_atlas_alloc(&atlas, 200);
    void* ptr3 = seraph_atlas_alloc(&atlas, 300);

    ASSERT_NOT_NULL(ptr1);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_NOT_NULL(ptr3);

    /* All should be different */
    ASSERT_NE(ptr1, ptr2);
    ASSERT_NE(ptr2, ptr3);
    ASSERT_NE(ptr1, ptr3);

    /* All should be within Atlas */
    ASSERT_TRUE(seraph_atlas_contains(&atlas, ptr1));
    ASSERT_TRUE(seraph_atlas_contains(&atlas, ptr2));
    ASSERT_TRUE(seraph_atlas_contains(&atlas, ptr3));

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_alloc_zero) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr = seraph_atlas_alloc(&atlas, 0);
    ASSERT_NULL(ptr);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_calloc) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr = seraph_atlas_calloc(&atlas, 100);
    ASSERT_NOT_NULL(ptr);

    /* Should be zeroed */
    uint8_t* bytes = (uint8_t*)ptr;
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(bytes[i], 0);
    }

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_alloc_pages) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr = seraph_atlas_alloc_pages(&atlas, 100);
    ASSERT_NOT_NULL(ptr);

    /* Should be page-aligned */
    ASSERT_EQ((uintptr_t)ptr % SERAPH_PAGE_SIZE, 0);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_alloc_until_full) {
    cleanup_test_files();

    /* Create small Atlas */
    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 64 * 1024);  /* 64KB */

    /* Allocate until full */
    int count = 0;
    while (seraph_atlas_alloc(&atlas, 1024) != NULL) {
        count++;
        if (count > 1000) break;  /* Safety limit */
    }

    /* Should have allocated some */
    ASSERT_TRUE(count > 10);

    /* Now allocation should fail gracefully */
    void* ptr = seraph_atlas_alloc(&atlas, 1024);
    ASSERT_NULL(ptr);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_free_basic) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr = seraph_atlas_alloc(&atlas, 100);
    ASSERT_NOT_NULL(ptr);

    /* Free should not crash */
    seraph_atlas_free(&atlas, ptr, 100);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_available) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    size_t before = seraph_atlas_available(&atlas);
    ASSERT_TRUE(before > 0);

    seraph_atlas_alloc(&atlas, 10000);

    size_t after = seraph_atlas_available(&atlas);
    ASSERT_TRUE(after < before);
    ASSERT_TRUE(before - after >= 10000);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

/*============================================================================
 * Pointer Utility Tests
 *============================================================================*/

TEST(test_atlas_contains) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr = seraph_atlas_alloc(&atlas, 100);
    ASSERT_TRUE(seraph_atlas_contains(&atlas, ptr));

    /* Stack pointer should not be in Atlas */
    int local;
    ASSERT_FALSE(seraph_atlas_contains(&atlas, &local));

    /* NULL should not be in Atlas */
    ASSERT_FALSE(seraph_atlas_contains(&atlas, NULL));

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_ptr_offset_conversion) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* ptr = seraph_atlas_alloc(&atlas, 100);
    ASSERT_NOT_NULL(ptr);

    /* Convert to offset */
    uint64_t offset = seraph_atlas_ptr_to_offset(&atlas, ptr);
    ASSERT_NE(offset, SERAPH_VOID_U64);

    /* Convert back */
    void* ptr2 = seraph_atlas_offset_to_ptr(&atlas, offset);
    ASSERT_EQ(ptr, ptr2);

    /* Invalid pointer should give VOID offset */
    int local;
    offset = seraph_atlas_ptr_to_offset(&atlas, &local);
    ASSERT_EQ(offset, SERAPH_VOID_U64);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

/*============================================================================
 * Transaction Tests
 *============================================================================*/

TEST(test_atlas_tx_begin) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    Seraph_Atlas_Transaction* tx = seraph_atlas_begin(&atlas);
    ASSERT_NOT_NULL(tx);
    ASSERT_EQ(tx->state, SERAPH_ATLAS_TX_ACTIVE);
    ASSERT_NE(tx->tx_id, SERAPH_VOID_U64);

    seraph_atlas_abort(&atlas, tx);
    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_tx_commit) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(&atlas);
    uint64_t commit_count_before = genesis->commit_count;

    Seraph_Atlas_Transaction* tx = seraph_atlas_begin(&atlas);
    ASSERT_NOT_NULL(tx);

    Seraph_Vbit result = seraph_atlas_commit(&atlas, tx);
    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_EQ(tx->state, SERAPH_ATLAS_TX_COMMITTED);

    /* Commit count should increase */
    ASSERT_EQ(genesis->commit_count, commit_count_before + 1);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_tx_abort) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(&atlas);
    uint64_t abort_count_before = genesis->abort_count;

    Seraph_Atlas_Transaction* tx = seraph_atlas_begin(&atlas);
    ASSERT_NOT_NULL(tx);

    seraph_atlas_abort(&atlas, tx);
    ASSERT_EQ(tx->state, SERAPH_ATLAS_TX_ABORTED);

    /* Abort count should increase */
    ASSERT_EQ(genesis->abort_count, abort_count_before + 1);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_tx_multiple) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    /* Start multiple transactions */
    Seraph_Atlas_Transaction* tx1 = seraph_atlas_begin(&atlas);
    Seraph_Atlas_Transaction* tx2 = seraph_atlas_begin(&atlas);

    ASSERT_NOT_NULL(tx1);
    ASSERT_NOT_NULL(tx2);
    ASSERT_NE(tx1, tx2);
    ASSERT_NE(tx1->tx_id, tx2->tx_id);

    seraph_atlas_abort(&atlas, tx1);
    seraph_atlas_abort(&atlas, tx2);
    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

/*============================================================================
 * Persistence Tests
 *============================================================================*/

TEST(test_atlas_data_survives_reopen) {
    cleanup_test_files();

    /* Create and write data */
    {
        Seraph_Atlas atlas;
        seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

        uint64_t* data = (uint64_t*)seraph_atlas_alloc(&atlas, sizeof(uint64_t));
        ASSERT_NOT_NULL(data);
        *data = 0xDEADBEEFCAFEBABEULL;

        seraph_atlas_set_root(&atlas, data);
        seraph_atlas_sync(&atlas);
        seraph_atlas_destroy(&atlas);
    }

    /* Reopen and verify */
    {
        Seraph_Atlas atlas;
        Seraph_Vbit result = seraph_atlas_init(&atlas, TEST_PATH, 0);
        ASSERT_TRUE(seraph_vbit_is_true(result));

        uint64_t* data = (uint64_t*)seraph_atlas_get_root(&atlas);
        ASSERT_NOT_NULL(data);
        ASSERT_EQ(*data, 0xDEADBEEFCAFEBABEULL);

        seraph_atlas_destroy(&atlas);
    }

    cleanup_test_files();
}

TEST(test_atlas_genesis_survives_reopen) {
    cleanup_test_files();

    uint64_t original_commit_count;

    /* Create and commit */
    {
        Seraph_Atlas atlas;
        seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

        Seraph_Atlas_Transaction* tx = seraph_atlas_begin(&atlas);
        seraph_atlas_commit(&atlas, tx);

        original_commit_count = seraph_atlas_genesis(&atlas)->commit_count;
        seraph_atlas_destroy(&atlas);
    }

    /* Reopen and verify */
    {
        Seraph_Atlas atlas;
        seraph_atlas_init(&atlas, TEST_PATH, 0);

        ASSERT_EQ(seraph_atlas_genesis(&atlas)->commit_count, original_commit_count);

        seraph_atlas_destroy(&atlas);
    }

    cleanup_test_files();
}

/*============================================================================
 * Generation Table Tests
 *============================================================================*/

TEST(test_atlas_gen_table_init) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(&atlas);
    ASSERT_NOT_NULL(table);
    ASSERT_EQ(table->entry_count, 0);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_generation_alloc) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    uint64_t id1 = seraph_atlas_alloc_generation(&atlas);
    uint64_t id2 = seraph_atlas_alloc_generation(&atlas);

    ASSERT_NE(id1, SERAPH_VOID_U64);
    ASSERT_NE(id2, SERAPH_VOID_U64);
    ASSERT_NE(id1, id2);

    Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(&atlas);
    ASSERT_EQ(table->entry_count, 2);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_generation_revoke) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    /* Allocate generation */
    uint64_t alloc_id = seraph_atlas_alloc_generation(&atlas);
    ASSERT_NE(alloc_id, SERAPH_VOID_U64);

    /* Get current generation */
    Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(&atlas);
    uint64_t gen_before = table->generations[alloc_id];

    /* Revoke */
    uint64_t gen_after = seraph_atlas_revoke(&atlas, alloc_id);
    ASSERT_EQ(gen_after, gen_before + 1);
    ASSERT_EQ(table->generations[alloc_id], gen_after);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_generation_check) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    /* Allocate generation */
    uint64_t alloc_id = seraph_atlas_alloc_generation(&atlas);
    Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(&atlas);
    uint64_t current_gen = table->generations[alloc_id];

    /* Check current generation - should be valid */
    Seraph_Vbit result = seraph_atlas_check_generation(&atlas, alloc_id, current_gen);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    /* Check old generation - should be invalid after revoke */
    seraph_atlas_revoke(&atlas, alloc_id);
    result = seraph_atlas_check_generation(&atlas, alloc_id, current_gen);
    ASSERT_TRUE(seraph_vbit_is_false(result));

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_generation_survives_reopen) {
    cleanup_test_files();

    uint64_t alloc_id;
    uint64_t gen_after_revoke;

    /* Create, allocate generation, revoke */
    {
        Seraph_Atlas atlas;
        seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

        alloc_id = seraph_atlas_alloc_generation(&atlas);
        gen_after_revoke = seraph_atlas_revoke(&atlas, alloc_id);

        seraph_atlas_sync(&atlas);
        seraph_atlas_destroy(&atlas);
    }

    /* Reopen and verify revocation persisted */
    {
        Seraph_Atlas atlas;
        seraph_atlas_init(&atlas, TEST_PATH, 0);

        Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(&atlas);
        ASSERT_EQ(table->generations[alloc_id], gen_after_revoke);

        /* Old generation should still be invalid */
        Seraph_Vbit result = seraph_atlas_check_generation(
            &atlas, alloc_id, gen_after_revoke - 1
        );
        ASSERT_TRUE(seraph_vbit_is_false(result));

        seraph_atlas_destroy(&atlas);
    }

    cleanup_test_files();
}

/*============================================================================
 * Statistics Tests
 *============================================================================*/

TEST(test_atlas_stats) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    Seraph_Atlas_Stats stats = seraph_atlas_get_stats(&atlas);
    ASSERT_TRUE(stats.initialized);
    ASSERT_EQ(stats.total_size, 1024 * 1024);
    ASSERT_TRUE(stats.free_size > 0);

    /* Allocate some data */
    seraph_atlas_alloc(&atlas, 10000);
    stats = seraph_atlas_get_stats(&atlas);
    ASSERT_TRUE(stats.used_size > 0);

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

/*============================================================================
 * VOID Tests
 *============================================================================*/

TEST(test_atlas_void_operations) {
    /* Operations on NULL/invalid atlas should return VOID or NULL */

    Seraph_Atlas invalid = {0};

    ASSERT_FALSE(seraph_atlas_is_valid(NULL));
    ASSERT_FALSE(seraph_atlas_is_valid(&invalid));

    ASSERT_NULL(seraph_atlas_genesis(NULL));
    ASSERT_NULL(seraph_atlas_get_root(NULL));
    ASSERT_NULL(seraph_atlas_alloc(NULL, 100));
    ASSERT_NULL(seraph_atlas_begin(NULL));

    ASSERT_EQ(seraph_atlas_available(NULL), 0);
    ASSERT_FALSE(seraph_atlas_contains(NULL, (void*)0x12345));

    Seraph_Vbit result = seraph_atlas_sync(NULL);
    ASSERT_TRUE(seraph_vbit_is_void(result));
}

/*============================================================================
 * Sync Tests
 *============================================================================*/

TEST(test_atlas_sync) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    /* Write some data */
    uint64_t* data = (uint64_t*)seraph_atlas_alloc(&atlas, sizeof(uint64_t));
    *data = 0x1234567890ABCDEFULL;

    /* Sync should succeed */
    Seraph_Vbit result = seraph_atlas_sync(&atlas);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

TEST(test_atlas_sync_range) {
    cleanup_test_files();

    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, TEST_PATH, 1024 * 1024);

    void* data = seraph_atlas_alloc(&atlas, 4096);
    ASSERT_NOT_NULL(data);

    Seraph_Vbit result = seraph_atlas_sync_range(&atlas, data, 4096);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    seraph_atlas_destroy(&atlas);
    cleanup_test_files();
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_atlas_tests(void) {
    printf("\n========================================\n");
    printf("     MC27: Atlas Tests\n");
    printf("========================================\n");

    /* Initialization tests */
    printf("\nInitialization Tests:\n");
    RUN_TEST(test_atlas_init_new);
    RUN_TEST(test_atlas_init_default);
    RUN_TEST(test_atlas_init_null_params);
    RUN_TEST(test_atlas_init_existing);
    RUN_TEST(test_atlas_destroy_null);

    /* Genesis tests */
    printf("\nGenesis Tests:\n");
    RUN_TEST(test_atlas_genesis_magic);
    RUN_TEST(test_atlas_genesis_version);
    RUN_TEST(test_atlas_genesis_null);
    RUN_TEST(test_atlas_root_set_get);

    /* Allocation tests */
    printf("\nAllocation Tests:\n");
    RUN_TEST(test_atlas_alloc_basic);
    RUN_TEST(test_atlas_alloc_multiple);
    RUN_TEST(test_atlas_alloc_zero);
    RUN_TEST(test_atlas_calloc);
    RUN_TEST(test_atlas_alloc_pages);
    RUN_TEST(test_atlas_alloc_until_full);
    RUN_TEST(test_atlas_free_basic);
    RUN_TEST(test_atlas_available);

    /* Pointer utility tests */
    printf("\nPointer Utility Tests:\n");
    RUN_TEST(test_atlas_contains);
    RUN_TEST(test_atlas_ptr_offset_conversion);

    /* Transaction tests */
    printf("\nTransaction Tests:\n");
    RUN_TEST(test_atlas_tx_begin);
    RUN_TEST(test_atlas_tx_commit);
    RUN_TEST(test_atlas_tx_abort);
    RUN_TEST(test_atlas_tx_multiple);

    /* Persistence tests */
    printf("\nPersistence Tests:\n");
    RUN_TEST(test_atlas_data_survives_reopen);
    RUN_TEST(test_atlas_genesis_survives_reopen);

    /* Generation table tests */
    printf("\nGeneration Table Tests:\n");
    RUN_TEST(test_atlas_gen_table_init);
    RUN_TEST(test_atlas_generation_alloc);
    RUN_TEST(test_atlas_generation_revoke);
    RUN_TEST(test_atlas_generation_check);
    RUN_TEST(test_atlas_generation_survives_reopen);

    /* Statistics tests */
    printf("\nStatistics Tests:\n");
    RUN_TEST(test_atlas_stats);

    /* VOID tests */
    printf("\nVOID Tests:\n");
    RUN_TEST(test_atlas_void_operations);

    /* Sync tests */
    printf("\nSync Tests:\n");
    RUN_TEST(test_atlas_sync);
    RUN_TEST(test_atlas_sync_range);

    printf("\n----------------------------------------\n");
    printf("Atlas Tests: %d/%d passed\n", tests_passed, tests_run);
    printf("----------------------------------------\n");

    /* Final cleanup */
    cleanup_test_files();
}
