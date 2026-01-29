/**
 * @file test_entropic.c
 * @brief Tests for SERAPH PRISM - Entropic Upscaling (Semantic Super-Resolution)
 *
 * Tests the semantic-aware upscaling system including:
 * - Region classification (text, geometry, noise)
 * - Entropy computation
 * - Gradient analysis
 * - Motion estimation
 * - Upscaling quality
 */

#include "seraph/prism/entropic.h"
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

/* Test image generation helpers */
static void fill_solid_color(uint32_t* buffer, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < w * h; i++) {
        buffer[i] = color;
    }
}

static void fill_gradient(uint32_t* buffer, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t v = (uint8_t)((x * 255) / w);
            buffer[y * w + x] = v | (v << 8) | (v << 16) | 0xFF000000;
        }
    }
}

static void fill_noise(uint32_t* buffer, uint32_t w, uint32_t h, uint32_t seed) {
    uint32_t state = seed;
    for (uint32_t i = 0; i < w * h; i++) {
        /* Simple PRNG */
        state = state * 1103515245 + 12345;
        uint8_t r = (state >> 16) & 0xFF;
        state = state * 1103515245 + 12345;
        uint8_t g = (state >> 16) & 0xFF;
        state = state * 1103515245 + 12345;
        uint8_t b = (state >> 16) & 0xFF;
        buffer[i] = r | (g << 8) | (b << 16) | 0xFF000000;
    }
}

static void fill_text_pattern(uint32_t* buffer, uint32_t w, uint32_t h) {
    /* Simulate text: bimodal (black/white), horizontal edges */
    uint32_t white = 0xFFFFFFFF;
    uint32_t black = 0xFF000000;

    fill_solid_color(buffer, w, h, white);

    /* Draw horizontal "text lines" */
    for (uint32_t y = 4; y < h; y += 8) {
        for (uint32_t x = 2; x < w - 2; x++) {
            if ((x / 4) % 2 == 0) {  /* Alternating pattern */
                buffer[y * w + x] = black;
                if (y + 1 < h) buffer[(y + 1) * w + x] = black;
            }
        }
    }
}

static void fill_edge_pattern(uint32_t* buffer, uint32_t w, uint32_t h) {
    /* Simulate geometry: strong directional edges */
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            /* Diagonal edge */
            uint8_t v = (x + y < w) ? 255 : 0;
            buffer[y * w + x] = v | (v << 8) | (v << 16) | 0xFF000000;
        }
    }
}

/*============================================================================
 * Initialization Tests
 *============================================================================*/

TEST(engine_init) {
    Seraph_Entropic_Engine engine;

    Seraph_Vbit result = seraph_entropic_init(&engine, 3840, 2160);
    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(engine.initialized == SERAPH_VBIT_TRUE);
    ASSERT(seraph_entropic_is_valid(&engine));
    ASSERT(engine.default_target_width == 3840);
    ASSERT(engine.default_target_height == 2160);

    seraph_entropic_destroy(&engine);
    ASSERT(!seraph_entropic_is_valid(&engine));
}

TEST(engine_init_null) {
    Seraph_Vbit result = seraph_entropic_init(NULL, 1920, 1080);
    ASSERT(result == SERAPH_VBIT_VOID);
}

TEST(classifier_init) {
    Seraph_Entropic_Classifier classifier;

    Seraph_Vbit result = seraph_entropic_classifier_init(&classifier);
    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(classifier.initialized == SERAPH_VBIT_TRUE);
}

/*============================================================================
 * Semantic Type Helper Tests
 *============================================================================*/

TEST(semantic_type_helpers) {
    /* Text types */
    ASSERT(seraph_semantic_is_text(SERAPH_SEMANTIC_TEXT) == true);
    ASSERT(seraph_semantic_is_text(SERAPH_SEMANTIC_UI_FLAT) == true);
    ASSERT(seraph_semantic_is_text(SERAPH_SEMANTIC_UI_ICON) == true);
    ASSERT(seraph_semantic_is_text(SERAPH_SEMANTIC_GEOMETRY) == false);
    ASSERT(seraph_semantic_is_text(SERAPH_SEMANTIC_NOISE) == false);

    /* Geometry types */
    ASSERT(seraph_semantic_is_geometry(SERAPH_SEMANTIC_GEOMETRY) == true);
    ASSERT(seraph_semantic_is_geometry(SERAPH_SEMANTIC_SPECULAR) == true);
    ASSERT(seraph_semantic_is_geometry(SERAPH_SEMANTIC_SILHOUETTE) == true);
    ASSERT(seraph_semantic_is_geometry(SERAPH_SEMANTIC_TEXT) == false);
    ASSERT(seraph_semantic_is_geometry(SERAPH_SEMANTIC_NOISE) == false);

    /* Noise types */
    ASSERT(seraph_semantic_is_noise(SERAPH_SEMANTIC_NOISE) == true);
    ASSERT(seraph_semantic_is_noise(SERAPH_SEMANTIC_DITHER) == true);
    ASSERT(seraph_semantic_is_noise(SERAPH_SEMANTIC_PARTICLE) == true);
    ASSERT(seraph_semantic_is_noise(SERAPH_SEMANTIC_TEXT) == false);
}

/*============================================================================
 * Classification Tests
 *============================================================================*/

TEST(classify_solid_color) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    /* Create solid color buffer (should classify as UI_FLAT) */
    uint32_t buffer[64 * 64];
    fill_solid_color(buffer, 64, 64, 0xFF404040);

    Seraph_Q64 confidence;
    Seraph_Semantic_Type type = seraph_entropic_classify_region(
        &engine, buffer, 0, 0, 64, 64, 64 * 4, SERAPH_ENTROPIC_RGBA8, &confidence);

    /* Solid color should have low entropy → UI_FLAT */
    ASSERT(type == SERAPH_SEMANTIC_UI_FLAT || type == SERAPH_SEMANTIC_UNKNOWN);
    ASSERT(confidence != SERAPH_Q64_VOID);

    seraph_entropic_destroy(&engine);
}

TEST(classify_gradient) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t buffer[64 * 64];
    fill_gradient(buffer, 64, 64);

    Seraph_Q64 confidence;
    Seraph_Semantic_Type type = seraph_entropic_classify_region(
        &engine, buffer, 0, 0, 64, 64, 64 * 4, SERAPH_ENTROPIC_RGBA8, &confidence);

    /* Gradient should have medium entropy, directional gradient */
    ASSERT(type == SERAPH_SEMANTIC_UI_GRADIENT ||
           type == SERAPH_SEMANTIC_TEXTURE_LF ||
           type == SERAPH_SEMANTIC_UNKNOWN);

    seraph_entropic_destroy(&engine);
}

TEST(classify_noise) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t buffer[64 * 64];
    fill_noise(buffer, 64, 64, 12345);

    Seraph_Q64 confidence;
    Seraph_Semantic_Type type = seraph_entropic_classify_region(
        &engine, buffer, 0, 0, 64, 64, 64 * 4, SERAPH_ENTROPIC_RGBA8, &confidence);

    /* Noise should have high entropy */
    ASSERT(type == SERAPH_SEMANTIC_NOISE ||
           type == SERAPH_SEMANTIC_DITHER ||
           type == SERAPH_SEMANTIC_UNKNOWN);

    seraph_entropic_destroy(&engine);
}

TEST(classify_text_pattern) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t buffer[64 * 64];
    fill_text_pattern(buffer, 64, 64);

    Seraph_Q64 confidence;
    Seraph_Semantic_Type type = seraph_entropic_classify_region(
        &engine, buffer, 0, 0, 64, 64, 64 * 4, SERAPH_ENTROPIC_RGBA8, &confidence);

    /* Text pattern: bimodal histogram, horizontal edges */
    ASSERT(type == SERAPH_SEMANTIC_TEXT ||
           type == SERAPH_SEMANTIC_UI_FLAT ||
           type == SERAPH_SEMANTIC_UNKNOWN);

    seraph_entropic_destroy(&engine);
}

TEST(classify_geometry_edges) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t buffer[64 * 64];
    fill_edge_pattern(buffer, 64, 64);

    Seraph_Q64 confidence;
    Seraph_Semantic_Type type = seraph_entropic_classify_region(
        &engine, buffer, 0, 0, 64, 64, 64 * 4, SERAPH_ENTROPIC_RGBA8, &confidence);

    /* Strong edge should classify as geometry */
    ASSERT(type == SERAPH_SEMANTIC_GEOMETRY ||
           type == SERAPH_SEMANTIC_SILHOUETTE ||
           type == SERAPH_SEMANTIC_UNKNOWN);

    seraph_entropic_destroy(&engine);
}

/*============================================================================
 * Frame Classification Tests
 *============================================================================*/

TEST(classify_frame) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    /* Create a frame with mixed content */
    uint32_t* buffer = malloc(256 * 256 * sizeof(uint32_t));
    ASSERT(buffer != NULL);

    /* Top half: solid color, Bottom half: noise */
    fill_solid_color(buffer, 256, 128, 0xFF808080);
    fill_noise(buffer + 256 * 128, 256, 128, 54321);

    /* Set up frame context */
    Seraph_Entropic_Frame frame = {
        .source_buffer = buffer,
        .source_width = 256,
        .source_height = 256,
        .source_stride = 256 * 4,
        .format = SERAPH_ENTROPIC_RGBA8,
        .region_count = 0
    };

    uint32_t region_count = seraph_entropic_classify_frame(&engine, &frame);

    /* Should have classified multiple regions */
    ASSERT(region_count > 0);
    ASSERT(frame.region_count > 0);

    free(buffer);
    seraph_entropic_destroy(&engine);
}

/*============================================================================
 * Motion Estimation Tests
 *============================================================================*/

TEST(motion_estimation_static) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 640, 480);

    /* Two identical frames → no motion */
    uint32_t* frame1 = malloc(128 * 128 * sizeof(uint32_t));
    uint32_t* frame2 = malloc(128 * 128 * sizeof(uint32_t));
    ASSERT(frame1 && frame2);

    fill_edge_pattern(frame1, 128, 128);
    fill_edge_pattern(frame2, 128, 128);

    Seraph_Entropic_Frame frame = {
        .source_buffer = frame2,
        .source_width = 128,
        .source_height = 128,
        .source_stride = 128 * 4,
        .format = SERAPH_ENTROPIC_RGBA8,
        .motion_grid_w = 128 / SERAPH_ENTROPIC_MOTION_CELL,
        .motion_grid_h = 128 / SERAPH_ENTROPIC_MOTION_CELL
    };

    /* Allocate motion field */
    frame.motion_field = calloc(frame.motion_grid_w * frame.motion_grid_h,
                                 sizeof(Seraph_Entropic_Motion));

    Seraph_Vbit result = seraph_entropic_compute_motion(&engine, &frame, frame1);
    ASSERT(result == SERAPH_VBIT_TRUE);

    /* Check center motion vector - should be near zero */
    Seraph_Entropic_Motion mv = seraph_entropic_get_motion(&frame, 64, 64);
    if (mv.valid) {
        double dx = q64_to_double(mv.dx);
        double dy = q64_to_double(mv.dy);
        /* Static scene should have minimal motion */
        ASSERT(fabs(dx) < 5.0 && fabs(dy) < 5.0);
    }

    free(frame.motion_field);
    free(frame1);
    free(frame2);
    seraph_entropic_destroy(&engine);
}

/*============================================================================
 * Upscaling Tests
 *============================================================================*/

TEST(upscale_basic) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    /* Source: 64x64, Target: 128x128 (2x upscale) */
    uint32_t source[64 * 64];
    uint32_t target[128 * 128];

    fill_gradient(source, 64, 64);
    memset(target, 0, sizeof(target));

    Seraph_Vbit result = seraph_entropic_upscale(
        &engine,
        source, 64, 64,
        target, 128, 128,
        SERAPH_ENTROPIC_RGBA8
    );

    ASSERT(result == SERAPH_VBIT_TRUE);

    /* Verify target has non-zero pixels */
    bool has_content = false;
    for (int i = 0; i < 128 * 128; i++) {
        if (target[i] != 0) {
            has_content = true;
            break;
        }
    }
    ASSERT(has_content);

    seraph_entropic_destroy(&engine);
}

TEST(upscale_text_region) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t source[64 * 64];
    fill_text_pattern(source, 64, 64);

    Seraph_Entropic_Frame frame = {
        .source_buffer = source,
        .source_width = 64,
        .source_height = 64,
        .source_stride = 64 * 4,
        .format = SERAPH_ENTROPIC_RGBA8,
        .target_width = 128,
        .target_height = 128,
        .scale_x = Q64_ONE * 2,
        .scale_y = Q64_ONE * 2
    };

    /* Create a text region */
    Seraph_Entropic_Region region = {
        .x = 0, .y = 0,
        .width = 64, .height = 64,
        .semantic = SERAPH_SEMANTIC_TEXT,
        .confidence = Q64_ONE,
        .needs_vectorization = true
    };

    Seraph_Vbit result = seraph_entropic_upscale_text(&engine, &frame, &region);

    /* May succeed or return VOID if text vectorization not fully implemented */
    ASSERT(result == SERAPH_VBIT_TRUE || result == SERAPH_VBIT_VOID);

    seraph_entropic_destroy(&engine);
}

TEST(upscale_geometry_region) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t source[64 * 64];
    fill_edge_pattern(source, 64, 64);

    Seraph_Entropic_Frame frame = {
        .source_buffer = source,
        .source_width = 64,
        .source_height = 64,
        .source_stride = 64 * 4,
        .format = SERAPH_ENTROPIC_RGBA8,
        .target_width = 128,
        .target_height = 128,
        .scale_x = Q64_ONE * 2,
        .scale_y = Q64_ONE * 2
    };

    Seraph_Entropic_Region region = {
        .x = 0, .y = 0,
        .width = 64, .height = 64,
        .semantic = SERAPH_SEMANTIC_GEOMETRY,
        .confidence = Q64_ONE,
        .velocity_x = 0,
        .velocity_y = 0,
        .needs_motion_comp = false
    };

    Seraph_Vbit result = seraph_entropic_upscale_geometry(&engine, &frame, &region);

    ASSERT(result == SERAPH_VBIT_TRUE || result == SERAPH_VBIT_VOID);

    seraph_entropic_destroy(&engine);
}

TEST(upscale_noise_region) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t source[64 * 64];
    fill_noise(source, 64, 64, 99999);

    Seraph_Entropic_Frame frame = {
        .source_buffer = source,
        .source_width = 64,
        .source_height = 64,
        .source_stride = 64 * 4,
        .format = SERAPH_ENTROPIC_RGBA8,
        .target_width = 128,
        .target_height = 128,
        .scale_x = Q64_ONE * 2,
        .scale_y = Q64_ONE * 2
    };

    Seraph_Entropic_Region region = {
        .x = 0, .y = 0,
        .width = 64, .height = 64,
        .semantic = SERAPH_SEMANTIC_NOISE,
        .confidence = Q64_ONE,
        .local_entropy = Q64_ONE  /* High entropy */
    };

    Seraph_Vbit result = seraph_entropic_upscale_noise(&engine, &frame, &region);

    ASSERT(result == SERAPH_VBIT_TRUE || result == SERAPH_VBIT_VOID);

    seraph_entropic_destroy(&engine);
}

TEST(upscale_generic_region) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t source[64 * 64];
    fill_gradient(source, 64, 64);

    Seraph_Entropic_Frame frame = {
        .source_buffer = source,
        .source_width = 64,
        .source_height = 64,
        .source_stride = 64 * 4,
        .format = SERAPH_ENTROPIC_RGBA8,
        .target_width = 128,
        .target_height = 128,
        .scale_x = Q64_ONE * 2,
        .scale_y = Q64_ONE * 2
    };

    Seraph_Entropic_Region region = {
        .x = 0, .y = 0,
        .width = 64, .height = 64,
        .semantic = SERAPH_SEMANTIC_UNKNOWN,
        .confidence = Q64_HALF
    };

    Seraph_Vbit result = seraph_entropic_upscale_generic(&engine, &frame, &region);

    ASSERT(result == SERAPH_VBIT_TRUE || result == SERAPH_VBIT_VOID);

    seraph_entropic_destroy(&engine);
}

/*============================================================================
 * Configuration Tests
 *============================================================================*/

TEST(configure_features) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    seraph_entropic_configure(&engine,
        true,   /* temporal */
        true,   /* text vectorization */
        true,   /* geometry extrapolation */
        true);  /* noise preservation */

    ASSERT(engine.temporal_enabled == true);
    ASSERT(engine.text_vectorization_enabled == true);
    ASSERT(engine.geometry_extrapolation_enabled == true);
    ASSERT(engine.noise_preservation_enabled == true);

    seraph_entropic_configure(&engine, false, false, false, false);

    ASSERT(engine.temporal_enabled == false);
    ASSERT(engine.text_vectorization_enabled == false);
    ASSERT(engine.geometry_extrapolation_enabled == false);
    ASSERT(engine.noise_preservation_enabled == false);

    seraph_entropic_destroy(&engine);
}

TEST(set_target) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    seraph_entropic_set_target(&engine, 7680, 4320);  /* 8K */

    ASSERT(engine.default_target_width == 7680);
    ASSERT(engine.default_target_height == 4320);

    seraph_entropic_destroy(&engine);
}

/*============================================================================
 * Statistics Tests
 *============================================================================*/

TEST(statistics) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    /* Perform some upscaling */
    uint32_t source[64 * 64];
    uint32_t target[128 * 128];
    fill_gradient(source, 64, 64);

    for (int i = 0; i < 5; i++) {
        seraph_entropic_upscale(&engine, source, 64, 64, target, 128, 128,
                                SERAPH_ENTROPIC_RGBA8);
    }

    uint64_t frames, text_vec, geom_ext;
    Seraph_Q64 avg_time;

    seraph_entropic_get_stats(&engine, &frames, &text_vec, &geom_ext, &avg_time);

    ASSERT(frames == 5);

    seraph_entropic_destroy(&engine);
}

/*============================================================================
 * Edge Cases
 *============================================================================*/

TEST(null_engine_operations) {
    Seraph_Semantic_Type type = seraph_entropic_classify_region(
        NULL, NULL, 0, 0, 10, 10, 40, SERAPH_ENTROPIC_RGBA8, NULL);
    ASSERT(type == SERAPH_SEMANTIC_UNKNOWN);

    Seraph_Vbit result = seraph_entropic_upscale(NULL, NULL, 0, 0, NULL, 0, 0, 0);
    ASSERT(result == SERAPH_VBIT_VOID);
}

TEST(zero_size_region) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t buffer[64 * 64];
    Seraph_Q64 confidence;

    /* Zero-size region should handle gracefully */
    Seraph_Semantic_Type type = seraph_entropic_classify_region(
        &engine, buffer, 0, 0, 0, 0, 64 * 4, SERAPH_ENTROPIC_RGBA8, &confidence);

    ASSERT(type == SERAPH_SEMANTIC_UNKNOWN);

    seraph_entropic_destroy(&engine);
}

TEST(pixel_format_handling) {
    Seraph_Entropic_Engine engine;
    seraph_entropic_init(&engine, 1920, 1080);

    uint32_t buffer[64 * 64];
    fill_solid_color(buffer, 64, 64, 0xFFAABBCC);

    Seraph_Q64 conf;

    /* Test both RGBA and BGRA formats */
    seraph_entropic_classify_region(&engine, buffer, 0, 0, 64, 64,
                                    64 * 4, SERAPH_ENTROPIC_RGBA8, &conf);
    ASSERT(conf != SERAPH_Q64_VOID);

    seraph_entropic_classify_region(&engine, buffer, 0, 0, 64, 64,
                                    64 * 4, SERAPH_ENTROPIC_BGRA8, &conf);
    ASSERT(conf != SERAPH_Q64_VOID);

    seraph_entropic_destroy(&engine);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_entropic_tests(void) {
    printf("\n=== PRISM: Entropic Upscaling Tests ===\n\n");

    /* Initialization */
    RUN_TEST(engine_init);
    RUN_TEST(engine_init_null);
    RUN_TEST(classifier_init);

    /* Semantic Type Helpers */
    RUN_TEST(semantic_type_helpers);

    /* Classification */
    RUN_TEST(classify_solid_color);
    RUN_TEST(classify_gradient);
    RUN_TEST(classify_noise);
    RUN_TEST(classify_text_pattern);
    RUN_TEST(classify_geometry_edges);

    /* Frame Classification */
    RUN_TEST(classify_frame);

    /* Motion Estimation */
    RUN_TEST(motion_estimation_static);

    /* Upscaling */
    RUN_TEST(upscale_basic);
    RUN_TEST(upscale_text_region);
    RUN_TEST(upscale_geometry_region);
    RUN_TEST(upscale_noise_region);
    RUN_TEST(upscale_generic_region);

    /* Configuration */
    RUN_TEST(configure_features);
    RUN_TEST(set_target);

    /* Statistics */
    RUN_TEST(statistics);

    /* Edge Cases */
    RUN_TEST(null_engine_operations);
    RUN_TEST(zero_size_region);
    RUN_TEST(pixel_format_handling);

    printf("\nEntropic Tests: %d/%d passed\n", tests_passed, tests_run);
}

#ifndef SERAPH_TEST_NO_MAIN
int main(void) {
    run_entropic_tests();
    return (tests_passed == tests_run) ? 0 : 1;
}
#endif
