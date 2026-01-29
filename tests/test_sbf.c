/**
 * @file test_sbf.c
 * @brief SERAPH Binary Format (SBF) Test Suite
 *
 * Tests for the SBF writer, loader, and validation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "seraph/sbf.h"
#include "seraph/seraphim/sbf_writer.h"
#include "seraph/seraphim/sbf_loader.h"
#include "seraph/crypto/sha256.h"

/*============================================================================
 * Test Utilities
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing: %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

#define ASSERT(cond) do { if (!(cond)) { printf("\n    ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); return 0; } } while(0)

/*============================================================================
 * SHA-256 Tests
 *============================================================================*/

static int test_sha256_basic(void) {
    /* Test empty string */
    uint8_t hash[SHA256_DIGEST_SIZE];
    sha256("", 0, hash);

    /* SHA-256("") = e3b0c442...b855 */
    ASSERT(hash[0] == 0xe3);
    ASSERT(hash[1] == 0xb0);
    ASSERT(hash[31] == 0x55);

    return 1;
}

static int test_sha256_abc(void) {
    uint8_t hash[SHA256_DIGEST_SIZE];
    sha256("abc", 3, hash);

    /* SHA-256("abc") = ba7816bf...15ad */
    ASSERT(hash[0] == 0xba);
    ASSERT(hash[1] == 0x78);
    ASSERT(hash[31] == 0xad);

    return 1;
}

static int test_sha256_incremental(void) {
    uint8_t hash1[SHA256_DIGEST_SIZE];
    uint8_t hash2[SHA256_DIGEST_SIZE];

    /* One-shot */
    sha256("hello world", 11, hash1);

    /* Incremental */
    SHA256_Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, "hello ", 6);
    sha256_update(&ctx, "world", 5);
    sha256_final(&ctx, hash2);

    ASSERT(sha256_equal(hash1, hash2));

    return 1;
}

static int test_sha256_merkle(void) {
    uint8_t leaves[3 * SHA256_DIGEST_SIZE];
    uint8_t root[SHA256_DIGEST_SIZE];

    /* Create 3 leaves */
    sha256("leaf1", 5, &leaves[0]);
    sha256("leaf2", 5, &leaves[SHA256_DIGEST_SIZE]);
    sha256("leaf3", 5, &leaves[2 * SHA256_DIGEST_SIZE]);

    /* Compute Merkle root */
    ASSERT(sha256_merkle_root_alloc(leaves, 3, root) == 1);

    /* Root should not be zeros */
    ASSERT(!sha256_is_zero(root));

    /* Single leaf should equal itself */
    uint8_t single_root[SHA256_DIGEST_SIZE];
    ASSERT(sha256_merkle_root_alloc(leaves, 1, single_root) == 1);
    ASSERT(sha256_equal(single_root, leaves));

    return 1;
}

static int test_sha256_hex(void) {
    uint8_t hash[SHA256_DIGEST_SIZE];
    char hex[65];
    uint8_t parsed[SHA256_DIGEST_SIZE];

    sha256("test", 4, hash);
    sha256_to_hex(hash, hex, sizeof(hex));

    ASSERT(strlen(hex) == 64);
    ASSERT(sha256_from_hex(hex, parsed) == 1);
    ASSERT(sha256_equal(hash, parsed));

    return 1;
}

/*============================================================================
 * SBF Header Tests
 *============================================================================*/

static int test_sbf_header_size(void) {
    ASSERT(sizeof(SBF_Header) == SBF_HEADER_SIZE);
    ASSERT(sizeof(SBF_Header) == 256);
    return 1;
}

static int test_sbf_manifest_size(void) {
    ASSERT(sizeof(SBF_Manifest) == SBF_MANIFEST_SIZE);
    ASSERT(sizeof(SBF_Manifest) == 256);
    return 1;
}

static int test_sbf_proof_entry_size(void) {
    ASSERT(sizeof(SBF_Proof_Entry) == 56);
    return 1;
}

static int test_sbf_proof_table_size(void) {
    ASSERT(sizeof(SBF_Proof_Table) == 48);
    return 1;
}

static int test_sbf_cap_template_size(void) {
    ASSERT(sizeof(SBF_Cap_Template) == 32);
    return 1;
}

static int test_sbf_effect_entry_size(void) {
    ASSERT(sizeof(SBF_Effect_Entry) == 24);
    return 1;
}

static int test_sbf_validate_header_quick(void) {
    SBF_Header hdr = {0};

    /* Invalid magic */
    ASSERT(sbf_validate_header_quick(&hdr) == SBF_ERR_INVALID_MAGIC);

    /* Valid magic, wrong version */
    hdr.magic = SBF_MAGIC;
    hdr.version = 0x00020000;  /* Major version 2 */
    ASSERT(sbf_validate_header_quick(&hdr) == SBF_ERR_INVALID_VERSION);

    /* Valid header */
    hdr.version = SBF_VERSION;
    hdr.header_size = SBF_HEADER_SIZE;
    ASSERT(sbf_validate_header_quick(&hdr) == SBF_VALID);

    return 1;
}

/*============================================================================
 * SBF Writer Tests
 *============================================================================*/

static int test_sbf_writer_create(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);
    ASSERT(!sbf_writer_is_finalized(writer));
    ASSERT(sbf_writer_get_error(writer) == SBF_WRITE_OK);
    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_writer_set_code(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    /* NOP sled for code */
    uint8_t code[64];
    memset(code, 0x90, sizeof(code));  /* x86 NOP */

    ASSERT(sbf_writer_set_code(writer, code, sizeof(code)) == SBF_WRITE_OK);

    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_writer_add_string(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    uint32_t off1 = sbf_writer_add_string(writer, "hello");
    uint32_t off2 = sbf_writer_add_string(writer, "world");

    ASSERT(off1 != (uint32_t)-1);
    ASSERT(off2 != (uint32_t)-1);
    ASSERT(off2 > off1);

    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_writer_add_proof(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    ASSERT(sbf_writer_add_proof_ex(writer,
        SBF_PROOF_BOUNDS, SBF_PROOF_PROVEN, 0x1000,
        "test.srph:10:5", "array access proven safe") == SBF_WRITE_OK);

    ASSERT(sbf_writer_get_proof_count(writer) == 1);

    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_writer_add_capability(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    ASSERT(sbf_writer_add_capability_ex(writer,
        0, 4096, SBF_CAP_READ | SBF_CAP_EXEC, ".code") == SBF_WRITE_OK);

    ASSERT(sbf_writer_get_cap_count(writer) == 1);

    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_writer_add_effect(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    ASSERT(sbf_writer_add_effect_ex(writer,
        0x1000, 256, SBF_EFFECT_NONE, SBF_EFFECT_NONE, 0, "main") == SBF_WRITE_OK);

    ASSERT(sbf_writer_get_effect_count(writer) == 1);

    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_writer_finalize(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    /* Cannot finalize without code */
    ASSERT(sbf_writer_finalize(writer) == SBF_WRITE_ERR_NO_CODE);

    /* Add some code */
    uint8_t code[64];
    memset(code, 0x90, sizeof(code));
    ASSERT(sbf_writer_set_code(writer, code, sizeof(code)) == SBF_WRITE_OK);

    /* Configure manifest */
    SBF_Manifest_Config manifest = {0};
    manifest.stack_size = 0x10000;
    manifest.heap_size = 0x100000;
    manifest.chronon_budget = 1000000;
    ASSERT(sbf_writer_configure_manifest(writer, &manifest) == SBF_WRITE_OK);

    /* Add standard capabilities */
    ASSERT(sbf_writer_add_standard_caps(writer, 0x10000) == SBF_WRITE_OK);

    /* Finalize */
    ASSERT(sbf_writer_finalize(writer) == SBF_WRITE_OK);
    ASSERT(sbf_writer_is_finalized(writer));

    /* Cannot modify after finalization */
    ASSERT(sbf_writer_set_code(writer, code, sizeof(code)) == SBF_WRITE_ERR_ALREADY_FINAL);

    /* Get binary */
    size_t size;
    const void* data = sbf_writer_get_binary(writer, &size);
    ASSERT(data != NULL);
    ASSERT(size >= SBF_HEADER_SIZE + SBF_MANIFEST_SIZE);

    /* Verify header */
    const SBF_Header* hdr = sbf_writer_get_header(writer);
    ASSERT(hdr != NULL);
    ASSERT(hdr->magic == SBF_MAGIC);
    ASSERT(hdr->version == SBF_VERSION);
    ASSERT(hdr->total_size == size);

    sbf_writer_destroy(writer);
    return 1;
}

/*============================================================================
 * SBF Loader Tests
 *============================================================================*/

static int test_sbf_loader_create(void) {
    SBF_Loader* loader = sbf_loader_create();
    ASSERT(loader != NULL);
    ASSERT(!sbf_loader_is_loaded(loader));
    sbf_loader_destroy(loader);
    return 1;
}

static int test_sbf_roundtrip(void) {
    /* Create and finalize a binary */
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    uint8_t code[128];
    memset(code, 0x90, sizeof(code));
    code[0] = 0x55;  /* push rbp */
    code[1] = 0x48; code[2] = 0x89; code[3] = 0xe5;  /* mov rbp, rsp */
    code[4] = 0xb8; code[5] = 0x00; code[6] = 0x00; code[7] = 0x00; code[8] = 0x00;  /* mov eax, 0 */
    code[9] = 0x5d;  /* pop rbp */
    code[10] = 0xc3; /* ret */

    ASSERT(sbf_writer_set_code(writer, code, sizeof(code)) == SBF_WRITE_OK);
    ASSERT(sbf_writer_set_entry(writer, 0) == SBF_WRITE_OK);

    SBF_Manifest_Config manifest = {0};
    manifest.stack_size = 0x10000;
    ASSERT(sbf_writer_configure_manifest(writer, &manifest) == SBF_WRITE_OK);

    ASSERT(sbf_writer_add_standard_caps(writer, 0x10000) == SBF_WRITE_OK);

    ASSERT(sbf_writer_add_proof_ex(writer,
        SBF_PROOF_TYPE, SBF_PROOF_PROVEN, 0,
        "main.srph:1:1", "main returns i32") == SBF_WRITE_OK);

    ASSERT(sbf_writer_finalize(writer) == SBF_WRITE_OK);

    size_t size;
    const void* data = sbf_writer_get_binary(writer, &size);
    ASSERT(data != NULL);

    /* Load the binary */
    SBF_Loader* loader = sbf_loader_create();
    ASSERT(loader != NULL);

    ASSERT(sbf_loader_load_buffer(loader, data, size, true) == SBF_LOAD_OK);
    ASSERT(sbf_loader_is_loaded(loader));

    /* Validate */
    ASSERT(sbf_loader_validate(loader) == SBF_VALID);

    /* Check header */
    const SBF_Header* hdr = sbf_loader_get_header(loader);
    ASSERT(hdr != NULL);
    ASSERT(hdr->magic == SBF_MAGIC);
    ASSERT(hdr->code_size == sizeof(code));

    /* Check code */
    size_t code_size;
    const void* loaded_code = sbf_loader_get_code(loader, &code_size);
    ASSERT(loaded_code != NULL);
    ASSERT(code_size == sizeof(code));
    ASSERT(memcmp(loaded_code, code, sizeof(code)) == 0);

    /* Check proof */
    ASSERT(sbf_loader_get_proof_count(loader) == 1);
    const SBF_Proof_Entry* proof = sbf_loader_get_proof(loader, 0);
    ASSERT(proof != NULL);
    ASSERT(proof->kind == SBF_PROOF_TYPE);
    ASSERT(proof->status == SBF_PROOF_PROVEN);

    /* Check manifest */
    ASSERT(sbf_loader_get_required_stack(loader) == 0x10000);

    sbf_loader_destroy(loader);
    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_validation(void) {
    /* Create a binary with a failed proof */
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    uint8_t code[64];
    memset(code, 0x90, sizeof(code));
    ASSERT(sbf_writer_set_code(writer, code, sizeof(code)) == SBF_WRITE_OK);

    SBF_Manifest_Config manifest = {0};
    ASSERT(sbf_writer_configure_manifest(writer, &manifest) == SBF_WRITE_OK);

    /* Add a failed proof */
    ASSERT(sbf_writer_add_proof_ex(writer,
        SBF_PROOF_BOUNDS, SBF_PROOF_FAILED, 0x20,
        "bad.srph:5:1", "array bounds check failed") == SBF_WRITE_OK);

    ASSERT(sbf_writer_finalize(writer) == SBF_WRITE_OK);

    size_t size;
    const void* data = sbf_writer_get_binary(writer, &size);

    /* Load with reject_failed_proofs enabled (default) */
    SBF_Loader_Config config = {
        .verify_content_hash = true,
        .verify_proof_root = true,
        .reject_failed_proofs = true,
    };
    SBF_Loader* loader = sbf_loader_create_with_config(&config);
    ASSERT(loader != NULL);

    ASSERT(sbf_loader_load_buffer(loader, data, size, true) == SBF_LOAD_OK);

    /* Validation should fail due to failed proof */
    ASSERT(sbf_loader_validate(loader) == SBF_ERR_PROOF_FAILED);
    ASSERT(sbf_loader_has_failed_proofs(loader));
    ASSERT(sbf_loader_get_failed_count(loader) == 1);

    sbf_loader_destroy(loader);

    /* Load with reject_failed_proofs disabled */
    config.reject_failed_proofs = false;
    loader = sbf_loader_create_with_config(&config);
    ASSERT(loader != NULL);

    ASSERT(sbf_loader_load_buffer(loader, data, size, true) == SBF_LOAD_OK);
    ASSERT(sbf_loader_validate(loader) == SBF_VALID);  /* Passes despite failed proof */

    sbf_loader_destroy(loader);
    sbf_writer_destroy(writer);
    return 1;
}

static int test_sbf_dump(void) {
    SBF_Writer* writer = sbf_writer_create();
    ASSERT(writer != NULL);

    uint8_t code[64];
    memset(code, 0x90, sizeof(code));
    ASSERT(sbf_writer_set_code(writer, code, sizeof(code)) == SBF_WRITE_OK);

    SBF_Manifest_Config manifest = {0};
    ASSERT(sbf_writer_configure_manifest(writer, &manifest) == SBF_WRITE_OK);

    ASSERT(sbf_writer_finalize(writer) == SBF_WRITE_OK);

    char buf[2048];
    size_t len = sbf_writer_dump(writer, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "SBF Writer State") != NULL);
    ASSERT(strstr(buf, "Code Size: 64 bytes") != NULL);

    size_t size;
    const void* data = sbf_writer_get_binary(writer, &size);

    SBF_Loader* loader = sbf_loader_create();
    ASSERT(sbf_loader_load_buffer(loader, data, size, true) == SBF_LOAD_OK);

    len = sbf_loader_dump(loader, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "SBF Loader State") != NULL);

    sbf_loader_destroy(loader);
    sbf_writer_destroy(writer);
    return 1;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    printf("=== SERAPH Binary Format (SBF) Test Suite ===\n\n");

    printf("SHA-256 Tests:\n");
    TEST(sha256_basic);
    TEST(sha256_abc);
    TEST(sha256_incremental);
    TEST(sha256_merkle);
    TEST(sha256_hex);

    printf("\nSBF Structure Tests:\n");
    TEST(sbf_header_size);
    TEST(sbf_manifest_size);
    TEST(sbf_proof_entry_size);
    TEST(sbf_proof_table_size);
    TEST(sbf_cap_template_size);
    TEST(sbf_effect_entry_size);
    TEST(sbf_validate_header_quick);

    printf("\nSBF Writer Tests:\n");
    TEST(sbf_writer_create);
    TEST(sbf_writer_set_code);
    TEST(sbf_writer_add_string);
    TEST(sbf_writer_add_proof);
    TEST(sbf_writer_add_capability);
    TEST(sbf_writer_add_effect);
    TEST(sbf_writer_finalize);

    printf("\nSBF Loader Tests:\n");
    TEST(sbf_loader_create);
    TEST(sbf_roundtrip);
    TEST(sbf_validation);
    TEST(sbf_dump);

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
