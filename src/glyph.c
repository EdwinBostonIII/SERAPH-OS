/**
 * @file glyph.c
 * @brief MC9: The Glyph - Hyper-Dual SDF Rendering Implementation
 *
 * Infinite-resolution graphics through Signed Distance Fields.
 * Store the EQUATION, not the pixels.
 *
 * SERAPH PHILOSOPHY:
 * - Every shape is a mathematical function: f(x,y) → distance
 * - Negative distance = inside, positive = outside, zero = on edge
 * - Galactic numbers give automatic gradients for perfect anti-aliasing
 * - VOID propagates through all operations
 */

#include "seraph/glyph.h"
#include <math.h>

/*============================================================================
 * Helper Constants
 *============================================================================*/

/** Half for blending calculations */
static const Seraph_Q128 Q128_HALF = { 0, 0x8000000000000000ULL };

/*============================================================================
 * Primitive SDF Functions
 *============================================================================*/

/**
 * @brief Circle SDF: d = |p - center| - radius
 *
 * The simplest and most elegant SDF. A circle is just
 * "distance from center minus radius."
 */
Seraph_SDF_Result seraph_sdf_circle(
    Seraph_Glyph_Point p,
    Seraph_Q128 center_x,
    Seraph_Q128 center_y,
    Seraph_Q128 radius
) {
    /* Check for VOID inputs */
    if (seraph_q128_is_void(center_x) || seraph_q128_is_void(center_y) ||
        seraph_q128_is_void(radius) || seraph_q128_is_void(p.x.primal) ||
        seraph_q128_is_void(p.y.primal)) {
        return SERAPH_SDF_VOID;
    }

    /* dx = p.x - center_x */
    Seraph_Galactic dx = seraph_galactic_sub(p.x, seraph_galactic_constant(center_x));

    /* dy = p.y - center_y */
    Seraph_Galactic dy = seraph_galactic_sub(p.y, seraph_galactic_constant(center_y));

    /* dist_sq = dx² + dy² */
    Seraph_Galactic dist_sq = seraph_galactic_add(
        seraph_galactic_mul(dx, dx),
        seraph_galactic_mul(dy, dy)
    );

    /* dist = sqrt(dist_sq) */
    Seraph_Galactic dist = seraph_galactic_sqrt(dist_sq);

    /* Handle zero distance (point at center) */
    if (seraph_q128_is_zero(dist.primal)) {
        /* At center: distance is -radius, gradient is undefined (use (0,0)) */
        return (Seraph_SDF_Result){
            .distance   = seraph_q128_neg(radius),
            .gradient_x = SERAPH_Q128_ZERO,
            .gradient_y = SERAPH_Q128_ZERO,
            .curvature  = seraph_q128_div(SERAPH_Q128_ONE, radius)
        };
    }

    /* distance = dist - radius */
    Seraph_Q128 distance = seraph_q128_sub(dist.primal, radius);

    /* Gradient = normalized direction = (dx, dy) / dist */
    Seraph_Q128 inv_dist = seraph_q128_div(SERAPH_Q128_ONE, dist.primal);
    Seraph_Q128 grad_x = seraph_q128_mul(dx.primal, inv_dist);
    Seraph_Q128 grad_y = seraph_q128_mul(dy.primal, inv_dist);

    /* Curvature = 1/radius for a circle (constant everywhere) */
    Seraph_Q128 curvature = seraph_q128_div(SERAPH_Q128_ONE, radius);

    return (Seraph_SDF_Result){
        .distance   = distance,
        .gradient_x = grad_x,
        .gradient_y = grad_y,
        .curvature  = curvature
    };
}

/**
 * @brief Box SDF: distance to axis-aligned rectangle
 *
 * Uses the "fold to first quadrant" trick: exploit symmetry
 * to simplify the calculation.
 */
Seraph_SDF_Result seraph_sdf_box(
    Seraph_Glyph_Point p,
    Seraph_Q128 half_width,
    Seraph_Q128 half_height
) {
    /* Check for VOID inputs */
    if (seraph_q128_is_void(half_width) || seraph_q128_is_void(half_height) ||
        seraph_q128_is_void(p.x.primal) || seraph_q128_is_void(p.y.primal)) {
        return SERAPH_SDF_VOID;
    }

    /* Fold to first quadrant: take absolute values */
    Seraph_Q128 ax = seraph_q128_abs(p.x.primal);
    Seraph_Q128 ay = seraph_q128_abs(p.y.primal);

    /* Distance to box edges (negative = inside) */
    Seraph_Q128 dx = seraph_q128_sub(ax, half_width);
    Seraph_Q128 dy = seraph_q128_sub(ay, half_height);

    /* Outside distance: clamp negatives to zero, compute length */
    Seraph_Q128 ox = seraph_q128_max(dx, SERAPH_Q128_ZERO);
    Seraph_Q128 oy = seraph_q128_max(dy, SERAPH_Q128_ZERO);
    Seraph_Q128 outside_dist = seraph_q128_sqrt(
        seraph_q128_add(
            seraph_q128_mul(ox, ox),
            seraph_q128_mul(oy, oy)
        )
    );

    /* Inside distance: max of negative components (closest edge) */
    Seraph_Q128 inside_dist = seraph_q128_min(
        seraph_q128_max(dx, dy),
        SERAPH_Q128_ZERO
    );

    /* Total distance */
    Seraph_Q128 distance = seraph_q128_add(outside_dist, inside_dist);

    /* Gradient: depends on which edge/corner we're nearest */
    Seraph_Q128 grad_x, grad_y;

    bool outside_x = dx.hi > 0 || (dx.hi == 0 && dx.lo > 0);
    bool outside_y = dy.hi > 0 || (dy.hi == 0 && dy.lo > 0);

    if (outside_x && outside_y) {
        /* Corner: gradient points to corner */
        Seraph_Q128 corner_dist = outside_dist;
        if (!seraph_q128_is_zero(corner_dist)) {
            Seraph_Q128 inv = seraph_q128_div(SERAPH_Q128_ONE, corner_dist);
            grad_x = seraph_q128_mul(ox, inv);
            grad_y = seraph_q128_mul(oy, inv);
        } else {
            grad_x = SERAPH_Q128_ZERO;
            grad_y = SERAPH_Q128_ZERO;
        }
    } else if (outside_x) {
        /* Outside on X edge only */
        grad_x = SERAPH_Q128_ONE;
        grad_y = SERAPH_Q128_ZERO;
    } else if (outside_y) {
        /* Outside on Y edge only */
        grad_x = SERAPH_Q128_ZERO;
        grad_y = SERAPH_Q128_ONE;
    } else {
        /* Inside: gradient points to nearest edge */
        if (seraph_q128_gt(dx, dy)) {
            grad_x = SERAPH_Q128_ONE;
            grad_y = SERAPH_Q128_ZERO;
        } else {
            grad_x = SERAPH_Q128_ZERO;
            grad_y = SERAPH_Q128_ONE;
        }
    }

    /* Apply original sign (we folded to first quadrant) */
    if (p.x.primal.hi < 0) {
        grad_x = seraph_q128_neg(grad_x);
    }
    if (p.y.primal.hi < 0) {
        grad_y = seraph_q128_neg(grad_y);
    }

    return (Seraph_SDF_Result){
        .distance   = distance,
        .gradient_x = grad_x,
        .gradient_y = grad_y,
        .curvature  = SERAPH_Q128_ZERO  /* Flat edges */
    };
}

/**
 * @brief Rounded Box SDF: box with rounded corners
 *
 * Simply shrink the box and then expand the SDF by the corner radius.
 */
Seraph_SDF_Result seraph_sdf_rounded_box(
    Seraph_Glyph_Point p,
    Seraph_Q128 half_width,
    Seraph_Q128 half_height,
    Seraph_Q128 corner_radius
) {
    /* Check for VOID inputs */
    if (seraph_q128_is_void(corner_radius)) {
        return SERAPH_SDF_VOID;
    }

    /* Shrink box by corner radius */
    Seraph_Q128 inner_hw = seraph_q128_sub(half_width, corner_radius);
    Seraph_Q128 inner_hh = seraph_q128_sub(half_height, corner_radius);

    /* Clamp to non-negative (handle case where radius > half_width/height) */
    inner_hw = seraph_q128_max(inner_hw, SERAPH_Q128_ZERO);
    inner_hh = seraph_q128_max(inner_hh, SERAPH_Q128_ZERO);

    /* Get SDF of inner box */
    Seraph_SDF_Result inner = seraph_sdf_box(p, inner_hw, inner_hh);
    if (seraph_sdf_is_void(inner)) {
        return SERAPH_SDF_VOID;
    }

    /* Expand by corner radius */
    inner.distance = seraph_q128_sub(inner.distance, corner_radius);

    /* Curvature at corners is 1/radius, edges are flat */
    /* Simplified: just use corner curvature */
    inner.curvature = seraph_q128_div(SERAPH_Q128_ONE, corner_radius);

    return inner;
}

/**
 * @brief Line segment SDF: distance to a line segment
 *
 * Uses projection to find closest point on segment.
 */
Seraph_SDF_Result seraph_sdf_line(
    Seraph_Glyph_Point p,
    Seraph_Q128 x1, Seraph_Q128 y1,
    Seraph_Q128 x2, Seraph_Q128 y2,
    Seraph_Q128 thickness
) {
    /* Check for VOID inputs */
    if (seraph_q128_is_void(x1) || seraph_q128_is_void(y1) ||
        seraph_q128_is_void(x2) || seraph_q128_is_void(y2) ||
        seraph_q128_is_void(thickness) || seraph_q128_is_void(p.x.primal) ||
        seraph_q128_is_void(p.y.primal)) {
        return SERAPH_SDF_VOID;
    }

    /* Line direction: v = (x2-x1, y2-y1) */
    Seraph_Q128 vx = seraph_q128_sub(x2, x1);
    Seraph_Q128 vy = seraph_q128_sub(y2, y1);

    /* Point relative to line start: w = (p - p1) */
    Seraph_Q128 wx = seraph_q128_sub(p.x.primal, x1);
    Seraph_Q128 wy = seraph_q128_sub(p.y.primal, y1);

    /* Length squared of line segment */
    Seraph_Q128 len_sq = seraph_q128_add(
        seraph_q128_mul(vx, vx),
        seraph_q128_mul(vy, vy)
    );

    /* Handle degenerate line (point) */
    if (seraph_q128_is_zero(len_sq)) {
        Seraph_Q128 dist_sq = seraph_q128_add(
            seraph_q128_mul(wx, wx),
            seraph_q128_mul(wy, wy)
        );
        Seraph_Q128 dist = seraph_q128_sqrt(dist_sq);
        Seraph_Q128 half_thick = seraph_q128_div(thickness, seraph_q128_from_i64(2));

        return (Seraph_SDF_Result){
            .distance   = seraph_q128_sub(dist, half_thick),
            .gradient_x = seraph_q128_is_zero(dist) ? SERAPH_Q128_ZERO :
                          seraph_q128_div(wx, dist),
            .gradient_y = seraph_q128_is_zero(dist) ? SERAPH_Q128_ZERO :
                          seraph_q128_div(wy, dist),
            .curvature  = SERAPH_Q128_ZERO
        };
    }

    /* Projection parameter: t = dot(w, v) / dot(v, v) */
    Seraph_Q128 dot_wv = seraph_q128_add(
        seraph_q128_mul(wx, vx),
        seraph_q128_mul(wy, vy)
    );
    Seraph_Q128 t = seraph_q128_div(dot_wv, len_sq);

    /* Clamp t to [0, 1] */
    t = seraph_q128_clamp(t, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    /* Closest point on segment: c = p1 + t * v */
    Seraph_Q128 cx = seraph_q128_add(x1, seraph_q128_mul(t, vx));
    Seraph_Q128 cy = seraph_q128_add(y1, seraph_q128_mul(t, vy));

    /* Distance from p to closest point */
    Seraph_Q128 dx = seraph_q128_sub(p.x.primal, cx);
    Seraph_Q128 dy = seraph_q128_sub(p.y.primal, cy);
    Seraph_Q128 dist_sq = seraph_q128_add(
        seraph_q128_mul(dx, dx),
        seraph_q128_mul(dy, dy)
    );
    Seraph_Q128 dist = seraph_q128_sqrt(dist_sq);

    /* Half thickness for capsule-style line */
    Seraph_Q128 half_thick = seraph_q128_div(thickness, seraph_q128_from_i64(2));

    /* Gradient: normalized direction from closest point */
    Seraph_Q128 grad_x, grad_y;
    if (seraph_q128_is_zero(dist)) {
        /* On the line: use perpendicular */
        Seraph_Q128 len = seraph_q128_sqrt(len_sq);
        grad_x = seraph_q128_div(seraph_q128_neg(vy), len);
        grad_y = seraph_q128_div(vx, len);
    } else {
        Seraph_Q128 inv_dist = seraph_q128_div(SERAPH_Q128_ONE, dist);
        grad_x = seraph_q128_mul(dx, inv_dist);
        grad_y = seraph_q128_mul(dy, inv_dist);
    }

    return (Seraph_SDF_Result){
        .distance   = seraph_q128_sub(dist, half_thick),
        .gradient_x = grad_x,
        .gradient_y = grad_y,
        .curvature  = SERAPH_Q128_ZERO  /* Straight line */
    };
}

/**
 * @brief Ring (annulus) SDF: donut shape
 *
 * d = ||p| - r| - thickness/2
 */
Seraph_SDF_Result seraph_sdf_ring(
    Seraph_Glyph_Point p,
    Seraph_Q128 center_x,
    Seraph_Q128 center_y,
    Seraph_Q128 radius,
    Seraph_Q128 thickness
) {
    /* Check for VOID inputs */
    if (seraph_q128_is_void(center_x) || seraph_q128_is_void(center_y) ||
        seraph_q128_is_void(radius) || seraph_q128_is_void(thickness) ||
        seraph_q128_is_void(p.x.primal) || seraph_q128_is_void(p.y.primal)) {
        return SERAPH_SDF_VOID;
    }

    /* Get circle SDF first */
    Seraph_SDF_Result circle = seraph_sdf_circle(p, center_x, center_y, radius);
    if (seraph_sdf_is_void(circle)) {
        return SERAPH_SDF_VOID;
    }

    /* Ring = take absolute of circle distance, subtract half thickness */
    Seraph_Q128 half_thick = seraph_q128_div(thickness, seraph_q128_from_i64(2));
    Seraph_Q128 ring_dist = seraph_q128_sub(
        seraph_q128_abs(circle.distance),
        half_thick
    );

    /* Gradient flips if we were inside the center circle */
    if (circle.distance.hi < 0) {
        circle.gradient_x = seraph_q128_neg(circle.gradient_x);
        circle.gradient_y = seraph_q128_neg(circle.gradient_y);
    }

    return (Seraph_SDF_Result){
        .distance   = ring_dist,
        .gradient_x = circle.gradient_x,
        .gradient_y = circle.gradient_y,
        .curvature  = circle.curvature
    };
}

/**
 * @brief Triangle SDF: distance to filled triangle
 *
 * Uses edge tests and closest-point calculation.
 */
Seraph_SDF_Result seraph_sdf_triangle(
    Seraph_Glyph_Point p,
    Seraph_Q128 x1, Seraph_Q128 y1,
    Seraph_Q128 x2, Seraph_Q128 y2,
    Seraph_Q128 x3, Seraph_Q128 y3
) {
    /* Check for VOID inputs */
    if (seraph_q128_is_void(x1) || seraph_q128_is_void(y1) ||
        seraph_q128_is_void(x2) || seraph_q128_is_void(y2) ||
        seraph_q128_is_void(x3) || seraph_q128_is_void(y3) ||
        seraph_q128_is_void(p.x.primal) || seraph_q128_is_void(p.y.primal)) {
        return SERAPH_SDF_VOID;
    }

    /* Get distances to all three edges */
    Seraph_SDF_Result e1 = seraph_sdf_line(p, x1, y1, x2, y2, SERAPH_Q128_ZERO);
    Seraph_SDF_Result e2 = seraph_sdf_line(p, x2, y2, x3, y3, SERAPH_Q128_ZERO);
    Seraph_SDF_Result e3 = seraph_sdf_line(p, x3, y3, x1, y1, SERAPH_Q128_ZERO);

    /* Find minimum distance (closest edge) */
    Seraph_SDF_Result result = e1;
    if (seraph_q128_lt(seraph_q128_abs(e2.distance), seraph_q128_abs(result.distance))) {
        result = e2;
    }
    if (seraph_q128_lt(seraph_q128_abs(e3.distance), seraph_q128_abs(result.distance))) {
        result = e3;
    }

    /* Determine if inside using cross products (winding) */
    /* Edge 1-2 */
    Seraph_Q128 c1 = seraph_q128_sub(
        seraph_q128_mul(seraph_q128_sub(x2, x1), seraph_q128_sub(p.y.primal, y1)),
        seraph_q128_mul(seraph_q128_sub(y2, y1), seraph_q128_sub(p.x.primal, x1))
    );
    /* Edge 2-3 */
    Seraph_Q128 c2 = seraph_q128_sub(
        seraph_q128_mul(seraph_q128_sub(x3, x2), seraph_q128_sub(p.y.primal, y2)),
        seraph_q128_mul(seraph_q128_sub(y3, y2), seraph_q128_sub(p.x.primal, x2))
    );
    /* Edge 3-1 */
    Seraph_Q128 c3 = seraph_q128_sub(
        seraph_q128_mul(seraph_q128_sub(x1, x3), seraph_q128_sub(p.y.primal, y3)),
        seraph_q128_mul(seraph_q128_sub(y1, y3), seraph_q128_sub(p.x.primal, x3))
    );

    /* Inside if all cross products have same sign */
    bool all_pos = (c1.hi >= 0) && (c2.hi >= 0) && (c3.hi >= 0);
    bool all_neg = (c1.hi <= 0) && (c2.hi <= 0) && (c3.hi <= 0);
    bool inside = all_pos || all_neg;

    /* If inside, negate the distance */
    if (inside) {
        result.distance = seraph_q128_neg(result.distance);
        result.gradient_x = seraph_q128_neg(result.gradient_x);
        result.gradient_y = seraph_q128_neg(result.gradient_y);
    }

    return result;
}

/*============================================================================
 * Boolean SDF Operations
 *============================================================================*/

/* Note: seraph_sdf_negate is defined as static inline in glyph.h */

/**
 * @brief Union: min(A, B) - combined shapes
 */
Seraph_SDF_Result seraph_sdf_union(Seraph_SDF_Result a, Seraph_SDF_Result b) {
    /* VOID propagation: Union(A, Void) = A */
    if (seraph_sdf_is_void(a)) return b;
    if (seraph_sdf_is_void(b)) return a;

    if (seraph_q128_lt(a.distance, b.distance)) {
        return a;
    }
    return b;
}

/**
 * @brief Intersection: max(A, B) - shared region only
 */
Seraph_SDF_Result seraph_sdf_intersect(Seraph_SDF_Result a, Seraph_SDF_Result b) {
    /* VOID propagation: Intersect(A, Void) = Void */
    if (seraph_sdf_is_void(a)) return SERAPH_SDF_VOID;
    if (seraph_sdf_is_void(b)) return SERAPH_SDF_VOID;

    if (seraph_q128_gt(a.distance, b.distance)) {
        return a;
    }
    return b;
}

/**
 * @brief Subtraction: max(A, -B) - A with B carved out
 */
Seraph_SDF_Result seraph_sdf_subtract(Seraph_SDF_Result a, Seraph_SDF_Result b) {
    /* VOID propagation: Subtract(A, Void) = A, Subtract(Void, B) = Void */
    if (seraph_sdf_is_void(a)) return SERAPH_SDF_VOID;
    if (seraph_sdf_is_void(b)) return a;

    /* Negate B */
    Seraph_SDF_Result neg_b = seraph_sdf_negate(b);

    return seraph_sdf_intersect(a, neg_b);
}

/**
 * @brief XOR: one or the other, but not both
 */
Seraph_SDF_Result seraph_sdf_xor(Seraph_SDF_Result a, Seraph_SDF_Result b) {
    /* XOR = Union(Subtract(A,B), Subtract(B,A)) */
    /* But more efficiently: max(min(A,B), -max(A,B)) */
    if (seraph_sdf_is_void(a)) return b;
    if (seraph_sdf_is_void(b)) return a;

    Seraph_Q128 min_d = seraph_q128_min(a.distance, b.distance);
    Seraph_Q128 max_d = seraph_q128_max(a.distance, b.distance);

    Seraph_SDF_Result result;
    result.distance = seraph_q128_max(min_d, seraph_q128_neg(max_d));

    /* Gradient from dominant shape */
    if (seraph_q128_lt(a.distance, b.distance)) {
        result.gradient_x = a.gradient_x;
        result.gradient_y = a.gradient_y;
        result.curvature = a.curvature;
    } else {
        result.gradient_x = b.gradient_x;
        result.gradient_y = b.gradient_y;
        result.curvature = b.curvature;
    }

    return result;
}

/**
 * @brief Smooth union: blended shapes with soft transition
 */
Seraph_SDF_Result seraph_sdf_smooth_union(
    Seraph_SDF_Result a,
    Seraph_SDF_Result b,
    Seraph_Q128 k
) {
    if (seraph_sdf_is_void(a)) return b;
    if (seraph_sdf_is_void(b)) return a;
    if (seraph_q128_is_void(k) || seraph_q128_is_zero(k)) {
        return seraph_sdf_union(a, b);
    }

    /* Smooth blend factor */
    Seraph_Q128 diff = seraph_q128_sub(b.distance, a.distance);
    Seraph_Q128 h = seraph_q128_clamp(
        seraph_q128_add(
            seraph_q128_div(diff, seraph_q128_mul(k, seraph_q128_from_i64(2))),
            Q128_HALF
        ),
        SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    /* Blended distance */
    Seraph_Q128 blend_dist = seraph_q128_lerp(b.distance, a.distance, h);
    Seraph_Q128 correction = seraph_q128_mul(
        seraph_q128_mul(k, h),
        seraph_q128_sub(SERAPH_Q128_ONE, h)
    );
    Seraph_Q128 distance = seraph_q128_sub(blend_dist, correction);

    /* Blend gradients */
    Seraph_Q128 grad_x = seraph_q128_lerp(b.gradient_x, a.gradient_x, h);
    Seraph_Q128 grad_y = seraph_q128_lerp(b.gradient_y, a.gradient_y, h);
    Seraph_Q128 curvature = seraph_q128_lerp(b.curvature, a.curvature, h);

    return (Seraph_SDF_Result){
        .distance   = distance,
        .gradient_x = grad_x,
        .gradient_y = grad_y,
        .curvature  = curvature
    };
}

/**
 * @brief Smooth subtraction: soft-edged carving
 */
Seraph_SDF_Result seraph_sdf_smooth_subtract(
    Seraph_SDF_Result a,
    Seraph_SDF_Result b,
    Seraph_Q128 k
) {
    return seraph_sdf_smooth_intersect(a, seraph_sdf_negate(b), k);
}

/**
 * @brief Smooth intersection
 */
Seraph_SDF_Result seraph_sdf_smooth_intersect(
    Seraph_SDF_Result a,
    Seraph_SDF_Result b,
    Seraph_Q128 k
) {
    if (seraph_sdf_is_void(a)) return SERAPH_SDF_VOID;
    if (seraph_sdf_is_void(b)) return SERAPH_SDF_VOID;
    if (seraph_q128_is_void(k) || seraph_q128_is_zero(k)) {
        return seraph_sdf_intersect(a, b);
    }

    /* Smooth blend factor (inverted from union) */
    Seraph_Q128 diff = seraph_q128_sub(a.distance, b.distance);
    Seraph_Q128 h = seraph_q128_clamp(
        seraph_q128_add(
            seraph_q128_div(diff, seraph_q128_mul(k, seraph_q128_from_i64(2))),
            Q128_HALF
        ),
        SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    /* Blended distance (take max, not min) */
    Seraph_Q128 blend_dist = seraph_q128_lerp(a.distance, b.distance, h);
    Seraph_Q128 correction = seraph_q128_mul(
        seraph_q128_mul(k, h),
        seraph_q128_sub(SERAPH_Q128_ONE, h)
    );
    Seraph_Q128 distance = seraph_q128_add(blend_dist, correction);

    /* Blend gradients */
    Seraph_Q128 grad_x = seraph_q128_lerp(a.gradient_x, b.gradient_x, h);
    Seraph_Q128 grad_y = seraph_q128_lerp(a.gradient_y, b.gradient_y, h);
    Seraph_Q128 curvature = seraph_q128_lerp(a.curvature, b.curvature, h);

    return (Seraph_SDF_Result){
        .distance   = distance,
        .gradient_x = grad_x,
        .gradient_y = grad_y,
        .curvature  = curvature
    };
}

/*============================================================================
 * Anti-Aliasing and Coverage
 *============================================================================*/

/**
 * @brief Calculate pixel coverage from SDF result
 *
 * This is the magic of SDF rendering: perfect anti-aliasing
 * from a single evaluation. The coverage is computed analytically
 * from the distance and gradient.
 */
Seraph_Q128 seraph_glyph_coverage(
    Seraph_SDF_Result result,
    Seraph_Q128 pixel_size
) {
    if (seraph_sdf_is_void(result) || seraph_q128_is_void(pixel_size)) {
        return SERAPH_Q128_VOID;
    }

    if (seraph_q128_is_zero(pixel_size)) {
        /* Infinitely small pixel: hard edge */
        if (result.distance.hi < 0) {
            return SERAPH_Q128_ONE;
        } else {
            return SERAPH_Q128_ZERO;
        }
    }

    /* Gradient magnitude for edge sharpness */
    Seraph_Q128 grad_mag = seraph_sdf_gradient_magnitude(result);
    if (seraph_q128_is_void(grad_mag) || seraph_q128_is_zero(grad_mag)) {
        grad_mag = SERAPH_Q128_ONE;  /* Default to sharp edge */
    }

    /* Distance in pixel units */
    Seraph_Q128 pixel_dist = seraph_q128_div(result.distance, pixel_size);

    /* Smoothstep transition over [-0.5, 0.5] pixel range */
    /* coverage = smoothstep(0.5, -0.5, pixel_dist) */
    Seraph_Q128 t = seraph_q128_clamp(
        seraph_q128_sub(Q128_HALF, pixel_dist),
        SERAPH_Q128_ZERO,
        SERAPH_Q128_ONE
    );

    /* Smoothstep: 3t² - 2t³ */
    Seraph_Q128 t2 = seraph_q128_mul(t, t);
    Seraph_Q128 t3 = seraph_q128_mul(t2, t);
    Seraph_Q128 coverage = seraph_q128_sub(
        seraph_q128_mul(seraph_q128_from_i64(3), t2),
        seraph_q128_mul(seraph_q128_from_i64(2), t3)
    );

    return seraph_q128_clamp(coverage, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);
}

/**
 * @brief Calculate alpha for rendering (double version)
 */
double seraph_glyph_alpha(
    Seraph_SDF_Result result,
    double pixel_size
) {
    Seraph_Q128 coverage = seraph_glyph_coverage(
        result,
        seraph_q128_from_double(pixel_size)
    );

    if (seraph_q128_is_void(coverage)) {
        return 0.0;
    }

    return seraph_q128_to_double(coverage);
}

/*============================================================================
 * Hit Testing and Distance Queries
 *============================================================================*/

/**
 * @brief Get distance to surface as double
 */
double seraph_glyph_distance(Seraph_SDF_Result result) {
    if (seraph_sdf_is_void(result)) {
        return INFINITY;  /* VOID is infinitely far */
    }

    return seraph_q128_to_double(result.distance);
}

/**
 * @brief Get surface normal at point
 */
void seraph_glyph_normal(
    Seraph_SDF_Result result,
    double* out_nx,
    double* out_ny
) {
    if (!out_nx || !out_ny) return;

    if (seraph_sdf_is_void(result)) {
        *out_nx = 0.0;
        *out_ny = 0.0;
        return;
    }

    /* Normalize gradient */
    Seraph_Q128 mag = seraph_sdf_gradient_magnitude(result);
    if (seraph_q128_is_void(mag) || seraph_q128_is_zero(mag)) {
        *out_nx = 0.0;
        *out_ny = 0.0;
        return;
    }

    *out_nx = seraph_q128_to_double(seraph_q128_div(result.gradient_x, mag));
    *out_ny = seraph_q128_to_double(seraph_q128_div(result.gradient_y, mag));
}
