/**
 * @file test_hive.c
 * @brief Tests for SERAPH PRISM - Aetheric Hive (Distributed GPU Rendering)
 *
 * Tests the thermal-aware distributed rendering system including:
 * - Thermal derivative computation (d²T/dt²)
 * - Thermal headroom prediction
 * - Node registration and scoring
 * - Frame distribution algorithms
 * - Tile management
 */

#include "seraph/prism/hive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/* Q64 helpers - use definitions from prism_types.h */

static inline double q64_to_double(Seraph_Q64 x) {
    if (x == SERAPH_Q64_VOID) return NAN;
    return (double)((int64_t)x) / (double)(1ULL << SERAPH_Q64_FRAC_BITS);
}

static inline bool approx_eq_q64(Seraph_Q64 a, Seraph_Q64 b, double tolerance) {
    double da = q64_to_double(a);
    double db = q64_to_double(b);
    if (isnan(da) && isnan(db)) return true;
    return fabs(da - db) < tolerance;
}

/*============================================================================
 * Initialization Tests
 *============================================================================*/

TEST(hive_init) {
    Seraph_Hive hive;

    Seraph_Vbit result = seraph_hive_init(&hive, NULL);
    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(hive.initialized == SERAPH_VBIT_TRUE);
    ASSERT(seraph_hive_is_valid(&hive));

    /* Check defaults */
    ASSERT(hive.tile_width == SERAPH_HIVE_DEFAULT_TILE_WIDTH);
    ASSERT(hive.tile_height == SERAPH_HIVE_DEFAULT_TILE_HEIGHT);
    ASSERT(hive.offload_enabled == true);
    ASSERT(hive.node_count == 0);

    seraph_hive_destroy(&hive);
    ASSERT(!seraph_hive_is_valid(&hive));
}

TEST(hive_init_null) {
    Seraph_Vbit result = seraph_hive_init(NULL, NULL);
    ASSERT(result == SERAPH_VBIT_VOID);
}

/*============================================================================
 * Thermal State Tests
 *============================================================================*/

TEST(thermal_update_basic) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Update thermal with a temperature reading */
    Seraph_Q64 temp = q64_from_int(50);  /* 50°C */
    Seraph_Vbit result = seraph_hive_update_thermal(&hive.local_thermal, temp, 1000);

    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(hive.local_thermal.temperature == temp);

    seraph_hive_destroy(&hive);
}

TEST(thermal_derivative_computation) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Simulate temperature rising: 50°C → 55°C → 62°C (accelerating) */
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(50), 0);
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(55), 100);  /* +5°C */
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(62), 200);  /* +7°C (acceleration) */

    /* First derivative (dT/dt) should be positive (heating) */
    ASSERT((int64_t)hive.local_thermal.temp_derivative > 0);

    /* Second derivative (d²T/dt²) should be positive (accelerating) */
    ASSERT((int64_t)hive.local_thermal.temp_acceleration > 0);

    seraph_hive_destroy(&hive);
}

TEST(thermal_derivative_cooling) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Simulate temperature falling: 80°C → 75°C → 72°C */
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(80), 0);
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(75), 100);
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(72), 200);

    /* First derivative should be negative (cooling) */
    ASSERT((int64_t)hive.local_thermal.temp_derivative < 0);

    seraph_hive_destroy(&hive);
}

TEST(thermal_headroom_computation) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Set max safe temp to 85°C */
    hive.local_thermal.max_safe_temp = q64_from_int(85);

    /* Current temp 50°C, rising slowly */
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(50), 0);
    seraph_hive_update_thermal(&hive.local_thermal, q64_from_int(51), 1000);

    Seraph_Q64 headroom = seraph_hive_compute_headroom(&hive.local_thermal);

    /* Headroom should be positive (we have time before throttling) */
    ASSERT(headroom != SERAPH_Q64_VOID);
    ASSERT((int64_t)headroom > 0);

    seraph_hive_destroy(&hive);
}

TEST(thermal_headroom_critical) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Set max safe temp to 80°C */
    hive.local_thermal.max_safe_temp = q64_from_int(80);

    /* Already above safe temp */
    hive.local_thermal.temperature = q64_from_int(85);
    hive.local_thermal.temp_derivative = q64_from_int(1);

    Seraph_Q64 headroom = seraph_hive_compute_headroom(&hive.local_thermal);

    /* Headroom should be zero (already overheating) */
    ASSERT(headroom == 0);

    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Offload Decision Tests
 *============================================================================*/

TEST(should_offload_cool) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Stable temperature, low acceleration */
    hive.local_thermal.temperature = q64_from_int(50);
    hive.local_thermal.temp_derivative = q64_from_int(0);
    hive.local_thermal.temp_acceleration = 0;
    hive.local_thermal.headroom = q64_from_int(5000);  /* Plenty of headroom */

    Seraph_Vbit should = seraph_hive_should_offload(&hive);

    /* Cool GPU shouldn't need offload */
    ASSERT(should == SERAPH_VBIT_FALSE);

    seraph_hive_destroy(&hive);
}

TEST(should_offload_hot) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* High thermal acceleration (runaway heating) */
    hive.local_thermal.temp_acceleration = hive.thermal_threshold_f2 + q64_from_int(1);
    hive.local_thermal.headroom = q64_from_int(100);  /* Low headroom */

    Seraph_Vbit should = seraph_hive_should_offload(&hive);

    /* Hot GPU should trigger offload */
    ASSERT(should == SERAPH_VBIT_TRUE);

    seraph_hive_destroy(&hive);
}

TEST(should_offload_disabled) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Disable offloading */
    seraph_hive_enable_offload(&hive, false);

    /* Even with hot GPU, should return false */
    hive.local_thermal.temp_acceleration = q64_from_int(100);

    Seraph_Vbit should = seraph_hive_should_offload(&hive);
    ASSERT(should == SERAPH_VBIT_FALSE);

    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Node Management Tests
 *============================================================================*/

TEST(register_node) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_GPU_Caps caps = {
        .vram_mb = 8192,
        .compute_units = 32,
        .max_width = 3840,
        .max_height = 2160,
        .texture_units = 64,
        .supports_ray_tracing = true
    };

    Seraph_Vbit result = seraph_hive_register_node(&hive, 1, 0x1000000, &caps);

    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(hive.node_count == 1);
    ASSERT(hive.nodes[0].node_id == 1);
    ASSERT(hive.nodes[0].caps.vram_mb == 8192);
    ASSERT(hive.nodes[0].online == SERAPH_VBIT_TRUE);

    seraph_hive_destroy(&hive);
}

TEST(register_multiple_nodes) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_GPU_Caps caps = {
        .vram_mb = 4096,
        .compute_units = 16
    };

    for (uint16_t i = 1; i <= 5; i++) {
        Seraph_Vbit result = seraph_hive_register_node(&hive, i, 0x1000000 * i, &caps);
        ASSERT(result == SERAPH_VBIT_TRUE);
    }

    ASSERT(hive.node_count == 5);

    seraph_hive_destroy(&hive);
}

TEST(register_duplicate_node) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_GPU_Caps caps = { .vram_mb = 4096 };

    seraph_hive_register_node(&hive, 1, 0x1000000, &caps);
    Seraph_Vbit result = seraph_hive_register_node(&hive, 1, 0x2000000, &caps);

    /* Duplicate registration should fail */
    ASSERT(result == SERAPH_VBIT_FALSE);
    ASSERT(hive.node_count == 1);

    seraph_hive_destroy(&hive);
}

TEST(node_offline) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_GPU_Caps caps = { .vram_mb = 4096 };
    seraph_hive_register_node(&hive, 1, 0x1000000, &caps);

    ASSERT(hive.nodes[0].online == SERAPH_VBIT_TRUE);

    seraph_hive_node_offline(&hive, 1);

    ASSERT(hive.nodes[0].online == SERAPH_VBIT_FALSE);

    seraph_hive_destroy(&hive);
}

TEST(update_node_thermal) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_GPU_Caps caps = { .vram_mb = 4096 };
    seraph_hive_register_node(&hive, 1, 0x1000000, &caps);

    Seraph_Vbit result = seraph_hive_update_node_thermal(&hive, 1, q64_from_int(65));

    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(hive.nodes[0].thermal.temperature == q64_from_int(65));

    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Frame Management Tests
 *============================================================================*/

TEST(begin_frame) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 1920, 1080, SERAPH_HIVE_FORMAT_RGBA8, 0x10000000);

    ASSERT(frame != NULL);
    ASSERT(frame->width == 1920);
    ASSERT(frame->height == 1080);
    ASSERT(frame->format == SERAPH_HIVE_FORMAT_RGBA8);
    ASSERT(frame->guest_framebuffer == 0x10000000);
    ASSERT(frame->complete == SERAPH_VBIT_FALSE);
    ASSERT(frame->tile_count > 0);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

TEST(frame_tile_grid) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* 1920x1080 with default 128x128 tiles = 15x9 = 135 tiles (rounded up) */
    seraph_hive_set_tile_size(&hive, 128, 128);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 1920, 1080, SERAPH_HIVE_FORMAT_RGBA8, 0);

    uint32_t expected_tiles_x = (1920 + 127) / 128;  /* 15 */
    uint32_t expected_tiles_y = (1080 + 127) / 128;  /* 9 */
    uint32_t expected_total = expected_tiles_x * expected_tiles_y;

    ASSERT(frame->tile_count == expected_total);

    /* Verify tile coordinates */
    ASSERT(frame->tiles[0].tile_x == 0);
    ASSERT(frame->tiles[0].tile_y == 0);
    ASSERT(frame->tiles[0].state == SERAPH_TILE_PENDING);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

TEST(multiple_frames_in_flight) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_Frame* frames[SERAPH_HIVE_MAX_FRAMES_IN_FLIGHT];

    /* Start multiple frames */
    for (int i = 0; i < SERAPH_HIVE_MAX_FRAMES_IN_FLIGHT; i++) {
        frames[i] = seraph_hive_begin_frame(
            &hive, 1920, 1080, SERAPH_HIVE_FORMAT_RGBA8, 0);
        ASSERT(frames[i] != NULL);
    }

    /* Should fail to start another frame (all slots busy) */
    Seraph_Hive_Frame* overflow = seraph_hive_begin_frame(
        &hive, 1920, 1080, SERAPH_HIVE_FORMAT_RGBA8, 0);
    ASSERT(overflow == NULL);

    /* End frames */
    for (int i = 0; i < SERAPH_HIVE_MAX_FRAMES_IN_FLIGHT; i++) {
        seraph_hive_end_frame(&hive, frames[i]);
    }

    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Frame Distribution Tests
 *============================================================================*/

TEST(distribute_local_only) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* No remote nodes registered */
    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 1920, 1080, SERAPH_HIVE_FORMAT_RGBA8, 0);

    Seraph_Vbit result = seraph_hive_distribute_frame(&hive, frame);

    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(frame->tiles_local == frame->tile_count);
    ASSERT(frame->tiles_remote == 0);

    /* All tiles should be marked LOCAL */
    for (uint32_t i = 0; i < frame->tile_count; i++) {
        ASSERT(frame->tiles[i].state == SERAPH_TILE_LOCAL);
        ASSERT(frame->tiles[i].assigned_node == 0);
    }

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

TEST(distribute_with_nodes) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Register remote nodes */
    Seraph_Hive_GPU_Caps caps = {
        .vram_mb = 8192,
        .compute_units = 32
    };

    seraph_hive_register_node(&hive, 1, 0x1000000, &caps);
    seraph_hive_register_node(&hive, 2, 0x2000000, &caps);

    /* Force offload by setting high thermal acceleration */
    hive.local_thermal.temp_acceleration = hive.thermal_threshold_f2 + q64_from_int(10);
    hive.local_thermal.headroom = hive.min_headroom_ms >> 1;  /* Below minimum */

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 640, 480, SERAPH_HIVE_FORMAT_RGBA8, 0);

    Seraph_Vbit result = seraph_hive_distribute_frame(&hive, frame);

    ASSERT(result == SERAPH_VBIT_TRUE);

    /* Should have some remote tiles when offloading */
    ASSERT(frame->tiles_local + frame->tiles_remote == frame->tile_count);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Tile Polling and Completion Tests
 *============================================================================*/

TEST(poll_tiles) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 256, 256, SERAPH_HIVE_FORMAT_RGBA8, 0);

    seraph_hive_distribute_frame(&hive, frame);

    /* Poll tiles (simulates completion) */
    uint32_t completed = seraph_hive_poll_tiles(&hive, frame);

    /* In test mode without real rendering, all tiles complete immediately */
    ASSERT(completed == frame->tile_count);
    ASSERT(frame->tiles_complete == frame->tile_count);
    ASSERT(frame->complete == SERAPH_VBIT_TRUE);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

TEST(wait_frame) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 256, 256, SERAPH_HIVE_FORMAT_RGBA8, 0);

    seraph_hive_distribute_frame(&hive, frame);

    Seraph_Vbit complete = seraph_hive_wait_frame(&hive, frame, q64_from_int(1000));

    ASSERT(complete == SERAPH_VBIT_TRUE);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Frame Composition Tests
 *============================================================================*/

TEST(compose_frame) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 256, 256, SERAPH_HIVE_FORMAT_RGBA8, 0);

    seraph_hive_distribute_frame(&hive, frame);
    seraph_hive_poll_tiles(&hive, frame);

    Seraph_Vbit result = seraph_hive_compose_frame(&hive, frame);

    ASSERT(result == SERAPH_VBIT_TRUE);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

TEST(compose_incomplete_frame) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 256, 256, SERAPH_HIVE_FORMAT_RGBA8, 0);

    seraph_hive_distribute_frame(&hive, frame);

    /* Don't poll - frame is incomplete */
    frame->complete = SERAPH_VBIT_FALSE;

    Seraph_Vbit result = seraph_hive_compose_frame(&hive, frame);

    /* Should fail on incomplete frame */
    ASSERT(result == SERAPH_VBIT_FALSE);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

TEST(present_frame) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
        &hive, 256, 256, SERAPH_HIVE_FORMAT_RGBA8, 0);

    seraph_hive_distribute_frame(&hive, frame);
    seraph_hive_poll_tiles(&hive, frame);
    seraph_hive_compose_frame(&hive, frame);

    Seraph_Vbit result = seraph_hive_present_frame(&hive, frame);

    ASSERT(result == SERAPH_VBIT_TRUE);

    seraph_hive_end_frame(&hive, frame);
    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Configuration Tests
 *============================================================================*/

TEST(set_tile_size) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    seraph_hive_set_tile_size(&hive, 64, 64);

    ASSERT(hive.tile_width == 64);
    ASSERT(hive.tile_height == 64);

    /* Test clamping */
    seraph_hive_set_tile_size(&hive, 8, 1000);  /* Too small/large */
    ASSERT(hive.tile_width >= 16);
    ASSERT(hive.tile_height <= 512);

    seraph_hive_destroy(&hive);
}

TEST(set_policy) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    seraph_hive_set_policy(&hive,
        q64_from_int(5),      /* thermal threshold */
        q64_from_int(10000),  /* latency budget */
        q64_from_int(1000));  /* min headroom */

    ASSERT(hive.thermal_threshold_f2 == q64_from_int(5));
    ASSERT(hive.latency_budget_us == q64_from_int(10000));
    ASSERT(hive.min_headroom_ms == q64_from_int(1000));

    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Statistics Tests
 *============================================================================*/

TEST(statistics) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    /* Process several frames */
    for (int i = 0; i < 5; i++) {
        Seraph_Hive_Frame* frame = seraph_hive_begin_frame(
            &hive, 256, 256, SERAPH_HIVE_FORMAT_RGBA8, 0);
        seraph_hive_distribute_frame(&hive, frame);
        seraph_hive_poll_tiles(&hive, frame);
        seraph_hive_end_frame(&hive, frame);
    }

    uint64_t frames_total, frames_dist, tiles_remote, thermal_trigs;
    seraph_hive_get_stats(&hive, &frames_total, &frames_dist, &tiles_remote, &thermal_trigs);

    ASSERT(frames_total == 5);

    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Edge Cases
 *============================================================================*/

TEST(null_hive_operations) {
    /* All operations on NULL hive should be safe */
    Seraph_Vbit result = seraph_hive_should_offload(NULL);
    ASSERT(result == SERAPH_VBIT_VOID);

    Seraph_Q64 headroom = seraph_hive_compute_headroom(NULL);
    ASSERT(headroom == SERAPH_Q64_VOID);

    Seraph_Hive_Frame* frame = seraph_hive_begin_frame(NULL, 100, 100, 0, 0);
    ASSERT(frame == NULL);
}

TEST(node_capacity) {
    Seraph_Hive hive;
    seraph_hive_init(&hive, NULL);

    Seraph_Hive_GPU_Caps caps = { .vram_mb = 4096 };

    /* Fill all node slots */
    for (uint16_t i = 0; i < SERAPH_HIVE_MAX_NODES; i++) {
        seraph_hive_register_node(&hive, i + 1, 0x1000000 * (i + 1), &caps);
    }

    ASSERT(hive.node_count == SERAPH_HIVE_MAX_NODES);

    /* Should fail when exceeding capacity */
    Seraph_Vbit result = seraph_hive_register_node(&hive, 999, 0xFFFF0000, &caps);
    ASSERT(result == SERAPH_VBIT_FALSE);

    seraph_hive_destroy(&hive);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_hive_tests(void) {
    printf("\n=== PRISM: Aetheric Hive Tests ===\n\n");

    /* Initialization */
    RUN_TEST(hive_init);
    RUN_TEST(hive_init_null);

    /* Thermal State */
    RUN_TEST(thermal_update_basic);
    RUN_TEST(thermal_derivative_computation);
    RUN_TEST(thermal_derivative_cooling);
    RUN_TEST(thermal_headroom_computation);
    RUN_TEST(thermal_headroom_critical);

    /* Offload Decision */
    RUN_TEST(should_offload_cool);
    RUN_TEST(should_offload_hot);
    RUN_TEST(should_offload_disabled);

    /* Node Management */
    RUN_TEST(register_node);
    RUN_TEST(register_multiple_nodes);
    RUN_TEST(register_duplicate_node);
    RUN_TEST(node_offline);
    RUN_TEST(update_node_thermal);

    /* Frame Management */
    RUN_TEST(begin_frame);
    RUN_TEST(frame_tile_grid);
    RUN_TEST(multiple_frames_in_flight);

    /* Frame Distribution */
    RUN_TEST(distribute_local_only);
    RUN_TEST(distribute_with_nodes);

    /* Tile Polling */
    RUN_TEST(poll_tiles);
    RUN_TEST(wait_frame);

    /* Frame Composition */
    RUN_TEST(compose_frame);
    RUN_TEST(compose_incomplete_frame);
    RUN_TEST(present_frame);

    /* Configuration */
    RUN_TEST(set_tile_size);
    RUN_TEST(set_policy);

    /* Statistics */
    RUN_TEST(statistics);

    /* Edge Cases */
    RUN_TEST(null_hive_operations);
    RUN_TEST(node_capacity);

    printf("\nHive Tests: %d/%d passed\n", tests_passed, tests_run);
}

#ifndef SERAPH_TEST_NO_MAIN
int main(void) {
    run_hive_tests();
    return (tests_passed == tests_run) ? 0 : 1;
}
#endif
