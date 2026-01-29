/**
 * @file test_capability.c
 * @brief Tests for MC6: Capability Tokens
 */

#include "seraph/capability.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s... ", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

/*============================================================================
 * Test Data
 *============================================================================*/

static uint8_t test_buffer[1024];
static uint32_t test_generation = 1;

/*============================================================================
 * Creation Tests
 *============================================================================*/

TEST(capability_create) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, test_generation,
                                               SERAPH_CAP_RW | SERAPH_CAP_DERIVE);

    ASSERT(!seraph_cap_is_void(cap));
    ASSERT(cap.base == test_buffer);
    ASSERT(cap.length == 1024);
    ASSERT(cap.generation == test_generation);
    ASSERT(cap.permissions == (SERAPH_CAP_RW | SERAPH_CAP_DERIVE));
    ASSERT(!seraph_cap_is_sealed(cap));
}

TEST(capability_create_void_params) {
    /* VOID pointer */
    Seraph_Capability cap = seraph_cap_create(SERAPH_VOID_PTR, 1024, 1, SERAPH_CAP_RW);
    ASSERT(seraph_cap_is_void(cap));

    /* VOID length */
    cap = seraph_cap_create(test_buffer, SERAPH_VOID_U64, 1, SERAPH_CAP_RW);
    ASSERT(seraph_cap_is_void(cap));

    /* VOID generation */
    cap = seraph_cap_create(test_buffer, 1024, SERAPH_VOID_U32, SERAPH_CAP_RW);
    ASSERT(seraph_cap_is_void(cap));
}

TEST(capability_null) {
    Seraph_Capability null_cap = SERAPH_CAP_NULL;

    ASSERT(!seraph_cap_is_void(null_cap));
    ASSERT(seraph_cap_is_null(null_cap));
    ASSERT(null_cap.base == NULL);
    ASSERT(null_cap.length == 0);
}

TEST(capability_void) {
    Seraph_Capability void_cap = SERAPH_CAP_VOID;

    ASSERT(seraph_cap_is_void(void_cap));
    ASSERT(!seraph_cap_exists(void_cap));
}

/*============================================================================
 * Permission Tests
 *============================================================================*/

TEST(capability_permissions) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RWX);

    ASSERT(seraph_cap_can_read(cap));
    ASSERT(seraph_cap_can_write(cap));
    ASSERT(seraph_cap_can_exec(cap));
    ASSERT(!seraph_cap_can_derive(cap));

    cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_READ);
    ASSERT(seraph_cap_can_read(cap));
    ASSERT(!seraph_cap_can_write(cap));
    ASSERT(!seraph_cap_can_exec(cap));
}

TEST(capability_restrict) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RWX);

    Seraph_Capability ro = seraph_cap_restrict(cap, SERAPH_CAP_WRITE | SERAPH_CAP_EXEC);

    ASSERT(seraph_cap_can_read(ro));
    ASSERT(!seraph_cap_can_write(ro));
    ASSERT(!seraph_cap_can_exec(ro));
}

/*============================================================================
 * Bounds Tests
 *============================================================================*/

TEST(capability_bounds) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);

    ASSERT(seraph_cap_in_bounds(cap, 0));
    ASSERT(seraph_cap_in_bounds(cap, 1023));
    ASSERT(!seraph_cap_in_bounds(cap, 1024));
    ASSERT(!seraph_cap_in_bounds(cap, 2000));

    ASSERT(seraph_cap_range_valid(cap, 0, 1024));
    ASSERT(seraph_cap_range_valid(cap, 100, 100));
    ASSERT(!seraph_cap_range_valid(cap, 1000, 100));  /* Exceeds end */
    ASSERT(!seraph_cap_range_valid(cap, 0, 2000));    /* Too long */
}

TEST(capability_get_ptr) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);

    void* ptr = seraph_cap_get_ptr(cap, 0);
    ASSERT(ptr == test_buffer);

    ptr = seraph_cap_get_ptr(cap, 100);
    ASSERT(ptr == test_buffer + 100);

    ptr = seraph_cap_get_ptr(cap, 1024);
    ASSERT(ptr == SERAPH_VOID_PTR);  /* Out of bounds */
}

/*============================================================================
 * Derivation Tests
 *============================================================================*/

TEST(capability_derive) {
    Seraph_Capability parent = seraph_cap_create(test_buffer, 1024, 1,
                                                   SERAPH_CAP_RW | SERAPH_CAP_DERIVE);

    /* Derive a sub-capability */
    Seraph_Capability child = seraph_cap_derive(parent, 100, 200, SERAPH_CAP_READ);

    ASSERT(!seraph_cap_is_void(child));
    ASSERT(child.base == test_buffer + 100);
    ASSERT(child.length == 200);
    ASSERT(child.generation == parent.generation);
    ASSERT(seraph_cap_can_read(child));
    ASSERT(!seraph_cap_can_write(child));  /* Reduced permissions */
}

TEST(capability_derive_fails_without_permission) {
    Seraph_Capability parent = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);
    /* No DERIVE permission */

    Seraph_Capability child = seraph_cap_derive(parent, 0, 100, SERAPH_CAP_READ);
    ASSERT(seraph_cap_is_void(child));
}

TEST(capability_derive_fails_out_of_bounds) {
    Seraph_Capability parent = seraph_cap_create(test_buffer, 1024, 1,
                                                   SERAPH_CAP_RW | SERAPH_CAP_DERIVE);

    /* Offset too large */
    Seraph_Capability child = seraph_cap_derive(parent, 2000, 100, SERAPH_CAP_READ);
    ASSERT(seraph_cap_is_void(child));

    /* Length exceeds bounds */
    child = seraph_cap_derive(parent, 900, 200, SERAPH_CAP_READ);
    ASSERT(seraph_cap_is_void(child));
}

TEST(capability_derive_fails_expanding_permissions) {
    Seraph_Capability parent = seraph_cap_create(test_buffer, 1024, 1,
                                                   SERAPH_CAP_READ | SERAPH_CAP_DERIVE);

    /* Trying to get WRITE from READ-only parent */
    Seraph_Capability child = seraph_cap_derive(parent, 0, 100, SERAPH_CAP_RW);
    ASSERT(seraph_cap_is_void(child));
}

TEST(capability_shrink) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);

    Seraph_Capability shrunk = seraph_cap_shrink(cap, 100, 500);

    ASSERT(!seraph_cap_is_void(shrunk));
    ASSERT(shrunk.base == test_buffer + 100);
    ASSERT(shrunk.length == 500);
    ASSERT(shrunk.permissions == cap.permissions);
}

/*============================================================================
 * Read/Write Tests
 *============================================================================*/

TEST(capability_read_write_u8) {
    memset(test_buffer, 0, sizeof(test_buffer));

    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);

    /* Write */
    Seraph_Vbit result = seraph_cap_write_u8(cap, 0, 0x42);
    ASSERT(seraph_vbit_is_true(result));
    ASSERT(test_buffer[0] == 0x42);

    /* Read back */
    uint8_t val = seraph_cap_read_u8(cap, 0);
    ASSERT(val == 0x42);
}

TEST(capability_read_write_u64) {
    memset(test_buffer, 0, sizeof(test_buffer));

    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);

    uint64_t test_val = 0x123456789ABCDEF0ULL;

    Seraph_Vbit result = seraph_cap_write_u64(cap, 8, test_val);
    ASSERT(seraph_vbit_is_true(result));

    uint64_t read_val = seraph_cap_read_u64(cap, 8);
    ASSERT(read_val == test_val);
}

TEST(capability_read_no_permission) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_WRITE);

    uint8_t val = seraph_cap_read_u8(cap, 0);
    ASSERT(SERAPH_IS_VOID_U8(val));
}

TEST(capability_write_no_permission) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_READ);

    Seraph_Vbit result = seraph_cap_write_u8(cap, 0, 0x42);
    ASSERT(seraph_vbit_is_false(result));
}

TEST(capability_access_out_of_bounds) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 10, 1, SERAPH_CAP_RW);

    /* Read past end */
    uint8_t val = seraph_cap_read_u8(cap, 10);
    ASSERT(SERAPH_IS_VOID_U8(val));

    /* Write past end */
    Seraph_Vbit result = seraph_cap_write_u8(cap, 10, 0x42);
    ASSERT(seraph_vbit_is_false(result));

    /* Multi-byte access crossing boundary */
    uint64_t val64 = seraph_cap_read_u64(cap, 5);  /* 5+8 > 10 */
    ASSERT(SERAPH_IS_VOID_U64(val64));
}

TEST(capability_copy) {
    uint8_t src_buffer[256];
    uint8_t dst_buffer[256];

    for (int i = 0; i < 256; i++) src_buffer[i] = (uint8_t)i;
    memset(dst_buffer, 0, 256);

    Seraph_Capability src = seraph_cap_create(src_buffer, 256, 1, SERAPH_CAP_READ);
    Seraph_Capability dst = seraph_cap_create(dst_buffer, 256, 1, SERAPH_CAP_WRITE);

    Seraph_Vbit result = seraph_cap_copy(dst, 0, src, 0, 100);
    ASSERT(seraph_vbit_is_true(result));
    ASSERT(memcmp(dst_buffer, src_buffer, 100) == 0);
}

/*============================================================================
 * Sealing Tests
 *============================================================================*/

TEST(capability_seal_unseal) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1,
                                               SERAPH_CAP_RW | SERAPH_CAP_SEAL | SERAPH_CAP_UNSEAL);

    ASSERT(!seraph_cap_is_sealed(cap));

    /* Seal with type 42 */
    Seraph_Capability sealed = seraph_cap_seal(cap, 42);
    ASSERT(!seraph_cap_is_void(sealed));
    ASSERT(seraph_cap_is_sealed(sealed));
    ASSERT(seraph_cap_get_type(sealed) == 42);

    /* Cannot read through sealed capability */
    uint8_t val = seraph_cap_read_u8(sealed, 0);
    ASSERT(SERAPH_IS_VOID_U8(val));

    /* Unseal with correct type */
    Seraph_Capability unsealed = seraph_cap_unseal(sealed, 42);
    ASSERT(!seraph_cap_is_void(unsealed));
    ASSERT(!seraph_cap_is_sealed(unsealed));

    /* Can read through unsealed capability */
    test_buffer[0] = 0x77;
    val = seraph_cap_read_u8(unsealed, 0);
    ASSERT(val == 0x77);
}

TEST(capability_unseal_wrong_type) {
    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1,
                                               SERAPH_CAP_RW | SERAPH_CAP_SEAL | SERAPH_CAP_UNSEAL);

    Seraph_Capability sealed = seraph_cap_seal(cap, 42);

    /* Wrong type should fail */
    Seraph_Capability unsealed = seraph_cap_unseal(sealed, 99);
    ASSERT(seraph_cap_is_void(unsealed));
}

/*============================================================================
 * CDT Tests
 *============================================================================*/

TEST(cdt_init_destroy) {
    Seraph_CDT cdt;

    Seraph_Vbit result = seraph_cdt_init(&cdt, 100);
    ASSERT(seraph_vbit_is_true(result));
    ASSERT(cdt.capacity == 100);
    ASSERT(cdt.count == 0);

    seraph_cdt_destroy(&cdt);
    ASSERT(cdt.entries == NULL);
}

TEST(cdt_alloc_lookup) {
    Seraph_CDT cdt;
    seraph_cdt_init(&cdt, 100);

    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);
    Seraph_CapCompact compact = seraph_cdt_alloc(&cdt, cap);

    ASSERT(!seraph_cap_compact_is_void(compact));
    ASSERT(cdt.count == 1);

    /* Look up */
    Seraph_Capability looked_up = seraph_cdt_lookup(&cdt, compact);
    ASSERT(!seraph_cap_is_void(looked_up));
    ASSERT(looked_up.base == cap.base);
    ASSERT(looked_up.length == cap.length);

    seraph_cdt_destroy(&cdt);
}

TEST(cdt_refcount) {
    Seraph_CDT cdt;
    seraph_cdt_init(&cdt, 100);

    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);
    Seraph_CapCompact compact = seraph_cdt_alloc(&cdt, cap);

    /* Add reference */
    seraph_cdt_addref(&cdt, compact);
    ASSERT(cdt.entries[compact.cdt_index].refcount == 2);

    /* Release once */
    seraph_cdt_release(&cdt, compact);
    ASSERT(cdt.entries[compact.cdt_index].refcount == 1);

    /* Release again - should free */
    seraph_cdt_release(&cdt, compact);
    ASSERT(cdt.count == 0);

    /* Lookup should now fail */
    Seraph_Capability invalid = seraph_cdt_lookup(&cdt, compact);
    ASSERT(seraph_cap_is_void(invalid));

    seraph_cdt_destroy(&cdt);
}

TEST(cdt_compact_with_offset) {
    Seraph_CDT cdt;
    seraph_cdt_init(&cdt, 100);

    Seraph_Capability cap = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RW);
    Seraph_CapCompact compact = seraph_cdt_alloc(&cdt, cap);

    /* Modify compact to have offset */
    compact.offset = 100;

    Seraph_Capability looked_up = seraph_cdt_lookup(&cdt, compact);
    ASSERT(!seraph_cap_is_void(looked_up));
    ASSERT(looked_up.base == test_buffer + 100);
    ASSERT(looked_up.length == 924);

    seraph_cdt_destroy(&cdt);
}

/*============================================================================
 * Subset Tests
 *============================================================================*/

TEST(capability_is_subset) {
    Seraph_Capability parent = seraph_cap_create(test_buffer, 1024, 1, SERAPH_CAP_RWX);
    Seraph_Capability child = seraph_cap_create(test_buffer + 100, 200, 1, SERAPH_CAP_RW);

    ASSERT(seraph_cap_is_subset(child, parent));
    ASSERT(!seraph_cap_is_subset(parent, child));

    /* Different generation - not subset */
    Seraph_Capability diff_gen = seraph_cap_create(test_buffer + 100, 200, 2, SERAPH_CAP_RW);
    ASSERT(!seraph_cap_is_subset(diff_gen, parent));
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_capability_tests(void) {
    printf("\n=== MC6: Capability Tokens Tests ===\n\n");

    /* Initialize test buffer */
    memset(test_buffer, 0, sizeof(test_buffer));

    /* Creation */
    RUN_TEST(capability_create);
    RUN_TEST(capability_create_void_params);
    RUN_TEST(capability_null);
    RUN_TEST(capability_void);

    /* Permissions */
    RUN_TEST(capability_permissions);
    RUN_TEST(capability_restrict);

    /* Bounds */
    RUN_TEST(capability_bounds);
    RUN_TEST(capability_get_ptr);

    /* Derivation */
    RUN_TEST(capability_derive);
    RUN_TEST(capability_derive_fails_without_permission);
    RUN_TEST(capability_derive_fails_out_of_bounds);
    RUN_TEST(capability_derive_fails_expanding_permissions);
    RUN_TEST(capability_shrink);

    /* Read/Write */
    RUN_TEST(capability_read_write_u8);
    RUN_TEST(capability_read_write_u64);
    RUN_TEST(capability_read_no_permission);
    RUN_TEST(capability_write_no_permission);
    RUN_TEST(capability_access_out_of_bounds);
    RUN_TEST(capability_copy);

    /* Sealing */
    RUN_TEST(capability_seal_unseal);
    RUN_TEST(capability_unseal_wrong_type);

    /* CDT */
    RUN_TEST(cdt_init_destroy);
    RUN_TEST(cdt_alloc_lookup);
    RUN_TEST(cdt_refcount);
    RUN_TEST(cdt_compact_with_offset);

    /* Subset */
    RUN_TEST(capability_is_subset);

    printf("\nCapability Tests: %d/%d passed\n", tests_passed, tests_run);
}
