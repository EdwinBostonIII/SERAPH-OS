/**
 * @file test_semantic_byte.c
 * @brief Tests for MC3: Semantic Byte
 */

#include "seraph/semantic_byte.h"
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
 * Creation Tests
 *============================================================================*/

TEST(sbyte_from_u8) {
    Seraph_SemanticByte sb = seraph_sbyte_from_u8(0xA5);
    ASSERT(sb.mask == 0xFF);
    ASSERT(sb.value == 0xA5);
}

TEST(sbyte_create) {
    Seraph_SemanticByte sb = seraph_sbyte_create(0xA5, 0xF0);
    ASSERT(sb.mask == 0xF0);
    ASSERT(sb.value == 0xA0);  /* Low nibble cleared due to mask */

    /* Full mask */
    sb = seraph_sbyte_create(0x12, 0xFF);
    ASSERT(sb.mask == 0xFF);
    ASSERT(sb.value == 0x12);

    /* Empty mask */
    sb = seraph_sbyte_create(0xFF, 0x00);
    ASSERT(sb.mask == 0x00);
    ASSERT(sb.value == 0x00);
}

TEST(sbyte_void) {
    Seraph_SemanticByte sb = seraph_sbyte_void();
    ASSERT(sb.mask == 0x00);
    ASSERT(sb.value == 0x00);
    ASSERT(seraph_sbyte_is_void(sb));
}

TEST(sbyte_from_vbits) {
    Seraph_Vbit bits[8] = {
        SERAPH_VBIT_TRUE,   /* bit 0 */
        SERAPH_VBIT_FALSE,  /* bit 1 */
        SERAPH_VBIT_VOID,   /* bit 2 */
        SERAPH_VBIT_TRUE,   /* bit 3 */
        SERAPH_VBIT_VOID,   /* bit 4 */
        SERAPH_VBIT_VOID,   /* bit 5 */
        SERAPH_VBIT_TRUE,   /* bit 6 */
        SERAPH_VBIT_FALSE   /* bit 7 */
    };
    Seraph_SemanticByte sb = seraph_sbyte_from_vbits(bits);
    ASSERT(sb.mask == 0b11001011);  /* bits 0,1,3,6,7 valid */
    ASSERT(sb.value == 0b01001001); /* bits 0,3,6 set */
}

/*============================================================================
 * Extraction Tests
 *============================================================================*/

TEST(sbyte_to_u8) {
    /* Fully valid */
    Seraph_SemanticByte sb = seraph_sbyte_from_u8(0x42);
    ASSERT(seraph_sbyte_to_u8(sb) == 0x42);

    /* Partially valid - returns VOID */
    sb = seraph_sbyte_create(0x42, 0xF0);
    ASSERT(SERAPH_IS_VOID_U8(seraph_sbyte_to_u8(sb)));
}

TEST(sbyte_to_u8_default) {
    Seraph_SemanticByte sb = seraph_sbyte_create(0xA0, 0xF0);  /* Valid: A?, Invalid: ?0 */

    /* Fill invalid bits with 0x05 */
    uint8_t result = seraph_sbyte_to_u8_default(sb, 0x05);
    ASSERT(result == 0xA5);

    /* Fill with 0x0F */
    result = seraph_sbyte_to_u8_default(sb, 0x0F);
    ASSERT(result == 0xAF);
}

TEST(sbyte_get_bit) {
    Seraph_SemanticByte sb = seraph_sbyte_create(0b10101010, 0b11110000);

    /* Valid bits */
    ASSERT(seraph_sbyte_get_bit(sb, 4) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_sbyte_get_bit(sb, 5) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_sbyte_get_bit(sb, 6) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_sbyte_get_bit(sb, 7) == SERAPH_VBIT_TRUE);

    /* Invalid bits */
    ASSERT(seraph_sbyte_get_bit(sb, 0) == SERAPH_VBIT_VOID);
    ASSERT(seraph_sbyte_get_bit(sb, 3) == SERAPH_VBIT_VOID);

    /* Out of range */
    ASSERT(seraph_sbyte_get_bit(sb, 8) == SERAPH_VBIT_VOID);
}

TEST(sbyte_to_vbits) {
    Seraph_SemanticByte sb = seraph_sbyte_create(0b10100000, 0b11110000);
    Seraph_Vbit bits[8];
    seraph_sbyte_to_vbits(sb, bits);

    ASSERT(bits[0] == SERAPH_VBIT_VOID);
    ASSERT(bits[1] == SERAPH_VBIT_VOID);
    ASSERT(bits[2] == SERAPH_VBIT_VOID);
    ASSERT(bits[3] == SERAPH_VBIT_VOID);
    ASSERT(bits[4] == SERAPH_VBIT_FALSE);
    ASSERT(bits[5] == SERAPH_VBIT_TRUE);
    ASSERT(bits[6] == SERAPH_VBIT_FALSE);
    ASSERT(bits[7] == SERAPH_VBIT_TRUE);
}

/*============================================================================
 * Validity Tests
 *============================================================================*/

TEST(sbyte_validity_checks) {
    Seraph_SemanticByte full = seraph_sbyte_from_u8(0x42);
    Seraph_SemanticByte partial = seraph_sbyte_create(0x42, 0xF0);
    Seraph_SemanticByte empty = seraph_sbyte_void();

    ASSERT(seraph_sbyte_is_valid(full) == true);
    ASSERT(seraph_sbyte_is_valid(partial) == false);
    ASSERT(seraph_sbyte_is_valid(empty) == false);

    ASSERT(seraph_sbyte_is_void(full) == false);
    ASSERT(seraph_sbyte_is_void(partial) == false);
    ASSERT(seraph_sbyte_is_void(empty) == true);

    ASSERT(seraph_sbyte_has_void(full) == false);
    ASSERT(seraph_sbyte_has_void(partial) == true);
    ASSERT(seraph_sbyte_has_void(empty) == true);

    ASSERT(seraph_sbyte_valid_count(full) == 8);
    ASSERT(seraph_sbyte_valid_count(partial) == 4);
    ASSERT(seraph_sbyte_valid_count(empty) == 0);

    ASSERT(seraph_sbyte_void_count(full) == 0);
    ASSERT(seraph_sbyte_void_count(partial) == 4);
    ASSERT(seraph_sbyte_void_count(empty) == 8);
}

/*============================================================================
 * Bitwise Operation Tests
 *============================================================================*/

TEST(sbyte_not) {
    Seraph_SemanticByte sb = seraph_sbyte_create(0b10100000, 0b11110000);
    Seraph_SemanticByte result = seraph_sbyte_not(sb);

    ASSERT(result.mask == 0b11110000);  /* Mask unchanged */
    ASSERT(result.value == 0b01010000); /* Valid bits inverted */
}

TEST(sbyte_and) {
    Seraph_SemanticByte a = seraph_sbyte_create(0xFF, 0xF0);  /* Valid: high nibble */
    Seraph_SemanticByte b = seraph_sbyte_create(0x0F, 0x0F);  /* Valid: low nibble */

    Seraph_SemanticByte result = seraph_sbyte_and(a, b);
    ASSERT(result.mask == 0x00);  /* No overlap = no valid bits */

    /* Both fully valid */
    a = seraph_sbyte_from_u8(0xAA);
    b = seraph_sbyte_from_u8(0x55);
    result = seraph_sbyte_and(a, b);
    ASSERT(result.mask == 0xFF);
    ASSERT(result.value == 0x00);  /* 0xAA & 0x55 = 0x00 */
}

TEST(sbyte_or) {
    Seraph_SemanticByte a = seraph_sbyte_from_u8(0xAA);
    Seraph_SemanticByte b = seraph_sbyte_from_u8(0x55);
    Seraph_SemanticByte result = seraph_sbyte_or(a, b);
    ASSERT(result.mask == 0xFF);
    ASSERT(result.value == 0xFF);  /* 0xAA | 0x55 = 0xFF */
}

TEST(sbyte_xor) {
    Seraph_SemanticByte a = seraph_sbyte_from_u8(0xFF);
    Seraph_SemanticByte b = seraph_sbyte_from_u8(0xAA);
    Seraph_SemanticByte result = seraph_sbyte_xor(a, b);
    ASSERT(result.mask == 0xFF);
    ASSERT(result.value == 0x55);  /* 0xFF ^ 0xAA = 0x55 */
}

TEST(sbyte_and_optimistic) {
    /* If we have a valid 0 bit, AND result is known 0 regardless of other input */
    Seraph_SemanticByte a = seraph_sbyte_create(0x00, 0x0F);  /* Low nibble: 0000 */
    Seraph_SemanticByte b = seraph_sbyte_create(0x00, 0x00);  /* All VOID */

    Seraph_SemanticByte result = seraph_sbyte_and_optimistic(a, b);
    /* Low nibble should be valid 0 (we know 0 AND anything = 0) */
    ASSERT((result.mask & 0x0F) == 0x0F);
    ASSERT((result.value & 0x0F) == 0x00);
}

TEST(sbyte_or_optimistic) {
    /* If we have a valid 1 bit, OR result is known 1 regardless of other input */
    Seraph_SemanticByte a = seraph_sbyte_create(0xFF, 0x0F);  /* Low nibble: 1111 */
    Seraph_SemanticByte b = seraph_sbyte_create(0x00, 0x00);  /* All VOID */

    Seraph_SemanticByte result = seraph_sbyte_or_optimistic(a, b);
    /* Low nibble should be valid 1 (we know 1 OR anything = 1) */
    ASSERT((result.mask & 0x0F) == 0x0F);
    ASSERT((result.value & 0x0F) == 0x0F);
}

/*============================================================================
 * Merge Tests
 *============================================================================*/

TEST(sbyte_merge) {
    /* Complementary knowledge */
    Seraph_SemanticByte a = seraph_sbyte_create(0xA0, 0xF0);  /* Know: A? */
    Seraph_SemanticByte b = seraph_sbyte_create(0x05, 0x0F);  /* Know: ?5 */

    Seraph_SemanticByte result = seraph_sbyte_merge(a, b);
    ASSERT(result.mask == 0xFF);
    ASSERT(result.value == 0xA5);

    /* Conflict: both valid but disagree on some bits */
    a = seraph_sbyte_create(0xF0, 0xF0);  /* Know: F? = 1111 */
    b = seraph_sbyte_create(0xA0, 0xF0);  /* Know: A? = 1010 (disagrees on bits 4,6!) */

    result = seraph_sbyte_merge(a, b);
    /* Only conflicting bits become VOID: bits 4 and 6 conflict, bits 5 and 7 agree */
    /* 0xF0 ^ 0xA0 = 0x50 (bits that differ), conflict = 0xF0 & 0x50 = 0x50 */
    /* result_mask = 0xF0 & ~0x50 = 0xA0 (only agreeing bits remain valid) */
    ASSERT(result.mask == 0xA0);  /* Bits 5 and 7 still valid, bits 4 and 6 voided */
    ASSERT((result.value & result.mask) == 0xA0);  /* Value matches where valid */
}

TEST(sbyte_coalesce) {
    Seraph_SemanticByte a = seraph_sbyte_create(0xA0, 0xF0);  /* Know: A? */
    Seraph_SemanticByte b = seraph_sbyte_create(0xB5, 0xFF);  /* Know: B5 */

    Seraph_SemanticByte result = seraph_sbyte_coalesce(a, b);
    /* a's high nibble preferred, b fills the rest */
    ASSERT(result.mask == 0xFF);
    ASSERT(result.value == 0xA5);  /* A from a, 5 from b */
}

/*============================================================================
 * Masking Tests
 *============================================================================*/

TEST(sbyte_mask_operations) {
    Seraph_SemanticByte sb = seraph_sbyte_from_u8(0xFF);

    /* Mask out low nibble */
    Seraph_SemanticByte result = seraph_sbyte_mask_out(sb, 0x0F);
    ASSERT(result.mask == 0xF0);
    ASSERT(result.value == 0xF0);

    /* Keep only low nibble */
    result = seraph_sbyte_mask_keep(sb, 0x0F);
    ASSERT(result.mask == 0x0F);
    ASSERT(result.value == 0x0F);
}

TEST(sbyte_set_bit) {
    Seraph_SemanticByte sb = seraph_sbyte_from_u8(0x00);

    /* Set bit 3 to TRUE */
    sb = seraph_sbyte_set_bit(sb, 3, SERAPH_VBIT_TRUE);
    ASSERT(sb.value == 0x08);
    ASSERT(sb.mask == 0xFF);

    /* Set bit 0 to VOID */
    sb = seraph_sbyte_set_bit(sb, 0, SERAPH_VBIT_VOID);
    ASSERT(sb.mask == 0xFE);
    ASSERT((sb.value & 0x01) == 0x00);
}

/*============================================================================
 * Comparison Tests
 *============================================================================*/

TEST(sbyte_eq) {
    Seraph_SemanticByte a = seraph_sbyte_create(0xA5, 0xFF);
    Seraph_SemanticByte b = seraph_sbyte_create(0xA5, 0xFF);
    Seraph_SemanticByte c = seraph_sbyte_create(0xA6, 0xFF);
    Seraph_SemanticByte d = seraph_sbyte_create(0xA5, 0xF0);

    ASSERT(seraph_sbyte_eq(a, b) == true);
    ASSERT(seraph_sbyte_eq(a, c) == false);
    ASSERT(seraph_sbyte_eq(a, d) == false);  /* Different masks */
}

TEST(sbyte_eq_vbit) {
    Seraph_SemanticByte a = seraph_sbyte_from_u8(0xA5);
    Seraph_SemanticByte b = seraph_sbyte_from_u8(0xA5);
    Seraph_SemanticByte c = seraph_sbyte_from_u8(0xB5);
    Seraph_SemanticByte d = seraph_sbyte_create(0xA5, 0xF0);

    ASSERT(seraph_sbyte_eq_vbit(a, b) == SERAPH_VBIT_TRUE);
    ASSERT(seraph_sbyte_eq_vbit(a, c) == SERAPH_VBIT_FALSE);
    ASSERT(seraph_sbyte_eq_vbit(a, d) == SERAPH_VBIT_VOID);  /* Partial comparison */
}

/*============================================================================
 * Shift Tests
 *============================================================================*/

TEST(sbyte_shifts) {
    Seraph_SemanticByte sb = seraph_sbyte_from_u8(0x0F);

    /* Shift left */
    Seraph_SemanticByte result = seraph_sbyte_shl(sb, 4);
    ASSERT(result.mask == 0xF0);  /* Low bits become VOID */
    ASSERT(result.value == 0xF0);

    /* Shift right */
    sb = seraph_sbyte_from_u8(0xF0);
    result = seraph_sbyte_shr(sb, 4);
    ASSERT(result.mask == 0x0F);  /* High bits become VOID */
    ASSERT(result.value == 0x0F);

    /* Over-shift */
    result = seraph_sbyte_shl(sb, 8);
    ASSERT(seraph_sbyte_is_void(result));
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_semantic_byte_tests(void) {
    printf("\n=== MC3: Semantic Byte Tests ===\n\n");

    /* Creation */
    RUN_TEST(sbyte_from_u8);
    RUN_TEST(sbyte_create);
    RUN_TEST(sbyte_void);
    RUN_TEST(sbyte_from_vbits);

    /* Extraction */
    RUN_TEST(sbyte_to_u8);
    RUN_TEST(sbyte_to_u8_default);
    RUN_TEST(sbyte_get_bit);
    RUN_TEST(sbyte_to_vbits);

    /* Validity */
    RUN_TEST(sbyte_validity_checks);

    /* Bitwise */
    RUN_TEST(sbyte_not);
    RUN_TEST(sbyte_and);
    RUN_TEST(sbyte_or);
    RUN_TEST(sbyte_xor);
    RUN_TEST(sbyte_and_optimistic);
    RUN_TEST(sbyte_or_optimistic);

    /* Merge */
    RUN_TEST(sbyte_merge);
    RUN_TEST(sbyte_coalesce);

    /* Masking */
    RUN_TEST(sbyte_mask_operations);
    RUN_TEST(sbyte_set_bit);

    /* Comparison */
    RUN_TEST(sbyte_eq);
    RUN_TEST(sbyte_eq_vbit);

    /* Shifts */
    RUN_TEST(sbyte_shifts);

    printf("\nSemantic Byte Tests: %d/%d passed\n", tests_passed, tests_run);
}
