/**
 * @file galactic.c
 * @brief MC5+: Galactic Numbers Implementation
 */

#include "seraph/galactic.h"

/*============================================================================
 * Transcendental Functions
 *============================================================================*/

Seraph_Galactic seraph_galactic_sqrt(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* Check for negative (branchless) */
    int64_t is_neg = -(int64_t)(seraph_q128_is_negative(x.primal));
    Seraph_Q128 neg_mask = { is_neg, (uint64_t)is_neg };
    void_mask.primal.hi |= neg_mask.hi;
    void_mask.primal.lo |= neg_mask.lo;
    void_mask.tangent.hi |= neg_mask.hi;
    void_mask.tangent.lo |= neg_mask.lo;

    /* sqrt(a + a'ε) = sqrt(a) + (a' / (2×sqrt(a)))ε */
    Seraph_Q128 sqrt_primal = seraph_q128_sqrt(x.primal);
    Seraph_Q128 two = seraph_q128_from_i64(2);
    Seraph_Q128 two_sqrt = seraph_q128_mul(two, sqrt_primal);
    Seraph_Q128 tangent = seraph_q128_div(x.tangent, two_sqrt);

    Seraph_Galactic result = { sqrt_primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_sin(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* sin(a + a'ε) = sin(a) + a'×cos(a)ε */
    Seraph_Q128 sin_primal = seraph_q128_sin(x.primal);
    Seraph_Q128 cos_primal = seraph_q128_cos(x.primal);
    Seraph_Q128 tangent = seraph_q128_mul(x.tangent, cos_primal);

    Seraph_Galactic result = { sin_primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_cos(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* cos(a + a'ε) = cos(a) - a'×sin(a)ε */
    Seraph_Q128 cos_primal = seraph_q128_cos(x.primal);
    Seraph_Q128 sin_primal = seraph_q128_sin(x.primal);
    Seraph_Q128 tangent = seraph_q128_neg(seraph_q128_mul(x.tangent, sin_primal));

    Seraph_Galactic result = { cos_primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_tan(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* tan(x) = sin(x) / cos(x), use quotient rule */
    Seraph_Galactic sin_x = seraph_galactic_sin(x);
    Seraph_Galactic cos_x = seraph_galactic_cos(x);

    Seraph_Galactic result = seraph_galactic_div(sin_x, cos_x);
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_exp(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* exp(a + a'ε) = exp(a) + a'×exp(a)ε */
    Seraph_Q128 exp_primal = seraph_q128_exp(x.primal);
    Seraph_Q128 tangent = seraph_q128_mul(x.tangent, exp_primal);

    Seraph_Galactic result = { exp_primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_ln(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* Check for non-positive (branchless) */
    int64_t not_pos = -(int64_t)(!seraph_q128_is_positive(x.primal));
    Seraph_Q128 pos_mask = { not_pos, (uint64_t)not_pos };
    void_mask.primal.hi |= pos_mask.hi;
    void_mask.primal.lo |= pos_mask.lo;
    void_mask.tangent.hi |= pos_mask.hi;
    void_mask.tangent.lo |= pos_mask.lo;

    /* ln(a + a'ε) = ln(a) + (a'/a)ε */
    Seraph_Q128 ln_primal = seraph_q128_ln(x.primal);
    Seraph_Q128 tangent = seraph_q128_div(x.tangent, x.primal);

    Seraph_Galactic result = { ln_primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_pow(Seraph_Galactic base, Seraph_Galactic exp) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask2(base, exp);

    /* base^exp = exp(exp × ln(base)) */
    Seraph_Galactic ln_base = seraph_galactic_ln(base);
    Seraph_Galactic product = seraph_galactic_mul(exp, ln_base);
    Seraph_Galactic result = seraph_galactic_exp(product);

    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_pow_scalar(Seraph_Galactic base, Seraph_Q128 exp) {
    return seraph_galactic_pow(base, seraph_galactic_constant(exp));
}

/*============================================================================
 * Hyperbolic Functions
 *============================================================================*/

Seraph_Galactic seraph_galactic_sinh(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* sinh(a + a'ε) = sinh(a) + a'×cosh(a)ε */
    Seraph_Q128 sinh_primal = seraph_q128_sinh(x.primal);
    Seraph_Q128 cosh_primal = seraph_q128_cosh(x.primal);
    Seraph_Q128 tangent = seraph_q128_mul(x.tangent, cosh_primal);

    Seraph_Galactic result = { sinh_primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_cosh(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* cosh(a + a'ε) = cosh(a) + a'×sinh(a)ε */
    Seraph_Q128 cosh_primal = seraph_q128_cosh(x.primal);
    Seraph_Q128 sinh_primal = seraph_q128_sinh(x.primal);
    Seraph_Q128 tangent = seraph_q128_mul(x.tangent, sinh_primal);

    Seraph_Galactic result = { cosh_primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

Seraph_Galactic seraph_galactic_tanh(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* tanh = sinh / cosh */
    Seraph_Galactic result = seraph_galactic_div(seraph_galactic_sinh(x), seraph_galactic_cosh(x));
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

Seraph_Galactic seraph_galactic_lerp(Seraph_Galactic a, Seraph_Galactic b,
                                      Seraph_Galactic t) {
    /* a + t × (b - a) */
    Seraph_Galactic diff = seraph_galactic_sub(b, a);
    Seraph_Galactic scaled = seraph_galactic_mul(t, diff);
    return seraph_galactic_add(a, scaled);
}

Seraph_Galactic seraph_galactic_clamp(Seraph_Galactic x,
                                       Seraph_Q128 lo, Seraph_Q128 hi) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);
    Seraph_Q128 lo_void = seraph_q128_void_mask(lo);
    Seraph_Q128 hi_void = seraph_q128_void_mask(hi);
    void_mask.primal.hi |= lo_void.hi | hi_void.hi;
    void_mask.primal.lo |= lo_void.lo | hi_void.lo;
    void_mask.tangent.hi |= lo_void.hi | hi_void.hi;
    void_mask.tangent.lo |= lo_void.lo | hi_void.lo;

    /* Clamp primal, tangent becomes 0 at boundaries (branchless) */
    int64_t below = -(int64_t)(seraph_vbit_is_true(seraph_q128_lt(x.primal, lo)));
    int64_t above = -(int64_t)(seraph_vbit_is_true(seraph_q128_gt(x.primal, hi)));

    Seraph_Q128 below_mask = { below, (uint64_t)below };
    Seraph_Q128 above_mask = { above, (uint64_t)above };

    Seraph_Galactic lo_result = { lo, SERAPH_Q128_ZERO };
    Seraph_Galactic hi_result = { hi, SERAPH_Q128_ZERO };
    Seraph_Galactic below_gal_mask = { below_mask, below_mask };
    Seraph_Galactic above_gal_mask = { above_mask, above_mask };

    Seraph_Galactic result = seraph_galactic_select(lo_result, x, below_gal_mask);
    result = seraph_galactic_select(hi_result, result, above_gal_mask);

    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/*============================================================================
 * Additional Utility Functions
 *============================================================================*/

/**
 * @brief Smooth step function (cubic Hermite)
 */
Seraph_Galactic seraph_galactic_smoothstep(Seraph_Galactic edge0,
                                            Seraph_Galactic edge1,
                                            Seraph_Galactic x) {
    /* t = clamp((x - edge0) / (edge1 - edge0), 0, 1) */
    /* return t * t * (3 - 2*t) */

    Seraph_Galactic range = seraph_galactic_sub(edge1, edge0);
    Seraph_Galactic t = seraph_galactic_div(
        seraph_galactic_sub(x, edge0),
        range
    );

    /* Clamp t to [0, 1] */
    t = seraph_galactic_clamp(t, SERAPH_Q128_ZERO, SERAPH_Q128_ONE);

    /* t² × (3 - 2t) */
    Seraph_Galactic three = seraph_galactic_constant(seraph_q128_from_i64(3));
    Seraph_Galactic two = seraph_galactic_constant(seraph_q128_from_i64(2));

    Seraph_Galactic t2 = seraph_galactic_mul(t, t);
    Seraph_Galactic inner = seraph_galactic_sub(three, seraph_galactic_mul(two, t));

    return seraph_galactic_mul(t2, inner);
}

/**
 * @brief Compute both sin and cos simultaneously (more efficient)
 */
void seraph_galactic_sincos(Seraph_Galactic x,
                            Seraph_Galactic* out_sin,
                            Seraph_Galactic* out_cos) {
    if (!out_sin || !out_cos) return;

    if (seraph_galactic_is_void(x)) {
        *out_sin = SERAPH_GALACTIC_VOID;
        *out_cos = SERAPH_GALACTIC_VOID;
        return;
    }

    Seraph_Q128 sin_p = seraph_q128_sin(x.primal);
    Seraph_Q128 cos_p = seraph_q128_cos(x.primal);

    /* sin derivative: cos(x) × x' */
    /* cos derivative: -sin(x) × x' */
    *out_sin = (Seraph_Galactic){
        sin_p,
        seraph_q128_mul(x.tangent, cos_p)
    };

    *out_cos = (Seraph_Galactic){
        cos_p,
        seraph_q128_neg(seraph_q128_mul(x.tangent, sin_p))
    };
}

/**
 * @brief Two-argument arctangent with derivative
 */
Seraph_Galactic seraph_galactic_atan2(Seraph_Galactic y, Seraph_Galactic x) {
    if (seraph_galactic_is_void(x) || seraph_galactic_is_void(y)) {
        return SERAPH_GALACTIC_VOID;
    }

    /* atan2(y, x) primal */
    Seraph_Q128 primal = seraph_q128_atan2(y.primal, x.primal);

    /* Derivative: (x × y' - y × x') / (x² + y²) */
    Seraph_Q128 x2 = seraph_q128_mul(x.primal, x.primal);
    Seraph_Q128 y2 = seraph_q128_mul(y.primal, y.primal);
    Seraph_Q128 denom = seraph_q128_add(x2, y2);

    Seraph_Q128 numer = seraph_q128_sub(
        seraph_q128_mul(x.primal, y.tangent),
        seraph_q128_mul(y.primal, x.tangent)
    );

    Seraph_Q128 tangent = seraph_q128_div(numer, denom);

    return (Seraph_Galactic){ primal, tangent };
}

/**
 * @brief Compute length of 2D vector with derivative
 */
Seraph_Galactic seraph_galactic_length2(Seraph_Galactic x, Seraph_Galactic y) {
    Seraph_Galactic x2 = seraph_galactic_mul(x, x);
    Seraph_Galactic y2 = seraph_galactic_mul(y, y);
    Seraph_Galactic sum = seraph_galactic_add(x2, y2);
    return seraph_galactic_sqrt(sum);
}

/**
 * @brief Compute length of 3D vector with derivative
 */
Seraph_Galactic seraph_galactic_length3(Seraph_Galactic x, Seraph_Galactic y,
                                         Seraph_Galactic z) {
    Seraph_Galactic x2 = seraph_galactic_mul(x, x);
    Seraph_Galactic y2 = seraph_galactic_mul(y, y);
    Seraph_Galactic z2 = seraph_galactic_mul(z, z);
    Seraph_Galactic sum = seraph_galactic_add(seraph_galactic_add(x2, y2), z2);
    return seraph_galactic_sqrt(sum);
}
