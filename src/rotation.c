/**
 * @file rotation.c
 * @brief SERAPH Rotation State Machine Implementation
 *
 * MC26: SERAPH Performance Revolution - Pillar 3
 *
 * Implements Q32.32 rotation and oscillator functions.
 */

#include "seraph/rotation.h"
#include "seraph/bmi2_intrin.h"
#include <string.h>

/*============================================================================
 * Q32.32 Trig (Simplified - Uses Q16 with scaling)
 *
 * For full Q32 precision, would use micro-table approach.
 *============================================================================*/

/* Q32.32 constants */
#define Q32_ONE     0x100000000LL
#define Q32_PI      0x3243F6A88LL   /* π in Q32.32 */
#define Q32_2PI     0x6487ED511LL   /* 2π in Q32.32 */

/**
 * @brief Q32.32 sin/cos using Q16 internally
 */
static void q32_sincos_internal(int64_t angle, int64_t* sin_out, int64_t* cos_out) {
    /* Scale Q32.32 angle to Q16.16 */
    Q16 angle_q16 = (Q16)(angle >> 16);

    Q16 s, c;
    q16_sincos(angle_q16, &s, &c);

    /* Scale Q16.16 result to Q32.32 */
    *sin_out = (int64_t)s << 16;
    *cos_out = (int64_t)c << 16;
}

/*============================================================================
 * Q32.32 Rotation Implementation
 *============================================================================*/

void seraph_rotation32_init(Seraph_Rotation32* rot,
                             int64_t initial_angle,
                             int64_t angular_velocity) {
    if (rot == NULL) return;

    rot->theta = initial_angle;
    rot->delta = angular_velocity;

    /* Compute initial sin/cos */
    q32_sincos_internal(initial_angle, &rot->sin_theta, &rot->cos_theta);

    /* Compute increment sin/cos */
    q32_sincos_internal(angular_velocity, &rot->sin_delta, &rot->cos_delta);
}

void seraph_rotation32_step(Seraph_Rotation32* rot) {
    if (rot == NULL) return;

    /* Complex multiplication:
     * sin_θ' = sin_θ*cos_Δ + cos_θ*sin_Δ
     * cos_θ' = cos_θ*cos_Δ - sin_θ*sin_Δ
     */
    int64_t new_sin = seraph_q32_mul(rot->sin_theta, rot->cos_delta) +
                      seraph_q32_mul(rot->cos_theta, rot->sin_delta);
    int64_t new_cos = seraph_q32_mul(rot->cos_theta, rot->cos_delta) -
                      seraph_q32_mul(rot->sin_theta, rot->sin_delta);

    rot->sin_theta = new_sin;
    rot->cos_theta = new_cos;
    rot->theta += rot->delta;
}

void seraph_rotation32_apply(const Seraph_Rotation32* rot,
                              int64_t* x, int64_t* y) {
    if (rot == NULL || x == NULL || y == NULL) return;

    /* x' = x*cos - y*sin */
    /* y' = x*sin + y*cos */
    int64_t new_x = seraph_q32_mul(*x, rot->cos_theta) -
                    seraph_q32_mul(*y, rot->sin_theta);
    int64_t new_y = seraph_q32_mul(*x, rot->sin_theta) +
                    seraph_q32_mul(*y, rot->cos_theta);
    *x = new_x;
    *y = new_y;
}

void seraph_rotation32_normalize(Seraph_Rotation32* rot) {
    if (rot == NULL) return;

    /* Compute sin² + cos² */
    int64_t sin2 = seraph_q32_mul(rot->sin_theta, rot->sin_theta);
    int64_t cos2 = seraph_q32_mul(rot->cos_theta, rot->cos_theta);
    int64_t mag_sq = sin2 + cos2;

    /* Newton-Raphson: scale = (3 - mag_sq) / 2 */
    int64_t scale = ((int64_t)3 * Q32_ONE - mag_sq) >> 1;

    /* Apply correction */
    rot->sin_theta = seraph_q32_mul(rot->sin_theta, scale);
    rot->cos_theta = seraph_q32_mul(rot->cos_theta, scale);
}

/*============================================================================
 * Oscillator Implementation
 *============================================================================*/

void seraph_oscillator16_init(Seraph_Oscillator16* osc,
                               uint32_t frequency,
                               uint32_t sample_rate,
                               Q16 amplitude) {
    if (osc == NULL || sample_rate == 0) return;

    osc->frequency = frequency;
    osc->sample_rate = sample_rate;
    osc->amplitude = amplitude;

    /* Angular velocity = 2π * f / sample_rate */
    /* In Q16.16: (2π * f / sr) */
    int64_t omega = ((int64_t)Q16_2PI * frequency) / sample_rate;
    Q16 angular_velocity = (Q16)omega;

    seraph_rotation16_init(&osc->state, 0, angular_velocity);
}

void seraph_oscillator16_generate(Seraph_Oscillator16* osc,
                                   Q16* buffer, size_t count) {
    if (osc == NULL || buffer == NULL) return;

    for (size_t i = 0; i < count; i++) {
        buffer[i] = seraph_oscillator16_sample(osc);
    }
}

void seraph_oscillator16_set_frequency(Seraph_Oscillator16* osc,
                                        uint32_t frequency) {
    if (osc == NULL || osc->sample_rate == 0) return;

    osc->frequency = frequency;

    /* Recalculate angular velocity */
    int64_t omega = ((int64_t)Q16_2PI * frequency) / osc->sample_rate;
    seraph_rotation16_set_velocity(&osc->state, (Q16)omega);
}

/*============================================================================
 * Batch Rotation
 *============================================================================*/

void seraph_rotation16_apply_batch(const Seraph_Rotation16* rot,
                                    Q16* points, size_t count) {
    if (rot == NULL || points == NULL) return;

    Q16 cos_theta = rot->cos_theta;
    Q16 sin_theta = rot->sin_theta;

    /* Process pairs of (x, y) */
    for (size_t i = 0; i < count; i++) {
        Q16 x = points[i * 2];
        Q16 y = points[i * 2 + 1];

        points[i * 2]     = q16_mul(x, cos_theta) - q16_mul(y, sin_theta);
        points[i * 2 + 1] = q16_mul(x, sin_theta) + q16_mul(y, cos_theta);
    }
}

/*============================================================================
 * Multi-Frequency Oscillator Bank
 *============================================================================*/

/**
 * @brief Initialize multiple oscillators efficiently
 */
void seraph_oscillator_bank_init(Seraph_Oscillator16* oscs, size_t count,
                                  const uint32_t* frequencies,
                                  uint32_t sample_rate,
                                  Q16 amplitude) {
    for (size_t i = 0; i < count; i++) {
        seraph_oscillator16_init(&oscs[i], frequencies[i], sample_rate, amplitude);
    }
}

/**
 * @brief Generate mixed output from oscillator bank
 */
Q16 seraph_oscillator_bank_sample(Seraph_Oscillator16* oscs, size_t count) {
    int32_t sum = 0;

    for (size_t i = 0; i < count; i++) {
        sum += seraph_oscillator16_sample(&oscs[i]);
    }

    /* Clamp to Q16 range */
    if (sum > 0x7FFF) sum = 0x7FFF;
    if (sum < -0x8000) sum = -0x8000;

    return (Q16)sum;
}
