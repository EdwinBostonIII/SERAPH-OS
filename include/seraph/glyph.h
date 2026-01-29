/**
 * @file glyph.h
 * @brief MC9: The Glyph - Hyper-Dual SDF Rendering
 *
 * Infinite-resolution graphics through Signed Distance Fields.
 * Store the EQUATION, not the pixels.
 *
 * Key innovations:
 * - SDF evaluation with automatic differentiation via Galactic numbers
 * - Analytic anti-aliasing (no supersampling needed)
 * - Boolean composition of shapes (union, intersect, subtract)
 * - Physics-aware hit testing using the same SDF
 * - VOID propagation through all operations
 */

#ifndef SERAPH_GLYPH_H
#define SERAPH_GLYPH_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/q128.h"
#include "seraph/galactic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Glyph Type Definition
 *============================================================================*/

/**
 * @brief 64-bit Glyph handle
 *
 * Bit layout: [ARENA:16][KIND:4][FLAGS:8][TRANSFORM:4][INSTANCE:32]
 *
 * A Glyph is not pixels - it's a mathematical equation handle.
 * The same glyph renders perfectly at any scale.
 */
typedef uint64_t Seraph_Glyph;

/* Bit field positions */
#define SERAPH_GLYPH_ARENA_SHIFT     48
#define SERAPH_GLYPH_KIND_SHIFT      44
#define SERAPH_GLYPH_FLAGS_SHIFT     36
#define SERAPH_GLYPH_TRANSFORM_SHIFT 32
#define SERAPH_GLYPH_INSTANCE_SHIFT  0

/* Bit field masks */
#define SERAPH_GLYPH_ARENA_MASK     ((uint64_t)0xFFFF000000000000ULL)
#define SERAPH_GLYPH_KIND_MASK      ((uint64_t)0x0000F00000000000ULL)
#define SERAPH_GLYPH_FLAGS_MASK     ((uint64_t)0x00000FF000000000ULL)
#define SERAPH_GLYPH_TRANSFORM_MASK ((uint64_t)0x0000000F00000000ULL)
#define SERAPH_GLYPH_INSTANCE_MASK  ((uint64_t)0x00000000FFFFFFFFULL)

/** VOID glyph - existentially absent */
#define SERAPH_GLYPH_VOID ((Seraph_Glyph)0xFFFFFFFFFFFFFFFFULL)

/*============================================================================
 * Glyph Kinds (Primitive Types)
 *============================================================================*/

/**
 * @brief Enumeration of SDF primitive types
 *
 * Each kind has its own SDF equation.
 */
typedef enum {
    SERAPH_GLYPH_KIND_CIRCLE      = 0,   /**< Circular SDF: d = |p| - r */
    SERAPH_GLYPH_KIND_BOX         = 1,   /**< Axis-aligned rectangle */
    SERAPH_GLYPH_KIND_ROUNDED_BOX = 2,   /**< Rectangle with corner radius */
    SERAPH_GLYPH_KIND_LINE        = 3,   /**< Line segment */
    SERAPH_GLYPH_KIND_RING        = 4,   /**< Annulus (donut) shape */
    SERAPH_GLYPH_KIND_TRIANGLE    = 5,   /**< Triangle */
    SERAPH_GLYPH_KIND_TEXT        = 6,   /**< Text string (MSDF) */
    SERAPH_GLYPH_KIND_COMPOSITE   = 7,   /**< Boolean combination */
    SERAPH_GLYPH_KIND_MSDF        = 8,   /**< Multi-channel SDF texture */
    SERAPH_GLYPH_KIND_PROCEDURAL  = 9,   /**< Procedural (noise, fractal) */
    SERAPH_GLYPH_KIND_BEZIER      = 10,  /**< Bezier curve */
    SERAPH_GLYPH_KIND_POLYGON     = 11,  /**< Arbitrary polygon */
    SERAPH_GLYPH_KIND_VOID        = 15   /**< Non-existent glyph */
} Seraph_Glyph_Kind;

/*============================================================================
 * Glyph Flags
 *============================================================================*/

#define SERAPH_GLYPH_FLAG_VISIBLE     (1 << 0)  /**< Render this glyph */
#define SERAPH_GLYPH_FLAG_INTERACTIVE (1 << 1)  /**< Hit-testable */
#define SERAPH_GLYPH_FLAG_SHADOW      (1 << 2)  /**< Has shadow band */
#define SERAPH_GLYPH_FLAG_GLOW        (1 << 3)  /**< Has glow band */
#define SERAPH_GLYPH_FLAG_CLIP_CHILD  (1 << 4)  /**< Clips children */
#define SERAPH_GLYPH_FLAG_PHYSICS     (1 << 5)  /**< Participates in physics */
#define SERAPH_GLYPH_FLAG_DIRTY       (1 << 6)  /**< Needs re-evaluation */
#define SERAPH_GLYPH_FLAG_VOID_STATE  (1 << 7)  /**< In VOID state */

/*============================================================================
 * SDF Result Structures
 *============================================================================*/

/**
 * @brief 2D point using Hyper-Dual coordinates
 *
 * By using Galactic numbers for coordinates, the SDF evaluation
 * automatically computes gradients (surface normals) for free.
 */
typedef struct {
    Seraph_Galactic x;  /**< x-coordinate as Hyper-Dual */
    Seraph_Galactic y;  /**< y-coordinate as Hyper-Dual */
} Seraph_Glyph_Point;

/**
 * @brief Result of SDF evaluation
 *
 * Contains:
 * - distance: Signed distance to surface (negative = inside)
 * - gradient: Surface normal direction
 * - curvature: For anti-aliasing quality
 */
typedef struct {
    Seraph_Q128 distance;    /**< Signed distance to surface */
    Seraph_Q128 gradient_x;  /**< d(dist)/dx - surface normal X */
    Seraph_Q128 gradient_y;  /**< d(dist)/dy - surface normal Y */
    Seraph_Q128 curvature;   /**< Second derivative for AA */
} Seraph_SDF_Result;

/** VOID SDF result - infinitely far from everything */
#define SERAPH_SDF_VOID ((Seraph_SDF_Result){ \
    .distance   = SERAPH_Q128_VOID, \
    .gradient_x = SERAPH_Q128_ZERO, \
    .gradient_y = SERAPH_Q128_ZERO, \
    .curvature  = SERAPH_Q128_ZERO  \
})

/**
 * @brief Maximum Q128 value for "infinite" distance
 */
#define SERAPH_Q128_INFINITY ((Seraph_Q128){ INT64_MAX, 0 })

/** SDF result for point infinitely far (VOID glyph distance) */
#define SERAPH_SDF_INFINITE ((Seraph_SDF_Result){ \
    .distance   = SERAPH_Q128_INFINITY, \
    .gradient_x = SERAPH_Q128_ZERO, \
    .gradient_y = SERAPH_Q128_ZERO, \
    .curvature  = SERAPH_Q128_ZERO  \
})

/*============================================================================
 * Glyph Field Accessors
 *============================================================================*/

/**
 * @brief Extract arena index from glyph
 */
static inline uint16_t seraph_glyph_arena(Seraph_Glyph g) {
    return (uint16_t)((g & SERAPH_GLYPH_ARENA_MASK) >> SERAPH_GLYPH_ARENA_SHIFT);
}

/**
 * @brief Extract kind from glyph
 */
static inline Seraph_Glyph_Kind seraph_glyph_kind(Seraph_Glyph g) {
    if (g == SERAPH_GLYPH_VOID) return SERAPH_GLYPH_KIND_VOID;
    return (Seraph_Glyph_Kind)((g & SERAPH_GLYPH_KIND_MASK) >> SERAPH_GLYPH_KIND_SHIFT);
}

/**
 * @brief Extract flags from glyph
 */
static inline uint8_t seraph_glyph_flags(Seraph_Glyph g) {
    return (uint8_t)((g & SERAPH_GLYPH_FLAGS_MASK) >> SERAPH_GLYPH_FLAGS_SHIFT);
}

/**
 * @brief Extract transform index from glyph
 */
static inline uint8_t seraph_glyph_transform(Seraph_Glyph g) {
    return (uint8_t)((g & SERAPH_GLYPH_TRANSFORM_MASK) >> SERAPH_GLYPH_TRANSFORM_SHIFT);
}

/**
 * @brief Extract instance ID from glyph
 */
static inline uint32_t seraph_glyph_instance(Seraph_Glyph g) {
    return (uint32_t)(g & SERAPH_GLYPH_INSTANCE_MASK);
}

/**
 * @brief Check if glyph is VOID
 */
static inline bool seraph_glyph_is_void(Seraph_Glyph g) {
    return g == SERAPH_GLYPH_VOID ||
           (seraph_glyph_flags(g) & SERAPH_GLYPH_FLAG_VOID_STATE);
}

/**
 * @brief Check if glyph exists (not VOID)
 */
static inline bool seraph_glyph_exists(Seraph_Glyph g) {
    return !seraph_glyph_is_void(g);
}

/**
 * @brief Check if glyph is visible
 */
static inline bool seraph_glyph_is_visible(Seraph_Glyph g) {
    return !seraph_glyph_is_void(g) &&
           (seraph_glyph_flags(g) & SERAPH_GLYPH_FLAG_VISIBLE);
}

/**
 * @brief Check if glyph is interactive (hit-testable)
 */
static inline bool seraph_glyph_is_interactive(Seraph_Glyph g) {
    return !seraph_glyph_is_void(g) &&
           (seraph_glyph_flags(g) & SERAPH_GLYPH_FLAG_INTERACTIVE);
}

/*============================================================================
 * Glyph Construction
 *============================================================================*/

/**
 * @brief Create a glyph handle from components
 */
static inline Seraph_Glyph seraph_glyph_create(
    uint16_t arena,
    Seraph_Glyph_Kind kind,
    uint8_t flags,
    uint8_t transform,
    uint32_t instance
) {
    return ((uint64_t)arena << SERAPH_GLYPH_ARENA_SHIFT) |
           ((uint64_t)(kind & 0xF) << SERAPH_GLYPH_KIND_SHIFT) |
           ((uint64_t)flags << SERAPH_GLYPH_FLAGS_SHIFT) |
           ((uint64_t)(transform & 0xF) << SERAPH_GLYPH_TRANSFORM_SHIFT) |
           ((uint64_t)instance << SERAPH_GLYPH_INSTANCE_SHIFT);
}

/**
 * @brief Set flags on a glyph
 */
static inline Seraph_Glyph seraph_glyph_set_flags(Seraph_Glyph g, uint8_t flags) {
    if (seraph_glyph_is_void(g)) return SERAPH_GLYPH_VOID;
    return (g & ~SERAPH_GLYPH_FLAGS_MASK) |
           ((uint64_t)flags << SERAPH_GLYPH_FLAGS_SHIFT);
}

/**
 * @brief Add flags to a glyph
 */
static inline Seraph_Glyph seraph_glyph_add_flags(Seraph_Glyph g, uint8_t flags) {
    if (seraph_glyph_is_void(g)) return SERAPH_GLYPH_VOID;
    return g | ((uint64_t)flags << SERAPH_GLYPH_FLAGS_SHIFT);
}

/**
 * @brief Remove flags from a glyph
 */
static inline Seraph_Glyph seraph_glyph_remove_flags(Seraph_Glyph g, uint8_t flags) {
    if (seraph_glyph_is_void(g)) return SERAPH_GLYPH_VOID;
    return g & ~((uint64_t)flags << SERAPH_GLYPH_FLAGS_SHIFT);
}

/*============================================================================
 * SDF Result Detection
 *============================================================================*/

/**
 * @brief Check if SDF result is VOID
 */
static inline bool seraph_sdf_is_void(Seraph_SDF_Result r) {
    return seraph_q128_is_void(r.distance);
}

/**
 * @brief Check if point is inside shape (negative distance)
 */
static inline bool seraph_sdf_is_inside(Seraph_SDF_Result r) {
    if (seraph_sdf_is_void(r)) return false;
    return r.distance.hi < 0;
}

/**
 * @brief Check if point is outside shape (positive distance)
 */
static inline bool seraph_sdf_is_outside(Seraph_SDF_Result r) {
    if (seraph_sdf_is_void(r)) return true;  /* VOID is "outside" */
    return r.distance.hi > 0 || (r.distance.hi == 0 && r.distance.lo > 0);
}

/*============================================================================
 * Glyph Point Construction
 *============================================================================*/

/**
 * @brief Create a point with standard tangent vectors
 *
 * For normal rendering, we want dx/dx = 1, dy/dy = 1.
 * This gives us the surface normal for free.
 */
static inline Seraph_Glyph_Point seraph_glyph_point_create(Seraph_Q128 x, Seraph_Q128 y) {
    return (Seraph_Glyph_Point){
        .x = seraph_galactic_variable(x),  /* tangent = 1 for x derivative */
        .y = seraph_galactic_constant(y)   /* tangent = 0 for y contribution to x */
    };
}

/**
 * @brief Create a point with both derivatives tracked
 *
 * For full gradient computation (dx, dy both tracked).
 */
static inline Seraph_Glyph_Point seraph_glyph_point_create_full(
    Seraph_Q128 x, Seraph_Q128 y,
    Seraph_Q128 tangent_x, Seraph_Q128 tangent_y
) {
    return (Seraph_Glyph_Point){
        .x = (Seraph_Galactic){ .primal = x, .tangent = tangent_x },
        .y = (Seraph_Galactic){ .primal = y, .tangent = tangent_y }
    };
}

/*============================================================================
 * Primitive SDF Functions
 *============================================================================*/

/**
 * @brief Circle SDF: d = |p - center| - radius
 */
Seraph_SDF_Result seraph_sdf_circle(
    Seraph_Glyph_Point p,
    Seraph_Q128 center_x,
    Seraph_Q128 center_y,
    Seraph_Q128 radius
);

/**
 * @brief Box SDF: d = max(|p.x| - w/2, |p.y| - h/2)
 */
Seraph_SDF_Result seraph_sdf_box(
    Seraph_Glyph_Point p,
    Seraph_Q128 half_width,
    Seraph_Q128 half_height
);

/**
 * @brief Rounded Box SDF: box with corner radius
 */
Seraph_SDF_Result seraph_sdf_rounded_box(
    Seraph_Glyph_Point p,
    Seraph_Q128 half_width,
    Seraph_Q128 half_height,
    Seraph_Q128 corner_radius
);

/**
 * @brief Line segment SDF
 */
Seraph_SDF_Result seraph_sdf_line(
    Seraph_Glyph_Point p,
    Seraph_Q128 x1, Seraph_Q128 y1,
    Seraph_Q128 x2, Seraph_Q128 y2,
    Seraph_Q128 thickness
);

/**
 * @brief Ring (annulus) SDF: d = ||p| - r| - thickness/2
 */
Seraph_SDF_Result seraph_sdf_ring(
    Seraph_Glyph_Point p,
    Seraph_Q128 center_x,
    Seraph_Q128 center_y,
    Seraph_Q128 radius,
    Seraph_Q128 thickness
);

/**
 * @brief Triangle SDF
 */
Seraph_SDF_Result seraph_sdf_triangle(
    Seraph_Glyph_Point p,
    Seraph_Q128 x1, Seraph_Q128 y1,
    Seraph_Q128 x2, Seraph_Q128 y2,
    Seraph_Q128 x3, Seraph_Q128 y3
);

/*============================================================================
 * Boolean SDF Operations
 *============================================================================*/

/**
 * @brief Union: min(A, B) - combined shapes
 *
 * VOID propagation: Union(A, Void) = A
 */
Seraph_SDF_Result seraph_sdf_union(Seraph_SDF_Result a, Seraph_SDF_Result b);

/**
 * @brief Intersection: max(A, B) - shared region only
 *
 * VOID propagation: Intersect(A, Void) = Void
 */
Seraph_SDF_Result seraph_sdf_intersect(Seraph_SDF_Result a, Seraph_SDF_Result b);

/**
 * @brief Subtraction: max(A, -B) - A with B carved out
 *
 * VOID propagation: Subtract(A, Void) = A, Subtract(Void, A) = Void
 */
Seraph_SDF_Result seraph_sdf_subtract(Seraph_SDF_Result a, Seraph_SDF_Result b);

/**
 * @brief XOR: one or the other, not both
 */
Seraph_SDF_Result seraph_sdf_xor(Seraph_SDF_Result a, Seraph_SDF_Result b);

/**
 * @brief Smooth union: blended shapes
 * @param k Blend radius (larger = more blending)
 */
Seraph_SDF_Result seraph_sdf_smooth_union(
    Seraph_SDF_Result a,
    Seraph_SDF_Result b,
    Seraph_Q128 k
);

/**
 * @brief Smooth subtraction: soft-edged carving
 */
Seraph_SDF_Result seraph_sdf_smooth_subtract(
    Seraph_SDF_Result a,
    Seraph_SDF_Result b,
    Seraph_Q128 k
);

/**
 * @brief Smooth intersection
 */
Seraph_SDF_Result seraph_sdf_smooth_intersect(
    Seraph_SDF_Result a,
    Seraph_SDF_Result b,
    Seraph_Q128 k
);

/*============================================================================
 * SDF Transformations
 *============================================================================*/

/**
 * @brief Negate SDF (invert inside/outside)
 */
static inline Seraph_SDF_Result seraph_sdf_negate(Seraph_SDF_Result r) {
    if (seraph_sdf_is_void(r)) return r;
    return (Seraph_SDF_Result){
        .distance   = seraph_q128_neg(r.distance),
        .gradient_x = seraph_q128_neg(r.gradient_x),
        .gradient_y = seraph_q128_neg(r.gradient_y),
        .curvature  = r.curvature
    };
}

/**
 * @brief Offset SDF (expand or shrink)
 */
static inline Seraph_SDF_Result seraph_sdf_offset(Seraph_SDF_Result r, Seraph_Q128 amount) {
    if (seraph_sdf_is_void(r)) return r;
    return (Seraph_SDF_Result){
        .distance   = seraph_q128_sub(r.distance, amount),
        .gradient_x = r.gradient_x,
        .gradient_y = r.gradient_y,
        .curvature  = r.curvature
    };
}

/**
 * @brief Round SDF (smooth corners by amount)
 */
static inline Seraph_SDF_Result seraph_sdf_round(Seraph_SDF_Result r, Seraph_Q128 radius) {
    return seraph_sdf_offset(r, radius);
}

/*============================================================================
 * Analytic Anti-Aliasing
 *============================================================================*/

/**
 * @brief Calculate pixel coverage from SDF result
 *
 * This is the magic of Hyper-Dual SDF: perfect anti-aliasing
 * from a single evaluation. No supersampling needed.
 *
 * @param result SDF evaluation result
 * @param pixel_size Size of one pixel in world units
 * @return Coverage value [0, 1] as Q128
 */
Seraph_Q128 seraph_glyph_coverage(
    Seraph_SDF_Result result,
    Seraph_Q128 pixel_size
);

/**
 * @brief Calculate alpha for rendering
 *
 * Same as coverage but returns as double for convenience.
 */
double seraph_glyph_alpha(
    Seraph_SDF_Result result,
    double pixel_size
);

/*============================================================================
 * Hit Testing
 *============================================================================*/

/**
 * @brief Test if a point is inside a glyph
 *
 * Uses the same SDF as rendering - no duplicate collision geometry.
 *
 * @param result Pre-computed SDF result
 * @return true if point is inside (distance < 0)
 */
static inline bool seraph_glyph_hit_test(Seraph_SDF_Result result) {
    return seraph_sdf_is_inside(result);
}

/**
 * @brief Get distance to surface
 *
 * Useful for proximity effects, magnetic snapping.
 *
 * @param result Pre-computed SDF result
 * @return Distance to surface as double (VOID returns +inf)
 */
double seraph_glyph_distance(Seraph_SDF_Result result);

/**
 * @brief Get surface normal at point
 *
 * Returns normalized gradient vector.
 *
 * @param result Pre-computed SDF result
 * @param out_nx Output normal X component
 * @param out_ny Output normal Y component
 */
void seraph_glyph_normal(
    Seraph_SDF_Result result,
    double* out_nx,
    double* out_ny
);

/*============================================================================
 * Gradient Magnitude
 *============================================================================*/

/**
 * @brief Compute gradient magnitude |grad|
 */
static inline Seraph_Q128 seraph_sdf_gradient_magnitude(Seraph_SDF_Result r) {
    if (seraph_sdf_is_void(r)) return SERAPH_Q128_VOID;

    Seraph_Q128 gx2 = seraph_q128_mul(r.gradient_x, r.gradient_x);
    Seraph_Q128 gy2 = seraph_q128_mul(r.gradient_y, r.gradient_y);
    return seraph_q128_sqrt(seraph_q128_add(gx2, gy2));
}

/*============================================================================
 * Utility: Q128 from double (helper for glyph operations)
 *============================================================================*/

/**
 * @brief Quick Q128 from double for glyph operations
 */
static inline Seraph_Q128 seraph_q128_from_double_approx(double d) {
    return seraph_q128_from_double(d);
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_GLYPH_H */
