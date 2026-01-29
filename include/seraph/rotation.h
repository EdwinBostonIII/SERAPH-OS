/**
 * @file rotation.h
 * @brief SERAPH Rotation State Machine
 *
 * MC26: SERAPH Performance Revolution - Pillar 3
 *
 * O(1) continuous rotation updates via complex number multiplication.
 * Instead of calling sin/cos every frame, we maintain a rotation state
 * and update it with incremental rotations.
 *
 * Mathematical Basis:
 *   Rotation by θ can be represented as complex multiplication:
 *     z' = z * (cos(Δ) + i*sin(Δ))
 *
 *   For a point (x, y) rotating by Δ:
 *     x' = x*cos(Δ) - y*sin(Δ)
 *     y' = x*sin(Δ) + y*cos(Δ)
 *
 *   Storing (sin_θ, cos_θ) state allows O(1) updates:
 *     sin_θ' = sin_θ*cos_Δ + cos_θ*sin_Δ
 *     cos_θ' = cos_θ*cos_Δ - sin_θ*sin_Δ
 *
 * Applications:
 *   - Continuous sprite rotation
 *   - Audio oscillators (sin wave generation)
 *   - Animation systems
 *   - Physics simulation (angular velocity)
 */

#ifndef SERAPH_ROTATION_H
#define SERAPH_ROTATION_H

#include <stdint.h>
#include "seraph/q16_trig.h"
#include "seraph/bmi2_intrin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Rotation State (Q16.16 precision)
 *============================================================================*/

/**
 * @brief Rotation state for Q16.16 precision
 *
 * Maintains current (sin, cos) pair for continuous rotation.
 * Also stores the rotation increment for efficient updates.
 */
typedef struct {
    Q16 sin_theta;      /**< Current sin(θ) */
    Q16 cos_theta;      /**< Current cos(θ) */
    Q16 sin_delta;      /**< Increment sin(Δ) for rotation step */
    Q16 cos_delta;      /**< Increment cos(Δ) for rotation step */
    Q16 theta;          /**< Current angle (for reference/reset) */
    Q16 delta;          /**< Angular increment per step */
} Seraph_Rotation16;

/**
 * @brief Initialize rotation state from angle
 *
 * @param rot Rotation state to initialize
 * @param initial_angle Starting angle in Q16 radians
 * @param angular_velocity Angular change per update in Q16 radians
 */
static inline void seraph_rotation16_init(Seraph_Rotation16* rot,
                                           Q16 initial_angle,
                                           Q16 angular_velocity) {
    rot->theta = initial_angle;
    rot->delta = angular_velocity;

    /* Compute initial sin/cos */
    q16_sincos(initial_angle, &rot->sin_theta, &rot->cos_theta);

    /* Compute increment sin/cos */
    q16_sincos(angular_velocity, &rot->sin_delta, &rot->cos_delta);
}

/**
 * @brief Update rotation state by one step (O(1))
 *
 * Applies complex multiplication to advance the rotation.
 * No trig function calls - just 4 multiplies and 2 adds.
 *
 * @param rot Rotation state to update
 */
static inline void seraph_rotation16_step(Seraph_Rotation16* rot) {
    /* sin_θ' = sin_θ*cos_Δ + cos_θ*sin_Δ */
    /* cos_θ' = cos_θ*cos_Δ - sin_θ*sin_Δ */
    Q16 new_sin = q16_mul(rot->sin_theta, rot->cos_delta) +
                  q16_mul(rot->cos_theta, rot->sin_delta);
    Q16 new_cos = q16_mul(rot->cos_theta, rot->cos_delta) -
                  q16_mul(rot->sin_theta, rot->sin_delta);

    rot->sin_theta = new_sin;
    rot->cos_theta = new_cos;
    rot->theta += rot->delta;
}

/**
 * @brief Apply rotation to a point
 *
 * Rotates (x, y) by current rotation angle.
 *
 * @param rot Rotation state
 * @param x X coordinate (modified in place)
 * @param y Y coordinate (modified in place)
 */
static inline void seraph_rotation16_apply(const Seraph_Rotation16* rot,
                                            Q16* x, Q16* y) {
    /* x' = x*cos - y*sin */
    /* y' = x*sin + y*cos */
    Q16 new_x = q16_mul(*x, rot->cos_theta) - q16_mul(*y, rot->sin_theta);
    Q16 new_y = q16_mul(*x, rot->sin_theta) + q16_mul(*y, rot->cos_theta);
    *x = new_x;
    *y = new_y;
}

/**
 * @brief Reset rotation to specific angle
 *
 * @param rot Rotation state
 * @param angle New angle in Q16 radians
 */
static inline void seraph_rotation16_set_angle(Seraph_Rotation16* rot,
                                                Q16 angle) {
    rot->theta = angle;
    q16_sincos(angle, &rot->sin_theta, &rot->cos_theta);
}

/**
 * @brief Set angular velocity
 *
 * @param rot Rotation state
 * @param velocity New angular velocity in Q16 radians per step
 */
static inline void seraph_rotation16_set_velocity(Seraph_Rotation16* rot,
                                                   Q16 velocity) {
    rot->delta = velocity;
    q16_sincos(velocity, &rot->sin_delta, &rot->cos_delta);
}

/**
 * @brief Renormalize rotation state
 *
 * Over many iterations, floating-point drift causes the sin²+cos² = 1
 * invariant to degrade. Call this periodically to fix.
 *
 * Uses Newton-Raphson: factor = (3 - (sin² + cos²)) / 2
 *
 * @param rot Rotation state to renormalize
 */
static inline void seraph_rotation16_normalize(Seraph_Rotation16* rot) {
    /* Compute sin² + cos² */
    Q16 mag_sq = q16_mul(rot->sin_theta, rot->sin_theta) +
                 q16_mul(rot->cos_theta, rot->cos_theta);

    /* Newton-Raphson step: scale = (3 - mag_sq) / 2 */
    Q16 scale = (Q16_FROM_INT(3) - mag_sq) >> 1;

    /* Apply correction */
    rot->sin_theta = q16_mul(rot->sin_theta, scale);
    rot->cos_theta = q16_mul(rot->cos_theta, scale);
}

/*============================================================================
 * Rotation State (Q32.32 precision)
 *============================================================================*/

/**
 * @brief Rotation state for Q32.32 precision
 */
typedef struct {
    int64_t sin_theta;      /**< Current sin(θ) in Q32.32 */
    int64_t cos_theta;      /**< Current cos(θ) in Q32.32 */
    int64_t sin_delta;      /**< Increment sin(Δ) */
    int64_t cos_delta;      /**< Increment cos(Δ) */
    int64_t theta;          /**< Current angle (reference) */
    int64_t delta;          /**< Angular increment */
} Seraph_Rotation32;

/**
 * @brief Initialize Q32.32 rotation state
 */
void seraph_rotation32_init(Seraph_Rotation32* rot,
                             int64_t initial_angle,
                             int64_t angular_velocity);

/**
 * @brief Update Q32.32 rotation by one step
 */
void seraph_rotation32_step(Seraph_Rotation32* rot);

/**
 * @brief Apply Q32.32 rotation to a point
 */
void seraph_rotation32_apply(const Seraph_Rotation32* rot,
                              int64_t* x, int64_t* y);

/**
 * @brief Renormalize Q32.32 rotation state
 */
void seraph_rotation32_normalize(Seraph_Rotation32* rot);

/*============================================================================
 * Oscillator (Audio-Focused Rotation)
 *============================================================================*/

/**
 * @brief Audio oscillator using rotation state machine
 *
 * Generates sine waves at specified frequency without calling sin().
 */
typedef struct {
    Seraph_Rotation16 state;    /**< Rotation state */
    Q16 amplitude;              /**< Output amplitude */
    uint32_t sample_rate;       /**< Samples per second */
    uint32_t frequency;         /**< Frequency in Hz */
} Seraph_Oscillator16;

/**
 * @brief Initialize oscillator
 *
 * @param osc Oscillator to initialize
 * @param frequency Frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @param amplitude Output amplitude in Q16
 */
void seraph_oscillator16_init(Seraph_Oscillator16* osc,
                               uint32_t frequency,
                               uint32_t sample_rate,
                               Q16 amplitude);

/**
 * @brief Generate next sample
 *
 * @param osc Oscillator state
 * @return Next sine sample in Q16
 */
static inline Q16 seraph_oscillator16_sample(Seraph_Oscillator16* osc) {
    Q16 sample = q16_mul(osc->state.sin_theta, osc->amplitude);
    seraph_rotation16_step(&osc->state);
    return sample;
}

/**
 * @brief Generate block of samples
 *
 * @param osc Oscillator state
 * @param buffer Output buffer
 * @param count Number of samples to generate
 */
void seraph_oscillator16_generate(Seraph_Oscillator16* osc,
                                   Q16* buffer, size_t count);

/**
 * @brief Set oscillator frequency
 *
 * @param osc Oscillator
 * @param frequency New frequency in Hz
 */
void seraph_oscillator16_set_frequency(Seraph_Oscillator16* osc,
                                        uint32_t frequency);

/*============================================================================
 * Batch Rotation
 *============================================================================*/

/**
 * @brief Rotate an array of points
 *
 * Efficiently rotates multiple points by the same angle.
 *
 * @param rot Rotation state
 * @param points Array of (x, y) pairs (interleaved)
 * @param count Number of points
 */
void seraph_rotation16_apply_batch(const Seraph_Rotation16* rot,
                                    Q16* points, size_t count);

/**
 * @brief Generate rotation matrix elements
 *
 * Returns the 2x2 rotation matrix as 4 Q16 values.
 *
 * @param rot Rotation state
 * @param m00 Output: cos(θ)
 * @param m01 Output: -sin(θ)
 * @param m10 Output: sin(θ)
 * @param m11 Output: cos(θ)
 */
static inline void seraph_rotation16_matrix(const Seraph_Rotation16* rot,
                                             Q16* m00, Q16* m01,
                                             Q16* m10, Q16* m11) {
    *m00 = rot->cos_theta;
    *m01 = -rot->sin_theta;
    *m10 = rot->sin_theta;
    *m11 = rot->cos_theta;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_ROTATION_H */
