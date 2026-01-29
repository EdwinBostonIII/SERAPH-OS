/**
 * @file test_bits.c
 * @brief Tests for MC2: Bit Operations
 */

#include "seraph/bits.h"
#include <stdio.h>
#include <stdlib.h>

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
 * Single Bit Tests
 *============================================================================*/

TEST(bit_get) {
    ASSERT(seraph_bit_get_u64(0b1010, 0) == 0);
    ASSERT(seraph_bit_get_u64(0b1010, 1) == 1);
    ASSERT(seraph_bit_get_u64(0b1010, 2) == 0);
    ASSERT(seraph_bit_get_u64(0b1010, 3) == 1);

    /* Out of range returns VOID */
    ASSERT(SERAPH_IS_VOID_U64(seraph_bit_get_u64(0b1010, 64)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_bit_get_u64(0b1010, 100)));

    /* VOID input returns VOID */
    ASSERT(SERAPH_IS_VOID_U64(seraph_bit_get_u64(SERAPH_VOID_U64, 0)));
}

TEST(bit_set) {
    ASSERT(seraph_bit_set_u64(0, 0) == 0b0001);
    ASSERT(seraph_bit_set_u64(0, 3) == 0b1000);
    ASSERT(seraph_bit_set_u64(0b0010, 0) == 0b0011);

    /* Setting already-set bit is idempotent */
    ASSERT(seraph_bit_set_u64(0b0001, 0) == 0b0001);

    /* Out of range */
    ASSERT(SERAPH_IS_VOID_U64(seraph_bit_set_u64(0, 64)));
}

TEST(bit_clear) {
    ASSERT(seraph_bit_clear_u64(0b1111, 0) == 0b1110);
    ASSERT(seraph_bit_clear_u64(0b1111, 3) == 0b0111);

    /* Clearing already-clear bit is idempotent */
    ASSERT(seraph_bit_clear_u64(0b1110, 0) == 0b1110);

    /* Out of range */
    ASSERT(SERAPH_IS_VOID_U64(seraph_bit_clear_u64(0, 64)));
}

TEST(bit_toggle) {
    ASSERT(seraph_bit_toggle_u64(0b1010, 0) == 0b1011);
    ASSERT(seraph_bit_toggle_u64(0b1010, 1) == 0b1000);

    /* Toggle twice returns original */
    uint64_t x = 0x12345678;
    ASSERT(seraph_bit_toggle_u64(seraph_bit_toggle_u64(x, 5), 5) == x);
}

TEST(bit_test) {
    ASSERT(seraph_bit_test_u64(0b1010, 0) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_bit_test_u64(0b1010, 1) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_bit_test_u64(SERAPH_VOID_U64, 0) == SERAPH_VBIT_VOID);
    ASSERT(seraph_bit_test_u64(0b1010, 64) == SERAPH_VBIT_VOID);
}

/*============================================================================
 * Bit Range Tests
 *============================================================================*/

TEST(bits_extract) {
    /* Extract low nibble */
    ASSERT(seraph_bits_extract_u64(0xABCD, 0, 4) == 0xD);

    /* Extract second nibble */
    ASSERT(seraph_bits_extract_u64(0xABCD, 4, 4) == 0xC);

    /* Extract byte */
    ASSERT(seraph_bits_extract_u64(0xABCD, 8, 8) == 0xAB);

    /* Invalid range */
    ASSERT(SERAPH_IS_VOID_U64(seraph_bits_extract_u64(0xABCD, 60, 8)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_bits_extract_u64(0xABCD, 0, 0)));
}

TEST(bits_insert) {
    /* Insert low nibble */
    ASSERT(seraph_bits_insert_u64(0xABC0, 0xF, 0, 4) == 0xABCF);

    /* Insert in middle */
    ASSERT(seraph_bits_insert_u64(0xA00D, 0xBC, 4, 8) == 0xABCD);

    /* Invalid range */
    ASSERT(SERAPH_IS_VOID_U64(seraph_bits_insert_u64(0, 0xFF, 60, 8)));
}

TEST(bitrange_struct) {
    Seraph_BitRange range = {4, 8};
    ASSERT(seraph_bitrange_valid_64(range) == true);

    ASSERT(seraph_bitrange_extract_u64(0xABCDEF, range) == 0xDE);

    Seraph_BitRange invalid = {60, 8};
    ASSERT(seraph_bitrange_valid_64(invalid) == false);
}

/*============================================================================
 * Shift Tests
 *============================================================================*/

TEST(shift_left) {
    ASSERT(seraph_shl_u64(1, 4) == 16);
    ASSERT(seraph_shl_u64(0xFF, 8) == 0xFF00);

    /* Shift by 0 */
    ASSERT(seraph_shl_u64(42, 0) == 42);

    /* Shift >= width returns VOID */
    ASSERT(SERAPH_IS_VOID_U64(seraph_shl_u64(1, 64)));
    ASSERT(SERAPH_IS_VOID_U64(seraph_shl_u64(1, 100)));

    /* VOID propagates */
    ASSERT(SERAPH_IS_VOID_U64(seraph_shl_u64(SERAPH_VOID_U64, 4)));
}

TEST(shift_right) {
    ASSERT(seraph_shr_u64(256, 4) == 16);
    ASSERT(seraph_shr_u64(0xFF00, 8) == 0xFF);

    ASSERT(SERAPH_IS_VOID_U64(seraph_shr_u64(1, 64)));
}

TEST(rotate) {
    /* Rotate left */
    ASSERT(seraph_rol_u64(1, 4) == 16);
    ASSERT(seraph_rol_u64(0x8000000000000000ULL, 1) == 1);

    /* Rotate right */
    ASSERT(seraph_ror_u64(16, 4) == 1);
    ASSERT(seraph_ror_u64(1, 1) == 0x8000000000000000ULL);

    /* Rotate by 0 is identity */
    ASSERT(seraph_rol_u64(0x12345678, 0) == 0x12345678);
    ASSERT(seraph_ror_u64(0x12345678, 0) == 0x12345678);

    /* Rotate by 64 is identity (modulo) */
    ASSERT(seraph_rol_u64(0x12345678, 64) == 0x12345678);
}

/*============================================================================
 * Population Count Tests
 *============================================================================*/

TEST(popcount) {
    ASSERT(seraph_popcount_u64(0) == 0);
    ASSERT(seraph_popcount_u64(1) == 1);
    ASSERT(seraph_popcount_u64(0xFF) == 8);
    /* Note: UINT64_MAX == SERAPH_VOID_U64 in SERAPH, so popcount returns 0xFF */
    ASSERT(seraph_popcount_u64(UINT64_MAX - 1) == 63);  /* One less than all-ones */
    ASSERT(seraph_popcount_u64(0x5555555555555555ULL) == 32);

    /* VOID returns special marker */
    ASSERT(seraph_popcount_u64(SERAPH_VOID_U64) == 0xFF);
}

TEST(clz) {
    ASSERT(seraph_clz_u64(1) == 63);
    ASSERT(seraph_clz_u64(0x8000000000000000ULL) == 0);
    ASSERT(seraph_clz_u64(0x0000000100000000ULL) == 31);

    /* Zero and VOID return 0xFF */
    ASSERT(seraph_clz_u64(0) == 0xFF);
    ASSERT(seraph_clz_u64(SERAPH_VOID_U64) == 0xFF);
}

TEST(ctz) {
    ASSERT(seraph_ctz_u64(1) == 0);
    ASSERT(seraph_ctz_u64(2) == 1);
    ASSERT(seraph_ctz_u64(0x8000000000000000ULL) == 63);
    ASSERT(seraph_ctz_u64(0x100) == 8);

    ASSERT(seraph_ctz_u64(0) == 0xFF);
    ASSERT(seraph_ctz_u64(SERAPH_VOID_U64) == 0xFF);
}

TEST(ffs_fls) {
    ASSERT(seraph_ffs_u64(1) == 1);
    ASSERT(seraph_ffs_u64(0b1000) == 4);
    ASSERT(seraph_ffs_u64(0) == 0);
    ASSERT(seraph_ffs_u64(SERAPH_VOID_U64) == 0xFF);

    ASSERT(seraph_fls_u64(1) == 1);
    ASSERT(seraph_fls_u64(0b1111) == 4);
    ASSERT(seraph_fls_u64(0x8000000000000000ULL) == 64);
    ASSERT(seraph_fls_u64(0) == 0);
}

/*============================================================================
 * Mask Generation Tests
 *============================================================================*/

TEST(masks) {
    ASSERT(seraph_mask_low_u64(0) == 0);
    ASSERT(seraph_mask_low_u64(4) == 0xF);
    ASSERT(seraph_mask_low_u64(8) == 0xFF);
    ASSERT(seraph_mask_low_u64(64) == UINT64_MAX);

    ASSERT(seraph_mask_high_u64(0) == 0);
    ASSERT(seraph_mask_high_u64(4) == 0xF000000000000000ULL);
    ASSERT(seraph_mask_high_u64(64) == UINT64_MAX);

    ASSERT(seraph_mask_range_u64(0, 4) == 0xF);
    ASSERT(seraph_mask_range_u64(4, 4) == 0xF0);
    ASSERT(seraph_mask_range_u64(8, 8) == 0xFF00);
}

/*============================================================================
 * Byte Swap Tests
 *============================================================================*/

TEST(bswap) {
    ASSERT(seraph_bswap_u64(0x0102030405060708ULL) == 0x0807060504030201ULL);
    ASSERT(seraph_bswap_u32(0x01020304UL) == 0x04030201UL);

    /* Double swap is identity */
    uint64_t x = 0x123456789ABCDEF0ULL;
    ASSERT(seraph_bswap_u64(seraph_bswap_u64(x)) == x);

    /* VOID propagates */
    ASSERT(SERAPH_IS_VOID_U64(seraph_bswap_u64(SERAPH_VOID_U64)));
}

/*============================================================================
 * Power of Two Tests
 *============================================================================*/

TEST(is_pow2) {
    ASSERT(seraph_is_pow2_u64(1) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_is_pow2_u64(2) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_is_pow2_u64(4) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_is_pow2_u64(1024) == SERAPH_VBIT_TRUE);

    ASSERT(seraph_is_pow2_u64(0) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_is_pow2_u64(3) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_is_pow2_u64(6) == SERAPH_VBIT_FALSE);

    ASSERT(seraph_is_pow2_u64(SERAPH_VOID_U64) == SERAPH_VBIT_VOID);
}

TEST(next_pow2) {
    ASSERT(seraph_next_pow2_u64(0) == 1);
    ASSERT(seraph_next_pow2_u64(1) == 1);
    ASSERT(seraph_next_pow2_u64(3) == 4);
    ASSERT(seraph_next_pow2_u64(5) == 8);
    ASSERT(seraph_next_pow2_u64(1023) == 1024);
    ASSERT(seraph_next_pow2_u64(1024) == 1024);

    /* Overflow returns VOID */
    ASSERT(SERAPH_IS_VOID_U64(seraph_next_pow2_u64(0x8000000000000001ULL)));
}

TEST(log2) {
    ASSERT(seraph_log2_u64(1) == 0);
    ASSERT(seraph_log2_u64(2) == 1);
    ASSERT(seraph_log2_u64(4) == 2);
    ASSERT(seraph_log2_u64(8) == 3);
    ASSERT(seraph_log2_u64(1024) == 10);
    ASSERT(seraph_log2_u64(1025) == 10);  /* floor */

    ASSERT(seraph_log2_u64(0) == 0xFF);
    ASSERT(seraph_log2_u64(SERAPH_VOID_U64) == 0xFF);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_bits_tests(void) {
    printf("\n=== MC2: Bit Operations Tests ===\n\n");

    /* Single Bit */
    RUN_TEST(bit_get);
    RUN_TEST(bit_set);
    RUN_TEST(bit_clear);
    RUN_TEST(bit_toggle);
    RUN_TEST(bit_test);

    /* Bit Ranges */
    RUN_TEST(bits_extract);
    RUN_TEST(bits_insert);
    RUN_TEST(bitrange_struct);

    /* Shifts */
    RUN_TEST(shift_left);
    RUN_TEST(shift_right);
    RUN_TEST(rotate);

    /* Population/Scanning */
    RUN_TEST(popcount);
    RUN_TEST(clz);
    RUN_TEST(ctz);
    RUN_TEST(ffs_fls);

    /* Masks */
    RUN_TEST(masks);

    /* Byte Swap */
    RUN_TEST(bswap);

    /* Power of Two */
    RUN_TEST(is_pow2);
    RUN_TEST(next_pow2);
    RUN_TEST(log2);

    printf("\nBits Tests: %d/%d passed\n", tests_passed, tests_run);
}
