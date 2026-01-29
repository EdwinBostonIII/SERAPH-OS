/**
 * @file test_proof_blob.c
 * @brief Tests for MC28: Zero-Overhead Strand Execution via Proof Blobs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seraph/proof_blob.h"
#include "seraph/strand.h"

/* Simple test macros */
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
        return 0; \
    } \
} while (0)

#define TEST_PASS(name) do { \
    fprintf(stderr, "PASS: %s\n", name); \
    return 1; \
} while (0)

/*============================================================================
 * Test: String Hashing
 *============================================================================*/

static int test_string_hash(void) {
    /* Same string should produce same hash */
    uint64_t h1 = seraph_proof_string_hash("hello");
    uint64_t h2 = seraph_proof_string_hash("hello");
    TEST_ASSERT(h1 == h2, "Same string should produce same hash");

    /* Different strings should produce different hashes */
    uint64_t h3 = seraph_proof_string_hash("world");
    TEST_ASSERT(h1 != h3, "Different strings should produce different hashes");

    /* NULL should return 0 */
    uint64_t h4 = seraph_proof_string_hash(NULL);
    TEST_ASSERT(h4 == 0, "NULL string should hash to 0");

    /* Empty string should have a non-zero hash */
    uint64_t h5 = seraph_proof_string_hash("");
    TEST_ASSERT(h5 != 0, "Empty string should have non-zero hash");

    TEST_PASS("test_string_hash");
}

/*============================================================================
 * Test: Location Hashing
 *============================================================================*/

static int test_location_hash(void) {
    uint64_t mod_hash = seraph_proof_string_hash("mymodule");
    uint64_t fn_hash = seraph_proof_string_hash("myfunction");

    uint64_t loc1 = seraph_proof_location_hash(mod_hash, fn_hash, 0, 0);
    uint64_t loc2 = seraph_proof_location_hash(mod_hash, fn_hash, 0, 0);
    TEST_ASSERT(loc1 == loc2, "Same location should produce same hash");

    /* Different offset should produce different hash */
    uint64_t loc3 = seraph_proof_location_hash(mod_hash, fn_hash, 1, 0);
    TEST_ASSERT(loc1 != loc3, "Different offset should produce different hash");

    /* Different expression index should produce different hash */
    uint64_t loc4 = seraph_proof_location_hash(mod_hash, fn_hash, 0, 1);
    TEST_ASSERT(loc1 != loc4, "Different expr index should produce different hash");

    TEST_PASS("test_location_hash");
}

/*============================================================================
 * Test: Builder Init/Destroy
 *============================================================================*/

static int test_builder_lifecycle(void) {
    Seraph_Proof_Blob_Builder builder;
    uint64_t mod_hash = seraph_proof_string_hash("test_module");

    /* Init should succeed */
    Seraph_Vbit result = seraph_proof_blob_builder_init(&builder, NULL, 0, mod_hash);
    TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Builder init should succeed");
    TEST_ASSERT(builder.module_hash == mod_hash, "Module hash should be set");
    TEST_ASSERT(builder.proof_count == 0, "Initial proof count should be 0");

    /* Destroy should clean up */
    seraph_proof_blob_builder_destroy(&builder);
    TEST_ASSERT(builder.temp_proofs == NULL, "Temp proofs should be NULL after destroy");

    TEST_PASS("test_builder_lifecycle");
}

/*============================================================================
 * Test: Add Proofs to Builder
 *============================================================================*/

static int test_builder_add_proofs(void) {
    Seraph_Proof_Blob_Builder builder;
    uint64_t mod_hash = seraph_proof_string_hash("test_module");

    Seraph_Vbit result = seraph_proof_blob_builder_init(&builder, NULL, 0, mod_hash);
    TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Builder init should succeed");

    /* Create a test proof */
    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    proof.bounds.array_size = 100;
    proof.bounds.index_min = 0;
    proof.bounds.index_max = 99;

    uint64_t loc_hash = seraph_proof_location_hash(mod_hash, 0, 10, 0);

    result = seraph_proof_blob_builder_add(&builder, loc_hash, &proof);
    TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Adding proof should succeed");
    TEST_ASSERT(builder.proof_count == 1, "Proof count should be 1");

    /* Add more proofs */
    for (int i = 0; i < 10; i++) {
        proof.bounds.index_max = (uint64_t)(100 + i);
        uint64_t loc = seraph_proof_location_hash(mod_hash, 0, (uint32_t)(20 + i), 0);
        result = seraph_proof_blob_builder_add(&builder, loc, &proof);
        TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Adding more proofs should succeed");
    }
    TEST_ASSERT(builder.proof_count == 11, "Proof count should be 11");

    seraph_proof_blob_builder_destroy(&builder);
    TEST_PASS("test_builder_add_proofs");
}

/*============================================================================
 * Test: Generate and Load Proof Blob
 *============================================================================*/

static int test_generate_and_load(void) {
    Seraph_Proof_Blob_Builder builder;
    uint64_t mod_hash = seraph_proof_string_hash("test_module");

    /* First pass: calculate size */
    Seraph_Vbit result = seraph_proof_blob_builder_init(&builder, NULL, 0, mod_hash);
    TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Builder init should succeed");

    /* Add test proofs */
    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    proof.bounds.array_size = 100;
    proof.bounds.index_min = 0;
    proof.bounds.index_max = 99;

    uint64_t loc1 = seraph_proof_location_hash(mod_hash, 0, 10, 0);
    seraph_proof_blob_builder_add(&builder, loc1, &proof);

    proof.kind = SERAPH_PROOF_VOID;
    proof.status = SERAPH_PROOF_STATUS_RUNTIME;
    uint64_t loc2 = seraph_proof_location_hash(mod_hash, 0, 20, 0);
    seraph_proof_blob_builder_add(&builder, loc2, &proof);

    size_t size = seraph_proof_blob_builder_finalize(&builder);
    TEST_ASSERT(size > 0, "Size calculation should return non-zero");
    seraph_proof_blob_builder_destroy(&builder);

    /* Second pass: generate blob */
    uint8_t* buffer = (uint8_t*)malloc(size);
    TEST_ASSERT(buffer != NULL, "Buffer allocation should succeed");

    result = seraph_proof_blob_builder_init(&builder, buffer, size, mod_hash);
    TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Builder init with buffer should succeed");

    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    seraph_proof_blob_builder_add(&builder, loc1, &proof);

    proof.kind = SERAPH_PROOF_VOID;
    proof.status = SERAPH_PROOF_STATUS_RUNTIME;
    seraph_proof_blob_builder_add(&builder, loc2, &proof);

    size_t actual_size = seraph_proof_blob_builder_finalize(&builder);
    TEST_ASSERT(actual_size == size, "Actual size should match calculated size");
    seraph_proof_blob_builder_destroy(&builder);

    /* Load the blob */
    Seraph_Proof_Blob blob;
    result = seraph_proof_blob_load(&blob, buffer, actual_size, true);
    TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Loading blob should succeed");
    TEST_ASSERT(blob.verified, "Blob should be verified");
    TEST_ASSERT(blob.header->proof_count == 2, "Should have 2 proofs");

    /* Query proofs */
    Seraph_Proof_Status status1 = seraph_proof_blob_query(&blob, loc1, SERAPH_PROOF_BOUNDS);
    TEST_ASSERT(status1 == SERAPH_PROOF_STATUS_PROVEN, "Bounds proof should be PROVEN");

    Seraph_Proof_Status status2 = seraph_proof_blob_query(&blob, loc2, SERAPH_PROOF_VOID);
    TEST_ASSERT(status2 == SERAPH_PROOF_STATUS_RUNTIME, "Void proof should be RUNTIME");

    /* Query non-existent proof */
    Seraph_Proof_Status status3 = seraph_proof_blob_query(&blob, loc1, SERAPH_PROOF_VOID);
    TEST_ASSERT(status3 == SERAPH_PROOF_STATUS_SKIPPED, "Non-existent proof should be SKIPPED");

    seraph_proof_blob_unload(&blob);
    free(buffer);

    TEST_PASS("test_generate_and_load");
}

/*============================================================================
 * Test: Proof Blob Has Proven
 *============================================================================*/

static int test_has_proven(void) {
    Seraph_Proof_Blob_Builder builder;
    uint64_t mod_hash = seraph_proof_string_hash("test_module");

    /* Build a blob with one proven proof */
    Seraph_Vbit result = seraph_proof_blob_builder_init(&builder, NULL, 0, mod_hash);
    TEST_ASSERT(result == SERAPH_VBIT_TRUE, "Builder init should succeed");

    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;

    uint64_t loc_proven = seraph_proof_location_hash(mod_hash, 0, 10, 0);
    seraph_proof_blob_builder_add(&builder, loc_proven, &proof);

    proof.status = SERAPH_PROOF_STATUS_RUNTIME;
    uint64_t loc_runtime = seraph_proof_location_hash(mod_hash, 0, 20, 0);
    seraph_proof_blob_builder_add(&builder, loc_runtime, &proof);

    size_t size = seraph_proof_blob_builder_finalize(&builder);
    seraph_proof_blob_builder_destroy(&builder);

    uint8_t* buffer = (uint8_t*)malloc(size);
    result = seraph_proof_blob_builder_init(&builder, buffer, size, mod_hash);
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    seraph_proof_blob_builder_add(&builder, loc_proven, &proof);
    proof.status = SERAPH_PROOF_STATUS_RUNTIME;
    seraph_proof_blob_builder_add(&builder, loc_runtime, &proof);
    seraph_proof_blob_builder_finalize(&builder);
    seraph_proof_blob_builder_destroy(&builder);

    Seraph_Proof_Blob blob;
    seraph_proof_blob_load(&blob, buffer, size, true);

    /* Test has_proven */
    bool proven = seraph_proof_blob_has_proven(&blob, loc_proven, SERAPH_PROOF_BOUNDS);
    TEST_ASSERT(proven, "PROVEN proof should return true");

    bool runtime = seraph_proof_blob_has_proven(&blob, loc_runtime, SERAPH_PROOF_BOUNDS);
    TEST_ASSERT(!runtime, "RUNTIME proof should return false");

    uint64_t loc_missing = seraph_proof_location_hash(mod_hash, 0, 30, 0);
    bool missing = seraph_proof_blob_has_proven(&blob, loc_missing, SERAPH_PROOF_BOUNDS);
    TEST_ASSERT(!missing, "Missing proof should return false");

    /* NULL blob should return false */
    bool null_blob = seraph_proof_blob_has_proven(NULL, loc_proven, SERAPH_PROOF_BOUNDS);
    TEST_ASSERT(!null_blob, "NULL blob should return false");

    seraph_proof_blob_unload(&blob);
    free(buffer);

    TEST_PASS("test_has_proven");
}

/*============================================================================
 * Test: Strand Proof Blob Attachment
 *============================================================================*/

static int test_strand_proof_attachment(void) {
    Seraph_Strand strand;
    Seraph_Strand_Error err;

    /* Create a strand */
    void dummy_entry(void* arg) { (void)arg; }
    err = seraph_strand_create(&strand, dummy_entry, NULL, 0);
    TEST_ASSERT(err == SERAPH_STRAND_OK, "Strand creation should succeed");

    /* Initially no proof blob */
    TEST_ASSERT(strand.proof_blob == NULL, "Initial proof blob should be NULL");
    TEST_ASSERT(strand.proof_flags == 0, "Initial proof flags should be 0");

    /* Build a simple proof blob */
    Seraph_Proof_Blob_Builder builder;
    uint64_t mod_hash = seraph_proof_string_hash("strand_test");
    seraph_proof_blob_builder_init(&builder, NULL, 0, mod_hash);

    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    uint64_t loc = seraph_proof_location_hash(mod_hash, 0, 10, 0);
    seraph_proof_blob_builder_add(&builder, loc, &proof);

    size_t size = seraph_proof_blob_builder_finalize(&builder);
    seraph_proof_blob_builder_destroy(&builder);

    uint8_t* buffer = (uint8_t*)malloc(size);
    seraph_proof_blob_builder_init(&builder, buffer, size, mod_hash);
    seraph_proof_blob_builder_add(&builder, loc, &proof);
    seraph_proof_blob_builder_finalize(&builder);
    seraph_proof_blob_builder_destroy(&builder);

    Seraph_Proof_Blob blob;
    seraph_proof_blob_load(&blob, buffer, size, true);

    /* Attach proof blob to strand */
    err = seraph_strand_attach_proof_blob(&strand, (const struct Seraph_Proof_Blob*)&blob, SERAPH_STRAND_PROOF_STATS);
    TEST_ASSERT(err == SERAPH_STRAND_OK, "Attaching proof blob should succeed");
    TEST_ASSERT(strand.proof_blob == (const struct Seraph_Proof_Blob*)&blob, "Proof blob should be attached");
    TEST_ASSERT(strand.proof_flags == SERAPH_STRAND_PROOF_STATS, "Proof flags should be set");

    /* Get stats (should be 0 initially) */
    uint64_t skipped, performed;
    seraph_strand_proof_stats(&strand, &skipped, &performed);
    TEST_ASSERT(skipped == 0, "Initial skipped should be 0");
    TEST_ASSERT(performed == 0, "Initial performed should be 0");

    /* Record some stats */
    seraph_strand_proof_skipped(&strand);
    seraph_strand_proof_skipped(&strand);
    seraph_strand_proof_performed(&strand);

    seraph_strand_proof_stats(&strand, &skipped, &performed);
    TEST_ASSERT(skipped == 2, "Skipped should be 2");
    TEST_ASSERT(performed == 1, "Performed should be 1");

    /* Detach proof blob */
    err = seraph_strand_detach_proof_blob(&strand);
    TEST_ASSERT(err == SERAPH_STRAND_OK, "Detaching proof blob should succeed");
    TEST_ASSERT(strand.proof_blob == NULL, "Proof blob should be NULL after detach");

    seraph_proof_blob_unload(&blob);
    free(buffer);
    seraph_strand_destroy(&strand);

    TEST_PASS("test_strand_proof_attachment");
}

/*============================================================================
 * Test: Proof Blob Statistics
 *============================================================================*/

static int test_proof_blob_stats(void) {
    Seraph_Proof_Blob_Builder builder;
    uint64_t mod_hash = seraph_proof_string_hash("stats_test");

    seraph_proof_blob_builder_init(&builder, NULL, 0, mod_hash);

    /* Add proofs with various statuses */
    Seraph_Proof proof = {0};

    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 0, 0), &proof);

    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 1, 0), &proof);

    proof.status = SERAPH_PROOF_STATUS_RUNTIME;
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 2, 0), &proof);

    proof.status = SERAPH_PROOF_STATUS_ASSUMED;
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 3, 0), &proof);

    size_t size = seraph_proof_blob_builder_finalize(&builder);
    seraph_proof_blob_builder_destroy(&builder);

    uint8_t* buffer = (uint8_t*)malloc(size);
    seraph_proof_blob_builder_init(&builder, buffer, size, mod_hash);

    proof.status = SERAPH_PROOF_STATUS_PROVEN;
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 0, 0), &proof);
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 1, 0), &proof);

    proof.status = SERAPH_PROOF_STATUS_RUNTIME;
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 2, 0), &proof);

    proof.status = SERAPH_PROOF_STATUS_ASSUMED;
    seraph_proof_blob_builder_add(&builder,
        seraph_proof_location_hash(mod_hash, 0, 3, 0), &proof);

    seraph_proof_blob_builder_finalize(&builder);
    seraph_proof_blob_builder_destroy(&builder);

    Seraph_Proof_Blob blob;
    seraph_proof_blob_load(&blob, buffer, size, true);

    Seraph_Proof_Blob_Stats stats;
    seraph_proof_blob_stats(&blob, &stats);

    TEST_ASSERT(stats.total_proofs == 4, "Total should be 4");
    TEST_ASSERT(stats.proven_count == 2, "Proven should be 2");
    TEST_ASSERT(stats.runtime_count == 1, "Runtime should be 1");
    TEST_ASSERT(stats.assumed_count == 1, "Assumed should be 1");

    seraph_proof_blob_unload(&blob);
    free(buffer);

    TEST_PASS("test_proof_blob_stats");
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_proof_blob_tests(void) {
    fprintf(stderr, "\n=== Proof Blob Tests (MC28: Zero-Overhead Execution) ===\n\n");

    int passed = 0;
    int total = 0;

    total++; passed += test_string_hash();
    total++; passed += test_location_hash();
    total++; passed += test_builder_lifecycle();
    total++; passed += test_builder_add_proofs();
    total++; passed += test_generate_and_load();
    total++; passed += test_has_proven();
    total++; passed += test_strand_proof_attachment();
    total++; passed += test_proof_blob_stats();

    fprintf(stderr, "\n=== Proof Blob Tests: %d/%d passed ===\n\n", passed, total);
}

#ifdef PROOF_BLOB_STANDALONE_TEST
int main(void) {
    run_proof_blob_tests();
    return 0;
}
#endif
