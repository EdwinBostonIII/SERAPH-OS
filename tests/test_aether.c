/**
 * @file test_aether.c
 * @brief Comprehensive tests for MC28: Aether Distributed Shared Memory
 *
 * Tests cover:
 * - Initialization and destruction
 * - Address encoding and decoding
 * - Node simulation
 * - Memory allocation
 * - Read/write operations
 * - Cache operations
 * - Global generations and revocation
 * - Coherence protocol
 * - VOID failure injection
 * - Statistics tracking
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seraph/aether.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s...", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf(" PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at line %d: %s\n", __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_TRUE(x) ASSERT((x))
#define ASSERT_FALSE(x) ASSERT(!(x))

/*============================================================================
 * Address Encoding Tests
 *============================================================================*/

TEST(address_encoding_basic) {
    /* Test basic address construction */
    /* Note: Node ID is 14 bits (max 0x3FFF), offset is 32 bits */
    uint64_t addr = seraph_aether_make_addr(0x1234, 0x90ABCDEF);
    ASSERT_TRUE(seraph_aether_is_aether_addr(addr));
    ASSERT_EQ(seraph_aether_get_node(addr), 0x1234);
    ASSERT_EQ(seraph_aether_get_offset(addr), 0x90ABCDEF);
}

TEST(address_encoding_boundaries) {
    /* Test boundary cases */

    /* Minimum node ID (0) */
    uint64_t addr0 = seraph_aether_make_addr(0, 0);
    ASSERT_TRUE(seraph_aether_is_aether_addr(addr0));
    ASSERT_EQ(seraph_aether_get_node(addr0), 0);
    ASSERT_EQ(seraph_aether_get_offset(addr0), 0);

    /* Maximum node ID (0x3FFF = 14 bits) */
    uint64_t addr_max = seraph_aether_make_addr(0x3FFF, 0);
    ASSERT_TRUE(seraph_aether_is_aether_addr(addr_max));
    ASSERT_EQ(seraph_aether_get_node(addr_max), 0x3FFF);

    /* Maximum offset (32 bits) */
    uint64_t addr_max_off = seraph_aether_make_addr(0, SERAPH_AETHER_OFFSET_MASK);
    ASSERT_EQ(seraph_aether_get_offset(addr_max_off), SERAPH_AETHER_OFFSET_MASK);
}

TEST(address_range_check) {
    /* In Aether range */
    ASSERT_TRUE(seraph_aether_is_aether_addr(SERAPH_AETHER_BASE));
    ASSERT_TRUE(seraph_aether_is_aether_addr(SERAPH_AETHER_END));
    ASSERT_TRUE(seraph_aether_is_aether_addr(seraph_aether_make_addr(100, 50000)));

    /* Not in Aether range (below) */
    ASSERT_FALSE(seraph_aether_is_aether_addr(0x0000000000000000ULL));
    ASSERT_FALSE(seraph_aether_is_aether_addr(0x0000800000000000ULL));  /* Atlas range */

    /* Check border cases */
    ASSERT_FALSE(seraph_aether_is_aether_addr(SERAPH_AETHER_BASE - 1));
}

TEST(page_alignment) {
    uint64_t addr = seraph_aether_make_addr(5, 12345);

    uint64_t aligned = seraph_aether_page_align(addr);
    uint64_t page_off = seraph_aether_page_offset(addr);

    /* Aligned address should be page-aligned */
    ASSERT_EQ(aligned % SERAPH_AETHER_PAGE_SIZE, 0);

    /* Page offset should be within page */
    ASSERT_TRUE(page_off < SERAPH_AETHER_PAGE_SIZE);

    /* Offset should be preserved in lower bits */
    ASSERT_EQ(12345 % SERAPH_AETHER_PAGE_SIZE, page_off);
}

/*============================================================================
 * Global Generation Tests
 *============================================================================*/

TEST(global_gen_pack_unpack) {
    uint16_t node_id = 0x1234;
    uint64_t local_gen = 0xABCDEF012345ULL;

    uint64_t packed = seraph_aether_pack_global_gen(node_id, local_gen);
    Seraph_Aether_Global_Gen unpacked = seraph_aether_unpack_global_gen(packed);

    ASSERT_EQ(unpacked.node_id, node_id);
    ASSERT_EQ(unpacked.local_gen, local_gen);
}

TEST(global_gen_boundaries) {
    /* Max node ID */
    uint64_t packed1 = seraph_aether_pack_global_gen(0xFFFF, 1);
    Seraph_Aether_Global_Gen unpacked1 = seraph_aether_unpack_global_gen(packed1);
    ASSERT_EQ(unpacked1.node_id, 0xFFFF);

    /* Max local gen (48 bits) */
    uint64_t packed2 = seraph_aether_pack_global_gen(0, 0x0000FFFFFFFFFFFFULL);
    Seraph_Aether_Global_Gen unpacked2 = seraph_aether_unpack_global_gen(packed2);
    ASSERT_EQ(unpacked2.local_gen, 0x0000FFFFFFFFFFFFULL);

    /* Zero values */
    uint64_t packed0 = seraph_aether_pack_global_gen(0, 0);
    Seraph_Aether_Global_Gen unpacked0 = seraph_aether_unpack_global_gen(packed0);
    ASSERT_EQ(unpacked0.node_id, 0);
    ASSERT_EQ(unpacked0.local_gen, 0);
}

/*============================================================================
 * Initialization Tests
 *============================================================================*/

TEST(init_basic) {
    Seraph_Aether aether;
    Seraph_Vbit result = seraph_aether_init(&aether, 0, 4);
    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_EQ(seraph_aether_get_local_node_id(&aether), 0);

    seraph_aether_destroy(&aether);
}

TEST(init_default) {
    Seraph_Aether aether;
    Seraph_Vbit result = seraph_aether_init_default(&aether);
    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_EQ(seraph_aether_get_local_node_id(&aether), 0);

    seraph_aether_destroy(&aether);
}

TEST(init_null_handling) {
    Seraph_Vbit result = seraph_aether_init(NULL, 0, 1);
    ASSERT_TRUE(seraph_vbit_is_void(result));

    /* Destroy NULL should not crash */
    seraph_aether_destroy(NULL);
}

TEST(destroy_cleans_up) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    seraph_aether_destroy(&aether);

    /* After destroy, initialization state should be cleared */
    ASSERT_FALSE(aether.initialized);
}

/*============================================================================
 * Simulated Node Tests
 *============================================================================*/

TEST(add_sim_node) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 4);

    Seraph_Vbit result = seraph_aether_add_sim_node(&aether, 0, 65536);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    result = seraph_aether_add_sim_node(&aether, 1, 65536);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    /* Can't add duplicate */
    result = seraph_aether_add_sim_node(&aether, 0, 65536);
    ASSERT_TRUE(seraph_vbit_is_false(result));

    seraph_aether_destroy(&aether);
}

TEST(node_online_status) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    /* Both online by default */
    uint64_t addr0 = seraph_aether_alloc_on_node(&aether, 0, 4096);
    ASSERT_NE(addr0, SERAPH_VOID_U64);

    uint64_t addr1 = seraph_aether_alloc_on_node(&aether, 1, 4096);
    ASSERT_NE(addr1, SERAPH_VOID_U64);

    /* Take node 1 offline */
    seraph_aether_set_node_online(&aether, 1, false);

    /* Allocation should fail on offline node */
    uint64_t addr2 = seraph_aether_alloc_on_node(&aether, 1, 4096);
    ASSERT_EQ(addr2, SERAPH_VOID_U64);

    /* But node 0 still works */
    uint64_t addr3 = seraph_aether_alloc_on_node(&aether, 0, 4096);
    ASSERT_NE(addr3, SERAPH_VOID_U64);

    seraph_aether_destroy(&aether);
}

TEST(is_local_check) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 5, 10);  /* Local node is 5 */

    uint64_t local_addr = seraph_aether_make_addr(5, 1000);
    uint64_t remote_addr = seraph_aether_make_addr(7, 1000);

    ASSERT_TRUE(seraph_aether_is_local(&aether, local_addr));
    ASSERT_FALSE(seraph_aether_is_local(&aether, remote_addr));

    /* Non-Aether address is never local */
    ASSERT_FALSE(seraph_aether_is_local(&aether, 0x1234));

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Memory Allocation Tests
 *============================================================================*/

TEST(alloc_basic) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);
    ASSERT_NE(addr, SERAPH_VOID_U64);
    ASSERT_TRUE(seraph_aether_is_aether_addr(addr));
    ASSERT_EQ(seraph_aether_get_node(addr), 0);

    seraph_aether_destroy(&aether);
}

TEST(alloc_multiple) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr1 = seraph_aether_alloc(&aether, 4096);
    uint64_t addr2 = seraph_aether_alloc(&aether, 4096);
    uint64_t addr3 = seraph_aether_alloc(&aether, 4096);

    ASSERT_NE(addr1, SERAPH_VOID_U64);
    ASSERT_NE(addr2, SERAPH_VOID_U64);
    ASSERT_NE(addr3, SERAPH_VOID_U64);

    /* Addresses should be different */
    ASSERT_NE(addr1, addr2);
    ASSERT_NE(addr2, addr3);
    ASSERT_NE(addr1, addr3);

    seraph_aether_destroy(&aether);
}

TEST(alloc_on_specific_node) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 4);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);
    seraph_aether_add_sim_node(&aether, 2, 65536);

    uint64_t addr0 = seraph_aether_alloc_on_node(&aether, 0, 4096);
    uint64_t addr1 = seraph_aether_alloc_on_node(&aether, 1, 4096);
    uint64_t addr2 = seraph_aether_alloc_on_node(&aether, 2, 4096);

    ASSERT_EQ(seraph_aether_get_node(addr0), 0);
    ASSERT_EQ(seraph_aether_get_node(addr1), 1);
    ASSERT_EQ(seraph_aether_get_node(addr2), 2);

    seraph_aether_destroy(&aether);
}

TEST(alloc_out_of_memory) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 8192);  /* Only 8KB */

    /* First allocation should succeed */
    uint64_t addr1 = seraph_aether_alloc(&aether, 4096);
    ASSERT_NE(addr1, SERAPH_VOID_U64);

    /* Second allocation should succeed */
    uint64_t addr2 = seraph_aether_alloc(&aether, 4096);
    ASSERT_NE(addr2, SERAPH_VOID_U64);

    /* Third allocation should fail (out of memory) */
    uint64_t addr3 = seraph_aether_alloc(&aether, 4096);
    ASSERT_EQ(addr3, SERAPH_VOID_U64);

    seraph_aether_destroy(&aether);
}

TEST(alloc_zero_size) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 0);
    ASSERT_EQ(addr, SERAPH_VOID_U64);

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Read/Write Tests
 *============================================================================*/

TEST(read_write_local) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);
    ASSERT_NE(addr, SERAPH_VOID_U64);

    /* Write data */
    uint64_t write_data = 0xDEADBEEFCAFEBABEULL;
    Seraph_Aether_Fetch_Result result = seraph_aether_write(
        &aether, addr, &write_data, sizeof(write_data));
    ASSERT_EQ(result.status, SERAPH_AETHER_OK);

    /* Read it back */
    uint64_t read_data = 0;
    result = seraph_aether_read(&aether, addr, &read_data, sizeof(read_data));
    ASSERT_EQ(result.status, SERAPH_AETHER_OK);
    ASSERT_EQ(read_data, write_data);

    seraph_aether_destroy(&aether);
}

TEST(read_write_vbit) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);

    /* Write using vbit interface */
    uint32_t write_val = 12345;
    Seraph_Vbit vresult = seraph_aether_write_vbit(
        &aether, addr, &write_val, sizeof(write_val));
    ASSERT_TRUE(seraph_vbit_is_true(vresult));

    /* Read using vbit interface */
    uint32_t read_val = 0;
    vresult = seraph_aether_read_vbit(&aether, addr, &read_val, sizeof(read_val));
    ASSERT_TRUE(seraph_vbit_is_true(vresult));
    ASSERT_EQ(read_val, write_val);

    seraph_aether_destroy(&aether);
}

TEST(read_write_remote) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    /* Allocate on node 1 (remote from node 0's perspective) */
    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);
    ASSERT_NE(addr, SERAPH_VOID_U64);
    ASSERT_EQ(seraph_aether_get_node(addr), 1);

    /* Write to remote */
    uint64_t write_data = 0x123456789ABCDEF0ULL;
    Seraph_Aether_Fetch_Result result = seraph_aether_write(
        &aether, addr, &write_data, sizeof(write_data));
    ASSERT_EQ(result.status, SERAPH_AETHER_OK);

    /* Read from remote */
    uint64_t read_data = 0;
    result = seraph_aether_read(&aether, addr, &read_data, sizeof(read_data));
    ASSERT_EQ(result.status, SERAPH_AETHER_OK);
    ASSERT_EQ(read_data, write_data);

    seraph_aether_destroy(&aether);
}

TEST(read_invalid_address) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);

    uint64_t data = 0;

    /* Invalid (non-Aether) address */
    Seraph_Aether_Fetch_Result result = seraph_aether_read(
        &aether, 0x1234, &data, sizeof(data));
    ASSERT_EQ(result.status, SERAPH_AETHER_INVALID_ADDR);

    seraph_aether_destroy(&aether);
}

TEST(write_to_array) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);

    /* Write array */
    uint32_t write_arr[4] = {100, 200, 300, 400};
    Seraph_Aether_Fetch_Result result = seraph_aether_write(
        &aether, addr, write_arr, sizeof(write_arr));
    ASSERT_EQ(result.status, SERAPH_AETHER_OK);

    /* Read array */
    uint32_t read_arr[4] = {0};
    result = seraph_aether_read(&aether, addr, read_arr, sizeof(read_arr));
    ASSERT_EQ(result.status, SERAPH_AETHER_OK);

    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(read_arr[i], write_arr[i]);
    }

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Cache Tests
 *============================================================================*/

TEST(cache_hit_miss) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    seraph_aether_reset_stats(&aether);

    /* Allocate on remote node */
    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);

    /* Write initial data */
    uint64_t data = 42;
    seraph_aether_write(&aether, addr, &data, sizeof(data));

    /* First read - cache miss */
    uint64_t read1 = 0;
    seraph_aether_read(&aether, addr, &read1, sizeof(read1));

    uint64_t hits, misses;
    seraph_aether_cache_stats(&aether, &hits, &misses);
    ASSERT_EQ(misses, 1);

    /* Second read - cache hit */
    uint64_t read2 = 0;
    seraph_aether_read(&aether, addr, &read2, sizeof(read2));

    seraph_aether_cache_stats(&aether, &hits, &misses);
    ASSERT_EQ(hits, 1);
    ASSERT_EQ(misses, 1);

    seraph_aether_destroy(&aether);
}

TEST(cache_invalidation) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);

    /* Write and read to populate cache */
    uint64_t data1 = 100;
    seraph_aether_write(&aether, addr, &data1, sizeof(data1));

    uint64_t read1 = 0;
    seraph_aether_read(&aether, addr, &read1, sizeof(read1));
    ASSERT_EQ(read1, data1);

    /* Verify it's cached */
    Seraph_Aether_Cache_Entry* entry = seraph_aether_cache_lookup(&aether, addr);
    ASSERT_NE(entry, NULL);

    /* Invalidate */
    seraph_aether_cache_invalidate(&aether, addr);

    /* Should no longer be cached */
    entry = seraph_aether_cache_lookup(&aether, addr);
    ASSERT_EQ(entry, NULL);

    seraph_aether_destroy(&aether);
}

TEST(cache_clear) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    /* Populate cache with multiple entries */
    for (int i = 0; i < 5; i++) {
        uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);
        uint64_t data = i;
        seraph_aether_write(&aether, addr, &data, sizeof(data));
        seraph_aether_read(&aether, addr, &data, sizeof(data));
    }

    /* Clear all */
    seraph_aether_cache_clear(&aether);

    /* Cache should be empty */
    uint64_t hits, misses;
    seraph_aether_cache_stats(&aether, &hits, &misses);
    /* Stats preserved, but entries gone */

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Generation and Revocation Tests
 *============================================================================*/

TEST(generation_tracking) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);

    /* Get initial generation */
    uint64_t gen1 = seraph_aether_get_generation(&aether, addr);
    ASSERT_NE(gen1, SERAPH_VOID_U64);

    /* Write should increment generation */
    uint64_t data = 42;
    seraph_aether_write(&aether, addr, &data, sizeof(data));

    uint64_t gen2 = seraph_aether_get_generation(&aether, addr);
    ASSERT_TRUE(gen2 > gen1);

    seraph_aether_destroy(&aether);
}

TEST(revocation) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);

    /* Get initial global generation */
    uint64_t global_gen1 = seraph_aether_get_global_gen(&aether, addr);
    ASSERT_NE(global_gen1, SERAPH_VOID_U64);

    /* Revoke */
    Seraph_Vbit result = seraph_aether_revoke(&aether, addr);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    /* Generation should have changed */
    uint64_t global_gen2 = seraph_aether_get_global_gen(&aether, addr);
    ASSERT_NE(global_gen2, global_gen1);

    seraph_aether_destroy(&aether);
}

TEST(check_generation) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);

    /* Get current global generation */
    uint64_t global_gen = seraph_aether_get_global_gen(&aether, addr);

    /* Check should pass */
    Seraph_Vbit result = seraph_aether_check_generation(&aether, addr, global_gen);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    /* Revoke */
    seraph_aether_revoke(&aether, addr);

    /* Old generation should now fail */
    result = seraph_aether_check_generation(&aether, addr, global_gen);
    ASSERT_TRUE(seraph_vbit_is_false(result));

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Failure Injection Tests (VOID over network)
 *============================================================================*/

TEST(inject_timeout) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);

    /* Inject timeout failure */
    seraph_aether_inject_failure(&aether, 1, SERAPH_AETHER_VOID_TIMEOUT);

    /* Read should fail with VOID */
    uint64_t data = 0;
    Seraph_Vbit vresult = seraph_aether_read_vbit(&aether, addr, &data, sizeof(data));
    ASSERT_TRUE(seraph_vbit_is_void(vresult));

    /* Check VOID reason */
    ASSERT_EQ(seraph_aether_get_void_reason(), SERAPH_AETHER_VOID_TIMEOUT);

    /* Clear failure */
    seraph_aether_clear_failure(&aether, 1);

    /* Should work now */
    uint64_t write_data = 42;
    seraph_aether_write(&aether, addr, &write_data, sizeof(write_data));
    vresult = seraph_aether_read_vbit(&aether, addr, &data, sizeof(data));
    ASSERT_TRUE(seraph_vbit_is_true(vresult));
    ASSERT_EQ(data, 42);

    seraph_aether_destroy(&aether);
}

TEST(inject_partition) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);

    /* Write data first */
    uint64_t data = 999;
    seraph_aether_write(&aether, addr, &data, sizeof(data));

    /* Inject partition */
    seraph_aether_inject_failure(&aether, 1, SERAPH_AETHER_VOID_PARTITION);

    /* Clear cache so we have to fetch */
    seraph_aether_cache_clear(&aether);

    /* Read should fail */
    uint64_t read_data = 0;
    Seraph_Aether_Fetch_Result result = seraph_aether_read(
        &aether, addr, &read_data, sizeof(read_data));
    ASSERT_EQ(result.reason, SERAPH_AETHER_VOID_PARTITION);

    seraph_aether_destroy(&aether);
}

TEST(node_offline) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);

    /* Take node offline */
    seraph_aether_set_node_online(&aether, 1, false);
    seraph_aether_cache_clear(&aether);

    /* Read should fail with unreachable */
    uint64_t data = 0;
    Seraph_Aether_Fetch_Result result = seraph_aether_read(
        &aether, addr, &data, sizeof(data));
    ASSERT_EQ(result.status, SERAPH_AETHER_UNREACHABLE);
    ASSERT_EQ(result.reason, SERAPH_AETHER_VOID_NODE_CRASHED);

    seraph_aether_destroy(&aether);
}

TEST(void_context) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    /* Clear any prior context */
    seraph_aether_clear_void_context();
    ASSERT_EQ(seraph_aether_get_void_reason(), SERAPH_AETHER_VOID_NONE);

    /* Trigger failure */
    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);
    seraph_aether_inject_failure(&aether, 1, SERAPH_AETHER_VOID_CORRUPTION);
    seraph_aether_cache_clear(&aether);

    uint64_t data = 0;
    seraph_aether_read(&aether, addr, &data, sizeof(data));

    /* Check context */
    ASSERT_EQ(seraph_aether_get_void_reason(), SERAPH_AETHER_VOID_CORRUPTION);
    ASSERT_EQ(seraph_aether_get_void_addr(), addr);

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Coherence Protocol Tests
 *============================================================================*/

TEST(coherence_read_request) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    /* Allocate and write on local node */
    uint64_t addr = seraph_aether_alloc(&aether, 4096);
    uint64_t offset = seraph_aether_get_offset(addr);

    uint64_t data = 0xABCD;
    seraph_aether_write(&aether, addr, &data, sizeof(data));

    /* Simulate read request from node 1 */
    Seraph_Aether_Response resp = seraph_aether_handle_read_request(
        &aether, 1, offset);
    ASSERT_EQ(resp.status, SERAPH_AETHER_RESP_OK);
    ASSERT_NE(resp.page_data, NULL);

    seraph_aether_destroy(&aether);
}

TEST(coherence_write_request) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    /* Allocate on local node */
    uint64_t addr = seraph_aether_alloc(&aether, 4096);
    uint64_t offset = seraph_aether_get_offset(addr);

    /* Initial data */
    uint64_t data1 = 100;
    seraph_aether_write(&aether, addr, &data1, sizeof(data1));

    /* Simulate write request from node 1 */
    uint64_t new_data = 200;
    Seraph_Aether_Response resp = seraph_aether_handle_write_request(
        &aether, 1, offset, &new_data, sizeof(new_data));
    ASSERT_EQ(resp.status, SERAPH_AETHER_RESP_OK);

    /* Read back - should see new data */
    uint64_t read_data = 0;
    seraph_aether_read(&aether, addr, &read_data, sizeof(read_data));
    ASSERT_EQ(read_data, new_data);

    seraph_aether_destroy(&aether);
}

TEST(directory_add_sharer) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 4);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    uint64_t addr = seraph_aether_alloc(&aether, 4096);
    uint64_t offset = seraph_aether_get_offset(addr);

    /* Get directory entry */
    Seraph_Aether_Directory_Entry* entry = seraph_aether_get_directory_entry(
        &aether, 0, offset);
    ASSERT_NE(entry, NULL);

    /* Add sharers */
    seraph_aether_directory_add_sharer(entry, 1);
    seraph_aether_directory_add_sharer(entry, 2);
    seraph_aether_directory_add_sharer(entry, 3);
    ASSERT_EQ(entry->sharer_count, 3);

    /* Adding same sharer twice should not duplicate */
    seraph_aether_directory_add_sharer(entry, 2);
    ASSERT_EQ(entry->sharer_count, 3);

    /* Remove sharer */
    seraph_aether_directory_remove_sharer(entry, 2);
    ASSERT_EQ(entry->sharer_count, 2);

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Statistics Tests
 *============================================================================*/

TEST(statistics_tracking) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 2);
    seraph_aether_add_sim_node(&aether, 0, 65536);
    seraph_aether_add_sim_node(&aether, 1, 65536);

    seraph_aether_reset_stats(&aether);

    /* Generate some activity */
    uint64_t addr = seraph_aether_alloc_on_node(&aether, 1, 4096);
    uint64_t data = 42;
    seraph_aether_write(&aether, addr, &data, sizeof(data));
    seraph_aether_read(&aether, addr, &data, sizeof(data));  /* Miss */
    seraph_aether_read(&aether, addr, &data, sizeof(data));  /* Hit */

    uint64_t hits, misses, fetches, inv_sent, inv_recv;
    seraph_aether_get_stats(&aether, &hits, &misses, &fetches, &inv_sent, &inv_recv);

    ASSERT_EQ(hits, 1);
    ASSERT_EQ(misses, 1);
    ASSERT_TRUE(fetches >= 1);

    seraph_aether_destroy(&aether);
}

TEST(statistics_reset) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);
    seraph_aether_add_sim_node(&aether, 0, 65536);

    /* Generate activity */
    uint64_t addr = seraph_aether_alloc(&aether, 4096);
    uint64_t data = 42;
    seraph_aether_write(&aether, addr, &data, sizeof(data));
    seraph_aether_read(&aether, addr, &data, sizeof(data));

    /* Reset */
    seraph_aether_reset_stats(&aether);

    uint64_t hits, misses, fetches, inv_sent, inv_recv;
    seraph_aether_get_stats(&aether, &hits, &misses, &fetches, &inv_sent, &inv_recv);

    ASSERT_EQ(hits, 0);
    ASSERT_EQ(misses, 0);
    ASSERT_EQ(fetches, 0);
    ASSERT_EQ(inv_sent, 0);
    ASSERT_EQ(inv_recv, 0);

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Edge Case Tests
 *============================================================================*/

TEST(null_parameter_handling) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, 1);

    /* Read with NULL dest */
    Seraph_Aether_Fetch_Result result = seraph_aether_read(
        &aether, SERAPH_AETHER_BASE, NULL, 100);
    ASSERT_NE(result.status, SERAPH_AETHER_OK);

    /* Write with NULL src */
    result = seraph_aether_write(&aether, SERAPH_AETHER_BASE, NULL, 100);
    ASSERT_NE(result.status, SERAPH_AETHER_OK);

    /* Operations on NULL aether */
    result = seraph_aether_read(NULL, SERAPH_AETHER_BASE, &result, sizeof(result));
    ASSERT_NE(result.status, SERAPH_AETHER_OK);

    seraph_aether_destroy(&aether);
}

TEST(multiple_sim_nodes) {
    Seraph_Aether aether;
    seraph_aether_init(&aether, 0, SERAPH_AETHER_MAX_SIM_NODES);

    /* Add maximum number of simulated nodes */
    for (uint16_t i = 0; i < SERAPH_AETHER_MAX_SIM_NODES; i++) {
        Seraph_Vbit result = seraph_aether_add_sim_node(&aether, i, 8192);
        ASSERT_TRUE(seraph_vbit_is_true(result));
    }

    /* Can't add more */
    Seraph_Vbit result = seraph_aether_add_sim_node(&aether, 100, 8192);
    ASSERT_TRUE(seraph_vbit_is_false(result));

    seraph_aether_destroy(&aether);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_aether_tests(void) {
    printf("\n=== MC28: Aether (Distributed Shared Memory) Tests ===\n\n");

    printf("Address Encoding:\n");
    RUN_TEST(address_encoding_basic);
    RUN_TEST(address_encoding_boundaries);
    RUN_TEST(address_range_check);
    RUN_TEST(page_alignment);

    printf("\nGlobal Generations:\n");
    RUN_TEST(global_gen_pack_unpack);
    RUN_TEST(global_gen_boundaries);

    printf("\nInitialization:\n");
    RUN_TEST(init_basic);
    RUN_TEST(init_default);
    RUN_TEST(init_null_handling);
    RUN_TEST(destroy_cleans_up);

    printf("\nSimulated Nodes:\n");
    RUN_TEST(add_sim_node);
    RUN_TEST(node_online_status);
    RUN_TEST(is_local_check);

    printf("\nMemory Allocation:\n");
    RUN_TEST(alloc_basic);
    RUN_TEST(alloc_multiple);
    RUN_TEST(alloc_on_specific_node);
    RUN_TEST(alloc_out_of_memory);
    RUN_TEST(alloc_zero_size);

    printf("\nRead/Write Operations:\n");
    RUN_TEST(read_write_local);
    RUN_TEST(read_write_vbit);
    RUN_TEST(read_write_remote);
    RUN_TEST(read_invalid_address);
    RUN_TEST(write_to_array);

    printf("\nCache Operations:\n");
    RUN_TEST(cache_hit_miss);
    RUN_TEST(cache_invalidation);
    RUN_TEST(cache_clear);

    printf("\nGeneration and Revocation:\n");
    RUN_TEST(generation_tracking);
    RUN_TEST(revocation);
    RUN_TEST(check_generation);

    printf("\nVOID Over Network (Failure Injection):\n");
    RUN_TEST(inject_timeout);
    RUN_TEST(inject_partition);
    RUN_TEST(node_offline);
    RUN_TEST(void_context);

    printf("\nCoherence Protocol:\n");
    RUN_TEST(coherence_read_request);
    RUN_TEST(coherence_write_request);
    RUN_TEST(directory_add_sharer);

    printf("\nStatistics:\n");
    RUN_TEST(statistics_tracking);
    RUN_TEST(statistics_reset);

    printf("\nEdge Cases:\n");
    RUN_TEST(null_parameter_handling);
    RUN_TEST(multiple_sim_nodes);

    printf("\n=== Aether Tests Complete ===\n");
    printf("Tests run: %d, Passed: %d\n\n", tests_run, tests_passed);
}
