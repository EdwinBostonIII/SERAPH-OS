/**
 * @file test_seraphim_effects.c
 * @brief Test suite for Seraphim Effect System
 *
 * MC26: Seraphim Compiler - Effect System Tests
 *
 * Tests cover:
 * - Effect operations (union, intersection, subset)
 * - Effect context management
 * - Effect inference from operators
 * - Built-in effect mappings
 *
 * Total: 30 tests
 */

#include <stdio.h>
#include <string.h>
#include "seraph/seraphim/effects.h"

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
 * Effect Operations Tests
 *============================================================================*/

TEST(test_effect_none) {
    ASSERT_EQ(SERAPH_EFFECT_NONE, 0);
    ASSERT_TRUE(seraph_effect_is_pure(SERAPH_EFFECT_NONE));
    ASSERT_FALSE(seraph_effect_is_pure(SERAPH_EFFECT_VOID));
    return 1;
}

TEST(test_effect_single) {
    ASSERT_TRUE(seraph_effect_has(SERAPH_EFFECT_VOID, SERAPH_EFFECT_VOID));
    ASSERT_FALSE(seraph_effect_has(SERAPH_EFFECT_VOID, SERAPH_EFFECT_PERSIST));
    ASSERT_TRUE(seraph_effect_has(SERAPH_EFFECT_ALL, SERAPH_EFFECT_VOID));
    ASSERT_TRUE(seraph_effect_has(SERAPH_EFFECT_ALL, SERAPH_EFFECT_IO));
    return 1;
}

TEST(test_effect_union) {
    Seraph_Effect_Flags a = SERAPH_EFFECT_VOID;
    Seraph_Effect_Flags b = SERAPH_EFFECT_PERSIST;
    Seraph_Effect_Flags u = seraph_effect_union(a, b);

    ASSERT_TRUE(seraph_effect_has(u, SERAPH_EFFECT_VOID));
    ASSERT_TRUE(seraph_effect_has(u, SERAPH_EFFECT_PERSIST));
    ASSERT_FALSE(seraph_effect_has(u, SERAPH_EFFECT_NETWORK));

    /* Union with NONE */
    ASSERT_EQ(seraph_effect_union(a, SERAPH_EFFECT_NONE), a);
    ASSERT_EQ(seraph_effect_union(SERAPH_EFFECT_NONE, b), b);

    return 1;
}

TEST(test_effect_intersect) {
    Seraph_Effect_Flags a = SERAPH_EFFECT_VOID | SERAPH_EFFECT_PERSIST;
    Seraph_Effect_Flags b = SERAPH_EFFECT_VOID | SERAPH_EFFECT_NETWORK;
    Seraph_Effect_Flags i = seraph_effect_intersect(a, b);

    ASSERT_TRUE(seraph_effect_has(i, SERAPH_EFFECT_VOID));
    ASSERT_FALSE(seraph_effect_has(i, SERAPH_EFFECT_PERSIST));
    ASSERT_FALSE(seraph_effect_has(i, SERAPH_EFFECT_NETWORK));

    /* Intersect with NONE */
    ASSERT_EQ(seraph_effect_intersect(a, SERAPH_EFFECT_NONE), SERAPH_EFFECT_NONE);

    return 1;
}

TEST(test_effect_subset_true) {
    /* NONE is subset of everything */
    ASSERT_TRUE(seraph_effect_subset(SERAPH_EFFECT_NONE, SERAPH_EFFECT_VOID));
    ASSERT_TRUE(seraph_effect_subset(SERAPH_EFFECT_NONE, SERAPH_EFFECT_NONE));
    ASSERT_TRUE(seraph_effect_subset(SERAPH_EFFECT_NONE, SERAPH_EFFECT_ALL));

    /* Same is subset */
    ASSERT_TRUE(seraph_effect_subset(SERAPH_EFFECT_VOID, SERAPH_EFFECT_VOID));

    /* Single is subset of combined */
    Seraph_Effect_Flags combined = SERAPH_EFFECT_VOID | SERAPH_EFFECT_PERSIST;
    ASSERT_TRUE(seraph_effect_subset(SERAPH_EFFECT_VOID, combined));
    ASSERT_TRUE(seraph_effect_subset(SERAPH_EFFECT_PERSIST, combined));

    /* Anything is subset of ALL */
    ASSERT_TRUE(seraph_effect_subset(combined, SERAPH_EFFECT_ALL));

    return 1;
}

TEST(test_effect_subset_false) {
    /* VOID not subset of NONE (pure) */
    ASSERT_FALSE(seraph_effect_subset(SERAPH_EFFECT_VOID, SERAPH_EFFECT_NONE));

    /* PERSIST not subset of VOID only */
    ASSERT_FALSE(seraph_effect_subset(SERAPH_EFFECT_PERSIST, SERAPH_EFFECT_VOID));

    /* Combined not subset of single */
    Seraph_Effect_Flags combined = SERAPH_EFFECT_VOID | SERAPH_EFFECT_PERSIST;
    ASSERT_FALSE(seraph_effect_subset(combined, SERAPH_EFFECT_VOID));

    return 1;
}

TEST(test_effect_name) {
    ASSERT_EQ(strcmp(seraph_effect_name(SERAPH_EFFECT_NONE), "pure"), 0);
    ASSERT_EQ(strcmp(seraph_effect_name(SERAPH_EFFECT_VOID), "VOID"), 0);
    ASSERT_EQ(strcmp(seraph_effect_name(SERAPH_EFFECT_PERSIST), "PERSIST"), 0);
    ASSERT_EQ(strcmp(seraph_effect_name(SERAPH_EFFECT_NETWORK), "NETWORK"), 0);
    ASSERT_EQ(strcmp(seraph_effect_name(SERAPH_EFFECT_TIMER), "TIMER"), 0);
    ASSERT_EQ(strcmp(seraph_effect_name(SERAPH_EFFECT_IO), "IO"), 0);
    ASSERT_EQ(strcmp(seraph_effect_name(SERAPH_EFFECT_ALL), "ALL"), 0);
    return 1;
}

TEST(test_effect_print) {
    char buf[64];

    seraph_effect_print(SERAPH_EFFECT_NONE, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "[pure]"), 0);

    seraph_effect_print(SERAPH_EFFECT_ALL, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "[unsafe]"), 0);

    seraph_effect_print(SERAPH_EFFECT_VOID, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "effects(VOID)"), 0);

    Seraph_Effect_Flags combined = SERAPH_EFFECT_VOID | SERAPH_EFFECT_PERSIST;
    seraph_effect_print(combined, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "effects(VOID, PERSIST)"), 0);

    return 1;
}

/*============================================================================
 * Effect Context Tests
 *============================================================================*/

TEST(test_effect_context_init) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_vbit_is_true(seraph_arena_create(&arena, 4096, 0, 0)));

    Seraph_Effect_Context ctx;
    ASSERT_TRUE(seraph_vbit_is_true(
        seraph_effect_context_init(&ctx, &arena, NULL)));

    /* Global scope allows all effects by default */
    ASSERT_EQ(ctx.allowed, SERAPH_EFFECT_ALL);
    ASSERT_EQ(ctx.inferred, SERAPH_EFFECT_NONE);
    ASSERT_EQ(ctx.fn_depth, 0);
    ASSERT_EQ(ctx.error_count, 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_effect_enter_exit_fn) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_vbit_is_true(seraph_arena_create(&arena, 4096, 0, 0)));

    Seraph_Effect_Context ctx;
    seraph_effect_context_init(&ctx, &arena, NULL);

    /* Enter a pure function */
    seraph_effect_enter_fn(&ctx, SERAPH_EFFECT_NONE);
    ASSERT_EQ(ctx.fn_depth, 1);
    ASSERT_EQ(ctx.allowed, SERAPH_EFFECT_NONE);
    ASSERT_EQ(ctx.inferred, SERAPH_EFFECT_NONE);

    /* Exit - should succeed (no effects inferred) */
    ASSERT_TRUE(seraph_vbit_is_true(seraph_effect_exit_fn(&ctx)));
    ASSERT_EQ(ctx.fn_depth, 0);
    ASSERT_EQ(ctx.allowed, SERAPH_EFFECT_ALL);  /* Back to global */

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_effect_nested_fn) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_vbit_is_true(seraph_arena_create(&arena, 4096, 0, 0)));

    Seraph_Effect_Context ctx;
    seraph_effect_context_init(&ctx, &arena, NULL);

    /* Outer function allows VOID */
    seraph_effect_enter_fn(&ctx, SERAPH_EFFECT_VOID);
    ASSERT_EQ(ctx.fn_depth, 1);

    /* Inner pure function */
    seraph_effect_enter_fn(&ctx, SERAPH_EFFECT_NONE);
    ASSERT_EQ(ctx.fn_depth, 2);
    ASSERT_EQ(ctx.allowed, SERAPH_EFFECT_NONE);

    /* Exit inner */
    seraph_effect_exit_fn(&ctx);
    ASSERT_EQ(ctx.fn_depth, 1);
    ASSERT_EQ(ctx.allowed, SERAPH_EFFECT_VOID);

    /* Exit outer */
    seraph_effect_exit_fn(&ctx);
    ASSERT_EQ(ctx.fn_depth, 0);

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_effect_violation_tracking) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_vbit_is_true(seraph_arena_create(&arena, 4096, 0, 0)));

    Seraph_Effect_Context ctx;
    seraph_effect_context_init(&ctx, &arena, NULL);

    /* Enter a pure function */
    seraph_effect_enter_fn(&ctx, SERAPH_EFFECT_NONE);

    /* Try to add VOID effect - should fail check */
    ASSERT_FALSE(seraph_vbit_is_true(
        seraph_effect_check(&ctx, SERAPH_EFFECT_VOID)));

    /* Add effect anyway (simulating inference) */
    seraph_effect_add(&ctx, SERAPH_EFFECT_VOID);
    ASSERT_EQ(ctx.inferred, SERAPH_EFFECT_VOID);

    /* Exit should fail because inferred > allowed */
    ASSERT_FALSE(seraph_vbit_is_true(seraph_effect_exit_fn(&ctx)));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_effect_has_errors) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_vbit_is_true(seraph_arena_create(&arena, 4096, 0, 0)));

    Seraph_Effect_Context ctx;
    seraph_effect_context_init(&ctx, &arena, NULL);

    ASSERT_FALSE(seraph_effect_has_errors(&ctx));

    Seraph_Source_Loc loc = {0};
    seraph_effect_violation(&ctx, loc, SERAPH_EFFECT_VOID, SERAPH_EFFECT_NONE);

    ASSERT_TRUE(seraph_effect_has_errors(&ctx));
    ASSERT_EQ(ctx.error_count, 1);

    seraph_arena_destroy(&arena);
    return 1;
}

/*============================================================================
 * Operator Effects Tests
 *============================================================================*/

TEST(test_effect_for_operator_div) {
    Seraph_Effect_Flags effects = seraph_effect_for_operator(SERAPH_TOK_SLASH);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_VOID));
    return 1;
}

TEST(test_effect_for_operator_mod) {
    Seraph_Effect_Flags effects = seraph_effect_for_operator(SERAPH_TOK_PERCENT);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_VOID));
    return 1;
}

TEST(test_effect_for_operator_add) {
    Seraph_Effect_Flags effects = seraph_effect_for_operator(SERAPH_TOK_PLUS);
    ASSERT_EQ(effects, SERAPH_EFFECT_NONE);
    return 1;
}

TEST(test_effect_for_operator_index) {
    Seraph_Effect_Flags effects = seraph_effect_for_operator(SERAPH_TOK_LBRACKET);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_VOID));
    return 1;
}

TEST(test_effect_for_operator_void_prop) {
    Seraph_Effect_Flags effects = seraph_effect_for_operator(SERAPH_TOK_VOID_PROP);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_VOID));
    return 1;
}

TEST(test_effect_for_operator_void_assert) {
    Seraph_Effect_Flags effects = seraph_effect_for_operator(SERAPH_TOK_VOID_ASSERT);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_VOID));
    return 1;
}

/*============================================================================
 * Builtin Effects Tests
 *============================================================================*/

TEST(test_builtin_atlas_persist) {
    Seraph_Effect_Flags effects = seraph_effect_for_builtin("atlas_alloc", 11);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_PERSIST));
    return 1;
}

TEST(test_builtin_aether_network) {
    Seraph_Effect_Flags effects = seraph_effect_for_builtin("aether_read", 11);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_NETWORK));
    return 1;
}

TEST(test_builtin_timer) {
    Seraph_Effect_Flags effects = seraph_effect_for_builtin("timer_now", 9);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_TIMER));
    return 1;
}

TEST(test_builtin_io) {
    Seraph_Effect_Flags effects = seraph_effect_for_builtin("print", 5);
    ASSERT_TRUE(seraph_effect_has(effects, SERAPH_EFFECT_IO));
    return 1;
}

TEST(test_builtin_unknown_pure) {
    /* Unknown builtins are assumed pure */
    Seraph_Effect_Flags effects = seraph_effect_for_builtin("my_fn", 5);
    ASSERT_EQ(effects, SERAPH_EFFECT_NONE);
    return 1;
}

/*============================================================================
 * Effect Accumulation Tests
 *============================================================================*/

TEST(test_effect_accumulation) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_vbit_is_true(seraph_arena_create(&arena, 4096, 0, 0)));

    Seraph_Effect_Context ctx;
    seraph_effect_context_init(&ctx, &arena, NULL);

    /* Enter a function that allows VOID */
    seraph_effect_enter_fn(&ctx, SERAPH_EFFECT_VOID);

    /* Add VOID effect */
    seraph_effect_add(&ctx, SERAPH_EFFECT_VOID);
    ASSERT_EQ(ctx.inferred, SERAPH_EFFECT_VOID);

    /* Adding again should be idempotent */
    seraph_effect_add(&ctx, SERAPH_EFFECT_VOID);
    ASSERT_EQ(ctx.inferred, SERAPH_EFFECT_VOID);

    /* Should pass since VOID is allowed */
    ASSERT_TRUE(seraph_vbit_is_true(seraph_effect_exit_fn(&ctx)));

    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_effect_current_allowed) {
    Seraph_Arena arena;
    ASSERT_TRUE(seraph_vbit_is_true(seraph_arena_create(&arena, 4096, 0, 0)));

    Seraph_Effect_Context ctx;
    seraph_effect_context_init(&ctx, &arena, NULL);

    ASSERT_EQ(seraph_effect_allowed(&ctx), SERAPH_EFFECT_ALL);
    ASSERT_EQ(seraph_effect_current(&ctx), SERAPH_EFFECT_NONE);

    seraph_effect_enter_fn(&ctx, SERAPH_EFFECT_VOID);
    ASSERT_EQ(seraph_effect_allowed(&ctx), SERAPH_EFFECT_VOID);

    seraph_effect_add(&ctx, SERAPH_EFFECT_VOID);
    ASSERT_EQ(seraph_effect_current(&ctx), SERAPH_EFFECT_VOID);

    seraph_effect_exit_fn(&ctx);
    seraph_arena_destroy(&arena);
    return 1;
}

TEST(test_effect_null_context) {
    /* Test NULL safety */
    ASSERT_EQ(seraph_effect_allowed(NULL), SERAPH_EFFECT_ALL);
    ASSERT_EQ(seraph_effect_current(NULL), SERAPH_EFFECT_NONE);
    ASSERT_FALSE(seraph_effect_has_errors(NULL));
    return 1;
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_seraphim_effects_tests(void) {
    printf("\n=== MC26: Seraphim Effect System Tests ===\n");

    /* Effect Operations Tests */
    printf("\nEffect Operations:\n");
    RUN_TEST(test_effect_none);
    RUN_TEST(test_effect_single);
    RUN_TEST(test_effect_union);
    RUN_TEST(test_effect_intersect);
    RUN_TEST(test_effect_subset_true);
    RUN_TEST(test_effect_subset_false);
    RUN_TEST(test_effect_name);
    RUN_TEST(test_effect_print);

    /* Context Tests */
    printf("\nEffect Context:\n");
    RUN_TEST(test_effect_context_init);
    RUN_TEST(test_effect_enter_exit_fn);
    RUN_TEST(test_effect_nested_fn);
    RUN_TEST(test_effect_violation_tracking);
    RUN_TEST(test_effect_has_errors);

    /* Operator Effects */
    printf("\nOperator Effects:\n");
    RUN_TEST(test_effect_for_operator_div);
    RUN_TEST(test_effect_for_operator_mod);
    RUN_TEST(test_effect_for_operator_add);
    RUN_TEST(test_effect_for_operator_index);
    RUN_TEST(test_effect_for_operator_void_prop);
    RUN_TEST(test_effect_for_operator_void_assert);

    /* Builtin Effects */
    printf("\nBuiltin Effects:\n");
    RUN_TEST(test_builtin_atlas_persist);
    RUN_TEST(test_builtin_aether_network);
    RUN_TEST(test_builtin_timer);
    RUN_TEST(test_builtin_io);
    RUN_TEST(test_builtin_unknown_pure);

    /* Effect Accumulation */
    printf("\nEffect Accumulation:\n");
    RUN_TEST(test_effect_accumulation);
    RUN_TEST(test_effect_current_allowed);
    RUN_TEST(test_effect_null_context);

    printf("\nSeraphim Effects: %d/%d tests passed\n", tests_passed, tests_run);
}
