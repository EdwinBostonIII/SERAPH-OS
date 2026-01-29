/**
 * @file test_glyph.c
 * @brief Test suite for MC9: Glyph SDF Rendering
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "seraph/glyph.h"

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
#define ASSERT_NEAR(a, b, tol) ASSERT(fabs((a) - (b)) < (tol))

/*============================================================================
 * Helper Functions
 *============================================================================*/

static Seraph_Glyph_Point make_point(double x, double y) {
    return seraph_glyph_point_create(
        seraph_q128_from_double(x),
        seraph_q128_from_double(y)
    );
}

static double sdf_dist(Seraph_SDF_Result r) {
    return seraph_q128_to_double(r.distance);
}

/*============================================================================
 * Glyph Handle Tests
 *============================================================================*/

TEST(glyph_void) {
    ASSERT_TRUE(seraph_glyph_is_void(SERAPH_GLYPH_VOID));
    ASSERT_FALSE(seraph_glyph_exists(SERAPH_GLYPH_VOID));
    ASSERT_EQ(seraph_glyph_kind(SERAPH_GLYPH_VOID), SERAPH_GLYPH_KIND_VOID);
}

TEST(glyph_create) {
    Seraph_Glyph g = seraph_glyph_create(
        1,                          /* arena */
        SERAPH_GLYPH_KIND_CIRCLE,   /* kind */
        SERAPH_GLYPH_FLAG_VISIBLE | SERAPH_GLYPH_FLAG_INTERACTIVE,
        0,                          /* transform */
        42                          /* instance */
    );

    ASSERT_FALSE(seraph_glyph_is_void(g));
    ASSERT_TRUE(seraph_glyph_exists(g));
    ASSERT_EQ(seraph_glyph_arena(g), 1);
    ASSERT_EQ(seraph_glyph_kind(g), SERAPH_GLYPH_KIND_CIRCLE);
    ASSERT_EQ(seraph_glyph_instance(g), 42);
    ASSERT_TRUE(seraph_glyph_is_visible(g));
    ASSERT_TRUE(seraph_glyph_is_interactive(g));
}

TEST(glyph_flags) {
    Seraph_Glyph g = seraph_glyph_create(0, SERAPH_GLYPH_KIND_BOX, 0, 0, 0);

    ASSERT_FALSE(seraph_glyph_is_visible(g));

    g = seraph_glyph_add_flags(g, SERAPH_GLYPH_FLAG_VISIBLE);
    ASSERT_TRUE(seraph_glyph_is_visible(g));

    g = seraph_glyph_remove_flags(g, SERAPH_GLYPH_FLAG_VISIBLE);
    ASSERT_FALSE(seraph_glyph_is_visible(g));
}

TEST(glyph_void_state_flag) {
    Seraph_Glyph g = seraph_glyph_create(0, SERAPH_GLYPH_KIND_CIRCLE, 0, 0, 0);
    ASSERT_FALSE(seraph_glyph_is_void(g));

    g = seraph_glyph_add_flags(g, SERAPH_GLYPH_FLAG_VOID_STATE);
    ASSERT_TRUE(seraph_glyph_is_void(g));
}

/*============================================================================
 * Circle SDF Tests
 *============================================================================*/

TEST(sdf_circle_center) {
    /* Point at center of unit circle */
    Seraph_Glyph_Point p = make_point(0.0, 0.0);
    Seraph_SDF_Result r = seraph_sdf_circle(
        p,
        SERAPH_Q128_ZERO,  /* center_x */
        SERAPH_Q128_ZERO,  /* center_y */
        SERAPH_Q128_ONE    /* radius */
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_inside(r));
    ASSERT_NEAR(sdf_dist(r), -1.0, 1e-6);  /* Distance = -radius at center */
}

TEST(sdf_circle_edge) {
    /* Point on edge of unit circle */
    Seraph_Glyph_Point p = make_point(1.0, 0.0);
    Seraph_SDF_Result r = seraph_sdf_circle(
        p,
        SERAPH_Q128_ZERO,
        SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_NEAR(sdf_dist(r), 0.0, 1e-6);  /* Exactly on edge */

    /* Check gradient points outward */
    ASSERT_NEAR(seraph_q128_to_double(r.gradient_x), 1.0, 1e-6);
    ASSERT_NEAR(seraph_q128_to_double(r.gradient_y), 0.0, 1e-6);
}

TEST(sdf_circle_outside) {
    /* Point outside unit circle */
    Seraph_Glyph_Point p = make_point(2.0, 0.0);
    Seraph_SDF_Result r = seraph_sdf_circle(
        p,
        SERAPH_Q128_ZERO,
        SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_outside(r));
    ASSERT_NEAR(sdf_dist(r), 1.0, 1e-6);  /* Distance = 2 - 1 = 1 */
}

TEST(sdf_circle_offset_center) {
    /* Circle centered at (3, 4), point at origin */
    Seraph_Glyph_Point p = make_point(0.0, 0.0);
    Seraph_Q128 cx = seraph_q128_from_i64(3);
    Seraph_Q128 cy = seraph_q128_from_i64(4);
    Seraph_Q128 radius = seraph_q128_from_i64(5);

    Seraph_SDF_Result r = seraph_sdf_circle(p, cx, cy, radius);

    /* Distance from origin to (3,4) = 5, radius = 5, so on edge */
    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_NEAR(sdf_dist(r), 0.0, 1e-6);
}

/*============================================================================
 * Box SDF Tests
 *============================================================================*/

TEST(sdf_box_center) {
    /* Point at center of 2x2 box */
    Seraph_Glyph_Point p = make_point(0.0, 0.0);
    Seraph_SDF_Result r = seraph_sdf_box(p, SERAPH_Q128_ONE, SERAPH_Q128_ONE);

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_inside(r));
    ASSERT_NEAR(sdf_dist(r), -1.0, 1e-6);  /* Distance to nearest edge */
}

TEST(sdf_box_edge) {
    /* Point on edge of 2x2 box */
    Seraph_Glyph_Point p = make_point(1.0, 0.0);
    Seraph_SDF_Result r = seraph_sdf_box(p, SERAPH_Q128_ONE, SERAPH_Q128_ONE);

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_NEAR(sdf_dist(r), 0.0, 1e-6);
}

TEST(sdf_box_outside) {
    /* Point outside 2x2 box */
    Seraph_Glyph_Point p = make_point(2.0, 0.0);
    Seraph_SDF_Result r = seraph_sdf_box(p, SERAPH_Q128_ONE, SERAPH_Q128_ONE);

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_outside(r));
    ASSERT_NEAR(sdf_dist(r), 1.0, 1e-6);
}

TEST(sdf_box_corner) {
    /* Point outside corner of 2x2 box */
    Seraph_Glyph_Point p = make_point(2.0, 2.0);
    Seraph_SDF_Result r = seraph_sdf_box(p, SERAPH_Q128_ONE, SERAPH_Q128_ONE);

    /* Distance to corner (1,1) from (2,2) = sqrt(2) */
    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_outside(r));
    ASSERT_NEAR(sdf_dist(r), sqrt(2.0), 1e-6);
}

/*============================================================================
 * Rounded Box SDF Tests
 *============================================================================*/

TEST(sdf_rounded_box) {
    /* 2x2 box with 0.5 corner radius */
    Seraph_Glyph_Point p = make_point(1.5, 1.5);
    Seraph_Q128 radius = seraph_q128_from_double(0.5);
    Seraph_SDF_Result r = seraph_sdf_rounded_box(p, SERAPH_Q128_ONE, SERAPH_Q128_ONE, radius);

    /* At corner, should be approximately distance to rounded corner */
    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_outside(r));
}

/*============================================================================
 * Line SDF Tests
 *============================================================================*/

TEST(sdf_line_on_line) {
    /* Point on line from (0,0) to (1,0) */
    Seraph_Glyph_Point p = make_point(0.5, 0.0);
    Seraph_Q128 thick = seraph_q128_from_double(0.1);
    Seraph_SDF_Result r = seraph_sdf_line(
        p,
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,  /* start */
        SERAPH_Q128_ONE, SERAPH_Q128_ZERO,   /* end */
        thick
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_inside(r));
    ASSERT_NEAR(sdf_dist(r), -0.05, 1e-6);  /* Inside by half thickness */
}

TEST(sdf_line_perpendicular) {
    /* Point perpendicular to line */
    Seraph_Glyph_Point p = make_point(0.5, 0.5);
    Seraph_Q128 thick = seraph_q128_from_double(0.0);  /* Zero thickness = line */
    Seraph_SDF_Result r = seraph_sdf_line(
        p,
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE, SERAPH_Q128_ZERO,
        thick
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_NEAR(sdf_dist(r), 0.5, 1e-6);  /* Distance = 0.5 */
}

TEST(sdf_line_endpoint) {
    /* Point past line endpoint */
    Seraph_Glyph_Point p = make_point(2.0, 0.0);
    Seraph_Q128 thick = seraph_q128_from_double(0.0);
    Seraph_SDF_Result r = seraph_sdf_line(
        p,
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE, SERAPH_Q128_ZERO,
        thick
    );

    /* Distance to endpoint (1,0) = 1.0 */
    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_NEAR(sdf_dist(r), 1.0, 1e-6);
}

/*============================================================================
 * Ring SDF Tests
 *============================================================================*/

TEST(sdf_ring_on_ring) {
    /* Point on center of ring */
    Seraph_Glyph_Point p = make_point(1.0, 0.0);
    Seraph_Q128 radius = SERAPH_Q128_ONE;
    Seraph_Q128 thick = seraph_q128_from_double(0.2);
    Seraph_SDF_Result r = seraph_sdf_ring(
        p,
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        radius, thick
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_inside(r));
    ASSERT_NEAR(sdf_dist(r), -0.1, 1e-6);  /* Inside by half thickness */
}

TEST(sdf_ring_center) {
    /* Point at center (inside hole) */
    Seraph_Glyph_Point p = make_point(0.0, 0.0);
    Seraph_Q128 radius = SERAPH_Q128_ONE;
    Seraph_Q128 thick = seraph_q128_from_double(0.2);
    Seraph_SDF_Result r = seraph_sdf_ring(
        p,
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        radius, thick
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_outside(r));
    /* Distance = |0 - 1| - 0.1 = 0.9 */
    ASSERT_NEAR(sdf_dist(r), 0.9, 1e-6);
}

/*============================================================================
 * Triangle SDF Tests
 *============================================================================*/

TEST(sdf_triangle_inside) {
    /* Point inside equilateral triangle */
    Seraph_Glyph_Point p = make_point(0.0, 0.3);
    Seraph_SDF_Result r = seraph_sdf_triangle(
        p,
        SERAPH_Q128_ZERO, SERAPH_Q128_ONE,           /* top */
        seraph_q128_from_double(-0.866), seraph_q128_from_double(-0.5),  /* bottom-left */
        seraph_q128_from_double(0.866), seraph_q128_from_double(-0.5)    /* bottom-right */
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_inside(r));
}

TEST(sdf_triangle_outside) {
    /* Point far outside triangle */
    Seraph_Glyph_Point p = make_point(5.0, 5.0);
    Seraph_SDF_Result r = seraph_sdf_triangle(
        p,
        SERAPH_Q128_ZERO, SERAPH_Q128_ONE,
        seraph_q128_from_double(-0.866), seraph_q128_from_double(-0.5),
        seraph_q128_from_double(0.866), seraph_q128_from_double(-0.5)
    );

    ASSERT_FALSE(seraph_sdf_is_void(r));
    ASSERT_TRUE(seraph_sdf_is_outside(r));
}

/*============================================================================
 * Boolean Operation Tests
 *============================================================================*/

TEST(sdf_union) {
    /* Two circles, one at origin, one at (1,0) */
    Seraph_Glyph_Point p = make_point(0.5, 0.0);

    Seraph_SDF_Result c1 = seraph_sdf_circle(p, SERAPH_Q128_ZERO, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);
    Seraph_SDF_Result c2 = seraph_sdf_circle(p, SERAPH_Q128_ONE, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    Seraph_SDF_Result u = seraph_sdf_union(c1, c2);

    /* Point is inside both circles, union should be inside */
    ASSERT_FALSE(seraph_sdf_is_void(u));
    ASSERT_TRUE(seraph_sdf_is_inside(u));

    /* Distance should be min of both */
    double d1 = sdf_dist(c1);
    double d2 = sdf_dist(c2);
    ASSERT_NEAR(sdf_dist(u), fmin(d1, d2), 1e-6);
}

TEST(sdf_intersect) {
    /* Two overlapping circles */
    Seraph_Glyph_Point p = make_point(0.5, 0.0);

    Seraph_SDF_Result c1 = seraph_sdf_circle(p, SERAPH_Q128_ZERO, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);
    Seraph_SDF_Result c2 = seraph_sdf_circle(p, SERAPH_Q128_ONE, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    Seraph_SDF_Result i = seraph_sdf_intersect(c1, c2);

    /* Point is in overlap, intersection should be inside */
    ASSERT_FALSE(seraph_sdf_is_void(i));
    ASSERT_TRUE(seraph_sdf_is_inside(i));

    /* Distance should be max of both */
    double d1 = sdf_dist(c1);
    double d2 = sdf_dist(c2);
    ASSERT_NEAR(sdf_dist(i), fmax(d1, d2), 1e-6);
}

TEST(sdf_subtract) {
    /* Circle with smaller circle subtracted */
    Seraph_Glyph_Point p = make_point(0.0, 0.0);

    Seraph_Q128 big_r = seraph_q128_from_i64(2);
    Seraph_SDF_Result big = seraph_sdf_circle(p, SERAPH_Q128_ZERO, SERAPH_Q128_ZERO, big_r);
    Seraph_SDF_Result small = seraph_sdf_circle(p, SERAPH_Q128_ZERO, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    Seraph_SDF_Result sub = seraph_sdf_subtract(big, small);

    /* Point at center: inside big, inside small, so outside subtraction */
    ASSERT_FALSE(seraph_sdf_is_void(sub));
    ASSERT_TRUE(seraph_sdf_is_outside(sub));
}

TEST(sdf_void_propagation_union) {
    Seraph_SDF_Result v = SERAPH_SDF_VOID;
    Seraph_SDF_Result c = seraph_sdf_circle(make_point(0,0), SERAPH_Q128_ZERO, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    /* Union with VOID should return the non-void operand */
    Seraph_SDF_Result u1 = seraph_sdf_union(v, c);
    Seraph_SDF_Result u2 = seraph_sdf_union(c, v);

    ASSERT_FALSE(seraph_sdf_is_void(u1));
    ASSERT_FALSE(seraph_sdf_is_void(u2));
}

TEST(sdf_void_propagation_intersect) {
    Seraph_SDF_Result v = SERAPH_SDF_VOID;
    Seraph_SDF_Result c = seraph_sdf_circle(make_point(0,0), SERAPH_Q128_ZERO, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    /* Intersection with VOID should return VOID */
    Seraph_SDF_Result i1 = seraph_sdf_intersect(v, c);
    Seraph_SDF_Result i2 = seraph_sdf_intersect(c, v);

    ASSERT_TRUE(seraph_sdf_is_void(i1));
    ASSERT_TRUE(seraph_sdf_is_void(i2));
}

/*============================================================================
 * Smooth Boolean Tests
 *============================================================================*/

TEST(sdf_smooth_union) {
    /* Two circles with smooth blend */
    Seraph_Glyph_Point p = make_point(0.5, 0.0);

    Seraph_SDF_Result c1 = seraph_sdf_circle(p, SERAPH_Q128_ZERO, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);
    Seraph_SDF_Result c2 = seraph_sdf_circle(p, SERAPH_Q128_ONE, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    Seraph_Q128 k = seraph_q128_from_double(0.5);  /* Blend radius */
    Seraph_SDF_Result su = seraph_sdf_smooth_union(c1, c2, k);

    ASSERT_FALSE(seraph_sdf_is_void(su));
    ASSERT_TRUE(seraph_sdf_is_inside(su));

    /* Smooth union distance should be less than hard union */
    Seraph_SDF_Result hu = seraph_sdf_union(c1, c2);
    ASSERT(sdf_dist(su) <= sdf_dist(hu) + 1e-6);
}

/*============================================================================
 * Coverage and Anti-Aliasing Tests
 *============================================================================*/

TEST(coverage_inside) {
    Seraph_SDF_Result r = {
        .distance = seraph_q128_from_double(-1.0),  /* Deep inside */
        .gradient_x = SERAPH_Q128_ONE,
        .gradient_y = SERAPH_Q128_ZERO,
        .curvature = SERAPH_Q128_ZERO
    };

    Seraph_Q128 pixel = seraph_q128_from_double(0.1);
    Seraph_Q128 coverage = seraph_glyph_coverage(r, pixel);

    /* Deep inside = full coverage */
    ASSERT_FALSE(seraph_q128_is_void(coverage));
    ASSERT_NEAR(seraph_q128_to_double(coverage), 1.0, 1e-3);
}

TEST(coverage_outside) {
    Seraph_SDF_Result r = {
        .distance = seraph_q128_from_double(1.0),  /* Far outside */
        .gradient_x = SERAPH_Q128_ONE,
        .gradient_y = SERAPH_Q128_ZERO,
        .curvature = SERAPH_Q128_ZERO
    };

    Seraph_Q128 pixel = seraph_q128_from_double(0.1);
    Seraph_Q128 coverage = seraph_glyph_coverage(r, pixel);

    /* Far outside = zero coverage */
    ASSERT_FALSE(seraph_q128_is_void(coverage));
    ASSERT_NEAR(seraph_q128_to_double(coverage), 0.0, 1e-3);
}

TEST(coverage_edge) {
    Seraph_SDF_Result r = {
        .distance = SERAPH_Q128_ZERO,  /* Exactly on edge */
        .gradient_x = SERAPH_Q128_ONE,
        .gradient_y = SERAPH_Q128_ZERO,
        .curvature = SERAPH_Q128_ZERO
    };

    Seraph_Q128 pixel = seraph_q128_from_double(0.1);
    Seraph_Q128 coverage = seraph_glyph_coverage(r, pixel);

    /* On edge = ~50% coverage */
    ASSERT_FALSE(seraph_q128_is_void(coverage));
    ASSERT_NEAR(seraph_q128_to_double(coverage), 0.5, 0.1);
}

TEST(alpha_from_sdf) {
    /*
     * Test point exactly on the edge where alpha should be ~0.5
     * Distance = 0 means exactly on boundary, so coverage ~ 0.5
     */
    Seraph_SDF_Result r = seraph_sdf_circle(
        make_point(1.0, 0.0),  /* Exactly on edge */
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    double alpha = seraph_glyph_alpha(r, 0.1);

    /* On edge = partial alpha near 0.5 */
    ASSERT(alpha > 0.3 && alpha < 0.7);
}

/*============================================================================
 * Hit Testing Tests
 *============================================================================*/

TEST(hit_test_inside) {
    Seraph_SDF_Result r = seraph_sdf_circle(
        make_point(0.0, 0.0),
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    ASSERT_TRUE(seraph_glyph_hit_test(r));
}

TEST(hit_test_outside) {
    Seraph_SDF_Result r = seraph_sdf_circle(
        make_point(2.0, 0.0),
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    ASSERT_FALSE(seraph_glyph_hit_test(r));
}

/*============================================================================
 * Distance and Normal Tests
 *============================================================================*/

TEST(distance_query) {
    Seraph_SDF_Result r = seraph_sdf_circle(
        make_point(2.0, 0.0),
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    double d = seraph_glyph_distance(r);
    ASSERT_NEAR(d, 1.0, 1e-6);
}

TEST(distance_void) {
    double d = seraph_glyph_distance(SERAPH_SDF_VOID);
    ASSERT(isinf(d) && d > 0);  /* Positive infinity */
}

TEST(normal_query) {
    Seraph_SDF_Result r = seraph_sdf_circle(
        make_point(1.0, 0.0),
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    double nx, ny;
    seraph_glyph_normal(r, &nx, &ny);

    ASSERT_NEAR(nx, 1.0, 1e-6);
    ASSERT_NEAR(ny, 0.0, 1e-6);
}

/*============================================================================
 * Transformation Tests
 *============================================================================*/

TEST(sdf_negate) {
    Seraph_SDF_Result r = seraph_sdf_circle(
        make_point(0.0, 0.0),
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    /* Inside circle */
    ASSERT_TRUE(seraph_sdf_is_inside(r));

    Seraph_SDF_Result neg = seraph_sdf_negate(r);

    /* After negation, outside */
    ASSERT_TRUE(seraph_sdf_is_outside(neg));
}

TEST(sdf_offset) {
    Seraph_SDF_Result r = seraph_sdf_circle(
        make_point(0.5, 0.0),
        SERAPH_Q128_ZERO, SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    double orig_dist = sdf_dist(r);

    /* Expand by 0.5 */
    Seraph_SDF_Result expanded = seraph_sdf_offset(r, seraph_q128_from_double(0.5));
    double exp_dist = sdf_dist(expanded);

    /* Distance decreased by offset amount */
    ASSERT_NEAR(exp_dist, orig_dist - 0.5, 1e-6);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_glyph_tests(void) {
    printf("\n=== MC9: Glyph SDF Rendering Tests ===\n\n");

    printf("Glyph Handle Tests:\n");
    RUN_TEST(glyph_void);
    RUN_TEST(glyph_create);
    RUN_TEST(glyph_flags);
    RUN_TEST(glyph_void_state_flag);

    printf("\nCircle SDF Tests:\n");
    RUN_TEST(sdf_circle_center);
    RUN_TEST(sdf_circle_edge);
    RUN_TEST(sdf_circle_outside);
    RUN_TEST(sdf_circle_offset_center);

    printf("\nBox SDF Tests:\n");
    RUN_TEST(sdf_box_center);
    RUN_TEST(sdf_box_edge);
    RUN_TEST(sdf_box_outside);
    RUN_TEST(sdf_box_corner);

    printf("\nRounded Box SDF Tests:\n");
    RUN_TEST(sdf_rounded_box);

    printf("\nLine SDF Tests:\n");
    RUN_TEST(sdf_line_on_line);
    RUN_TEST(sdf_line_perpendicular);
    RUN_TEST(sdf_line_endpoint);

    printf("\nRing SDF Tests:\n");
    RUN_TEST(sdf_ring_on_ring);
    RUN_TEST(sdf_ring_center);

    printf("\nTriangle SDF Tests:\n");
    RUN_TEST(sdf_triangle_inside);
    RUN_TEST(sdf_triangle_outside);

    printf("\nBoolean Operation Tests:\n");
    RUN_TEST(sdf_union);
    RUN_TEST(sdf_intersect);
    RUN_TEST(sdf_subtract);
    RUN_TEST(sdf_void_propagation_union);
    RUN_TEST(sdf_void_propagation_intersect);

    printf("\nSmooth Boolean Tests:\n");
    RUN_TEST(sdf_smooth_union);

    printf("\nCoverage/Anti-Aliasing Tests:\n");
    RUN_TEST(coverage_inside);
    RUN_TEST(coverage_outside);
    RUN_TEST(coverage_edge);
    RUN_TEST(alpha_from_sdf);

    printf("\nHit Testing Tests:\n");
    RUN_TEST(hit_test_inside);
    RUN_TEST(hit_test_outside);

    printf("\nDistance/Normal Tests:\n");
    RUN_TEST(distance_query);
    RUN_TEST(distance_void);
    RUN_TEST(normal_query);

    printf("\nTransformation Tests:\n");
    RUN_TEST(sdf_negate);
    RUN_TEST(sdf_offset);

    printf("\nGlyph Tests: %d/%d passed\n", tests_passed, tests_run);
}
