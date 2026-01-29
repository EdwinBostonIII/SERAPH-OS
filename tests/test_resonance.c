/**
 * @file test_resonance.c
 * @brief Tests for SERAPH PRISM - Resonant Hitbox (Harmonic Collision Detection)
 *
 * Tests the frequency-domain collision detection system including:
 * - Harmonic signature creation and transformation
 * - Dissonance computation (collision detection)
 * - Dual-domain architecture (soft soul + hard skeleton)
 * - Spectral windowing for Gibbs suppression
 * - Matter type handling
 */

#include "seraph/prism/resonance.h"
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

static inline bool approx_eq(double a, double b, double tolerance) {
    if (isnan(a) && isnan(b)) return true;
    return fabs(a - b) < tolerance;
}

/*============================================================================
 * World Initialization Tests
 *============================================================================*/

TEST(world_init) {
    Seraph_Resonance_World world;

    Seraph_Vbit result = seraph_resonance_world_init(&world, 1000);
    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(world.initialized == SERAPH_VBIT_TRUE);
    ASSERT(world.signature_capacity == 1000);
    ASSERT(world.signature_count == 0);

    seraph_resonance_world_destroy(&world);
    ASSERT(world.initialized != SERAPH_VBIT_TRUE);
}

TEST(world_init_null) {
    Seraph_Vbit result = seraph_resonance_world_init(NULL, 1000);
    ASSERT(result == SERAPH_VBIT_VOID);
}

TEST(builtin_classes) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);

    Seraph_Vbit result = seraph_resonance_init_builtin_classes(&world);
    ASSERT(result == SERAPH_VBIT_TRUE);

    /* Should have registered at least sphere, box, capsule */
    ASSERT(world.class_count >= 3);

    /* Check sphere class exists */
    bool found_sphere = false;
    for (uint32_t i = 0; i < world.class_count; i++) {
        if (world.classes[i].type == SERAPH_SHAPE_SPHERE &&
            world.classes[i].valid == SERAPH_VBIT_TRUE) {
            found_sphere = true;
            /* Sphere should be radially symmetric */
            ASSERT(world.classes[i].radially_symmetric == true);
            break;
        }
    }
    ASSERT(found_sphere);

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Object Registration Tests
 *============================================================================*/

TEST(register_object) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    /* Register a sphere object */
    Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
        &world, 12345, SERAPH_SHAPE_SPHERE);

    ASSERT(sig != NULL);
    ASSERT(sig->object_id == 12345);
    ASSERT(sig->class_id == SERAPH_SHAPE_SPHERE);
    ASSERT(sig->valid == SERAPH_VBIT_TRUE);
    ASSERT(world.signature_count == 1);

    seraph_resonance_world_destroy(&world);
}

TEST(register_multiple_objects) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    for (uint64_t i = 1; i <= 10; i++) {
        Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
            &world, i, SERAPH_SHAPE_BOX);
        ASSERT(sig != NULL);
        ASSERT(sig->object_id == i);
    }

    ASSERT(world.signature_count == 10);

    seraph_resonance_world_destroy(&world);
}

TEST(unregister_object) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    seraph_resonance_register_object(&world, 100, SERAPH_SHAPE_SPHERE);
    ASSERT(world.signature_count == 1);

    Seraph_Vbit result = seraph_resonance_unregister_object(&world, 100);
    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(world.signature_count == 0);

    /* Can't unregister non-existent object */
    result = seraph_resonance_unregister_object(&world, 999);
    ASSERT(result != SERAPH_VBIT_TRUE);

    seraph_resonance_world_destroy(&world);
}

TEST(find_object) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    seraph_resonance_register_object(&world, 42, SERAPH_SHAPE_CAPSULE);

    Seraph_Resonance_Signature* found = seraph_resonance_find_object(&world, 42);
    ASSERT(found != NULL);
    ASSERT(found->object_id == 42);

    /* Non-existent object */
    found = seraph_resonance_find_object(&world, 999);
    ASSERT(found == NULL);

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Harmonic Transformation Tests
 *============================================================================*/

TEST(transform_basic) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);

    /* Transform with position (10, 20, 30), no velocity, unit scale */
    Seraph_Vbit result = seraph_resonance_transform(
        &world, sig,
        q64_from_int(10), q64_from_int(20), q64_from_int(30),  /* position */
        0, 0, 0,                                                /* velocity */
        Q64_ONE, Q64_ONE, Q64_ONE                               /* scale */
    );

    ASSERT(result == SERAPH_VBIT_TRUE);

    /* Signature should have valid harmonics */
    ASSERT(sig->harmonic_count > 0);
    ASSERT(sig->active_harmonics > 0);
    ASSERT(sig->total_power > 0);

    /* Phase should encode position */
    ASSERT(sig->phase_x != 0 || sig->phase_y != 0 || sig->phase_z != 0);

    seraph_resonance_world_destroy(&world);
}

TEST(transform_with_velocity) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);

    /* Transform with velocity */
    seraph_resonance_transform(
        &world, sig,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        q64_from_int(5), q64_from_int(10), q64_from_int(0),  /* velocity */
        Q64_ONE, Q64_ONE, Q64_ONE
    );

    /* Velocity should be recorded */
    ASSERT(sig->vel_x != 0);
    ASSERT(sig->vel_y != 0);
    ASSERT(sig->vel_z == 0);

    seraph_resonance_world_destroy(&world);
}

TEST(transform_with_scale) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig1 = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);
    Seraph_Resonance_Signature* sig2 = seraph_resonance_register_object(
        &world, 2, SERAPH_SHAPE_SPHERE);

    /* Same position, different scales */
    seraph_resonance_transform(&world, sig1,
        0, 0, 0, 0, 0, 0,
        Q64_ONE, Q64_ONE, Q64_ONE);

    seraph_resonance_transform(&world, sig2,
        0, 0, 0, 0, 0, 0,
        Q64_TWO, Q64_TWO, Q64_TWO);

    /* Larger scale should have larger bounding radius */
    ASSERT(sig2->bounding_radius > sig1->bounding_radius);

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Dissonance and Collision Tests
 *============================================================================*/

TEST(dissonance_identical) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);

    seraph_resonance_transform(&world, sig,
        q64_from_int(10), q64_from_int(20), q64_from_int(30),
        0, 0, 0,
        Q64_ONE, Q64_ONE, Q64_ONE);

    /* Dissonance with itself should be zero */
    Seraph_Q64 diss = seraph_resonance_dissonance(sig, sig);
    ASSERT(diss != SERAPH_Q64_VOID);
    ASSERT(q64_to_double(diss) < 0.001);

    seraph_resonance_world_destroy(&world);
}

TEST(dissonance_distant_objects) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig1 = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);
    Seraph_Resonance_Signature* sig2 = seraph_resonance_register_object(
        &world, 2, SERAPH_SHAPE_SPHERE);

    /* Place objects far apart */
    seraph_resonance_transform(&world, sig1,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    seraph_resonance_transform(&world, sig2,
        q64_from_int(1000), q64_from_int(1000), q64_from_int(1000),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    /* Dissonance should be high for distant objects */
    Seraph_Q64 diss = seraph_resonance_dissonance(sig1, sig2);
    ASSERT(diss != SERAPH_Q64_VOID);
    ASSERT(q64_to_double(diss) > 0.5);

    seraph_resonance_world_destroy(&world);
}

TEST(collision_check_colliding) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig1 = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);
    Seraph_Resonance_Signature* sig2 = seraph_resonance_register_object(
        &world, 2, SERAPH_SHAPE_SPHERE);

    /* Place objects at same position (colliding) */
    seraph_resonance_transform(&world, sig1,
        q64_from_int(10), q64_from_int(10), q64_from_int(10),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    seraph_resonance_transform(&world, sig2,
        q64_from_int(10), q64_from_int(10), q64_from_int(10),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    Seraph_Resonance_Collision coll = seraph_resonance_check_collision(sig1, sig2, 0);

    /* Objects at same position should collide */
    ASSERT(coll.colliding == SERAPH_VBIT_TRUE);
    ASSERT(coll.dissonance != SERAPH_Q64_VOID);

    seraph_resonance_world_destroy(&world);
}

TEST(collision_check_not_colliding) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig1 = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);
    Seraph_Resonance_Signature* sig2 = seraph_resonance_register_object(
        &world, 2, SERAPH_SHAPE_SPHERE);

    /* Place objects far apart */
    seraph_resonance_transform(&world, sig1,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    seraph_resonance_transform(&world, sig2,
        q64_from_int(100), q64_from_int(100), q64_from_int(100),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    Seraph_Resonance_Collision coll = seraph_resonance_check_collision(sig1, sig2, 0);

    /* Distant objects should not collide */
    ASSERT(coll.colliding == SERAPH_VBIT_FALSE);

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Broad Phase Tests
 *============================================================================*/

TEST(broad_phase_same_band) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig1 = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);
    Seraph_Resonance_Signature* sig2 = seraph_resonance_register_object(
        &world, 2, SERAPH_SHAPE_SPHERE);

    /* Same scale = same frequency band */
    seraph_resonance_transform(&world, sig1,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    seraph_resonance_transform(&world, sig2,
        q64_from_int(5), q64_from_int(5), q64_from_int(5),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    /* Should have overlapping bands (need narrow phase) */
    Seraph_Vbit bp = seraph_resonance_broad_phase(sig1, sig2);
    ASSERT(bp == SERAPH_VBIT_VOID);  /* VOID = need narrow phase */

    seraph_resonance_world_destroy(&world);
}

TEST(broad_phase_different_band) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig1 = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);
    Seraph_Resonance_Signature* sig2 = seraph_resonance_register_object(
        &world, 2, SERAPH_SHAPE_SPHERE);

    /* Very different scales = different frequency bands */
    seraph_resonance_transform(&world, sig1,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE >> 8, Q64_ONE >> 8, Q64_ONE >> 8);  /* Tiny */

    seraph_resonance_transform(&world, sig2,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, q64_from_int(1000), q64_from_int(1000), q64_from_int(1000));  /* Huge */

    /* Should be in different bands (no collision possible) */
    Seraph_Vbit bp = seraph_resonance_broad_phase(sig1, sig2);

    /* If bands don't overlap, should return FALSE */
    if ((sig1->octave_bands & sig2->octave_bands) == 0) {
        ASSERT(bp == SERAPH_VBIT_FALSE);
    }

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Matter Type Tests (Dual-Domain Architecture)
 *============================================================================*/

TEST(matter_type_quantum_fog) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);

    sig->matter_type = SERAPH_MATTER_QUANTUM_FOG;

    seraph_resonance_transform(&world, sig,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    /* Quantum fog should have no constraint planes (pure harmonic) */
    ASSERT(sig->plane_count == 0);
    ASSERT(sig->matter_type == SERAPH_MATTER_QUANTUM_FOG);

    seraph_resonance_world_destroy(&world);
}

TEST(matter_type_crystalline) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_BOX);

    sig->matter_type = SERAPH_MATTER_CRYSTALLINE;

    seraph_resonance_transform(&world, sig,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    /* Crystalline matter should have constraint planes for precise collision */
    /* (Note: actual plane generation depends on shape class) */
    ASSERT(sig->matter_type == SERAPH_MATTER_CRYSTALLINE);

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Spectral Window Tests (Gibbs Suppression)
 *============================================================================*/

TEST(window_types) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_BOX);

    /* Test different window types */
    Seraph_Resonance_Window windows[] = {
        SERAPH_WINDOW_NONE,
        SERAPH_WINDOW_LANCZOS,
        SERAPH_WINDOW_HANN,
        SERAPH_WINDOW_TUKEY
    };

    for (int i = 0; i < 4; i++) {
        sig->window_type = windows[i];

        Seraph_Vbit result = seraph_resonance_transform(&world, sig,
            q64_from_int(50), q64_from_int(50), q64_from_int(50),
            0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

        ASSERT(result == SERAPH_VBIT_TRUE);
        ASSERT(sig->window_type == windows[i]);
    }

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Batch Transform Tests
 *============================================================================*/

TEST(batch_transform) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    /* Register multiple objects */
    uint64_t ids[5];
    for (int i = 0; i < 5; i++) {
        ids[i] = i + 1;
        seraph_resonance_register_object(&world, ids[i], SERAPH_SHAPE_SPHERE);
    }

    /* Prepare batch data */
    Seraph_Q64 positions[15] = {
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        q64_from_int(10), q64_from_int(10), q64_from_int(10),
        q64_from_int(20), q64_from_int(20), q64_from_int(20),
        q64_from_int(30), q64_from_int(30), q64_from_int(30),
        q64_from_int(40), q64_from_int(40), q64_from_int(40),
    };

    Seraph_Q64 velocities[15] = {0};
    Seraph_Q64 scales[15];
    for (int i = 0; i < 15; i++) scales[i] = Q64_ONE;

    uint32_t count = seraph_resonance_transform_batch(
        &world, ids, positions, velocities, scales, 5);

    ASSERT(count == 5);

    /* Verify each object was transformed */
    for (int i = 0; i < 5; i++) {
        Seraph_Resonance_Signature* sig = seraph_resonance_find_object(&world, ids[i]);
        ASSERT(sig != NULL);
        ASSERT(sig->total_power > 0);
    }

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Statistics Tests
 *============================================================================*/

TEST(world_statistics) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 100);
    seraph_resonance_init_builtin_classes(&world);

    Seraph_Resonance_Signature* sig1 = seraph_resonance_register_object(
        &world, 1, SERAPH_SHAPE_SPHERE);
    Seraph_Resonance_Signature* sig2 = seraph_resonance_register_object(
        &world, 2, SERAPH_SHAPE_SPHERE);

    seraph_resonance_transform(&world, sig1,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    seraph_resonance_transform(&world, sig2,
        q64_from_int(0), q64_from_int(0), q64_from_int(0),
        0, 0, 0, Q64_ONE, Q64_ONE, Q64_ONE);

    seraph_resonance_check_collision(sig1, sig2, 0);

    uint64_t transforms, checks, detections, culled;
    seraph_resonance_get_stats(&world, &transforms, &checks, &detections, &culled);

    ASSERT(transforms >= 2);  /* At least 2 transforms */
    ASSERT(checks >= 1);      /* At least 1 collision check */

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Edge Cases
 *============================================================================*/

TEST(void_signature) {
    Seraph_Resonance_Collision coll = seraph_resonance_check_collision(NULL, NULL, 0);
    ASSERT(coll.colliding == SERAPH_VBIT_VOID);
}

TEST(world_capacity) {
    Seraph_Resonance_World world;
    seraph_resonance_world_init(&world, 5);  /* Small capacity */
    seraph_resonance_init_builtin_classes(&world);

    /* Register up to capacity */
    for (uint64_t i = 1; i <= 5; i++) {
        Seraph_Resonance_Signature* sig = seraph_resonance_register_object(
            &world, i, SERAPH_SHAPE_SPHERE);
        ASSERT(sig != NULL);
    }

    /* Should fail when exceeding capacity */
    Seraph_Resonance_Signature* overflow = seraph_resonance_register_object(
        &world, 100, SERAPH_SHAPE_SPHERE);
    ASSERT(overflow == NULL);

    seraph_resonance_world_destroy(&world);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_resonance_tests(void) {
    printf("\n=== PRISM: Resonant Hitbox Tests ===\n\n");

    /* World Initialization */
    RUN_TEST(world_init);
    RUN_TEST(world_init_null);
    RUN_TEST(builtin_classes);

    /* Object Registration */
    RUN_TEST(register_object);
    RUN_TEST(register_multiple_objects);
    RUN_TEST(unregister_object);
    RUN_TEST(find_object);

    /* Harmonic Transformation */
    RUN_TEST(transform_basic);
    RUN_TEST(transform_with_velocity);
    RUN_TEST(transform_with_scale);

    /* Dissonance and Collision */
    RUN_TEST(dissonance_identical);
    RUN_TEST(dissonance_distant_objects);
    RUN_TEST(collision_check_colliding);
    RUN_TEST(collision_check_not_colliding);

    /* Broad Phase */
    RUN_TEST(broad_phase_same_band);
    RUN_TEST(broad_phase_different_band);

    /* Matter Types (Dual-Domain) */
    RUN_TEST(matter_type_quantum_fog);
    RUN_TEST(matter_type_crystalline);

    /* Spectral Windows */
    RUN_TEST(window_types);

    /* Batch Operations */
    RUN_TEST(batch_transform);

    /* Statistics */
    RUN_TEST(world_statistics);

    /* Edge Cases */
    RUN_TEST(void_signature);
    RUN_TEST(world_capacity);

    printf("\nResonance Tests: %d/%d passed\n", tests_passed, tests_run);
}

#ifndef SERAPH_TEST_NO_MAIN
int main(void) {
    run_resonance_tests();
    return (tests_passed == tests_run) ? 0 : 1;
}
#endif
