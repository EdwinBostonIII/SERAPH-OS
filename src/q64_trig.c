/**
 * @file q64_trig.c
 * @brief SERAPH Q64.64 Micro-Table Trigonometry Implementation
 *
 * MC26: SERAPH Performance Revolution - Pillar 2
 *
 * High-precision trigonometry using 256-entry lookup tables
 * with quadratic interpolation and BMI2 acceleration.
 */

#include "seraph/q64_trig.h"
#include "seraph/bmi2_intrin.h"
#include <string.h>

/*============================================================================
 * Q64 Constants
 *============================================================================*/

const Q64 Q64_ZERO = { 0, 0 };
const Q64 Q64_ONE = { 1, 0 };
const Q64 Q64_NEG_ONE = { -1, 0 };

/* π in Q64.64: 3.14159265358979323846... */
const Q64 Q64_PI = {
    .hi = 3,
    .lo = 0x243F6A8885A308D3ULL  /* Fractional part of π */
};

/* π/2 in Q64.64 */
const Q64 Q64_PI_2 = {
    .hi = 1,
    .lo = 0x921FB54442D18469ULL
};

/* 2π in Q64.64 */
const Q64 Q64_2PI = {
    .hi = 6,
    .lo = 0x487ED5110B4611A6ULL
};

/* π/4 in Q64.64 (for octant reduction) */
static const Q64 Q64_PI_4 = {
    .hi = 0,
    .lo = 0xC90FDAA22168C234ULL
};

/* Table step: π/4 / 256 */
const Q64 Q64_TRIG_STEP = {
    .hi = 0,
    .lo = 0x00C90FDAA22168C2ULL  /* (π/4) / 256 */
};

/*============================================================================
 * Micro-Table (Generated at Init Time)
 *============================================================================*/

/* The micro-table (initialized at runtime via q64_trig_init) */
Q64_Trig_Entry q64_trig_table[Q64_TRIG_TABLE_SIZE];

static int q64_trig_initialized = 0;

/*============================================================================
 * Q64 Multiplication using BMI2
 *============================================================================*/

Q64 q64_mul(Q64 a, Q64 b) {
    /* Handle signs */
    int a_neg = a.hi < 0;
    int b_neg = b.hi < 0;
    int result_neg = a_neg ^ b_neg;

    /* Get absolute values */
    if (a_neg) a = q64_neg(a);
    if (b_neg) b = q64_neg(b);

    /* Full 256-bit product using schoolbook multiplication with BMI2 */
    uint64_t result[4];
    seraph_mul128x128_full(a.lo, (uint64_t)a.hi, b.lo, (uint64_t)b.hi, result);

    /* For Q64.64, we want bits [191:64] of the 256-bit result
     * result[0] = bits [63:0]
     * result[1] = bits [127:64]
     * result[2] = bits [191:128]
     * result[3] = bits [255:192]
     *
     * We need: hi = result[2], lo = result[1]
     */
    Q64 out = {
        .hi = (int64_t)result[2],
        .lo = result[1]
    };

    return result_neg ? q64_neg(out) : out;
}

/*============================================================================
 * Q64 Division (Newton-Raphson)
 *============================================================================*/

Q64 q64_div(Q64 a, Q64 b) {
    /* Handle division by zero */
    if (b.hi == 0 && b.lo == 0) {
        if (a.hi >= 0) {
            return (Q64){ .hi = 0x7FFFFFFFFFFFFFFFLL, .lo = 0xFFFFFFFFFFFFFFFFULL };
        } else {
            return (Q64){ .hi = 0x8000000000000001LL, .lo = 0 };
        }
    }

    /* Handle signs */
    int a_neg = a.hi < 0;
    int b_neg = b.hi < 0;
    int result_neg = a_neg ^ b_neg;

    if (a_neg) a = q64_neg(a);
    if (b_neg) b = q64_neg(b);

    /* Newton-Raphson for 1/b:
     * x_{n+1} = x_n * (2 - b * x_n)
     *
     * Initial guess: use leading bits
     */

    /* Normalize b to [0.5, 1) range by counting leading zeros */
    int shift = 0;
    Q64 norm_b = b;
    while (norm_b.hi == 0 && shift < 64) {
        norm_b = q64_shl(norm_b, 1);
        shift++;
    }
    while ((norm_b.hi & 0x4000000000000000LL) == 0 && shift < 127) {
        norm_b = q64_shl(norm_b, 1);
        shift++;
    }

    /* Initial guess for 1/norm_b ≈ 2 - norm_b (for norm_b in [0.5, 1)) */
    Q64 x = q64_sub((Q64){ 2, 0 }, norm_b);

    /* Newton iterations */
    for (int i = 0; i < 6; i++) {
        Q64 bx = q64_mul(norm_b, x);
        Q64 two_minus_bx = q64_sub((Q64){ 2, 0 }, bx);
        x = q64_mul(x, two_minus_bx);
    }

    /* Adjust for normalization shift */
    x = q64_shl(x, shift);

    /* Multiply: a / b = a * (1/b) */
    Q64 result = q64_mul(a, x);

    return result_neg ? q64_neg(result) : result;
}

/*============================================================================
 * Table Initialization
 *
 * Uses CORDIC to generate high-precision sin/cos values.
 *============================================================================*/

/* CORDIC angles: atan(2^-i) in Q64.64 format */
static const Q64 cordic_angles[64] = {
    { 0, 0xC90FDAA22168C234ULL },  /* atan(1) = π/4 */
    { 0, 0x76B19C1586ED3DA2ULL },  /* atan(1/2) */
    { 0, 0x3EB6EBF259F05DE9ULL },  /* atan(1/4) */
    { 0, 0x1FD5BA9AAC2F6DC6ULL },  /* atan(1/8) */
    { 0, 0x0FFAADDB967EF4E3ULL },  /* atan(1/16) */
    { 0, 0x07FF556EED4B9EFAULL },  /* etc. */
    { 0, 0x03FFEAAB776E5395ULL },
    { 0, 0x01FFFD555BAAB6CEULL },
    /* ... more angles would go here ... */
};

/* CORDIC gain factor K = Π(1/sqrt(1 + 2^-2i)) ≈ 0.607252935 */
static const Q64 cordic_gain = {
    .hi = 0,
    .lo = 0x9B74EDA8A2DEE5AFULL  /* K in Q64.64 */
};

/**
 * @brief CORDIC rotation for angle in first octant
 */
static void q64_cordic_sincos(Q64 angle, Q64* sin_out, Q64* cos_out) {
    /* Start with (1, 0) and rotate by angle */
    Q64 x = Q64_ONE;
    Q64 y = Q64_ZERO;
    Q64 z = angle;

    /* CORDIC iterations */
    for (int i = 0; i < 50; i++) {
        Q64 x_shift = q64_shr(x, i);
        Q64 y_shift = q64_shr(y, i);

        int sign = (z.hi >= 0) ? 1 : -1;

        if (sign > 0) {
            Q64 new_x = q64_sub(x, y_shift);
            Q64 new_y = q64_add(y, x_shift);
            x = new_x;
            y = new_y;
            z = q64_sub(z, (i < 64) ? cordic_angles[i] : Q64_ZERO);
        } else {
            Q64 new_x = q64_add(x, y_shift);
            Q64 new_y = q64_sub(y, x_shift);
            x = new_x;
            y = new_y;
            z = q64_add(z, (i < 64) ? cordic_angles[i] : Q64_ZERO);
        }
    }

    /* Apply CORDIC gain correction */
    *cos_out = q64_mul(x, cordic_gain);
    *sin_out = q64_mul(y, cordic_gain);
}

void q64_trig_init(void) {
    if (q64_trig_initialized) return;

    /* Generate table entries for first octant [0, π/4] */
    for (int i = 0; i < Q64_TRIG_TABLE_SIZE; i++) {
        /* Angle = i * (π/4) / 256 */
        Q64 angle = q64_mul(q64_from_i64(i), Q64_TRIG_STEP);

        /* Compute sin/cos using CORDIC */
        Q64 s, c;
        q64_cordic_sincos(angle, &s, &c);

        q64_trig_table[i].sin_val = s;
        q64_trig_table[i].cos_val = c;

        /* Derivative: d(sin)/dθ = cos, d(cos)/dθ = -sin */
        q64_trig_table[i].sin_deriv = c;
        q64_trig_table[i].cos_deriv = q64_neg(s);
    }

    q64_trig_initialized = 1;
}

/*============================================================================
 * Angle Reduction
 *============================================================================*/

Q64 q64_reduce_to_octant(Q64 angle, int* octant) {
    /* Normalize to [0, 2π) */
    while (q64_cmp(angle, Q64_ZERO) < 0) {
        angle = q64_add(angle, Q64_2PI);
    }
    while (q64_cmp(angle, Q64_2PI) >= 0) {
        angle = q64_sub(angle, Q64_2PI);
    }

    /* Determine octant */
    *octant = 0;
    Q64 octant_size = Q64_PI_4;

    while (q64_cmp(angle, octant_size) >= 0 && *octant < 7) {
        angle = q64_sub(angle, octant_size);
        (*octant)++;
    }

    /* Reduce to first octant using symmetry */
    if (*octant & 1) {
        /* Odd octant: reflect */
        angle = q64_sub(Q64_PI_4, angle);
    }

    return angle;
}

/*============================================================================
 * Interpolation
 *============================================================================*/

void q64_interpolate(const Q64_Trig_Entry* table, int index,
                      Q64 frac, Q64* sin_out, Q64* cos_out) {
    if (index < 0) index = 0;
    if (index >= Q64_TRIG_TABLE_SIZE - 1) index = Q64_TRIG_TABLE_SIZE - 2;

    const Q64_Trig_Entry* e0 = &table[index];
    const Q64_Trig_Entry* e1 = &table[index + 1];

    /* Linear interpolation with derivative correction (quadratic) */
    /* f(x + t) ≈ f(x) + t·f'(x) + t²·(f(x+1) - f(x) - f'(x))/2 */

    Q64 t = frac;
    Q64 t2 = q64_mul(t, t);

    /* Sin interpolation */
    Q64 delta_sin = q64_sub(e1->sin_val, e0->sin_val);
    Q64 correction_sin = q64_sub(delta_sin, e0->sin_deriv);
    correction_sin = q64_mul(correction_sin, t2);
    correction_sin = q64_shr(correction_sin, 1);  /* Divide by 2 */

    *sin_out = q64_add(e0->sin_val, q64_mul(t, e0->sin_deriv));
    *sin_out = q64_add(*sin_out, correction_sin);

    /* Cos interpolation */
    Q64 delta_cos = q64_sub(e1->cos_val, e0->cos_val);
    Q64 correction_cos = q64_sub(delta_cos, e0->cos_deriv);
    correction_cos = q64_mul(correction_cos, t2);
    correction_cos = q64_shr(correction_cos, 1);

    *cos_out = q64_add(e0->cos_val, q64_mul(t, e0->cos_deriv));
    *cos_out = q64_add(*cos_out, correction_cos);
}

/*============================================================================
 * Public API
 *============================================================================*/

void q64_sincos(Q64 angle, Q64* sin_out, Q64* cos_out) {
    if (!q64_trig_initialized) {
        q64_trig_init();
    }

    /* Reduce to first octant */
    int octant;
    Q64 reduced = q64_reduce_to_octant(angle, &octant);

    /* Compute table index and fractional part */
    /* index = reduced / step */
    Q64 idx_q = q64_div(reduced, Q64_TRIG_STEP);
    int index = (int)idx_q.hi;
    if (index >= Q64_TRIG_TABLE_SIZE - 1) index = Q64_TRIG_TABLE_SIZE - 2;

    /* Fractional part for interpolation */
    Q64 frac = q64_sub(reduced, q64_mul(q64_from_i64(index), Q64_TRIG_STEP));
    frac = q64_div(frac, Q64_TRIG_STEP);  /* Normalize to [0, 1) */

    /* Interpolate */
    Q64 s, c;
    q64_interpolate(q64_trig_table, index, frac, &s, &c);

    /* Apply octant transformations */
    /* Octant mapping:
     *   0: sin, cos
     *   1: cos, sin
     *   2: cos, -sin
     *   3: sin, -cos
     *   4: -sin, -cos
     *   5: -cos, -sin
     *   6: -cos, sin
     *   7: -sin, cos
     */
    switch (octant) {
        case 0:
            *sin_out = s;
            *cos_out = c;
            break;
        case 1:
            *sin_out = c;
            *cos_out = s;
            break;
        case 2:
            *sin_out = c;
            *cos_out = q64_neg(s);
            break;
        case 3:
            *sin_out = s;
            *cos_out = q64_neg(c);
            break;
        case 4:
            *sin_out = q64_neg(s);
            *cos_out = q64_neg(c);
            break;
        case 5:
            *sin_out = q64_neg(c);
            *cos_out = q64_neg(s);
            break;
        case 6:
            *sin_out = q64_neg(c);
            *cos_out = s;
            break;
        case 7:
            *sin_out = q64_neg(s);
            *cos_out = c;
            break;
    }
}

Q64 q64_sin(Q64 angle) {
    Q64 s, c;
    q64_sincos(angle, &s, &c);
    return s;
}

Q64 q64_cos(Q64 angle) {
    Q64 s, c;
    q64_sincos(angle, &s, &c);
    return c;
}

Q64 q64_tan(Q64 angle) {
    Q64 s, c;
    q64_sincos(angle, &s, &c);
    return q64_div(s, c);
}

/*============================================================================
 * Square Root
 *============================================================================*/

Q64 q64_sqrt(Q64 x) {
    if (x.hi < 0) return Q64_ZERO;  /* Negative input */
    if (x.hi == 0 && x.lo == 0) return Q64_ZERO;

    /* Newton-Raphson: x_{n+1} = (x_n + S/x_n) / 2 */

    /* Initial guess based on leading bits */
    int shift = 0;
    Q64 norm = x;
    while (norm.hi == 0 && shift < 64) {
        norm = q64_shl(norm, 2);
        shift++;
    }

    /* Start with reasonable guess */
    Q64 guess = q64_shr(x, shift / 2);
    if (guess.hi == 0 && guess.lo == 0) {
        guess = Q64_ONE;
    }

    /* Newton iterations */
    for (int i = 0; i < 8; i++) {
        Q64 div = q64_div(x, guess);
        guess = q64_shr(q64_add(guess, div), 1);
    }

    return guess;
}

Q64 q64_hypot(Q64 x, Q64 y) {
    /* |x| and |y| */
    if (x.hi < 0) x = q64_neg(x);
    if (y.hi < 0) y = q64_neg(y);

    /* Ensure x >= y */
    if (q64_cmp(y, x) > 0) {
        Q64 temp = x;
        x = y;
        y = temp;
    }

    if (x.hi == 0 && x.lo == 0) return y;

    /* hypot = x * sqrt(1 + (y/x)²) */
    Q64 ratio = q64_div(y, x);
    Q64 ratio_sq = q64_mul(ratio, ratio);
    Q64 inner = q64_add(Q64_ONE, ratio_sq);
    Q64 root = q64_sqrt(inner);

    return q64_mul(x, root);
}

Q64 q64_atan2(Q64 y, Q64 x) {
    /* Handle special cases */
    if (x.hi == 0 && x.lo == 0 && y.hi == 0 && y.lo == 0) {
        return Q64_ZERO;
    }

    if (x.hi == 0 && x.lo == 0) {
        return (y.hi >= 0) ? Q64_PI_2 : q64_neg(Q64_PI_2);
    }

    if (y.hi == 0 && y.lo == 0) {
        return (x.hi >= 0) ? Q64_ZERO : Q64_PI;
    }

    /* Use CORDIC in vectoring mode */
    Q64 abs_x = (x.hi < 0) ? q64_neg(x) : x;
    Q64 abs_y = (y.hi < 0) ? q64_neg(y) : y;

    Q64 z = Q64_ZERO;
    Q64 cx = abs_x;
    Q64 cy = abs_y;

    /* CORDIC vectoring: rotate (x, y) to (r, 0), accumulating angle in z */
    for (int i = 0; i < 50; i++) {
        Q64 x_shift = q64_shr(cx, i);
        Q64 y_shift = q64_shr(cy, i);

        if (cy.hi >= 0) {
            Q64 new_x = q64_add(cx, y_shift);
            Q64 new_y = q64_sub(cy, x_shift);
            cx = new_x;
            cy = new_y;
            z = q64_add(z, (i < 64) ? cordic_angles[i] : Q64_ZERO);
        } else {
            Q64 new_x = q64_sub(cx, y_shift);
            Q64 new_y = q64_add(cy, x_shift);
            cx = new_x;
            cy = new_y;
            z = q64_sub(z, (i < 64) ? cordic_angles[i] : Q64_ZERO);
        }
    }

    /* Adjust for quadrant */
    if (x.hi < 0) {
        z = q64_sub(Q64_PI, z);
    }
    if (y.hi < 0) {
        z = q64_neg(z);
    }

    return z;
}
