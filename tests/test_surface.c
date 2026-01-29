/**
 * @file test_surface.c
 * @brief Test suite for MC11: The Surface
 *
 * Tests the physics-based UI compositor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "seraph/surface.h"

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;
static int current_test_failed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n  Assertion failed: %s\n  Line %d\n", #cond, __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED\n  Expected %lld == %lld\n  Line %d\n", \
               (long long)(a), (long long)(b), __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabs((double)(a) - (double)(b)) > (eps)) { \
        printf("FAILED\n  Expected %f ~= %f (eps=%f)\n  Line %d\n", \
               (double)(a), (double)(b), (double)(eps), __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_VBIT_TRUE(v) do { \
    if (!seraph_vbit_is_true(v)) { \
        printf("FAILED\n  Expected VBIT_TRUE\n  Line %d\n", __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_VBIT_FALSE(v) do { \
    if (!seraph_vbit_is_false(v)) { \
        printf("FAILED\n  Expected VBIT_FALSE\n  Line %d\n", __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_VBIT_VOID(v) do { \
    if (!seraph_vbit_is_void(v)) { \
        printf("FAILED\n  Expected VBIT_VOID\n  Line %d\n", __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    tests_run++; \
    current_test_failed = 0; \
    test_##name(); \
    if (!current_test_failed) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
    fflush(stdout); \
} while(0)

/*============================================================================
 * Theme Tests
 *============================================================================*/

void test_theme_colors(void) {
    /* Verify theme colors are defined correctly */
    Seraph_Color bg = SERAPH_THEME_BACKGROUND;
    ASSERT_EQ(bg.r, 0x0D);
    ASSERT_EQ(bg.g, 0x0E);
    ASSERT_EQ(bg.b, 0x14);
    ASSERT_EQ(bg.a, 255);

    Seraph_Color orb = SERAPH_THEME_ORB_BASE;
    ASSERT_EQ(orb.r, 0x6B);
    ASSERT_EQ(orb.g, 0x7B);
    ASSERT_EQ(orb.b, 0x8E);
}

void test_color_to_u32(void) {
    Seraph_Color c = SERAPH_RGB(0xAB, 0xCD, 0xEF);
    uint32_t u = seraph_color_to_u32(c);
    ASSERT_EQ(u, 0xABCDEFFF);
}

void test_color_lerp(void) {
    Seraph_Color a = SERAPH_RGB(0, 0, 0);
    Seraph_Color b = SERAPH_RGB(100, 100, 100);

    Seraph_Color mid = seraph_color_lerp(a, b, 0.5f);
    ASSERT_EQ(mid.r, 50);
    ASSERT_EQ(mid.g, 50);
    ASSERT_EQ(mid.b, 50);

    Seraph_Color full = seraph_color_lerp(a, b, 1.0f);
    ASSERT_EQ(full.r, 100);
}

/*============================================================================
 * Configuration Tests
 *============================================================================*/

void test_default_config(void) {
    Seraph_Surface_Config config = SERAPH_SURFACE_CONFIG_DEFAULT;
    ASSERT(config.instant_mode == false);
    ASSERT(config.physics_enabled == true);
    ASSERT_NEAR(config.magnetism_strength, 1.0f, 0.001f);
    ASSERT_NEAR(config.swell_factor, 5.0f, 0.001f);
}

/*============================================================================
 * Surface Initialization Tests
 *============================================================================*/

void test_surface_init(void) {
    Seraph_Surface surface;
    Seraph_Vbit result = seraph_surface_init(&surface, 800, 600);
    ASSERT_VBIT_TRUE(result);
    ASSERT(surface.initialized);
    ASSERT_EQ(surface.width, 800);
    ASSERT_EQ(surface.height, 600);
    ASSERT_EQ(surface.orb_count, 0);
    ASSERT_EQ(surface.expanded_orb_index, -1);
    seraph_surface_destroy(&surface);
}

void test_surface_init_null(void) {
    Seraph_Vbit result = seraph_surface_init(NULL, 800, 600);
    ASSERT_VBIT_VOID(result);
}

void test_surface_init_with_config(void) {
    Seraph_Surface surface;
    Seraph_Surface_Config config = SERAPH_SURFACE_CONFIG_DEFAULT;
    config.physics_enabled = false;
    config.magnetism_strength = 2.5f;

    Seraph_Vbit result = seraph_surface_init_with_config(&surface, 1920, 1080, config);
    ASSERT_VBIT_TRUE(result);
    ASSERT(surface.config.physics_enabled == false);
    ASSERT_NEAR(surface.config.magnetism_strength, 2.5f, 0.001f);
    seraph_surface_destroy(&surface);
}

void test_surface_destroy(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);
    ASSERT(surface.initialized);
    seraph_surface_destroy(&surface);
    ASSERT(!surface.initialized);
}

void test_surface_is_valid(void) {
    Seraph_Surface surface;
    memset(&surface, 0, sizeof(surface));  /* Clear to known state */
    ASSERT(!seraph_surface_is_valid(&surface));  /* Uninitialized (initialized=false) */

    seraph_surface_init(&surface, 800, 600);
    ASSERT(seraph_surface_is_valid(&surface));

    seraph_surface_destroy(&surface);
    ASSERT(!seraph_surface_is_valid(&surface));
}

/*============================================================================
 * Locus Tests
 *============================================================================*/

void test_locus_initialization(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    /* Locus should be at center (uses separate X/Y Galactics for 2D physics) */
    float locus_x = (float)seraph_q128_to_double(surface.locus.position_x.primal);
    float locus_y = (float)seraph_q128_to_double(surface.locus.position_y.primal);

    ASSERT_NEAR(locus_x, 400.0f, 1.0f);
    ASSERT_NEAR(locus_y, 300.0f, 1.0f);
    ASSERT_VBIT_TRUE(surface.locus.active);

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Orb Creation Tests
 *============================================================================*/

void test_orb_create(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    /* Create a fake capability */
    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t orb_idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);
    ASSERT(orb_idx >= 0);
    ASSERT_EQ(surface.orb_count, 1);

    Seraph_Orb* orb = seraph_surface_get_orb(&surface, orb_idx);
    ASSERT(orb != NULL);
    ASSERT(orb->state == SERAPH_ORB_IDLE);
    ASSERT_VBIT_TRUE(orb->visible);

    seraph_surface_destroy(&surface);
}

void test_orb_create_multiple(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data1[32], data2[32], data3[32];
    Seraph_Capability cap1 = seraph_cap_create(data1, sizeof(data1), 1, SERAPH_CAP_RW);
    Seraph_Capability cap2 = seraph_cap_create(data2, sizeof(data2), 1, SERAPH_CAP_RW);
    Seraph_Capability cap3 = seraph_cap_create(data3, sizeof(data3), 1, SERAPH_CAP_RW);

    int32_t idx1 = seraph_surface_create_orb(&surface, cap1, 100.0f, 0.0f);
    int32_t idx2 = seraph_surface_create_orb(&surface, cap2, 100.0f, 2.094f);  /* 120 degrees */
    int32_t idx3 = seraph_surface_create_orb(&surface, cap3, 100.0f, 4.189f);  /* 240 degrees */

    ASSERT(idx1 >= 0);
    ASSERT(idx2 >= 0);
    ASSERT(idx3 >= 0);
    ASSERT(idx1 != idx2);
    ASSERT(idx2 != idx3);
    ASSERT_EQ(surface.orb_count, 3);

    seraph_surface_destroy(&surface);
}

void test_orb_unique_ids(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data1[32], data2[32];
    Seraph_Capability cap1 = seraph_cap_create(data1, sizeof(data1), 1, SERAPH_CAP_RW);
    Seraph_Capability cap2 = seraph_cap_create(data2, sizeof(data2), 1, SERAPH_CAP_RW);

    int32_t idx1 = seraph_surface_create_orb(&surface, cap1, 100.0f, 0.0f);
    int32_t idx2 = seraph_surface_create_orb(&surface, cap2, 100.0f, 1.0f);

    Seraph_Orb* orb1 = seraph_surface_get_orb(&surface, idx1);
    Seraph_Orb* orb2 = seraph_surface_get_orb(&surface, idx2);

    ASSERT(orb1->orb_id != orb2->orb_id);
    ASSERT(orb1->orb_id != SERAPH_VOID_U64);
    ASSERT(orb2->orb_id != SERAPH_VOID_U64);

    seraph_surface_destroy(&surface);
}

void test_orb_remove(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);
    ASSERT_EQ(surface.orb_count, 1);

    Seraph_Vbit result = seraph_surface_remove_orb(&surface, idx);
    ASSERT_VBIT_TRUE(result);
    ASSERT_EQ(surface.orb_count, 0);

    /* Can't get removed orb */
    Seraph_Orb* orb = seraph_surface_get_orb(&surface, idx);
    ASSERT(orb == NULL);

    seraph_surface_destroy(&surface);
}

void test_orb_find_by_cap(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t created_idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);
    int32_t found_idx = seraph_surface_find_orb(&surface, cap);

    ASSERT_EQ(created_idx, found_idx);

    /* Not found for different cap */
    char other_data[32];
    Seraph_Capability other_cap = seraph_cap_create(other_data, sizeof(other_data), 1, SERAPH_CAP_RW);
    int32_t not_found = seraph_surface_find_orb(&surface, other_cap);
    ASSERT_EQ(not_found, -1);

    seraph_surface_destroy(&surface);
}

void test_orb_theme_colors(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);
    Seraph_Orb* orb = seraph_surface_get_orb(&surface, idx);

    /* Orb should have theme colors applied */
    Seraph_Color expected_base = SERAPH_THEME_ORB_BASE;
    ASSERT_EQ(orb->color_base.r, expected_base.r);
    ASSERT_EQ(orb->color_base.g, expected_base.g);
    ASSERT_EQ(orb->color_base.b, expected_base.b);

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Input Tests
 *============================================================================*/

void test_cursor_update(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    seraph_surface_update_cursor(&surface, 123.0f, 456.0f);

    float cx = (float)seraph_q128_to_double(surface.cursor_x.primal);
    float cy = (float)seraph_q128_to_double(surface.cursor_y.primal);

    ASSERT_NEAR(cx, 123.0f, 0.001f);
    ASSERT_NEAR(cy, 456.0f, 0.001f);
    ASSERT_VBIT_TRUE(surface.cursor_present);

    seraph_surface_destroy(&surface);
}

void test_cursor_presence(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    /* Initially not present */
    ASSERT_VBIT_FALSE(surface.cursor_present);

    /* Update makes it present */
    seraph_surface_update_cursor(&surface, 100.0f, 100.0f);
    ASSERT_VBIT_TRUE(surface.cursor_present);

    /* Can set to not present */
    seraph_surface_set_cursor_present(&surface, SERAPH_VBIT_FALSE);
    ASSERT_VBIT_FALSE(surface.cursor_present);

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Physics Tests
 *============================================================================*/

void test_swell_radius(void) {
    float base = 30.0f;
    float swell = 5.0f;

    /* At distance 0, radius = base + swell */
    float r0 = seraph_surface_swell_radius(0.0f, 0.0f, 0.0f, 0.0f, base, swell);
    ASSERT_NEAR(r0, 35.0f, 0.001f);

    /* At large distance, radius approaches base */
    float r_far = seraph_surface_swell_radius(1000.0f, 0.0f, 0.0f, 0.0f, base, swell);
    ASSERT(r_far < 30.1f);
    ASSERT(r_far > 30.0f);
}

void test_orb_distance(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Create orb at known position */
    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);

    /* Move cursor to a known position */
    seraph_surface_update_cursor(&surface, 400.0f, 300.0f);

    float dist = seraph_surface_orb_distance(&surface, idx);
    /* Orb is at locus (400,300) + (100, 0) = (500, 300)
     * Distance from (400, 300) to (500, 300) = 100 */
    ASSERT_NEAR(dist, 100.0f, 1.0f);

    seraph_surface_destroy(&surface);
}

void test_physics_step_swelling(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 50.0f, 0.0f);
    Seraph_Orb* orb = seraph_surface_get_orb(&surface, idx);

    float initial_radius = orb->base_radius;

    /* Move cursor very close to orb (same position as orb) */
    float locus_x = (float)seraph_q128_to_double(surface.locus.position_x.primal);
    seraph_surface_update_cursor(&surface, locus_x + 50.0f, 300.0f);

    /* Run physics for a bit */
    seraph_surface_physics_step(&surface, 16000);  /* 16ms */
    seraph_surface_physics_step(&surface, 16000);
    seraph_surface_physics_step(&surface, 16000);

    float new_radius = (float)seraph_q128_to_double(orb->radius.primal);
    /* Radius should have increased due to swelling */
    ASSERT(new_radius > initial_radius);

    seraph_surface_destroy(&surface);
}

void test_physics_disabled(void) {
    Seraph_Surface surface;
    Seraph_Surface_Config config = SERAPH_SURFACE_CONFIG_DEFAULT;
    config.physics_enabled = false;

    seraph_surface_init_with_config(&surface, 800, 600, config);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);
    Seraph_Orb* orb = seraph_surface_get_orb(&surface, idx);
    float initial_radius = (float)seraph_q128_to_double(orb->radius.primal);

    seraph_surface_update_cursor(&surface, 500.0f, 300.0f);
    seraph_surface_physics_step(&surface, 16000);

    float new_radius = (float)seraph_q128_to_double(orb->radius.primal);
    /* With physics disabled, radius should not change */
    ASSERT_NEAR(new_radius, initial_radius, 0.001f);

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Intent Detection Tests
 *============================================================================*/

void test_detect_intent_none(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    /* No orbs, no intent */
    int32_t intent = seraph_surface_detect_intent(&surface);
    ASSERT_EQ(intent, -1);

    seraph_surface_destroy(&surface);
}

void test_detect_intent_cursor_over_orb(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);

    /* Get orb position (uses separate X/Y Galactics for 2D physics) */
    Seraph_Orb* orb = seraph_surface_get_orb(&surface, idx);
    float orb_x = (float)seraph_q128_to_double(orb->position_x.primal);
    float orb_y = (float)seraph_q128_to_double(orb->position_y.primal);

    /* Move cursor directly over orb */
    seraph_surface_update_cursor(&surface, orb_x, orb_y);

    int32_t intent = seraph_surface_detect_intent(&surface);
    ASSERT_EQ(intent, idx);

    seraph_surface_destroy(&surface);
}

void test_cancel_intent(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    surface.intent.phase = SERAPH_INTENT_PREVIEW;
    surface.intent.source_orb = 0;

    seraph_surface_cancel_intent(&surface);

    ASSERT_EQ(surface.intent.phase, SERAPH_INTENT_NONE);
    ASSERT_EQ(surface.intent.source_orb, -1);

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Expansion Tests
 *============================================================================*/

void test_expand_orb(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);

    Seraph_Vbit result = seraph_surface_expand_orb(&surface, idx);
    ASSERT_VBIT_TRUE(result);
    ASSERT_EQ(surface.expanded_orb_index, idx);

    Seraph_Orb* orb = seraph_surface_get_orb(&surface, idx);
    ASSERT_EQ(orb->state, SERAPH_ORB_FULLSCREEN);

    seraph_surface_destroy(&surface);
}

void test_expand_invalid_orb(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    Seraph_Vbit result = seraph_surface_expand_orb(&surface, -1);
    ASSERT_VBIT_FALSE(result);

    result = seraph_surface_expand_orb(&surface, 100);
    ASSERT_VBIT_FALSE(result);

    seraph_surface_destroy(&surface);
}

void test_contract_orb(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);
    seraph_surface_expand_orb(&surface, idx);
    ASSERT_EQ(surface.expanded_orb_index, idx);

    Seraph_Vbit result = seraph_surface_contract_current(&surface);
    ASSERT_VBIT_TRUE(result);
    ASSERT_EQ(surface.expanded_orb_index, -1);

    Seraph_Orb* orb = seraph_surface_get_orb(&surface, idx);
    ASSERT_EQ(orb->state, SERAPH_ORB_IDLE);

    seraph_surface_destroy(&surface);
}

void test_expand_moves_others_to_peripheral(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data1[32], data2[32];
    Seraph_Capability cap1 = seraph_cap_create(data1, sizeof(data1), 1, SERAPH_CAP_RW);
    Seraph_Capability cap2 = seraph_cap_create(data2, sizeof(data2), 1, SERAPH_CAP_RW);

    int32_t idx1 = seraph_surface_create_orb(&surface, cap1, 100.0f, 0.0f);
    int32_t idx2 = seraph_surface_create_orb(&surface, cap2, 100.0f, 3.14f);

    seraph_surface_expand_orb(&surface, idx1);

    Seraph_Orb* orb1 = seraph_surface_get_orb(&surface, idx1);
    Seraph_Orb* orb2 = seraph_surface_get_orb(&surface, idx2);

    ASSERT_EQ(orb1->state, SERAPH_ORB_FULLSCREEN);
    ASSERT_EQ(orb2->state, SERAPH_ORB_PERIPHERAL);

    seraph_surface_destroy(&surface);
}

void test_is_orb_expanded(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    int32_t idx = seraph_surface_create_orb(&surface, cap, 100.0f, 0.0f);

    ASSERT(!seraph_surface_is_orb_expanded(&surface, idx));

    seraph_surface_expand_orb(&surface, idx);
    ASSERT(seraph_surface_is_orb_expanded(&surface, idx));

    seraph_surface_contract_current(&surface);
    ASSERT(!seraph_surface_is_orb_expanded(&surface, idx));

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Rendering Tests (basic sanity checks)
 *============================================================================*/

void test_render_no_crash(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 100, 100);

    uint32_t* framebuffer = (uint32_t*)malloc(100 * 100 * sizeof(uint32_t));
    ASSERT(framebuffer != NULL);

    /* Render empty surface */
    seraph_surface_render(&surface, framebuffer, 100, 100);

    /* Add an orb and render */
    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);
    seraph_surface_create_orb(&surface, cap, 30.0f, 0.0f);

    seraph_surface_render(&surface, framebuffer, 100, 100);

    free(framebuffer);
    seraph_surface_destroy(&surface);
}

void test_render_background_color(void) {
    /* Use a larger buffer so corners are outside locus glow range */
    Seraph_Surface surface;
    seraph_surface_init(&surface, 200, 200);

    uint32_t* framebuffer = (uint32_t*)malloc(200 * 200 * sizeof(uint32_t));
    ASSERT(framebuffer != NULL);

    seraph_surface_render(&surface, framebuffer, 200, 200);

    /* Background should be theme color in corners (far from locus at center) */
    uint32_t expected = seraph_color_to_u32(SERAPH_THEME_BACKGROUND);
    /* Check top-left corner - should be pure background */
    ASSERT_EQ(framebuffer[0], expected);

    free(framebuffer);
    seraph_surface_destroy(&surface);
}

void test_render_null_safety(void) {
    Seraph_Surface surface;
    seraph_surface_init(&surface, 100, 100);

    /* Should not crash with null framebuffer */
    seraph_surface_render(&surface, NULL, 100, 100);

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Orb State Tests
 *============================================================================*/

void test_orb_state_is_visible(void) {
    ASSERT(seraph_orb_state_is_visible(SERAPH_ORB_IDLE));
    ASSERT(seraph_orb_state_is_visible(SERAPH_ORB_HOVER));
    ASSERT(seraph_orb_state_is_visible(SERAPH_ORB_SWELLING));
    ASSERT(seraph_orb_state_is_visible(SERAPH_ORB_FULLSCREEN));
    ASSERT(!seraph_orb_state_is_visible(SERAPH_ORB_VOID));
    ASSERT(!seraph_orb_state_is_visible(SERAPH_ORB_PERIPHERAL));
}

void test_orb_state_is_interactive(void) {
    ASSERT(seraph_orb_state_is_interactive(SERAPH_ORB_IDLE));
    ASSERT(seraph_orb_state_is_interactive(SERAPH_ORB_HOVER));
    ASSERT(seraph_orb_state_is_interactive(SERAPH_ORB_SWELLING));
    ASSERT(!seraph_orb_state_is_interactive(SERAPH_ORB_FULLSCREEN));
    ASSERT(!seraph_orb_state_is_interactive(SERAPH_ORB_VOID));
}

/*============================================================================
 * Atlas Persistence Tests
 *============================================================================
 *
 * "A UI that survives the apocalypse."
 *
 * These tests verify that Surface state can be persisted to Atlas and
 * restored after a restart/crash, maintaining exact Orb positions.
 */

/* Test file path for Atlas persistence tests */
#define TEST_ATLAS_PATH "test_surface_atlas.dat"

/*
 * Static storage for large structures to avoid stack overflow.
 * Seraph_Atlas is ~100KB and Seraph_Surface is ~17KB - too large for stack.
 */
static Seraph_Atlas s_test_atlas;
static Seraph_Surface s_test_surface;

/* Helper to remove test file */
static void cleanup_test_atlas(void) {
#ifdef _WIN32
    _unlink(TEST_ATLAS_PATH);
#else
    unlink(TEST_ATLAS_PATH);
#endif
}

void test_surface_set_atlas(void) {
    cleanup_test_atlas();

    Seraph_Surface* surface = &s_test_surface;
    seraph_surface_init(surface, 800, 600);

    /* Initialize Atlas */
    Seraph_Atlas* atlas = &s_test_atlas;
    Seraph_Vbit atlas_result = seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);
    ASSERT_VBIT_TRUE(atlas_result);

    /* Connect Surface to Atlas */
    Seraph_Vbit result = seraph_surface_set_atlas(surface, atlas);
    ASSERT_VBIT_TRUE(result);
    ASSERT(surface->atlas == atlas);
    ASSERT(surface->persistent != NULL);

    /* Verify persistent state is initialized */
    ASSERT(surface->persistent->magic == SERAPH_SURFACE_MAGIC);
    ASSERT(surface->persistent->version == SERAPH_SURFACE_VERSION);

    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);
    cleanup_test_atlas();
}

void test_surface_persist_orb(void) {
    cleanup_test_atlas();

    Seraph_Surface* surface = &s_test_surface;
    seraph_surface_init(surface, 800, 600);

    Seraph_Atlas* atlas = &s_test_atlas;
    seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);
    seraph_surface_set_atlas(surface, atlas);

    /* Create an orb */
    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);
    int32_t idx = seraph_surface_create_orb(surface, cap, 100.0f, 0.5f);
    ASSERT(idx >= 0);

    /* Persist the orb */
    Seraph_Vbit result = seraph_surface_persist_orb(surface, idx);
    ASSERT_VBIT_TRUE(result);

    /* Verify persistent state */
    ASSERT_EQ(surface->persistent->orb_count, 1);
    Seraph_Orb* orb = seraph_surface_get_orb(surface, idx);
    ASSERT(surface->persistent->orbs[0].orb_id == orb->orb_id);

    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);
    cleanup_test_atlas();
}

void test_surface_persist_full(void) {
    cleanup_test_atlas();

    Seraph_Surface* surface = &s_test_surface;
    seraph_surface_init(surface, 1920, 1080);

    Seraph_Atlas* atlas = &s_test_atlas;
    seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);
    seraph_surface_set_atlas(surface, atlas);

    /* Create multiple orbs */
    char data1[32], data2[32], data3[32];
    Seraph_Capability cap1 = seraph_cap_create(data1, sizeof(data1), 1, SERAPH_CAP_RW);
    Seraph_Capability cap2 = seraph_cap_create(data2, sizeof(data2), 1, SERAPH_CAP_RW);
    Seraph_Capability cap3 = seraph_cap_create(data3, sizeof(data3), 1, SERAPH_CAP_RW);

    seraph_surface_create_orb(surface, cap1, 150.0f, 0.0f);
    seraph_surface_create_orb(surface, cap2, 150.0f, 2.094f);
    seraph_surface_create_orb(surface, cap3, 150.0f, 4.189f);

    /* Persist entire surface */
    Seraph_Vbit result = seraph_surface_persist(surface);
    ASSERT_VBIT_TRUE(result);

    /* Verify all orbs persisted */
    ASSERT_EQ(surface->persistent->orb_count, 3);
    ASSERT_EQ(surface->persistent->width, 1920);
    ASSERT_EQ(surface->persistent->height, 1080);

    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);
    cleanup_test_atlas();
}

void test_surface_init_from_atlas(void) {
    cleanup_test_atlas();

    Seraph_Surface* surface = &s_test_surface;
    Seraph_Atlas* atlas = &s_test_atlas;

    /* Phase 1: Create surface with orbs and persist */
    seraph_surface_init(surface, 800, 600);
    seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);
    seraph_surface_set_atlas(surface, atlas);

    /* Create orbs at known positions */
    char data1[32], data2[32];
    Seraph_Capability cap1 = seraph_cap_create(data1, sizeof(data1), 1, SERAPH_CAP_RW);
    Seraph_Capability cap2 = seraph_cap_create(data2, sizeof(data2), 1, SERAPH_CAP_RW);

    int32_t idx1 = seraph_surface_create_orb(surface, cap1, 100.0f, 0.0f);
    int32_t idx2 = seraph_surface_create_orb(surface, cap2, 200.0f, 1.57f);

    /* Get orb IDs for verification */
    Seraph_Orb* orb1 = seraph_surface_get_orb(surface, idx1);
    Seraph_Orb* orb2 = seraph_surface_get_orb(surface, idx2);
    uint64_t id1 = orb1->orb_id;
    uint64_t id2 = orb2->orb_id;

    /* Persist */
    seraph_surface_persist(surface);

    /* "Apocalypse" - destroy everything */
    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);

    /* Verify we remembered the IDs correctly */
    ASSERT(id1 != SERAPH_VOID_U64);
    ASSERT(id2 != SERAPH_VOID_U64);

    /* Phase 2: Restore from Atlas (post-apocalypse) */
    Seraph_Vbit atlas_result = seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);
    ASSERT_VBIT_TRUE(atlas_result);

    /* Check persistent state exists */
    ASSERT(seraph_surface_has_persistent_state(atlas));

    /* Initialize surface from Atlas */
    Seraph_Vbit result = seraph_surface_init_from_atlas(surface, atlas);
    ASSERT_VBIT_TRUE(result);

    /* Verify surface restored correctly */
    ASSERT(surface->initialized);
    ASSERT_EQ(surface->width, 800);
    ASSERT_EQ(surface->height, 600);
    ASSERT_EQ(surface->orb_count, 2);

    /* Verify orbs exist */
    int orbs_found = 0;
    for (int i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        if (surface->orbs[i].state != SERAPH_ORB_VOID) {
            orbs_found++;
        }
    }
    ASSERT_EQ(orbs_found, 2);

    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);

    cleanup_test_atlas();
}

void test_surface_persist_position_accuracy(void) {
    cleanup_test_atlas();

    Seraph_Surface* surface = &s_test_surface;
    Seraph_Atlas* atlas = &s_test_atlas;
    float saved_x = 0.0f, saved_y = 0.0f;

    /* Phase 1: Create, position, persist */
    seraph_surface_init(surface, 800, 600);
    seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);
    seraph_surface_set_atlas(surface, atlas);

    char data[32];
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);
    int32_t idx = seraph_surface_create_orb(surface, cap, 123.456f, 0.789f);

    Seraph_Orb* orb = seraph_surface_get_orb(surface, idx);

    /* Get exact position */
    saved_x = (float)seraph_q128_to_double(orb->position_x.primal);
    saved_y = (float)seraph_q128_to_double(orb->position_y.primal);

    seraph_surface_persist(surface);
    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);

    /* Phase 2: Restore and verify exact position */
    seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);
    seraph_surface_init_from_atlas(surface, atlas);

    /* Find the restored orb */
    orb = NULL;
    for (int i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        if (surface->orbs[i].state != SERAPH_ORB_VOID) {
            orb = &surface->orbs[i];
            break;
        }
    }
    ASSERT(orb != NULL);

    /* Verify position matches exactly */
    float restored_x = (float)seraph_q128_to_double(orb->position_x.primal);
    float restored_y = (float)seraph_q128_to_double(orb->position_y.primal);

    ASSERT_NEAR(restored_x, saved_x, 0.001f);
    ASSERT_NEAR(restored_y, saved_y, 0.001f);

    /* Verify velocity is zero (physics starts from rest) */
    float vel_x = (float)seraph_q128_to_double(orb->position_x.tangent);
    float vel_y = (float)seraph_q128_to_double(orb->position_y.tangent);
    ASSERT_NEAR(vel_x, 0.0f, 0.001f);
    ASSERT_NEAR(vel_y, 0.0f, 0.001f);

    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);

    cleanup_test_atlas();
}

void test_surface_has_persistent_state(void) {
    cleanup_test_atlas();

    Seraph_Atlas* atlas = &s_test_atlas;
    seraph_atlas_init(atlas, TEST_ATLAS_PATH, 0);

    /* Initially no surface state */
    ASSERT(!seraph_surface_has_persistent_state(atlas));

    /* Create surface and connect to Atlas */
    Seraph_Surface* surface = &s_test_surface;
    seraph_surface_init(surface, 800, 600);
    seraph_surface_set_atlas(surface, atlas);

    /* Now has state */
    ASSERT(seraph_surface_has_persistent_state(atlas));

    seraph_surface_destroy(surface);
    seraph_atlas_destroy(atlas);
    cleanup_test_atlas();
}

void test_surface_persist_null_safety(void) {
    /* Null surface */
    Seraph_Vbit result = seraph_surface_persist(NULL);
    ASSERT_VBIT_VOID(result);

    /* Surface not connected to Atlas */
    Seraph_Surface surface;
    seraph_surface_init(&surface, 800, 600);

    result = seraph_surface_persist(&surface);
    ASSERT_VBIT_VOID(result);

    result = seraph_surface_persist_orb(&surface, 0);
    ASSERT_VBIT_VOID(result);

    seraph_surface_destroy(&surface);
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_surface_tests(void) {
    printf("\n========================================\n");
    printf("     MC11: Surface Tests\n");
    printf("========================================\n");

    /* Theme tests */
    RUN_TEST(theme_colors);
    RUN_TEST(color_to_u32);
    RUN_TEST(color_lerp);

    /* Configuration tests */
    RUN_TEST(default_config);

    /* Surface initialization tests */
    RUN_TEST(surface_init);
    RUN_TEST(surface_init_null);
    RUN_TEST(surface_init_with_config);
    RUN_TEST(surface_destroy);
    RUN_TEST(surface_is_valid);

    /* Locus tests */
    RUN_TEST(locus_initialization);

    /* Orb creation tests */
    RUN_TEST(orb_create);
    RUN_TEST(orb_create_multiple);
    RUN_TEST(orb_unique_ids);
    RUN_TEST(orb_remove);
    RUN_TEST(orb_find_by_cap);
    RUN_TEST(orb_theme_colors);

    /* Input tests */
    RUN_TEST(cursor_update);
    RUN_TEST(cursor_presence);

    /* Physics tests */
    RUN_TEST(swell_radius);
    RUN_TEST(orb_distance);
    RUN_TEST(physics_step_swelling);
    RUN_TEST(physics_disabled);

    /* Intent detection tests */
    RUN_TEST(detect_intent_none);
    RUN_TEST(detect_intent_cursor_over_orb);
    RUN_TEST(cancel_intent);

    /* Expansion tests */
    RUN_TEST(expand_orb);
    RUN_TEST(expand_invalid_orb);
    RUN_TEST(contract_orb);
    RUN_TEST(expand_moves_others_to_peripheral);
    RUN_TEST(is_orb_expanded);

    /* Rendering tests */
    RUN_TEST(render_no_crash);
    RUN_TEST(render_background_color);
    RUN_TEST(render_null_safety);

    /* State tests */
    RUN_TEST(orb_state_is_visible);
    RUN_TEST(orb_state_is_interactive);

    /* Atlas persistence tests */
    RUN_TEST(surface_set_atlas);
    RUN_TEST(surface_persist_orb);
    RUN_TEST(surface_persist_full);
    RUN_TEST(surface_init_from_atlas);
    RUN_TEST(surface_persist_position_accuracy);
    RUN_TEST(surface_has_persistent_state);
    RUN_TEST(surface_persist_null_safety);

    printf("\n----------------------------------------\n");
    printf("Surface Tests: %d/%d passed\n", tests_passed, tests_run);
    printf("----------------------------------------\n");

    if (tests_passed != tests_run) {
        printf("*** SURFACE TESTS FAILED ***\n");
        exit(1);
    }
}
