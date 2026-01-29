/**
 * @file harmonics.c
 * @brief SERAPH Harmonic Synthesis Implementation
 *
 * MC26: SERAPH Performance Revolution - Pillar 4
 *
 * Implements Fourier synthesis, waveform generation, and FFT twiddles.
 */

#include "seraph/harmonics.h"
#include <string.h>

/*============================================================================
 * Fourier Synthesis
 *============================================================================*/

void seraph_fourier16_init(Seraph_Fourier16* fourier,
                            Q16 theta,
                            const Q16* a_coeffs,
                            const Q16* b_coeffs,
                            int num_harmonics) {
    if (fourier == NULL) return;

    seraph_harmonic16_init(&fourier->harm, theta);
    fourier->a_coeffs = a_coeffs;
    fourier->b_coeffs = b_coeffs;
    fourier->num_harmonics = num_harmonics;
}

Q16 seraph_fourier16_eval(Seraph_Fourier16* fourier) {
    if (fourier == NULL) return 0;

    seraph_harmonic16_reset(&fourier->harm);

    int32_t sum = 0;

    for (int n = 0; n <= fourier->num_harmonics; n++) {
        if (n > 0) {
            seraph_harmonic16_next(&fourier->harm);
        }

        Q16 cos_n = fourier->harm.cos_curr;
        Q16 sin_n = fourier->harm.sin_curr;

        if (fourier->a_coeffs) {
            sum += q16_mul(fourier->a_coeffs[n], cos_n);
        }
        if (fourier->b_coeffs) {
            sum += q16_mul(fourier->b_coeffs[n], sin_n);
        }
    }

    /* Clamp to Q16 range */
    if (sum > 0x7FFFFFFF) sum = 0x7FFFFFFF;
    if (sum < -0x7FFFFFFF) sum = -0x7FFFFFFF;

    return (Q16)sum;
}

/*============================================================================
 * Waveform Generation
 *============================================================================*/

Q16 seraph_harmonic16_sawtooth(Q16 theta, int num_harmonics) {
    if (num_harmonics < 1) return 0;

    Seraph_Harmonic16 harm;
    seraph_harmonic16_init(&harm, theta);

    int32_t sum = 0;
    int32_t sign = Q16_ONE;

    for (int n = 1; n <= num_harmonics; n++) {
        seraph_harmonic16_next(&harm);

        /* (-1)^n / n */
        Q16 coeff = q16_div(sign, Q16_FROM_INT(n));
        sum += q16_mul(coeff, harm.sin_curr);

        sign = -sign;  /* Alternate sign */
    }

    /* Scale: sawtooth = -(2/π) * Σ(...) */
    /* Approximate 2/π ≈ 0.6366 ≈ 0xA2F9 in Q16 */
    sum = q16_mul(sum, (Q16)0xA2F9);
    sum = -sum;

    return (Q16)sum;
}

Q16 seraph_harmonic16_square(Q16 theta, int num_harmonics) {
    if (num_harmonics < 1) return 0;

    Seraph_Harmonic16 harm;
    seraph_harmonic16_init(&harm, theta);

    int32_t sum = 0;

    /* Only odd harmonics: n = 1, 3, 5, 7, ... */
    for (int k = 0; k < num_harmonics; k++) {
        int n = 2 * k + 1;  /* Odd: 1, 3, 5, ... */

        /* Advance to harmonic n */
        while (harm.harmonic < n) {
            seraph_harmonic16_next(&harm);
        }

        /* 1/n */
        Q16 coeff = q16_div(Q16_ONE, Q16_FROM_INT(n));
        sum += q16_mul(coeff, harm.sin_curr);
    }

    /* Scale: square = (4/π) * Σ(...) */
    /* 4/π ≈ 1.2732 ≈ 0x145F3 in Q16 */
    sum = q16_mul(sum, (Q16)0x145F3);

    return (Q16)sum;
}

Q16 seraph_harmonic16_triangle(Q16 theta, int num_harmonics) {
    if (num_harmonics < 1) return 0;

    Seraph_Harmonic16 harm;
    seraph_harmonic16_init(&harm, theta);

    int32_t sum = 0;
    int32_t sign = Q16_ONE;

    /* Only odd harmonics with (-1)^k / n² */
    for (int k = 0; k < num_harmonics; k++) {
        int n = 2 * k + 1;  /* Odd: 1, 3, 5, ... */

        /* Advance to harmonic n */
        while (harm.harmonic < n) {
            seraph_harmonic16_next(&harm);
        }

        /* (-1)^k / n² */
        Q16 n_sq = Q16_FROM_INT(n * n);
        Q16 coeff = q16_div(sign, n_sq);
        sum += q16_mul(coeff, harm.sin_curr);

        sign = -sign;
    }

    /* Scale: triangle = (8/π²) * Σ(...) */
    /* 8/π² ≈ 0.8106 ≈ 0xCF6C in Q16 */
    sum = q16_mul(sum, (Q16)0xCF6C);

    return (Q16)sum;
}

/*============================================================================
 * FFT Twiddle Factors
 *============================================================================*/

void seraph_harmonic16_fft_twiddles(int N, Q16* cos_out, Q16* sin_out) {
    if (N <= 0 || cos_out == NULL || sin_out == NULL) return;

    /* W_N^k = exp(-2πik/N) = cos(2πk/N) - i·sin(2πk/N)
     *
     * Generate using harmonic recurrence with θ = 2π/N
     */
    Q16 theta = q16_div(Q16_2PI, Q16_FROM_INT(N));

    Seraph_Harmonic16 harm;
    seraph_harmonic16_init(&harm, theta);

    int half_N = N / 2;

    for (int k = 0; k < half_N; k++) {
        /* For k=0, we have cos(0)=1, sin(0)=0 */
        if (k == 0) {
            cos_out[0] = Q16_ONE;
            sin_out[0] = 0;
        } else {
            /* Advance to harmonic k */
            while (harm.harmonic < k) {
                seraph_harmonic16_next(&harm);
            }
            cos_out[k] = harm.cos_curr;
            sin_out[k] = -harm.sin_curr;  /* Negative for DFT convention */
        }
    }
}

/*============================================================================
 * Stability
 *============================================================================*/

void seraph_harmonic16_normalize(Seraph_Harmonic16* harm) {
    if (harm == NULL) return;

    /* Normalize current values */
    Q16 sin2 = q16_mul(harm->sin_curr, harm->sin_curr);
    Q16 cos2 = q16_mul(harm->cos_curr, harm->cos_curr);
    Q16 mag_sq = sin2 + cos2;

    if (mag_sq == 0) return;

    /* Newton-Raphson for 1/sqrt(mag_sq):
     * x = (3 - mag_sq) / 2  (one iteration starting from x=1)
     */
    Q16 scale = (Q16_FROM_INT(3) - mag_sq) >> 1;

    harm->sin_curr = q16_mul(harm->sin_curr, scale);
    harm->cos_curr = q16_mul(harm->cos_curr, scale);

    /* Also normalize prev values */
    sin2 = q16_mul(harm->sin_prev, harm->sin_prev);
    cos2 = q16_mul(harm->cos_prev, harm->cos_prev);
    mag_sq = sin2 + cos2;

    if (mag_sq != 0) {
        scale = (Q16_FROM_INT(3) - mag_sq) >> 1;
        harm->sin_prev = q16_mul(harm->sin_prev, scale);
        harm->cos_prev = q16_mul(harm->cos_prev, scale);
    }
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Generate sine wave samples efficiently
 */
void seraph_harmonic16_sine_wave(Q16 frequency, Q16 sample_rate,
                                  Q16* buffer, size_t count) {
    if (buffer == NULL || count == 0) return;

    /* Angular step = 2π * f / sr */
    Q16 theta = q16_div(q16_mul(Q16_2PI, frequency), sample_rate);

    Seraph_Harmonic16 harm;
    seraph_harmonic16_init(&harm, theta);

    /* First sample is sin(0) = 0, but we want sin(theta) for first sample */
    seraph_harmonic16_next(&harm);

    for (size_t i = 0; i < count; i++) {
        buffer[i] = harm.sin_curr;
        seraph_harmonic16_next(&harm);

        /* Periodic renormalization for long buffers */
        if ((i & 0xFFF) == 0xFFF) {
            seraph_harmonic16_normalize(&harm);
        }
    }
}

/**
 * @brief Compute power spectrum component
 *
 * For signal f(t), returns |F(k)|² where F is DFT.
 */
Q16 seraph_harmonic16_power_at(const Q16* signal, size_t length, int k) {
    if (signal == NULL || length == 0) return 0;

    /* DFT at frequency k:
     * F(k) = Σ f(n) * exp(-2πikn/N)
     *      = Σ f(n) * (cos(2πkn/N) - i·sin(2πkn/N))
     */
    Q16 theta = q16_div(Q16_2PI, Q16_FROM_INT((int)length));
    theta = q16_mul(theta, Q16_FROM_INT(k));

    Seraph_Harmonic16 harm;
    seraph_harmonic16_init(&harm, theta);

    int32_t real_sum = 0;
    int32_t imag_sum = 0;

    for (size_t n = 0; n < length; n++) {
        if (n > 0) {
            seraph_harmonic16_next(&harm);
        }

        real_sum += q16_mul(signal[n], harm.cos_curr);
        imag_sum -= q16_mul(signal[n], harm.sin_curr);
    }

    /* Power = real² + imag² */
    Q16 real = (Q16)(real_sum >> 8);  /* Scale down to prevent overflow */
    Q16 imag = (Q16)(imag_sum >> 8);

    return q16_mul(real, real) + q16_mul(imag, imag);
}
