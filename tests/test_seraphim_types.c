/**
 * @file test_seraphim_types.c
 * @brief Test suite for Seraphim Type System
 *
 * MC26: Seraphim Compiler - Type System Tests
 *
 * Tests cover:
 * - Type construction (primitives, arrays, refs, voidable, functions)
 * - Type queries (is_integer, is_numeric, is_copy, size, align)
 * - Type comparison and subtyping
 * - Type unification for inference
 * - Symbol table and scope management
 * - Type printing
 *
 * Total: 42 tests
 */

#include <stdio.h>
#include <string.h>
#include "seraph/seraphim/types.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static int name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d): %s\n", __func__, __LINE__, #cond); \
        return 0; \
    } \
} while(0)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_TRUE(x) ASSERT((x) != 0)
#define ASSERT_FALSE(x) ASSERT((x) == 0)
#define ASSERT_NULL(x) ASSERT((x) == NULL)
#define ASSERT_NOT_NULL(x) ASSERT((x) != NULL)

#define RUN_TEST(name) do { \
    tests_run++; \
    if (name()) { \
        tests_passed++; \
        printf("  PASS: %s\n", #name); \
    } \
} while(0)

/*============================================================================
 * Construction Tests
 *============================================================================*/

TEST(test_type_prim_all_kinds) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    /* Test all primitive types */
    Seraph_Type* u8 = seraph_type_prim(&arena, SERAPH_TYPE_U8);
    ASSERT_NOT_NULL(u8);
    ASSERT_EQ(u8->kind, SERAPH_TYPE_U8);

    Seraph_Type* u16 = seraph_type_prim(&arena, SERAPH_TYPE_U16);
    ASSERT_EQ(u16->kind, SERAPH_TYPE_U16);

    Seraph_Type* u32 = seraph_type_prim(&arena, SERAPH_TYPE_U32);
    ASSERT_EQ(u32->kind, SERAPH_TYPE_U32);

    Seraph_Type* u64 = seraph_type_prim(&arena, SERAPH_TYPE_U64);
    ASSERT_EQ(u64->kind, SERAPH_TYPE_U64);

    Seraph_Type* i8 = seraph_type_prim(&arena, SERAPH_TYPE_I8);
    ASSERT_EQ(i8->kind, SERAPH_TYPE_I8);

    Seraph_Type* i16 = seraph_type_prim(&arena, SERAPH_TYPE_I16);
    ASSERT_EQ(i16->kind, SERAPH_TYPE_I16);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    ASSERT_EQ(i32->kind, SERAPH_TYPE_I32);

    Seraph_Type* i64 = seraph_type_prim(&arena, SERAPH_TYPE_I64);
    ASSERT_EQ(i64->kind, SERAPH_TYPE_I64);

    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);
    ASSERT_EQ(bool_t->kind, SERAPH_TYPE_BOOL);

    Seraph_Type* char_t = seraph_type_prim(&arena, SERAPH_TYPE_CHAR);
    ASSERT_EQ(char_t->kind, SERAPH_TYPE_CHAR);

    Seraph_Type* scalar = seraph_type_prim(&arena, SERAPH_TYPE_SCALAR);
    ASSERT_EQ(scalar->kind, SERAPH_TYPE_SCALAR);

    Seraph_Type* dual = seraph_type_prim(&arena, SERAPH_TYPE_DUAL);
    ASSERT_EQ(dual->kind, SERAPH_TYPE_DUAL);

    Seraph_Type* galactic = seraph_type_prim(&arena, SERAPH_TYPE_GALACTIC);
    ASSERT_EQ(galactic->kind, SERAPH_TYPE_GALACTIC);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_array_creation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* arr = seraph_type_array(&arena, i32, 10);

    ASSERT_NOT_NULL(arr);
    ASSERT_EQ(arr->kind, SERAPH_TYPE_ARRAY);
    ASSERT_EQ(arr->array.elem, i32);
    ASSERT_EQ(arr->array.size, 10);

    /* Nested array */
    Seraph_Type* arr2d = seraph_type_array(&arena, arr, 5);
    ASSERT_NOT_NULL(arr2d);
    ASSERT_EQ(arr2d->kind, SERAPH_TYPE_ARRAY);
    ASSERT_EQ(arr2d->array.elem, arr);
    ASSERT_EQ(arr2d->array.size, 5);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_slice_creation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* u8 = seraph_type_prim(&arena, SERAPH_TYPE_U8);
    Seraph_Type* slice = seraph_type_slice(&arena, u8);

    ASSERT_NOT_NULL(slice);
    ASSERT_EQ(slice->kind, SERAPH_TYPE_SLICE);
    ASSERT_EQ(slice->slice.elem, u8);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_tuple_creation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);
    Seraph_Type* elems[2] = { i32, bool_t };

    Seraph_Type* tuple = seraph_type_tuple(&arena, elems, 2);
    ASSERT_NOT_NULL(tuple);
    ASSERT_EQ(tuple->kind, SERAPH_TYPE_TUPLE);
    ASSERT_EQ(tuple->tuple.count, 2);
    ASSERT_EQ(tuple->tuple.elems[0], i32);
    ASSERT_EQ(tuple->tuple.elems[1], bool_t);

    /* Empty tuple */
    Seraph_Type* empty = seraph_type_tuple(&arena, NULL, 0);
    ASSERT_NOT_NULL(empty);
    ASSERT_EQ(empty->tuple.count, 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_ref_creation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i64 = seraph_type_prim(&arena, SERAPH_TYPE_I64);

    /* Immutable reference */
    Seraph_Type* ref = seraph_type_ref(&arena, i64, 0, SERAPH_SUBSTRATE_VOLATILE);
    ASSERT_NOT_NULL(ref);
    ASSERT_EQ(ref->kind, SERAPH_TYPE_REF);
    ASSERT_EQ(ref->ref.inner, i64);
    ASSERT_FALSE(ref->ref.is_mut);
    ASSERT_EQ(ref->ref.substrate, SERAPH_SUBSTRATE_VOLATILE);

    /* Mutable reference */
    Seraph_Type* mut_ref = seraph_type_ref(&arena, i64, 1, SERAPH_SUBSTRATE_VOLATILE);
    ASSERT_NOT_NULL(mut_ref);
    ASSERT_EQ(mut_ref->kind, SERAPH_TYPE_REF_MUT);
    ASSERT_TRUE(mut_ref->ref.is_mut);

    /* Atlas reference */
    Seraph_Type* atlas_ref = seraph_type_ref(&arena, i64, 0, SERAPH_SUBSTRATE_ATLAS);
    ASSERT_EQ(atlas_ref->ref.substrate, SERAPH_SUBSTRATE_ATLAS);

    /* Aether reference */
    Seraph_Type* aether_ref = seraph_type_ref(&arena, i64, 0, SERAPH_SUBSTRATE_AETHER);
    ASSERT_EQ(aether_ref->ref.substrate, SERAPH_SUBSTRATE_AETHER);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_voidable_creation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* voidable = seraph_type_voidable(&arena, i32);

    ASSERT_NOT_NULL(voidable);
    ASSERT_EQ(voidable->kind, SERAPH_TYPE_VOIDABLE);
    ASSERT_EQ(voidable->voidable.inner, i32);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_voidable_collapse) {
    /* ????T should collapse to ??T */
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* voidable1 = seraph_type_voidable(&arena, i32);
    Seraph_Type* voidable2 = seraph_type_voidable(&arena, voidable1);

    /* Should collapse: ????i32 = ??i32 */
    ASSERT_EQ(voidable2, voidable1);
    ASSERT_EQ(voidable2->voidable.inner, i32);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_fn_creation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);
    Seraph_Type* params[2] = { i32, bool_t };

    Seraph_Type* fn = seraph_type_fn(&arena, params, 2, i32, SERAPH_EFFECT_NONE);
    ASSERT_NOT_NULL(fn);
    ASSERT_EQ(fn->kind, SERAPH_TYPE_FN);
    ASSERT_EQ(fn->fn.param_count, 2);
    ASSERT_EQ(fn->fn.params[0], i32);
    ASSERT_EQ(fn->fn.params[1], bool_t);
    ASSERT_EQ(fn->fn.ret, i32);
    ASSERT_EQ(fn->fn.effects, SERAPH_EFFECT_NONE);

    /* Function with effects */
    Seraph_Type* fn2 = seraph_type_fn(&arena, params, 2, i32,
                                       SERAPH_EFFECT_VOID | SERAPH_EFFECT_IO);
    ASSERT_EQ(fn2->fn.effects, SERAPH_EFFECT_VOID | SERAPH_EFFECT_IO);

    /* Zero-param function */
    Seraph_Type* fn0 = seraph_type_fn(&arena, NULL, 0, bool_t, SERAPH_EFFECT_NONE);
    ASSERT_NOT_NULL(fn0);
    ASSERT_EQ(fn0->fn.param_count, 0);
    ASSERT_EQ(fn0->fn.ret, bool_t);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_var_creation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* tv1 = seraph_type_var(&ctx, "T", 1);
    ASSERT_NOT_NULL(tv1);
    ASSERT_EQ(tv1->kind, SERAPH_TYPE_TYPEVAR);
    ASSERT_EQ(tv1->typevar.id, 0);
    ASSERT_NULL(tv1->typevar.bound);

    Seraph_Type* tv2 = seraph_type_var(&ctx, "U", 1);
    ASSERT_EQ(tv2->typevar.id, 1);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_special_void_unit_never) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* void_t = seraph_type_void(&arena);
    ASSERT_NOT_NULL(void_t);
    ASSERT_EQ(void_t->kind, SERAPH_TYPE_VOID);
    ASSERT_TRUE(seraph_type_is_void(void_t));

    Seraph_Type* unit_t = seraph_type_unit(&arena);
    ASSERT_NOT_NULL(unit_t);
    ASSERT_EQ(unit_t->kind, SERAPH_TYPE_UNIT);

    Seraph_Type* never_t = seraph_type_never(&arena);
    ASSERT_NOT_NULL(never_t);
    ASSERT_EQ(never_t->kind, SERAPH_TYPE_NEVER);

    /* NULL is also VOID */
    ASSERT_TRUE(seraph_type_is_void(NULL));

    seraph_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Query Tests
 *============================================================================*/

TEST(test_type_is_integer) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    /* Integer types */
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_U8)));
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_U16)));
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_U32)));
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_U64)));
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_I8)));
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_I16)));
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_I32)));
    ASSERT_TRUE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_I64)));

    /* Non-integer types */
    ASSERT_FALSE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_BOOL)));
    ASSERT_FALSE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_CHAR)));
    ASSERT_FALSE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_SCALAR)));
    ASSERT_FALSE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_DUAL)));
    ASSERT_FALSE(seraph_type_is_integer(seraph_type_prim(&arena, SERAPH_TYPE_GALACTIC)));

    /* NULL */
    ASSERT_FALSE(seraph_type_is_integer(NULL));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_is_numeric) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    /* Integer types are numeric */
    ASSERT_TRUE(seraph_type_is_numeric(seraph_type_prim(&arena, SERAPH_TYPE_I64)));

    /* Numeric types */
    ASSERT_TRUE(seraph_type_is_numeric(seraph_type_prim(&arena, SERAPH_TYPE_SCALAR)));
    ASSERT_TRUE(seraph_type_is_numeric(seraph_type_prim(&arena, SERAPH_TYPE_DUAL)));
    ASSERT_TRUE(seraph_type_is_numeric(seraph_type_prim(&arena, SERAPH_TYPE_GALACTIC)));

    /* Non-numeric */
    ASSERT_FALSE(seraph_type_is_numeric(seraph_type_prim(&arena, SERAPH_TYPE_BOOL)));
    ASSERT_FALSE(seraph_type_is_numeric(seraph_type_prim(&arena, SERAPH_TYPE_CHAR)));
    ASSERT_FALSE(seraph_type_is_numeric(NULL));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_is_ref) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* ref = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_VOLATILE);
    Seraph_Type* mut_ref = seraph_type_ref(&arena, i32, 1, SERAPH_SUBSTRATE_VOLATILE);

    ASSERT_TRUE(seraph_type_is_ref(ref));
    ASSERT_TRUE(seraph_type_is_ref(mut_ref));
    ASSERT_FALSE(seraph_type_is_ref(i32));
    ASSERT_FALSE(seraph_type_is_ref(NULL));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_is_voidable) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* voidable = seraph_type_voidable(&arena, i32);

    ASSERT_TRUE(seraph_type_is_voidable(voidable));
    ASSERT_FALSE(seraph_type_is_voidable(i32));
    ASSERT_FALSE(seraph_type_is_voidable(NULL));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_is_copy_primitives) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    /* All primitives are Copy */
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_U8)));
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_I64)));
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_BOOL)));
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_CHAR)));
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_SCALAR)));
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_DUAL)));
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_GALACTIC)));
    ASSERT_TRUE(seraph_type_is_copy(seraph_type_prim(&arena, SERAPH_TYPE_UNIT)));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_is_copy_composite) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);

    /* Immutable ref is Copy */
    Seraph_Type* ref = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_VOLATILE);
    ASSERT_TRUE(seraph_type_is_copy(ref));

    /* Mutable ref is NOT Copy */
    Seraph_Type* mut_ref = seraph_type_ref(&arena, i32, 1, SERAPH_SUBSTRATE_VOLATILE);
    ASSERT_FALSE(seraph_type_is_copy(mut_ref));

    /* Array of Copy is Copy */
    Seraph_Type* arr = seraph_type_array(&arena, i32, 5);
    ASSERT_TRUE(seraph_type_is_copy(arr));

    /* VOID-able of Copy is Copy */
    Seraph_Type* voidable = seraph_type_voidable(&arena, i32);
    ASSERT_TRUE(seraph_type_is_copy(voidable));

    /* Tuple of Copy is Copy */
    Seraph_Type* elems[2] = { i32, i32 };
    Seraph_Type* tuple = seraph_type_tuple(&arena, elems, 2);
    ASSERT_TRUE(seraph_type_is_copy(tuple));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_size) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_U8)), 1);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_I8)), 1);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_BOOL)), 1);

    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_U16)), 2);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_I16)), 2);

    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_U32)), 4);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_I32)), 4);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_CHAR)), 4);

    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_U64)), 8);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_I64)), 8);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_SCALAR)), 8);

    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_DUAL)), 16);
    ASSERT_EQ(seraph_type_size(seraph_type_prim(&arena, SERAPH_TYPE_GALACTIC)), 32);

    /* Unit has zero size */
    ASSERT_EQ(seraph_type_size(seraph_type_unit(&arena)), 0);

    /* Array size */
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* arr = seraph_type_array(&arena, i32, 10);
    ASSERT_EQ(seraph_type_size(arr), 40);

    /* Reference size is pointer size */
    Seraph_Type* ref = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_VOLATILE);
    ASSERT_EQ(seraph_type_size(ref), sizeof(void*));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_align) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    ASSERT_EQ(seraph_type_align(seraph_type_prim(&arena, SERAPH_TYPE_U8)), 1);
    ASSERT_EQ(seraph_type_align(seraph_type_prim(&arena, SERAPH_TYPE_U16)), 2);
    ASSERT_EQ(seraph_type_align(seraph_type_prim(&arena, SERAPH_TYPE_U32)), 4);
    ASSERT_EQ(seraph_type_align(seraph_type_prim(&arena, SERAPH_TYPE_U64)), 8);
    ASSERT_EQ(seraph_type_align(seraph_type_prim(&arena, SERAPH_TYPE_DUAL)), 16);
    ASSERT_EQ(seraph_type_align(seraph_type_prim(&arena, SERAPH_TYPE_GALACTIC)), 16);

    /* Array alignment is element alignment */
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* arr = seraph_type_array(&arena, i32, 10);
    ASSERT_EQ(seraph_type_align(arr), 4);

    /* Reference alignment is pointer alignment */
    Seraph_Type* ref = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_VOLATILE);
    ASSERT_EQ(seraph_type_align(ref), sizeof(void*));

    seraph_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Comparison Tests
 *============================================================================*/

TEST(test_type_eq_primitives) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32_a = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* i32_b = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* u32 = seraph_type_prim(&arena, SERAPH_TYPE_U32);

    ASSERT_TRUE(seraph_type_eq(i32_a, i32_a));  /* Same object */
    ASSERT_TRUE(seraph_type_eq(i32_a, i32_b));  /* Same kind */
    ASSERT_FALSE(seraph_type_eq(i32_a, u32));   /* Different kind */
    ASSERT_FALSE(seraph_type_eq(i32_a, NULL));
    ASSERT_FALSE(seraph_type_eq(NULL, i32_a));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_eq_composite) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* u32 = seraph_type_prim(&arena, SERAPH_TYPE_U32);

    /* Arrays */
    Seraph_Type* arr1 = seraph_type_array(&arena, i32, 10);
    Seraph_Type* arr2 = seraph_type_array(&arena, i32, 10);
    Seraph_Type* arr3 = seraph_type_array(&arena, i32, 5);
    Seraph_Type* arr4 = seraph_type_array(&arena, u32, 10);

    ASSERT_TRUE(seraph_type_eq(arr1, arr2));
    ASSERT_FALSE(seraph_type_eq(arr1, arr3));  /* Different size */
    ASSERT_FALSE(seraph_type_eq(arr1, arr4));  /* Different elem */

    /* Slices */
    Seraph_Type* slice1 = seraph_type_slice(&arena, i32);
    Seraph_Type* slice2 = seraph_type_slice(&arena, i32);
    Seraph_Type* slice3 = seraph_type_slice(&arena, u32);

    ASSERT_TRUE(seraph_type_eq(slice1, slice2));
    ASSERT_FALSE(seraph_type_eq(slice1, slice3));

    /* References */
    Seraph_Type* ref1 = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_VOLATILE);
    Seraph_Type* ref2 = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_VOLATILE);
    Seraph_Type* ref3 = seraph_type_ref(&arena, i32, 1, SERAPH_SUBSTRATE_VOLATILE);  /* mut */
    Seraph_Type* ref4 = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_ATLAS);

    ASSERT_TRUE(seraph_type_eq(ref1, ref2));
    ASSERT_FALSE(seraph_type_eq(ref1, ref3));  /* Different mutability */
    ASSERT_FALSE(seraph_type_eq(ref1, ref4));  /* Different substrate */

    /* VOID-able */
    Seraph_Type* v1 = seraph_type_voidable(&arena, i32);
    Seraph_Type* v2 = seraph_type_voidable(&arena, i32);
    Seraph_Type* v3 = seraph_type_voidable(&arena, u32);

    ASSERT_TRUE(seraph_type_eq(v1, v2));
    ASSERT_FALSE(seraph_type_eq(v1, v3));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_eq_with_typevar) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* tv1 = seraph_type_var(&ctx, "T", 1);
    Seraph_Type* tv2 = seraph_type_var(&ctx, "U", 1);
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);

    /* Unbound typevars compare by ID */
    ASSERT_TRUE(seraph_type_eq(tv1, tv1));
    ASSERT_FALSE(seraph_type_eq(tv1, tv2));

    /* Bound typevar compares to bound type */
    ((Seraph_Type*)tv1)->typevar.bound = i32;
    ASSERT_TRUE(seraph_type_eq(tv1, i32));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_subtype_basic) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* u32 = seraph_type_prim(&arena, SERAPH_TYPE_U32);

    /* Same type is subtype of itself */
    ASSERT_TRUE(seraph_type_subtype(i32, i32));

    /* Different types are not subtypes */
    ASSERT_FALSE(seraph_type_subtype(i32, u32));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_subtype_voidable) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* voidable_i32 = seraph_type_voidable(&arena, i32);

    /* T <: ??T */
    ASSERT_TRUE(seraph_type_subtype(i32, voidable_i32));

    /* ??T <: ??T */
    ASSERT_TRUE(seraph_type_subtype(voidable_i32, voidable_i32));

    /* ??T is NOT subtype of T */
    ASSERT_FALSE(seraph_type_subtype(voidable_i32, i32));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_subtype_never) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type* never = seraph_type_never(&arena);
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);

    /* Never is subtype of everything */
    ASSERT_TRUE(seraph_type_subtype(never, i32));
    ASSERT_TRUE(seraph_type_subtype(never, bool_t));
    ASSERT_TRUE(seraph_type_subtype(never, never));

    seraph_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Unification Tests
 *============================================================================*/

TEST(test_type_unify_same) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* unified = seraph_type_unify(&ctx, i32, i32);

    ASSERT_EQ(unified, i32);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_unify_typevar) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* tv = seraph_type_var(&ctx, "T", 1);
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);

    /* Unifying typevar with concrete type binds it */
    Seraph_Type* unified = seraph_type_unify(&ctx, tv, i32);
    ASSERT_EQ(unified, i32);
    ASSERT_EQ(tv->typevar.bound, i32);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_unify_array) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* arr1 = seraph_type_array(&arena, i32, 10);
    Seraph_Type* arr2 = seraph_type_array(&arena, i32, 10);

    Seraph_Type* unified = seraph_type_unify(&ctx, arr1, arr2);
    ASSERT_NOT_NULL(unified);
    ASSERT_EQ(unified->kind, SERAPH_TYPE_ARRAY);
    ASSERT_EQ(unified->array.size, 10);

    /* Different sizes don't unify */
    Seraph_Type* arr3 = seraph_type_array(&arena, i32, 5);
    Seraph_Type* fail = seraph_type_unify(&ctx, arr1, arr3);
    ASSERT_TRUE(seraph_type_is_void(fail));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_unify_voidable) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* v1 = seraph_type_voidable(&arena, i32);
    Seraph_Type* v2 = seraph_type_voidable(&arena, i32);

    Seraph_Type* unified = seraph_type_unify(&ctx, v1, v2);
    ASSERT_NOT_NULL(unified);
    ASSERT_EQ(unified->kind, SERAPH_TYPE_VOIDABLE);
    ASSERT_TRUE(seraph_type_eq(unified->voidable.inner, i32));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_unify_fn) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);
    Seraph_Type* params[1] = { i32 };

    Seraph_Type* fn1 = seraph_type_fn(&arena, params, 1, bool_t, SERAPH_EFFECT_NONE);
    Seraph_Type* fn2 = seraph_type_fn(&arena, params, 1, bool_t, SERAPH_EFFECT_VOID);

    /* Functions with same signature but different effects - effects are combined */
    Seraph_Type* unified = seraph_type_unify(&ctx, fn1, fn2);
    ASSERT_NOT_NULL(unified);
    ASSERT_EQ(unified->kind, SERAPH_TYPE_FN);
    ASSERT_EQ(unified->fn.effects, SERAPH_EFFECT_VOID);  /* Union of effects */

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_unify_mismatch) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);

    Seraph_Type* result = seraph_type_unify(&ctx, i32, bool_t);
    ASSERT_TRUE(seraph_type_is_void(result));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_join_equal) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* joined = seraph_type_join(&ctx, i32, i32);

    ASSERT_EQ(joined, i32);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_join_voidable) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* voidable = seraph_type_voidable(&arena, i32);

    /* Join of T and ??T is ??T */
    Seraph_Type* joined = seraph_type_join(&ctx, i32, voidable);
    ASSERT_NOT_NULL(joined);
    ASSERT_EQ(joined->kind, SERAPH_TYPE_VOIDABLE);

    seraph_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Symbol Table Tests
 *============================================================================*/

TEST(test_context_init) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    ASSERT_NOT_NULL(ctx.scope);
    ASSERT_EQ(ctx.scope, ctx.global);
    ASSERT_EQ(ctx.error_count, 0);
    ASSERT_EQ(ctx.next_typevar_id, 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_scope_push_pop) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Scope* global = ctx.scope;

    /* Push new scope */
    seraph_type_push_scope(&ctx);
    ASSERT_NE(ctx.scope, global);
    ASSERT_EQ(ctx.scope->parent, global);

    /* Push another */
    Seraph_Scope* inner = ctx.scope;
    seraph_type_push_scope(&ctx);
    ASSERT_EQ(ctx.scope->parent, inner);

    /* Pop back */
    seraph_type_pop_scope(&ctx);
    ASSERT_EQ(ctx.scope, inner);

    seraph_type_pop_scope(&ctx);
    ASSERT_EQ(ctx.scope, global);

    /* Can't pop global */
    seraph_type_pop_scope(&ctx);
    ASSERT_EQ(ctx.scope, global);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_symbol_define_lookup) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);

    /* Define a symbol */
    ASSERT_TRUE(seraph_type_define(&ctx, "x", 1, i32, NULL, 0) == SERAPH_VBIT_TRUE);

    /* Look it up */
    Seraph_Symbol* sym = seraph_type_lookup(&ctx, "x", 1);
    ASSERT_NOT_NULL(sym);
    ASSERT_EQ(sym->type, i32);
    ASSERT_FALSE(sym->is_mut);

    /* Define mutable symbol */
    ASSERT_TRUE(seraph_type_define(&ctx, "y", 1, i32, NULL, 1) == SERAPH_VBIT_TRUE);
    sym = seraph_type_lookup(&ctx, "y", 1);
    ASSERT_NOT_NULL(sym);
    ASSERT_TRUE(sym->is_mut);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_symbol_shadowing) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);

    /* Define x in global scope */
    seraph_type_define(&ctx, "x", 1, i32, NULL, 0);

    /* Push new scope and shadow */
    seraph_type_push_scope(&ctx);
    seraph_type_define(&ctx, "x", 1, bool_t, NULL, 0);

    /* Inner scope sees bool */
    Seraph_Symbol* sym = seraph_type_lookup(&ctx, "x", 1);
    ASSERT_EQ(sym->type, bool_t);

    /* Pop and see i32 again */
    seraph_type_pop_scope(&ctx);
    sym = seraph_type_lookup(&ctx, "x", 1);
    ASSERT_EQ(sym->type, i32);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_symbol_not_found) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    Seraph_Type_Context ctx;
    ASSERT_TRUE(seraph_type_context_init(&ctx, &arena) == SERAPH_VBIT_TRUE);

    Seraph_Symbol* sym = seraph_type_lookup(&ctx, "undefined", 9);
    ASSERT_NULL(sym);

    seraph_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Type Printing Tests
 *============================================================================*/

TEST(test_type_print_primitives) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    char buf[64];

    seraph_type_print(seraph_type_prim(&arena, SERAPH_TYPE_I32), buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "i32"), 0);

    seraph_type_print(seraph_type_prim(&arena, SERAPH_TYPE_U64), buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "u64"), 0);

    seraph_type_print(seraph_type_prim(&arena, SERAPH_TYPE_BOOL), buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "bool"), 0);

    seraph_type_print(seraph_type_prim(&arena, SERAPH_TYPE_GALACTIC), buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "galactic"), 0);

    seraph_type_print(seraph_type_unit(&arena), buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "()"), 0);

    seraph_type_print(seraph_type_never(&arena), buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "!"), 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_print_composite) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    char buf[64];
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);

    /* Array */
    Seraph_Type* arr = seraph_type_array(&arena, i32, 10);
    seraph_type_print(arr, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "[i32; 10]"), 0);

    /* Slice */
    Seraph_Type* slice = seraph_type_slice(&arena, i32);
    seraph_type_print(slice, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "[i32]"), 0);

    /* VOID-able */
    Seraph_Type* voidable = seraph_type_voidable(&arena, i32);
    seraph_type_print(voidable, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "??i32"), 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_print_ref) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    char buf[64];
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);

    /* Immutable ref */
    Seraph_Type* ref = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_VOLATILE);
    seraph_type_print(ref, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "&i32"), 0);

    /* Mutable ref */
    Seraph_Type* mut_ref = seraph_type_ref(&arena, i32, 1, SERAPH_SUBSTRATE_VOLATILE);
    seraph_type_print(mut_ref, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "&mut i32"), 0);

    /* Atlas ref */
    Seraph_Type* atlas_ref = seraph_type_ref(&arena, i32, 0, SERAPH_SUBSTRATE_ATLAS);
    seraph_type_print(atlas_ref, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "&atlas i32"), 0);

    /* Aether mut ref */
    Seraph_Type* aether_ref = seraph_type_ref(&arena, i32, 1, SERAPH_SUBSTRATE_AETHER);
    seraph_type_print(aether_ref, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "&mut aether i32"), 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_print_fn) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_arena_create(&arena, 4096, 0, 0) == SERAPH_VBIT_TRUE);

    char buf[128];
    Seraph_Type* i32 = seraph_type_prim(&arena, SERAPH_TYPE_I32);
    Seraph_Type* bool_t = seraph_type_prim(&arena, SERAPH_TYPE_BOOL);

    /* fn(i32, bool) -> i32 */
    Seraph_Type* params[2] = { i32, bool_t };
    Seraph_Type* fn = seraph_type_fn(&arena, params, 2, i32, SERAPH_EFFECT_NONE);
    seraph_type_print(fn, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "fn(i32, bool) -> i32"), 0);

    /* fn() -> bool */
    Seraph_Type* fn0 = seraph_type_fn(&arena, NULL, 0, bool_t, SERAPH_EFFECT_NONE);
    seraph_type_print(fn0, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "fn() -> bool"), 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_type_kind_name) {
    ASSERT_EQ(strcmp(seraph_type_kind_name(SERAPH_TYPE_VOID), "VOID"), 0);
    ASSERT_EQ(strcmp(seraph_type_kind_name(SERAPH_TYPE_I32), "i32"), 0);
    ASSERT_EQ(strcmp(seraph_type_kind_name(SERAPH_TYPE_BOOL), "bool"), 0);
    ASSERT_EQ(strcmp(seraph_type_kind_name(SERAPH_TYPE_GALACTIC), "galactic"), 0);
    ASSERT_EQ(strcmp(seraph_type_kind_name(SERAPH_TYPE_FN), "fn"), 0);
    ASSERT_EQ(strcmp(seraph_type_kind_name(SERAPH_TYPE_NEVER), "!"), 0);
    return 1;
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_seraphim_types_tests(void) {
    printf("\n=== MC26: Seraphim Type System Tests ===\n");

    /* Construction Tests */
    printf("\nConstruction Tests:\n");
    RUN_TEST(test_type_prim_all_kinds);
    RUN_TEST(test_type_array_creation);
    RUN_TEST(test_type_slice_creation);
    RUN_TEST(test_type_tuple_creation);
    RUN_TEST(test_type_ref_creation);
    RUN_TEST(test_type_voidable_creation);
    RUN_TEST(test_type_voidable_collapse);
    RUN_TEST(test_type_fn_creation);
    RUN_TEST(test_type_var_creation);
    RUN_TEST(test_type_special_void_unit_never);

    /* Query Tests */
    printf("\nQuery Tests:\n");
    RUN_TEST(test_type_is_integer);
    RUN_TEST(test_type_is_numeric);
    RUN_TEST(test_type_is_ref);
    RUN_TEST(test_type_is_voidable);
    RUN_TEST(test_type_is_copy_primitives);
    RUN_TEST(test_type_is_copy_composite);
    RUN_TEST(test_type_size);
    RUN_TEST(test_type_align);

    /* Comparison Tests */
    printf("\nComparison Tests:\n");
    RUN_TEST(test_type_eq_primitives);
    RUN_TEST(test_type_eq_composite);
    RUN_TEST(test_type_eq_with_typevar);
    RUN_TEST(test_type_subtype_basic);
    RUN_TEST(test_type_subtype_voidable);
    RUN_TEST(test_type_subtype_never);

    /* Unification Tests */
    printf("\nUnification Tests:\n");
    RUN_TEST(test_type_unify_same);
    RUN_TEST(test_type_unify_typevar);
    RUN_TEST(test_type_unify_array);
    RUN_TEST(test_type_unify_voidable);
    RUN_TEST(test_type_unify_fn);
    RUN_TEST(test_type_unify_mismatch);
    RUN_TEST(test_type_join_equal);
    RUN_TEST(test_type_join_voidable);

    /* Symbol Table Tests */
    printf("\nSymbol Table Tests:\n");
    RUN_TEST(test_context_init);
    RUN_TEST(test_scope_push_pop);
    RUN_TEST(test_symbol_define_lookup);
    RUN_TEST(test_symbol_shadowing);
    RUN_TEST(test_symbol_not_found);

    /* Type Printing Tests */
    printf("\nType Printing Tests:\n");
    RUN_TEST(test_type_print_primitives);
    RUN_TEST(test_type_print_composite);
    RUN_TEST(test_type_print_ref);
    RUN_TEST(test_type_print_fn);
    RUN_TEST(test_type_kind_name);

    printf("\nSeraphim Types: %d/%d tests passed\n", tests_passed, tests_run);
}
