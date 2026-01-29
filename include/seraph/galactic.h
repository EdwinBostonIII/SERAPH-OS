/**
 * @file galactic.h
 * @brief MC5+: Galactic Numbers - Hyper-Dual Automatic Differentiation
 *
 * 256-bit numbers that automatically track derivatives.
 * primal = value, tangent = derivative (∂/∂x)
 */

#ifndef SERAPH_GALACTIC_H
#define SERAPH_GALACTIC_H

#include "seraph/q128.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Galactic Type Definition
 *============================================================================*/

/**
 * @brief Dual number for automatic differentiation
 *
 * Represents: primal + tangent × ε where ε² = 0
 */
typedef struct {
    Seraph_Q128 primal;   /**< The value */
    Seraph_Q128 tangent;  /**< The derivative (∂/∂x) */
} Seraph_Galactic;

/*============================================================================
 * Galactic Constants
 *============================================================================*/

/** VOID Galactic (both components VOID) */
#define SERAPH_GALACTIC_VOID ((Seraph_Galactic){ SERAPH_Q128_VOID, SERAPH_Q128_VOID })

/** Zero constant */
#define SERAPH_GALACTIC_ZERO ((Seraph_Galactic){ SERAPH_Q128_ZERO, SERAPH_Q128_ZERO })

/** One constant */
#define SERAPH_GALACTIC_ONE ((Seraph_Galactic){ SERAPH_Q128_ONE, SERAPH_Q128_ZERO })

/*============================================================================
 * Galactic Detection
 *============================================================================*/

/**
 * @brief Check if Galactic is VOID
 */
static inline bool seraph_galactic_is_void(Seraph_Galactic x) {
    return seraph_q128_is_void(x.primal) | seraph_q128_is_void(x.tangent);
}

/**
 * @brief Generate VOID mask for Galactic (branchless)
 */
static inline Seraph_Galactic seraph_galactic_void_mask(Seraph_Galactic x) {
    Seraph_Q128 primal_mask = seraph_q128_void_mask(x.primal);
    Seraph_Q128 tangent_mask = seraph_q128_void_mask(x.tangent);
    Seraph_Q128 combined = { primal_mask.hi | tangent_mask.hi,
                              primal_mask.lo | tangent_mask.lo };
    return (Seraph_Galactic){ combined, combined };
}

/**
 * @brief Generate combined VOID mask for two Galactic values
 */
static inline Seraph_Galactic seraph_galactic_void_mask2(Seraph_Galactic a, Seraph_Galactic b) {
    Seraph_Galactic mask_a = seraph_galactic_void_mask(a);
    Seraph_Galactic mask_b = seraph_galactic_void_mask(b);
    Seraph_Q128 combined = { mask_a.primal.hi | mask_b.primal.hi,
                              mask_a.primal.lo | mask_b.primal.lo };
    return (Seraph_Galactic){ combined, combined };
}

/**
 * @brief Branchless select between Galactic values
 */
static inline Seraph_Galactic seraph_galactic_select(Seraph_Galactic if_void,
                                                       Seraph_Galactic if_valid,
                                                       Seraph_Galactic mask) {
    return (Seraph_Galactic){
        seraph_q128_select(if_void.primal, if_valid.primal, mask.primal),
        seraph_q128_select(if_void.tangent, if_valid.tangent, mask.tangent)
    };
}

/**
 * @brief Check if Galactic exists (is not VOID)
 */
static inline bool seraph_galactic_exists(Seraph_Galactic x) {
    return !seraph_galactic_is_void(x);
}

/*============================================================================
 * Galactic Creation
 *============================================================================*/

/**
 * @brief Create Galactic from primal and tangent Q128 values
 */
static inline Seraph_Galactic seraph_galactic_create(Seraph_Q128 primal,
                                                      Seraph_Q128 tangent) {
    return (Seraph_Galactic){ primal, tangent };
}

/**
 * @brief Create Galactic variable (tangent = 1)
 *
 * Use this for the independent variable: d(x)/dx = 1
 */
static inline Seraph_Galactic seraph_galactic_variable(Seraph_Q128 val) {
    return (Seraph_Galactic){ val, SERAPH_Q128_ONE };
}

/**
 * @brief Create Galactic variable from double
 */
static inline Seraph_Galactic seraph_galactic_variable_d(double val) {
    return seraph_galactic_variable(seraph_q128_from_double(val));
}

/**
 * @brief Create Galactic constant (tangent = 0)
 *
 * Use this for constants: d(c)/dx = 0
 */
static inline Seraph_Galactic seraph_galactic_constant(Seraph_Q128 val) {
    return (Seraph_Galactic){ val, SERAPH_Q128_ZERO };
}

/**
 * @brief Create Galactic constant from double
 */
static inline Seraph_Galactic seraph_galactic_constant_d(double val) {
    return seraph_galactic_constant(seraph_q128_from_double(val));
}

/**
 * @brief Promote Q128 to Galactic constant
 */
static inline Seraph_Galactic seraph_galactic_from_q128(Seraph_Q128 val) {
    return seraph_galactic_constant(val);
}

/*============================================================================
 * Galactic Extraction
 *============================================================================*/

/**
 * @brief Get primal (value) component
 */
static inline Seraph_Q128 seraph_galactic_primal(Seraph_Galactic x) {
    return x.primal;
}

/**
 * @brief Get tangent (derivative) component
 */
static inline Seraph_Q128 seraph_galactic_tangent(Seraph_Galactic x) {
    return x.tangent;
}

/**
 * @brief Get primal as double
 */
static inline double seraph_galactic_primal_to_double(Seraph_Galactic x) {
    return seraph_q128_to_double(x.primal);
}

/**
 * @brief Get tangent as double
 */
static inline double seraph_galactic_tangent_to_double(Seraph_Galactic x) {
    return seraph_q128_to_double(x.tangent);
}

/*============================================================================
 * Galactic Basic Arithmetic
 *============================================================================*/

/**
 * @brief Add two Galactic numbers (branchless)
 * (a + a'ε) + (b + b'ε) = (a+b) + (a'+b')ε
 */
static inline Seraph_Galactic seraph_galactic_add(Seraph_Galactic a,
                                                   Seraph_Galactic b) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask2(a, b);
    Seraph_Galactic result = {
        seraph_q128_add(a.primal, b.primal),
        seraph_q128_add(a.tangent, b.tangent)
    };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/**
 * @brief Subtract two Galactic numbers (branchless)
 */
static inline Seraph_Galactic seraph_galactic_sub(Seraph_Galactic a,
                                                   Seraph_Galactic b) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask2(a, b);
    Seraph_Galactic result = {
        seraph_q128_sub(a.primal, b.primal),
        seraph_q128_sub(a.tangent, b.tangent)
    };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/**
 * @brief Multiply two Galactic numbers (product rule, branchless)
 * (a + a'ε) × (b + b'ε) = ab + (a'b + ab')ε
 */
static inline Seraph_Galactic seraph_galactic_mul(Seraph_Galactic a,
                                                   Seraph_Galactic b) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask2(a, b);
    Seraph_Q128 primal = seraph_q128_mul(a.primal, b.primal);
    Seraph_Q128 tangent = seraph_q128_add(
        seraph_q128_mul(a.tangent, b.primal),
        seraph_q128_mul(a.primal, b.tangent)
    );
    Seraph_Galactic result = { primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/**
 * @brief Divide two Galactic numbers (quotient rule, branchless)
 * (a + a'ε) / (b + b'ε) = (a/b) + ((a'b - ab')/b²)ε
 */
static inline Seraph_Galactic seraph_galactic_div(Seraph_Galactic a,
                                                   Seraph_Galactic b) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask2(a, b);

    /* Division by zero check (branchless) */
    int64_t is_zero = -(int64_t)(seraph_q128_is_zero(b.primal));
    Seraph_Q128 zero_mask = { is_zero, (uint64_t)is_zero };
    Seraph_Galactic zero_void_mask = { zero_mask, zero_mask };
    void_mask = seraph_galactic_select(void_mask, zero_void_mask,
                                        (Seraph_Galactic){ zero_mask, zero_mask });

    Seraph_Q128 primal = seraph_q128_div(a.primal, b.primal);
    Seraph_Q128 b_squared = seraph_q128_mul(b.primal, b.primal);
    Seraph_Q128 tangent = seraph_q128_div(
        seraph_q128_sub(
            seraph_q128_mul(a.tangent, b.primal),
            seraph_q128_mul(a.primal, b.tangent)
        ),
        b_squared
    );
    Seraph_Galactic result = { primal, tangent };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/**
 * @brief Negate Galactic number (branchless)
 */
static inline Seraph_Galactic seraph_galactic_neg(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);
    Seraph_Galactic result = {
        seraph_q128_neg(x.primal),
        seraph_q128_neg(x.tangent)
    };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/**
 * @brief Absolute value (branchless)
 */
static inline Seraph_Galactic seraph_galactic_abs(Seraph_Galactic x) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);

    /* Negate if primal is negative (branchless) */
    int64_t is_neg = -(int64_t)(seraph_q128_is_negative(x.primal));
    Seraph_Q128 neg_mask = { is_neg, (uint64_t)is_neg };
    Seraph_Galactic neg_x = seraph_galactic_neg(x);
    Seraph_Galactic neg_gal_mask = { neg_mask, neg_mask };
    Seraph_Galactic result = seraph_galactic_select(neg_x, x, neg_gal_mask);

    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/**
 * @brief Scale by Q128 constant (branchless)
 */
static inline Seraph_Galactic seraph_galactic_scale(Seraph_Galactic x,
                                                     Seraph_Q128 c) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);
    Seraph_Q128 c_mask = seraph_q128_void_mask(c);
    void_mask.primal.hi |= c_mask.hi;
    void_mask.primal.lo |= c_mask.lo;
    void_mask.tangent.hi |= c_mask.hi;
    void_mask.tangent.lo |= c_mask.lo;

    Seraph_Galactic result = {
        seraph_q128_mul(x.primal, c),
        seraph_q128_mul(x.tangent, c)
    };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/**
 * @brief Add Q128 constant (branchless)
 */
static inline Seraph_Galactic seraph_galactic_add_scalar(Seraph_Galactic x,
                                                          Seraph_Q128 c) {
    Seraph_Galactic void_mask = seraph_galactic_void_mask(x);
    Seraph_Q128 c_mask = seraph_q128_void_mask(c);
    void_mask.primal.hi |= c_mask.hi;
    void_mask.primal.lo |= c_mask.lo;
    void_mask.tangent.hi |= c_mask.hi;
    void_mask.tangent.lo |= c_mask.lo;

    Seraph_Galactic result = {
        seraph_q128_add(x.primal, c),
        x.tangent  /* Derivative of constant is 0 */
    };
    return seraph_galactic_select(SERAPH_GALACTIC_VOID, result, void_mask);
}

/*============================================================================
 * Galactic Transcendental Functions
 *============================================================================*/

/**
 * @brief Square root with derivative
 * sqrt(a + a'ε) = sqrt(a) + (a' / (2×sqrt(a)))ε
 */
Seraph_Galactic seraph_galactic_sqrt(Seraph_Galactic x);

/**
 * @brief Sine with derivative
 * sin(a + a'ε) = sin(a) + a'×cos(a)ε
 */
Seraph_Galactic seraph_galactic_sin(Seraph_Galactic x);

/**
 * @brief Cosine with derivative
 * cos(a + a'ε) = cos(a) - a'×sin(a)ε
 */
Seraph_Galactic seraph_galactic_cos(Seraph_Galactic x);

/**
 * @brief Tangent with derivative
 */
Seraph_Galactic seraph_galactic_tan(Seraph_Galactic x);

/**
 * @brief Exponential with derivative
 * exp(a + a'ε) = exp(a) + a'×exp(a)ε
 */
Seraph_Galactic seraph_galactic_exp(Seraph_Galactic x);

/**
 * @brief Natural logarithm with derivative
 * ln(a + a'ε) = ln(a) + (a'/a)ε
 */
Seraph_Galactic seraph_galactic_ln(Seraph_Galactic x);

/**
 * @brief Power function with derivative
 */
Seraph_Galactic seraph_galactic_pow(Seraph_Galactic base, Seraph_Galactic exp);

/**
 * @brief Power with Q128 exponent
 */
Seraph_Galactic seraph_galactic_pow_scalar(Seraph_Galactic base, Seraph_Q128 exp);

/*============================================================================
 * Galactic Hyperbolic Functions
 *============================================================================*/

Seraph_Galactic seraph_galactic_sinh(Seraph_Galactic x);
Seraph_Galactic seraph_galactic_cosh(Seraph_Galactic x);
Seraph_Galactic seraph_galactic_tanh(Seraph_Galactic x);

/*============================================================================
 * Galactic Comparison
 *============================================================================*/

/**
 * @brief Compare primal values (derivatives ignored)
 */
static inline Seraph_Vbit seraph_galactic_lt(Seraph_Galactic a, Seraph_Galactic b) {
    return seraph_q128_lt(a.primal, b.primal);
}

static inline Seraph_Vbit seraph_galactic_le(Seraph_Galactic a, Seraph_Galactic b) {
    return seraph_q128_le(a.primal, b.primal);
}

static inline Seraph_Vbit seraph_galactic_gt(Seraph_Galactic a, Seraph_Galactic b) {
    return seraph_q128_gt(a.primal, b.primal);
}

static inline Seraph_Vbit seraph_galactic_ge(Seraph_Galactic a, Seraph_Galactic b) {
    return seraph_q128_ge(a.primal, b.primal);
}

/*============================================================================
 * Galactic Prediction (Physics Integration)
 *============================================================================*/

/**
 * @brief Predict future position using derivative
 *
 * Given a Galactic position (primal = position, tangent = velocity),
 * predict the position at time dt in the future:
 *
 *   predicted_position = primal + tangent * dt
 *
 * This enables ANTICIPATION in physics simulations:
 * - If the cursor is moving quickly toward an orb, the orb can react
 *   before the cursor actually arrives.
 * - Smooth physics without explicit velocity tracking.
 *
 * @param pos Galactic position (primal=position, tangent=velocity)
 * @param dt Time delta (in whatever units velocity uses)
 * @return Predicted primal value at time t+dt
 */
static inline Seraph_Q128 seraph_galactic_predict(Seraph_Galactic pos,
                                                   Seraph_Q128 dt) {
    /* Check for VOID inputs */
    if (seraph_q128_is_void(seraph_q128_void_mask(dt)) ||
        seraph_galactic_is_void(pos)) {
        return SERAPH_Q128_VOID;
    }

    /* predicted = primal + tangent * dt */
    Seraph_Q128 delta = seraph_q128_mul(pos.tangent, dt);
    return seraph_q128_add(pos.primal, delta);
}

/**
 * @brief Compute relative velocity between two Galactic positions
 *
 * Returns the rate at which two objects are approaching (negative)
 * or receding (positive) from each other.
 *
 * @param a First position/velocity pair
 * @param b Second position/velocity pair
 * @return Relative velocity component along the line connecting them
 */
static inline Seraph_Q128 seraph_galactic_relative_velocity(Seraph_Galactic a,
                                                             Seraph_Galactic b) {
    /* Velocity difference in the direction of position difference */
    return seraph_q128_sub(a.tangent, b.tangent);
}

/**
 * @brief Create a Galactic position with velocity from separate values
 */
static inline Seraph_Galactic seraph_galactic_from_pos_vel(double pos, double vel) {
    return (Seraph_Galactic){
        seraph_q128_from_double(pos),
        seraph_q128_from_double(vel)
    };
}

/**
 * @brief Update a Galactic position based on its velocity and time delta
 *
 * This integrates velocity into position:
 *   new_primal = primal + tangent * dt
 *   new_tangent = tangent (velocity unchanged)
 *
 * For physics with acceleration, use seraph_galactic_integrate_accel.
 */
static inline Seraph_Galactic seraph_galactic_integrate(Seraph_Galactic pos,
                                                         Seraph_Q128 dt) {
    Seraph_Q128 new_pos = seraph_galactic_predict(pos, dt);
    return (Seraph_Galactic){ new_pos, pos.tangent };
}

/**
 * @brief Apply acceleration to velocity and integrate position
 *
 * Semi-implicit Euler integration:
 *   new_velocity = velocity + acceleration * dt
 *   new_position = position + new_velocity * dt
 */
static inline Seraph_Galactic seraph_galactic_integrate_accel(Seraph_Galactic pos,
                                                               Seraph_Q128 accel,
                                                               Seraph_Q128 dt) {
    /* Update velocity first */
    Seraph_Q128 new_vel = seraph_q128_add(pos.tangent, seraph_q128_mul(accel, dt));
    /* Then integrate position with new velocity */
    Seraph_Q128 new_pos = seraph_q128_add(pos.primal, seraph_q128_mul(new_vel, dt));
    return (Seraph_Galactic){ new_pos, new_vel };
}

/*============================================================================
 * Galactic Utility
 *============================================================================*/

/**
 * @brief Linear interpolation with derivative
 */
Seraph_Galactic seraph_galactic_lerp(Seraph_Galactic a, Seraph_Galactic b,
                                      Seraph_Galactic t);

/**
 * @brief Clamp primal to range
 */
Seraph_Galactic seraph_galactic_clamp(Seraph_Galactic x,
                                       Seraph_Q128 lo, Seraph_Q128 hi);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_GALACTIC_H */
