/**
 * @file test_arena.c
 * @brief Test suite for MC8: Spectral Arena
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seraph/arena.h"

static int tests_run = 0;
static int tests_passed = 0;

static int current_test_failed = 0;
#define TEST(name) __attribute__((unused)) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); fflush(stdout); \
    tests_run++; \
    current_test_failed = 0; \
    test_##name(); \
    if (!current_test_failed) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
    fflush(stdout); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_TRUE(x) ASSERT((x) == true)
#define ASSERT_FALSE(x) ASSERT((x) == false)

/*============================================================================
 * Test Structures
 *============================================================================*/

/* Simple 3D point for testing */
typedef struct {
    float x;
    float y;
    float z;
} Point3D;

/* More complex structure */
typedef struct {
    uint32_t id;
    float position[3];
    uint8_t flags;
    uint8_t padding[3];
} Entity;

/*============================================================================
 * Arena Basic Tests
 *============================================================================*/

TEST(arena_create) {
    Seraph_Arena arena;
    Seraph_Vbit result = seraph_arena_create(&arena, 4096, 0, SERAPH_ARENA_FLAG_NONE);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_TRUE(seraph_arena_is_valid(&arena));
    ASSERT_EQ(arena.capacity, 4096);
    ASSERT_EQ(arena.used, 0);
    ASSERT_EQ(arena.generation, 1);
    ASSERT_EQ(arena.alignment, SERAPH_ARENA_DEFAULT_ALIGNMENT);

    seraph_arena_destroy(&arena);
}

TEST(arena_create_null) {
    Seraph_Vbit result = seraph_arena_create(NULL, 4096, 0, 0);
    ASSERT_EQ(result, SERAPH_VBIT_VOID);
}

TEST(arena_create_zero_capacity) {
    Seraph_Arena arena;
    Seraph_Vbit result = seraph_arena_create(&arena, 0, 0, 0);
    ASSERT_EQ(result, SERAPH_VBIT_FALSE);
}

TEST(arena_create_custom_alignment) {
    Seraph_Arena arena;
    Seraph_Vbit result = seraph_arena_create(&arena, 4096, 128, 0);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_EQ(arena.alignment, 128);

    seraph_arena_destroy(&arena);
}

TEST(arena_destroy) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    seraph_arena_destroy(&arena);

    ASSERT_EQ(arena.memory, NULL);
    ASSERT_EQ(arena.capacity, 0);

    /* Double destroy should be safe */
    seraph_arena_destroy(&arena);
}

TEST(arena_reset) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    /* Allocate some memory */
    void* ptr1 = seraph_arena_alloc(&arena, 100, 0);
    void* ptr2 = seraph_arena_alloc(&arena, 200, 0);
    ASSERT_NE(ptr1, SERAPH_VOID_PTR);
    ASSERT_NE(ptr2, SERAPH_VOID_PTR);
    ASSERT(arena.used > 0);

    uint32_t old_gen = arena.generation;

    /* Reset arena */
    uint32_t new_gen = seraph_arena_reset(&arena);

    ASSERT_EQ(new_gen, old_gen + 1);
    ASSERT_EQ(arena.used, 0);
    ASSERT_EQ(arena.alloc_count, 0);

    /* Can allocate again */
    void* ptr3 = seraph_arena_alloc(&arena, 100, 0);
    ASSERT_NE(ptr3, SERAPH_VOID_PTR);

    seraph_arena_destroy(&arena);
}

/*============================================================================
 * Allocation Tests
 *============================================================================*/

TEST(arena_alloc_basic) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    void* ptr = seraph_arena_alloc(&arena, 64, 0);
    ASSERT_NE(ptr, SERAPH_VOID_PTR);
    ASSERT(seraph_is_aligned(ptr, SERAPH_ARENA_DEFAULT_ALIGNMENT));
    ASSERT_EQ(arena.alloc_count, 1);

    seraph_arena_destroy(&arena);
}

TEST(arena_alloc_multiple) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    void* ptr1 = seraph_arena_alloc(&arena, 64, 0);
    void* ptr2 = seraph_arena_alloc(&arena, 128, 0);
    void* ptr3 = seraph_arena_alloc(&arena, 256, 0);

    ASSERT_NE(ptr1, SERAPH_VOID_PTR);
    ASSERT_NE(ptr2, SERAPH_VOID_PTR);
    ASSERT_NE(ptr3, SERAPH_VOID_PTR);

    /* Verify they don't overlap */
    ASSERT((uintptr_t)ptr2 >= (uintptr_t)ptr1 + 64);
    ASSERT((uintptr_t)ptr3 >= (uintptr_t)ptr2 + 128);

    ASSERT_EQ(arena.alloc_count, 3);

    seraph_arena_destroy(&arena);
}

TEST(arena_alloc_aligned) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 16, 0);  /* 16-byte default alignment */

    /* Allocate with specific alignment */
    void* ptr1 = seraph_arena_alloc(&arena, 17, 64);  /* 64-byte aligned */
    ASSERT_NE(ptr1, SERAPH_VOID_PTR);
    ASSERT(seraph_is_aligned(ptr1, 64));

    void* ptr2 = seraph_arena_alloc(&arena, 33, 128);  /* 128-byte aligned */
    ASSERT_NE(ptr2, SERAPH_VOID_PTR);
    ASSERT(seraph_is_aligned(ptr2, 128));

    seraph_arena_destroy(&arena);
}

TEST(arena_alloc_until_full) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 1024, 64, 0);

    /* Fill the arena */
    int alloc_count = 0;
    while (seraph_arena_remaining(&arena) >= 64) {
        void* ptr = seraph_arena_alloc(&arena, 64, 0);
        if (ptr == SERAPH_VOID_PTR) break;
        alloc_count++;
    }

    ASSERT(alloc_count > 0);

    /* Next allocation should fail */
    void* fail = seraph_arena_alloc(&arena, 64, 0);
    ASSERT_EQ(fail, SERAPH_VOID_PTR);

    seraph_arena_destroy(&arena);
}

TEST(arena_alloc_zero_size) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    void* ptr = seraph_arena_alloc(&arena, 0, 0);
    ASSERT_EQ(ptr, SERAPH_VOID_PTR);

    seraph_arena_destroy(&arena);
}

TEST(arena_alloc_array) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    float* arr = (float*)seraph_arena_alloc_array(&arena, sizeof(float), 100, 0);
    ASSERT_NE(arr, SERAPH_VOID_PTR);

    /* Write and read back */
    for (int i = 0; i < 100; i++) {
        arr[i] = (float)i;
    }
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(arr[i], (float)i);
    }

    seraph_arena_destroy(&arena);
}

TEST(arena_calloc) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    uint8_t* ptr = (uint8_t*)seraph_arena_calloc(&arena, 256, 0);
    ASSERT_NE(ptr, SERAPH_VOID_PTR);

    /* Verify zero-initialized */
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(ptr[i], 0);
    }

    seraph_arena_destroy(&arena);
}

/*============================================================================
 * Capability Integration Tests
 *============================================================================*/

TEST(arena_get_capability) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    void* ptr = seraph_arena_alloc(&arena, 100, 0);
    ASSERT_NE(ptr, SERAPH_VOID_PTR);

    Seraph_Capability cap = seraph_arena_get_capability(&arena, ptr, 100, SERAPH_CAP_RW);
    ASSERT_FALSE(seraph_cap_is_void(cap));
    ASSERT_EQ(cap.base, ptr);
    ASSERT_EQ(cap.length, 100);
    ASSERT_EQ(cap.generation, arena.generation);

    seraph_arena_destroy(&arena);
}

TEST(arena_capability_invalid_after_reset) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    void* ptr = seraph_arena_alloc(&arena, 100, 0);
    Seraph_Capability cap = seraph_arena_get_capability(&arena, ptr, 100, SERAPH_CAP_RW);

    /* Capability should be valid */
    ASSERT_EQ(seraph_arena_check_capability(&arena, cap), SERAPH_VBIT_TRUE);

    /* Reset arena */
    seraph_arena_reset(&arena);

    /* Capability should now be invalid (generation mismatch) */
    ASSERT_EQ(seraph_arena_check_capability(&arena, cap), SERAPH_VBIT_FALSE);

    seraph_arena_destroy(&arena);
}

/*============================================================================
 * SoA Schema Tests
 *============================================================================*/

TEST(soa_schema_create) {
    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    Seraph_Vbit result = seraph_soa_schema_create(&schema, sizeof(Point3D),
                                                   _Alignof(Point3D), fields, 3);

    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_TRUE(seraph_soa_schema_is_valid(&schema));
    ASSERT_EQ(schema.field_count, 3);
    ASSERT_EQ(schema.struct_size, sizeof(Point3D));

    seraph_soa_schema_destroy(&schema);
}

/*============================================================================
 * SoA Array Tests
 *============================================================================*/

TEST(soa_array_create) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 65536, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Point3D), _Alignof(Point3D), fields, 3);

    Seraph_SoA_Array array;
    Seraph_Vbit result = seraph_soa_array_create(&array, &arena, &schema, 1000);

    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_TRUE(seraph_soa_array_is_valid(&array));
    ASSERT_EQ(array.capacity, 1000);
    ASSERT_EQ(array.count, 0);

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

TEST(soa_array_push_get) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 65536, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Point3D), _Alignof(Point3D), fields, 3);

    Seraph_SoA_Array array;
    seraph_soa_array_create(&array, &arena, &schema, 100);

    /* Push some elements */
    Point3D p1 = {1.0f, 2.0f, 3.0f};
    Point3D p2 = {4.0f, 5.0f, 6.0f};
    Point3D p3 = {7.0f, 8.0f, 9.0f};

    size_t idx1 = seraph_soa_array_push(&array, &p1);
    size_t idx2 = seraph_soa_array_push(&array, &p2);
    size_t idx3 = seraph_soa_array_push(&array, &p3);

    ASSERT_EQ(idx1, 0);
    ASSERT_EQ(idx2, 1);
    ASSERT_EQ(idx3, 2);
    ASSERT_EQ(seraph_soa_array_count(&array), 3);

    /* Get them back */
    Point3D out;

    seraph_soa_array_get(&array, 0, &out);
    ASSERT_EQ(out.x, 1.0f);
    ASSERT_EQ(out.y, 2.0f);
    ASSERT_EQ(out.z, 3.0f);

    seraph_soa_array_get(&array, 1, &out);
    ASSERT_EQ(out.x, 4.0f);
    ASSERT_EQ(out.y, 5.0f);
    ASSERT_EQ(out.z, 6.0f);

    seraph_soa_array_get(&array, 2, &out);
    ASSERT_EQ(out.x, 7.0f);
    ASSERT_EQ(out.y, 8.0f);
    ASSERT_EQ(out.z, 9.0f);

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

TEST(soa_array_set) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 65536, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Point3D), _Alignof(Point3D), fields, 3);

    Seraph_SoA_Array array;
    seraph_soa_array_create(&array, &arena, &schema, 100);

    /* Push initial element */
    Point3D p1 = {1.0f, 2.0f, 3.0f};
    seraph_soa_array_push(&array, &p1);

    /* Modify it */
    Point3D p2 = {10.0f, 20.0f, 30.0f};
    Seraph_Vbit result = seraph_soa_array_set(&array, 0, &p2);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);

    /* Verify change */
    Point3D out;
    seraph_soa_array_get(&array, 0, &out);
    ASSERT_EQ(out.x, 10.0f);
    ASSERT_EQ(out.y, 20.0f);
    ASSERT_EQ(out.z, 30.0f);

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

/*============================================================================
 * Prism Tests
 *============================================================================*/

TEST(prism_basic) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 65536, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Point3D), _Alignof(Point3D), fields, 3);

    Seraph_SoA_Array array;
    seraph_soa_array_create(&array, &arena, &schema, 100);

    /* Push elements */
    for (int i = 0; i < 10; i++) {
        Point3D p = {(float)i, (float)(i * 2), (float)(i * 3)};
        seraph_soa_array_push(&array, &p);
    }

    /* Get prism for x field */
    Seraph_Prism x_prism = seraph_soa_get_prism(&array, 0);
    ASSERT_TRUE(seraph_prism_is_valid(x_prism));
    ASSERT_EQ(x_prism.count, 10);
    ASSERT_EQ(x_prism.element_size, sizeof(float));

    /* Verify values through prism */
    for (size_t i = 0; i < 10; i++) {
        float* val = (float*)seraph_prism_get_ptr(x_prism, i);
        ASSERT_NE(val, SERAPH_VOID_PTR);
        ASSERT_EQ(*val, (float)i);
    }

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

TEST(prism_read_write_u32) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 65536, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Entity, id),
        {offsetof(Entity, position), sizeof(float[3]), _Alignof(float)},
        SERAPH_FIELD(Entity, flags)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Entity), _Alignof(Entity), fields, 3);

    Seraph_SoA_Array array;
    seraph_soa_array_create(&array, &arena, &schema, 100);

    /* Push elements */
    for (uint32_t i = 0; i < 10; i++) {
        Entity e = {.id = i * 100, .position = {0, 0, 0}, .flags = 0};
        seraph_soa_array_push(&array, &e);
    }

    /* Get prism for id field */
    Seraph_Prism id_prism = seraph_soa_get_prism(&array, 0);
    ASSERT_TRUE(seraph_prism_is_valid(id_prism));

    /* Read through prism */
    for (size_t i = 0; i < 10; i++) {
        uint32_t id = seraph_prism_read_u32(id_prism, i);
        ASSERT_EQ(id, (uint32_t)(i * 100));
    }

    /* Write through prism */
    for (size_t i = 0; i < 10; i++) {
        Seraph_Vbit result = seraph_prism_write_u32(id_prism, i, (uint32_t)(i * 1000));
        ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    }

    /* Verify writes */
    for (size_t i = 0; i < 10; i++) {
        uint32_t id = seraph_prism_read_u32(id_prism, i);
        ASSERT_EQ(id, (uint32_t)(i * 1000));
    }

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

TEST(prism_bounds_check) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 65536, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Point3D), _Alignof(Point3D), fields, 3);

    Seraph_SoA_Array array;
    seraph_soa_array_create(&array, &arena, &schema, 10);

    /* Push 5 elements */
    for (int i = 0; i < 5; i++) {
        Point3D p = {(float)i, (float)i, (float)i};
        seraph_soa_array_push(&array, &p);
    }

    Seraph_Prism prism = seraph_soa_get_prism(&array, 0);

    /* In bounds */
    void* ptr = seraph_prism_get_ptr(prism, 4);
    ASSERT_NE(ptr, SERAPH_VOID_PTR);

    /* Out of bounds */
    ptr = seraph_prism_get_ptr(prism, 5);
    ASSERT_EQ(ptr, SERAPH_VOID_PTR);

    ptr = seraph_prism_get_ptr(prism, 1000);
    ASSERT_EQ(ptr, SERAPH_VOID_PTR);

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

/*============================================================================
 * Integration Tests
 *============================================================================*/

TEST(soa_cache_locality) {
    /*
     * This test demonstrates the cache-friendliness of SoA layout.
     * We iterate over one field (x) for all elements - in SoA, these
     * are stored contiguously, leading to optimal cache utilization.
     */

    Seraph_Arena arena;
    seraph_arena_create(&arena, 1024 * 1024, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Point3D), _Alignof(Point3D), fields, 3);

    Seraph_SoA_Array array;
    seraph_soa_array_create(&array, &arena, &schema, 10000);

    /* Fill with data */
    for (int i = 0; i < 10000; i++) {
        Point3D p = {(float)i, (float)(i * 2), (float)(i * 3)};
        seraph_soa_array_push(&array, &p);
    }

    /* Get prism for x field */
    Seraph_Prism x_prism = seraph_soa_get_prism(&array, 0);

    /* Sum all x values (cache-friendly: contiguous memory access) */
    float sum = 0.0f;
    for (size_t i = 0; i < x_prism.count; i++) {
        float* ptr = (float*)seraph_prism_get_ptr(x_prism, i);
        sum += *ptr;
    }

    /*
     * Expected: sum of 0 + 1 + 2 + ... + 9999 = 49995000
     *
     * With single-precision floats, accumulating 10000 values causes
     * significant rounding error (~0.01%). This is expected behavior
     * for IEEE 754 floats - we're testing SoA correctness, not float precision.
     */
    float expected = (9999.0f * 10000.0f) / 2.0f;
    float tolerance = expected * 1e-3f;  /* 0.1% tolerance for accumulated float error */
    ASSERT(sum > expected - tolerance && sum < expected + tolerance);

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

TEST(arena_generation_tracking) {
    /*
     * Demonstrates generation-based temporal safety:
     * After arena reset, old SoA arrays become invalid.
     */

    Seraph_Arena arena;
    seraph_arena_create(&arena, 65536, 0, 0);

    Seraph_FieldDesc fields[] = {
        SERAPH_FIELD(Point3D, x),
        SERAPH_FIELD(Point3D, y),
        SERAPH_FIELD(Point3D, z)
    };

    Seraph_SoA_Schema schema;
    seraph_soa_schema_create(&schema, sizeof(Point3D), _Alignof(Point3D), fields, 3);

    Seraph_SoA_Array array;
    seraph_soa_array_create(&array, &arena, &schema, 100);

    Point3D p = {1.0f, 2.0f, 3.0f};
    size_t idx = seraph_soa_array_push(&array, &p);
    ASSERT_NE(idx, SERAPH_VOID_U64);

    /* Get prism - should be valid */
    Seraph_Prism prism = seraph_soa_get_prism(&array, 0);
    ASSERT_TRUE(seraph_prism_is_valid(prism));

    /* Reset arena */
    seraph_arena_reset(&arena);

    /* Old prism should now fail (generation mismatch detected during get) */
    Seraph_Prism new_prism = seraph_soa_get_prism(&array, 0);
    /* The prism we get will be VOID because array.generation != arena.generation */
    ASSERT_FALSE(seraph_prism_is_valid(new_prism));

    seraph_soa_schema_destroy(&schema);
    seraph_arena_destroy(&arena);
}

/*============================================================================
 * mmap/Atlas-Ready Tests
 *============================================================================*/

TEST(arena_mmap_anonymous) {
    /* Create arena with mmap flag (anonymous mapping) */
    Seraph_Arena arena;
    Seraph_Vbit result = seraph_arena_create(&arena, 4096, 0, SERAPH_ARENA_FLAG_MMAP);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_TRUE(seraph_arena_is_valid(&arena));
    ASSERT(arena.flags & SERAPH_ARENA_FLAG_MMAP);

    /* Basic allocation should work */
    void* ptr = seraph_arena_alloc(&arena, 100, 0);
    ASSERT_NE(ptr, SERAPH_VOID_PTR);

    /* Write and read back */
    memset(ptr, 0xAB, 100);
    ASSERT_EQ(((uint8_t*)ptr)[0], 0xAB);
    ASSERT_EQ(((uint8_t*)ptr)[99], 0xAB);

    seraph_arena_destroy(&arena);
}

#ifndef _WIN32
/* File-based tests only on POSIX for simplicity */
#include <unistd.h>

TEST(arena_persistent) {
    const char* test_file = "/tmp/seraph_arena_test.dat";

    /* Remove test file if exists */
    unlink(test_file);

    /* Create persistent arena */
    Seraph_Arena arena;
    Seraph_Vbit result = seraph_arena_create_persistent(&arena, test_file, 4096, 0, false);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_TRUE(seraph_arena_is_valid(&arena));
    ASSERT(arena.flags & SERAPH_ARENA_FLAG_MMAP);
    ASSERT(arena.flags & SERAPH_ARENA_FLAG_PERSISTENT);

    /* Allocate and write data */
    uint64_t* data = (uint64_t*)seraph_arena_alloc(&arena, sizeof(uint64_t) * 10, 0);
    ASSERT_NE(data, SERAPH_VOID_PTR);

    for (int i = 0; i < 10; i++) {
        data[i] = 0xDEADBEEF00000000ULL | i;
    }

    /* Sync to disk */
    result = seraph_arena_sync(&arena);
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);

    seraph_arena_destroy(&arena);

    /* Clean up */
    unlink(test_file);
}

TEST(arena_sync_non_persistent) {
    /* Create non-persistent arena */
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4096, 0, 0);

    /* Sync should return FALSE (not persistent) */
    Seraph_Vbit result = seraph_arena_sync(&arena);
    ASSERT_EQ(result, SERAPH_VBIT_FALSE);

    seraph_arena_destroy(&arena);
}
#endif /* !_WIN32 */

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_arena_tests(void) {
    printf("\n=== MC8: Spectral Arena Tests ===\n\n");

    printf("Arena Basic Tests:\n");
    RUN_TEST(arena_create);
    RUN_TEST(arena_create_null);
    RUN_TEST(arena_create_zero_capacity);
    RUN_TEST(arena_create_custom_alignment);
    RUN_TEST(arena_destroy);
    RUN_TEST(arena_reset);

    printf("\nAllocation Tests:\n");
    RUN_TEST(arena_alloc_basic);
    RUN_TEST(arena_alloc_multiple);
    RUN_TEST(arena_alloc_aligned);
    RUN_TEST(arena_alloc_until_full);
    RUN_TEST(arena_alloc_zero_size);
    RUN_TEST(arena_alloc_array);
    RUN_TEST(arena_calloc);

    printf("\nCapability Integration Tests:\n");
    RUN_TEST(arena_get_capability);
    RUN_TEST(arena_capability_invalid_after_reset);

    printf("\nSoA Schema Tests:\n");
    RUN_TEST(soa_schema_create);

    printf("\nSoA Array Tests:\n");
    RUN_TEST(soa_array_create);
    RUN_TEST(soa_array_push_get);
    RUN_TEST(soa_array_set);

    printf("\nPrism Tests:\n");
    RUN_TEST(prism_basic);
    RUN_TEST(prism_read_write_u32);
    RUN_TEST(prism_bounds_check);

    printf("\nIntegration Tests:\n");
    RUN_TEST(soa_cache_locality);
    RUN_TEST(arena_generation_tracking);

    printf("\nmmap/Atlas-Ready Tests:\n");
    RUN_TEST(arena_mmap_anonymous);
#ifndef _WIN32
    RUN_TEST(arena_persistent);
    RUN_TEST(arena_sync_non_persistent);
#else
    printf("  (Persistent arena tests skipped on Windows)\n");
#endif

    printf("\nArena Tests: %d/%d passed\n", tests_passed, tests_run);
}
