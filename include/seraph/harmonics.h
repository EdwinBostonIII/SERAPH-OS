/**
 * @file harmonics.h
 * @brief SERAPH Harmonic Synthesis via Chebyshev Recurrence
 *
 * MC26: SERAPH Performance Revolution - Pillar 4
 *
 * Generates higher harmonics sin(nθ) and cos(nθ) from a single seed
 * using Chebyshev recurrence relations. Zero redundant trig calls.
 *
 * Mathematical Basis:
 *   Chebyshev polynomials satisfy the recurrence:
 *     T_n(x) = 2x·T_{n-1}(x) - T_{n-2}(x)
 *
 *   For trig functions, with x = cos(θ):
 *     cos(nθ) = 2·cos(θ)·cos((n-1)θ) - cos((n-2)θ)
 *
 *   And for sin:
 *     sin(nθ) = 2·cos(θ)·sin((n-1)θ) - sin((n-2)θ)
 *
 *   Starting values:
 *     sin(0) = 0,  cos(0) = 1
 *     sin(θ) = s,  cos(θ) = c  (computed once)
 *
 * Applications:
 *   - Fourier synthesis (additive audio)
 *   - Spectral analysis
 *   - Waveform generation
 *   - Physics simulations with multiple modes
 */

#ifndef SERAPH_HARMONICS_H
#define SERAPH_HARMONICS_H

#include <stdint.h>
#include "seraph/q16_trig.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Harmonic Generator State (Q16.16)
 *============================================================================*/

/**
 * @brief Harmonic generator for Q16.16 precision
 *
 * Generates sin(nθ) and cos(nθ) for n = 0, 1, 2, 3, ...
 * using the Chebyshev recurrence.
 */
typedef struct {
    Q16 cos_theta;      /**< cos(θ) - the fundamental frequency */
    Q16 two_cos_theta;  /**< 2·cos(θ) - precomputed for efficiency */

    Q16 sin_prev;       /**< sin((n-1)θ) */
    Q16 sin_curr;       /**< sin(nθ) */
    Q16 cos_prev;       /**< cos((n-1)θ) */
    Q16 cos_curr;       /**< cos(nθ) */

    int harmonic;       /**< Current harmonic number n */
    Q16 theta;          /**< Base angle (for reference) */
} Seraph_Harmonic16;

/**
 * @brief Initialize harmonic generator
 *
 * @param harm Harmonic generator to initialize
 * @param theta Base angle in Q16 radians
 */
static inline void seraph_harmonic16_init(Seraph_Harmonic16* harm, Q16 theta) {
    harm->theta = theta;
    harm->harmonic = 0;

    /* Compute fundamental sin/cos */
    Q16 s, c;
    q16_sincos(theta, &s, &c);

    harm->cos_theta = c;
    harm->two_cos_theta = c << 1;  /* 2·cos(θ) */

    /* Initial values: n=0 gives sin(0)=0, cos(0)=1 */
    harm->sin_prev = 0;         /* sin(-θ) = -sin(θ) but we don't need it */
    harm->sin_curr = 0;         /* sin(0) = 0 */
    harm->cos_prev = c;         /* cos(-θ) = cos(θ) */
    harm->cos_curr = Q16_ONE;   /* cos(0) = 1 */
}

/**
 * @brief Advance to next harmonic
 *
 * Moves from harmonic n to harmonic n+1.
 * After this call, sin_curr = sin((n+1)θ), cos_curr = cos((n+1)θ).
 *
 * @param harm Harmonic generator
 */
static inline void seraph_harmonic16_next(Seraph_Harmonic16* harm) {
    /* Chebyshev recurrence:
     *   sin((n+1)θ) = 2·cos(θ)·sin(nθ) - sin((n-1)θ)
     *   cos((n+1)θ) = 2·cos(θ)·cos(nθ) - cos((n-1)θ)
     */
    Q16 new_sin = q16_mul(harm->two_cos_theta, harm->sin_curr) - harm->sin_prev;
    Q16 new_cos = q16_mul(harm->two_cos_theta, harm->cos_curr) - harm->cos_prev;

    harm->sin_prev = harm->sin_curr;
    harm->sin_curr = new_sin;
    harm->cos_prev = harm->cos_curr;
    harm->cos_curr = new_cos;
    harm->harmonic++;
}

/**
 * @brief Get current sin(nθ)
 */
static inline Q16 seraph_harmonic16_sin(const Seraph_Harmonic16* harm) {
    return harm->sin_curr;
}

/**
 * @brief Get current cos(nθ)
 */
static inline Q16 seraph_harmonic16_cos(const Seraph_Harmonic16* harm) {
    return harm->cos_curr;
}

/**
 * @brief Get current harmonic number
 */
static inline int seraph_harmonic16_n(const Seraph_Harmonic16* harm) {
    return harm->harmonic;
}

/**
 * @brief Reset to harmonic 0
 */
static inline void seraph_harmonic16_reset(Seraph_Harmonic16* harm) {
    harm->harmonic = 0;
    harm->sin_prev = 0;
    harm->sin_curr = 0;
    harm->cos_prev = harm->cos_theta;
    harm->cos_curr = Q16_ONE;
}

/**
 * @brief Generate all harmonics up to max_harmonic
 *
 * Fills sin_out[0..max_harmonic] with sin(0), sin(θ), sin(2θ), ...
 * Fills cos_out[0..max_harmonic] with cos(0), cos(θ), cos(2θ), ...
 *
 * @param theta Base angle in Q16 radians
 * @param max_harmonic Maximum harmonic to generate
 * @param sin_out Output array for sin values (size: max_harmonic+1)
 * @param cos_out Output array for cos values (size: max_harmonic+1)
 */
static inline void seraph_harmonic16_generate_all(
    Q16 theta,
    int max_harmonic,
    Q16* sin_out,
    Q16* cos_out)
{
    Seraph_Harmonic16 harm;
    seraph_harmonic16_init(&harm, theta);

    for (int n = 0; n <= max_harmonic; n++) {
        if (n > 0) {
            seraph_harmonic16_next(&harm);
        }
        sin_out[n] = harm.sin_curr;
        cos_out[n] = harm.cos_curr;
    }
}

/*============================================================================
 * Fourier Synthesis
 *============================================================================*/

/**
 * @brief Fourier series generator
 *
 * Synthesizes a signal from harmonic coefficients:
 *   f(θ) = Σ (a_n·cos(nθ) + b_n·sin(nθ))
 */
typedef struct {
    Seraph_Harmonic16 harm;     /**< Harmonic generator */
    const Q16* a_coeffs;        /**< Cosine coefficients */
    const Q16* b_coeffs;        /**< Sine coefficients */
    int num_harmonics;          /**< Number of harmonics */
} Seraph_Fourier16;

/**
 * @brief Initialize Fourier synthesizer
 *
 * @param fourier Synthesizer to initialize
 * @param theta Base angle
 * @param a_coeffs Cosine coefficients (a_0, a_1, ..., a_n)
 * @param b_coeffs Sine coefficients (b_0, b_1, ..., b_n)
 * @param num_harmonics Number of harmonics
 */
void seraph_fourier16_init(Seraph_Fourier16* fourier,
                            Q16 theta,
                            const Q16* a_coeffs,
                            const Q16* b_coeffs,
                            int num_harmonics);

/**
 * @brief Evaluate Fourier series at current angle
 *
 * @param fourier Synthesizer state
 * @return f(θ) = Σ (a_n·cos(nθ) + b_n·sin(nθ))
 */
Q16 seraph_fourier16_eval(Seraph_Fourier16* fourier);

/*============================================================================
 * Waveform Generation
 *============================================================================*/

/**
 * @brief Generate sawtooth wave using harmonics
 *
 * Sawtooth = Σ ((-1)^n / n) · sin(nθ) for n = 1, 2, 3, ...
 *
 * @param theta Angle in Q16 radians
 * @param num_harmonics Number of harmonics to use
 * @return Sawtooth wave value in Q16
 */
Q16 seraph_harmonic16_sawtooth(Q16 theta, int num_harmonics);

/**
 * @brief Generate square wave using harmonics
 *
 * Square = Σ (1/n) · sin(nθ) for odd n = 1, 3, 5, ...
 *
 * @param theta Angle in Q16 radians
 * @param num_harmonics Number of odd harmonics to use
 * @return Square wave value in Q16
 */
Q16 seraph_harmonic16_square(Q16 theta, int num_harmonics);

/**
 * @brief Generate triangle wave using harmonics
 *
 * Triangle = Σ ((-1)^n / n²) · sin(nθ) for odd n = 1, 3, 5, ...
 *
 * @param theta Angle in Q16 radians
 * @param num_harmonics Number of odd harmonics to use
 * @return Triangle wave value in Q16
 */
Q16 seraph_harmonic16_triangle(Q16 theta, int num_harmonics);

/*============================================================================
 * DFT/FFT Support
 *============================================================================*/

/**
 * @brief Precompute twiddle factors for FFT
 *
 * Generates W_N^k = exp(-2πik/N) = cos(2πk/N) - i·sin(2πk/N)
 * for k = 0, 1, ..., N/2-1.
 *
 * Uses harmonic generator for efficient computation.
 *
 * @param N FFT size (must be power of 2)
 * @param cos_out Output: cos twiddle factors (size: N/2)
 * @param sin_out Output: sin twiddle factors (size: N/2)
 */
void seraph_harmonic16_fft_twiddles(int N, Q16* cos_out, Q16* sin_out);

/*============================================================================
 * Stability and Accuracy
 *============================================================================*/

/**
 * @brief Renormalize harmonic state to prevent drift
 *
 * After many iterations, accumulated error can cause sin²+cos² ≠ 1.
 * This function corrects the drift.
 *
 * @param harm Harmonic generator to renormalize
 */
void seraph_harmonic16_normalize(Seraph_Harmonic16* harm);

/**
 * @brief Check accuracy of harmonic generator
 *
 * Computes sin²(nθ) + cos²(nθ) and returns deviation from 1.
 *
 * @param harm Harmonic generator to check
 * @return |sin² + cos² - 1| in Q16
 */
static inline Q16 seraph_harmonic16_accuracy(const Seraph_Harmonic16* harm) {
    Q16 sin2 = q16_mul(harm->sin_curr, harm->sin_curr);
    Q16 cos2 = q16_mul(harm->cos_curr, harm->cos_curr);
    Q16 mag = sin2 + cos2;
    Q16 error = mag - Q16_ONE;
    return (error < 0) ? -error : error;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_HARMONICS_H */
