# MC5: Q128 Fixed-Point Numbers (Q64.64)

## What is Q128? (Plain English)

Q128 is SERAPH's fixed-point number format. It uses 128 bits total:
- 64 bits for the integer part (including sign)
- 64 bits for the fractional part

This gives you roughly 18 decimal digits of precision on both sides of the decimal point. Unlike floating-point, the precision is **uniform** - you get the same accuracy whether the number is 0.00001 or 1,000,000.

## Why Fixed-Point Instead of Floating-Point?

| Issue | Floating-Point | Q128 Fixed-Point |
|-------|----------------|------------------|
| Determinism | Can vary by CPU/compiler | Always identical |
| Rounding | Complex, often surprising | Simple truncation |
| Precision | Varies with magnitude | Uniform throughout |
| Implementation | Hardware-specific | Pure integer math |
| VOID semantics | NaN is different from VOID | Consistent with integers |

## The Q64.64 Format

```
┌───────────────────────────────────────────────────────────────────┐
│ Sign │           Integer Part (63 bits)                          │
│  1b  │           Bits 127-64                                      │
├───────────────────────────────────────────────────────────────────┤
│                    Fractional Part (64 bits)                      │
│                    Bits 63-0                                       │
└───────────────────────────────────────────────────────────────────┘
```

- **Value** = (integer_part + fractional_part / 2^64)
- **Range**: approximately ±9.22 × 10^18
- **Precision**: ~1.08 × 10^-19 (about 18 decimal places)

## VOID in Q128

Q128 VOID is all 1s, same as integers:
- High 64 bits = 0xFFFFFFFFFFFFFFFF
- Low 64 bits = 0xFFFFFFFFFFFFFFFF

This represents the most negative value (-1 in the usual interpretation), which we sacrifice to use as VOID.

## Basic Operations

### Creation

```c
// From integer
Seraph_Q128 a = seraph_q128_from_i64(42);      // 42.0
Seraph_Q128 b = seraph_q128_from_i64(-100);    // -100.0

// From fraction (numerator / denominator)
Seraph_Q128 half = seraph_q128_from_frac(1, 2);  // 0.5
Seraph_Q128 third = seraph_q128_from_frac(1, 3); // 0.333...

// From double (for initialization only - loses precision)
Seraph_Q128 pi = seraph_q128_from_double(3.14159265358979);

// Constants
Seraph_Q128 zero = SERAPH_Q128_ZERO;
Seraph_Q128 one = SERAPH_Q128_ONE;
Seraph_Q128 pi = SERAPH_Q128_PI;
Seraph_Q128 e = SERAPH_Q128_E;
```

### Arithmetic

```c
Seraph_Q128 sum = seraph_q128_add(a, b);
Seraph_Q128 diff = seraph_q128_sub(a, b);
Seraph_Q128 prod = seraph_q128_mul(a, b);
Seraph_Q128 quot = seraph_q128_div(a, b);  // VOID if b == 0

Seraph_Q128 neg = seraph_q128_neg(a);
Seraph_Q128 abs_val = seraph_q128_abs(a);
```

### Comparison

```c
Seraph_Vbit eq = seraph_q128_eq(a, b);    // a == b?
Seraph_Vbit lt = seraph_q128_lt(a, b);    // a < b?
Seraph_Vbit le = seraph_q128_le(a, b);    // a <= b?
Seraph_Vbit gt = seraph_q128_gt(a, b);    // a > b?
Seraph_Vbit ge = seraph_q128_ge(a, b);    // a >= b?

// All return VOID if either operand is VOID
```

## Transcendental Functions

Q128 provides all standard math functions using integer algorithms:

### Square Root (Newton-Raphson)
```c
Seraph_Q128 root = seraph_q128_sqrt(x);  // VOID if x < 0
```

### Trigonometric (Taylor/CORDIC)
```c
Seraph_Q128 sine = seraph_q128_sin(x);
Seraph_Q128 cosine = seraph_q128_cos(x);
Seraph_Q128 tangent = seraph_q128_tan(x);  // VOID at π/2, 3π/2, etc.

Seraph_Q128 arcsine = seraph_q128_asin(x);  // VOID if |x| > 1
Seraph_Q128 arccosine = seraph_q128_acos(x);
Seraph_Q128 arctangent = seraph_q128_atan(x);
Seraph_Q128 atan2_val = seraph_q128_atan2(y, x);
```

### Exponential and Logarithm
```c
Seraph_Q128 exp_val = seraph_q128_exp(x);
Seraph_Q128 ln_val = seraph_q128_ln(x);    // VOID if x <= 0
Seraph_Q128 log2_val = seraph_q128_log2(x);
Seraph_Q128 log10_val = seraph_q128_log10(x);

Seraph_Q128 power = seraph_q128_pow(base, exp);
```

### Hyperbolic
```c
Seraph_Q128 sinh_val = seraph_q128_sinh(x);
Seraph_Q128 cosh_val = seraph_q128_cosh(x);
Seraph_Q128 tanh_val = seraph_q128_tanh(x);
```

## Algorithm Implementations

### Square Root (Newton-Raphson)

```
x_{n+1} = (x_n + S/x_n) / 2

Starting guess: x_0 = S/2 (or use bit manipulation for better guess)
Converges quadratically (doubles correct digits each iteration)
```

### Sine (Taylor Series)

```
sin(x) = x - x³/3! + x⁵/5! - x⁷/7! + ...

First reduce x to [-π, π] range
Then use Taylor series with enough terms for 64-bit fractional precision
```

### Exponential (Taylor Series)

```
exp(x) = 1 + x + x²/2! + x³/3! + x⁴/4! + ...

First reduce x: exp(x) = exp(floor(x)) * exp(frac(x))
Use precomputed powers of e for integer part
Use Taylor for fractional part
```

### Natural Logarithm

```
Use identity: ln(x) = 2 * atanh((x-1)/(x+1))

Or Newton-Raphson on: y = e^x, find x = ln(y)
```

## Precision Guarantees

All operations maintain at least 60 bits of precision in the fractional part. This means:
- At least 18 decimal digits of accuracy
- Errors accumulate slowly (sub-ULP per operation)
- Suitable for financial calculations, physics simulations, etc.

## Conversion Functions

```c
// To integer (truncates fractional part)
int64_t i = seraph_q128_to_i64(x);

// To double (loses precision beyond ~15 digits)
double d = seraph_q128_to_double(x);

// To string (for display)
char buf[64];
seraph_q128_to_string(x, buf, sizeof(buf), 18);  // 18 decimal places
```

## Overflow Handling

Q128 operations use VOID mode by default:
- If result would overflow Q64.64 range, returns VOID
- Division by zero returns VOID
- sqrt of negative returns VOID
- ln of non-positive returns VOID

## Technical Specification

### Type Definition

```c
typedef struct {
    int64_t hi;   // Integer part (signed)
    uint64_t lo;  // Fractional part
} Seraph_Q128;
```

Alternatively, on platforms with `__int128`:
```c
typedef __int128 Seraph_Q128_Raw;
```

### Constants

```c
#define SERAPH_Q128_VOID   ((Seraph_Q128){ -1, UINT64_MAX })
#define SERAPH_Q128_ZERO   ((Seraph_Q128){ 0, 0 })
#define SERAPH_Q128_ONE    ((Seraph_Q128){ 1, 0 })
#define SERAPH_Q128_HALF   ((Seraph_Q128){ 0, 0x8000000000000000ULL })
```

### Multiplication Algorithm

Q128 × Q128 produces a 256-bit intermediate result. We keep the middle 128 bits:

```
       [  A_hi  ][  A_lo  ]   (128 bits)
     × [  B_hi  ][  B_lo  ]   (128 bits)
    ─────────────────────────
       [      result       ]   (middle 128 bits of 256)
```

This is equivalent to:
1. Multiply to get 256-bit result
2. Right-shift by 64 bits
3. Take low 128 bits

### Division Algorithm

Use Newton-Raphson to compute 1/B, then multiply:
```
A / B = A × (1/B)

1/B: Start with estimate, iterate:
  x_{n+1} = x_n × (2 - B × x_n)
```

## Example: Computing Distance

```c
Seraph_Q128 distance(Seraph_Q128 x1, Seraph_Q128 y1,
                      Seraph_Q128 x2, Seraph_Q128 y2) {
    Seraph_Q128 dx = seraph_q128_sub(x2, x1);
    Seraph_Q128 dy = seraph_q128_sub(y2, y1);

    Seraph_Q128 dx2 = seraph_q128_mul(dx, dx);
    Seraph_Q128 dy2 = seraph_q128_mul(dy, dy);

    Seraph_Q128 sum = seraph_q128_add(dx2, dy2);
    return seraph_q128_sqrt(sum);
}
```

## Source Files

| File | Description |
|------|-------------|
| `src/q128.c` | Q64.64 fixed-point arithmetic, transcendental functions |
| `include/seraph/q128.h` | Q128 structure, constants, API |

---

# MC26: Zero-FPU Architecture

## Overview

SERAPH's Zero-FPU architecture provides complete trigonometric and mathematical functionality using **only integer operations**. This is critical for:

- **Kernel code**: FPU state is expensive to save/restore during interrupts
- **Determinism**: Integer math is bit-exact across all platforms
- **Embedded systems**: Some targets lack FPU hardware
- **Security**: Timing-attack resistant (no floating-point timing variations)

The architecture consists of 6 pillars:

| Pillar | Component | Description |
|--------|-----------|-------------|
| 1 | Q16.16 Zero-Table | Polynomial trig using Chebyshev approximation |
| 2 | Q64.64 Micro-Table | 256-entry table with quadratic interpolation |
| 3 | Rotation FSM | O(1) rotation updates via complex multiplication |
| 4 | Harmonic Synthesis | Chebyshev recurrence for Fourier series |
| 5 | Tier Architecture | Q16/Q32/Q64 selection based on precision needs |
| 6 | Compiler Enforcement | Static analysis rejects FPU instruction generation |

## Q16.16 Fixed-Point (Pillar 1)

Q16.16 uses 32 bits: 16 for integer, 16 for fraction. Ideal for graphics, audio, and game logic.

### Format

```
┌─────────────────┬─────────────────┐
│  Integer (16b)  │  Fraction (16b) │
│   Bits 31-16    │    Bits 15-0    │
└─────────────────┴─────────────────┘

Value = raw_value / 65536.0
Range: -32768.0 to +32767.99998...
Precision: ~0.000015 (1/65536)
```

### Constants

```c
#define Q16_ONE      0x00010000    /* 1.0 */
#define Q16_HALF     0x00008000    /* 0.5 */
#define Q16_PI       0x0003243F    /* π ≈ 3.14159 */
#define Q16_2PI      0x0006487E    /* 2π */
#define Q16_PI_2     0x00019220    /* π/2 */
```

### Zero-Table Trigonometry

Q16 sin/cos use **Chebyshev polynomial approximation** - no lookup tables needed:

```c
/* Chebyshev coefficients for sin(x) on [0, π/4] */
/* sin(x) ≈ x - x³/6 + x⁵/120 - x⁷/5040 + ... */
/* Optimized: c1*x + c3*x³ + c5*x⁵ + c7*x⁷ */

Q16 q16_sin(Q16 angle);     /* ±1 ULP accuracy */
Q16 q16_cos(Q16 angle);     /* ±1 ULP accuracy */
void q16_sincos(Q16 angle, Q16* sin_out, Q16* cos_out);  /* Both at once */
```

### API

```c
/* Basic trig */
Q16 q16_sin(Q16 angle);
Q16 q16_cos(Q16 angle);
Q16 q16_tan(Q16 angle);
Q16 q16_atan2(Q16 y, Q16 x);

/* Square root and hypot */
Q16 q16_sqrt(Q16 x);
Q16 q16_hypot(Q16 x, Q16 y);  /* √(x² + y²) without overflow */

/* Arithmetic */
Q16 q16_mul(Q16 a, Q16 b);
Q16 q16_div(Q16 a, Q16 b);
```

## Q64.64 Fixed-Point (Pillar 2)

Q64.64 provides high precision for physics, scientific computing, and financial calculations.

### Micro-Table Design

Q64 uses a **256-entry lookup table** covering the first octant [0, π/4], with **quadratic interpolation** for sub-table precision:

```c
typedef struct {
    Q64 sin_val;      /* sin(i * step) */
    Q64 cos_val;      /* cos(i * step) */
    Q64 sin_deriv;    /* d(sin)/dθ = cos */
    Q64 cos_deriv;    /* d(cos)/dθ = -sin */
} Q64_Trig_Entry;

extern Q64_Trig_Entry q64_trig_table[256];
```

Interpolation formula:
```
sin(θ) ≈ sin(θ₀) + cos(θ₀)·Δ - ½·sin(θ₀)·Δ²
```

### API

```c
/* Initialize table (call once at startup) */
void q64_trig_init(void);

/* High-precision trig */
Q64 q64_sin(Q64 angle);
Q64 q64_cos(Q64 angle);
void q64_sincos(Q64 angle, Q64* sin_out, Q64* cos_out);
```

## Rotation State Machine (Pillar 3)

For continuous rotation (animations, physics), recomputing sin/cos each frame is wasteful. The rotation state machine provides **O(1) updates**:

```c
typedef struct {
    Q16 theta;          /* Current angle */
    Q16 delta;          /* Angular velocity */
    Q16 sin_theta;      /* sin(θ) - cached */
    Q16 cos_theta;      /* cos(θ) - cached */
    Q16 sin_delta;      /* sin(Δ) - precomputed */
    Q16 cos_delta;      /* cos(Δ) - precomputed */
} Seraph_Rotation16;
```

### Complex Multiplication Update

Instead of calling sin/cos, we use the angle addition formula as complex multiplication:

```
(cos θ' + i·sin θ') = (cos θ + i·sin θ) × (cos Δ + i·sin Δ)

sin θ' = sin θ·cos Δ + cos θ·sin Δ
cos θ' = cos θ·cos Δ - sin θ·sin Δ
```

This is **4 multiplies + 2 adds** instead of a full trig evaluation.

### API

```c
void seraph_rotation16_init(Seraph_Rotation16* rot, Q16 angle, Q16 velocity);
void seraph_rotation16_step(Seraph_Rotation16* rot);  /* O(1) update */
void seraph_rotation16_apply(const Seraph_Rotation16* rot, Q16* x, Q16* y);
```

### Oscillator

For audio synthesis, the oscillator wraps the rotation state machine:

```c
typedef struct {
    Seraph_Rotation16 state;
    uint32_t frequency;
    uint32_t sample_rate;
    Q16 amplitude;
} Seraph_Oscillator16;

void seraph_oscillator16_init(Seraph_Oscillator16* osc,
                               uint32_t freq, uint32_t sample_rate, Q16 amp);
Q16 seraph_oscillator16_sample(Seraph_Oscillator16* osc);  /* Get next sample */
```

## Harmonic Synthesis (Pillar 4)

For Fourier series and waveform generation, computing `sin(nθ)` for n=1,2,3,... naively requires O(n) trig calls. The **Chebyshev recurrence** reduces this to O(1) per harmonic:

```
sin(nθ) = 2·cos(θ)·sin((n-1)θ) - sin((n-2)θ)
cos(nθ) = 2·cos(θ)·cos((n-1)θ) - cos((n-2)θ)
```

### API

```c
typedef struct {
    Q16 theta;
    Q16 two_cos_theta;  /* 2·cos(θ) - precomputed */
    Q16 sin_prev, sin_curr;
    Q16 cos_prev, cos_curr;
    int harmonic;
} Seraph_Harmonic16;

void seraph_harmonic16_init(Seraph_Harmonic16* harm, Q16 theta);
void seraph_harmonic16_next(Seraph_Harmonic16* harm);  /* Advance to n+1 */
```

### Waveform Generation

```c
Q16 seraph_harmonic16_sawtooth(Q16 theta, int num_harmonics);
Q16 seraph_harmonic16_square(Q16 theta, int num_harmonics);
Q16 seraph_harmonic16_triangle(Q16 theta, int num_harmonics);
```

### FFT Twiddle Factors

```c
void seraph_harmonic16_fft_twiddles(int N, Q16* cos_out, Q16* sin_out);
```

## Tier Architecture (Pillar 5)

The tier system selects precision based on use case:

| Tier | Type | Precision | Use Case |
|------|------|-----------|----------|
| 1 | Q16.16 | ~4 decimal digits | Graphics, audio, games |
| 2 | Q32.32 | ~9 decimal digits | Physics, 3D transforms |
| 3 | Q64.64 | ~18 decimal digits | Scientific, financial |

### Selection Macros

```c
#define SERAPH_MATH_TIER_GRAPHICS  1
#define SERAPH_MATH_TIER_PHYSICS   2
#define SERAPH_MATH_TIER_PRECISION 3

/* Strand flags for integer-only mode */
#define SERAPH_STRAND_INTEGER_ONLY  0x01  /* Never use FPU */
#define SERAPH_STRAND_LAZY_FPU      0x02  /* Defer FPU save/restore */
```

## Branchless Math Cache (Pillar 5 Support)

For repeated calculations, the math cache provides **constant-time memoization**:

```c
typedef struct {
    Q16 angle;
    Q16 sin_val;
    Q16 cos_val;
    uint8_t valid;
} Seraph_Q16_Trig_Cache_Entry;

typedef struct {
    Seraph_Q16_Trig_Cache_Entry entries[256];
    uint32_t hits, misses;  /* Stats (optional) */
} Seraph_Q16_Trig_Cache;
```

The cache is **thread-local** (no locking) and uses **branchless selection**:

```c
/* Branchless select: returns a if cond != 0, else b */
static inline uint32_t seraph_select32(int cond, uint32_t a, uint32_t b) {
    uint32_t mask = -(uint32_t)(cond != 0);
    return (a & mask) | (b & ~mask);
}
```

## Compiler FPU Enforcement (Pillar 6)

The Seraphim compiler can **reject code that would generate FPU instructions**:

### Assembly Scanning

```c
int seraph_fpu_scan_asm(const char* asm_text, const char* filename);
```

Detects x87 (`fld`, `fsin`, `fcos`), SSE (`addss`, `mulsd`), and AVX (`vaddps`) mnemonics.

### IR Checking

```c
int seraph_fpu_check_function(const Celestial_Function* func);
int seraph_fpu_check_module(const Celestial_Module* module);
```

### Enforcement Levels

```c
void seraph_fpu_set_enforcement(int level);
/* 0 = allow FPU (userspace default) */
/* 1 = warn on FPU usage */
/* 2 = error on FPU usage (kernel default) */
```

### Pattern Optimization

The pattern optimizer detects mathematical patterns and replaces them with optimized versions:

| Pattern | Optimization |
|---------|--------------|
| `sin(x)` + `cos(x)` | Combined `sincos(x)` call |
| `x² + y²` | `hypot(x, y)²` (overflow-safe) |
| `x * 2^n` | `x << n` (strength reduction) |

## BMI2 Intrinsics

For high-performance 128-bit multiplication, the Zero-FPU system uses BMI2 instructions when available:

```c
/* MULX: Multiply without affecting flags */
static inline uint64_t seraph_mulx_u64(uint64_t a, uint64_t b, uint64_t* hi);

/* ADCX/ADOX: Parallel carry chains */
static inline uint64_t seraph_adcx_u64(uint64_t a, uint64_t b,
                                        uint8_t cf_in, uint8_t* cf_out);
```

Runtime detection falls back to standard multiplication if BMI2 is unavailable.

## Zero-FPU Source Files

| File | Description |
|------|-------------|
| `include/seraph/bmi2_intrin.h` | BMI2 intrinsics (MULX, ADCX, ADOX) |
| `include/seraph/q16_trig.h` | Q16.16 trig API |
| `src/q16_trig.c` | Q16.16 implementation (Chebyshev) |
| `include/seraph/q64_trig.h` | Q64.64 trig API |
| `src/q64_trig.c` | Q64.64 implementation (micro-table) |
| `include/seraph/rotation.h` | Rotation state machine API |
| `src/rotation.c` | Rotation/oscillator implementation |
| `include/seraph/harmonics.h` | Harmonic synthesis API |
| `src/harmonics.c` | Fourier/waveform implementation |
| `include/seraph/math_tier.h` | Tier selection macros |
| `include/seraph/math_cache.h` | Branchless cache API |
| `src/math_cache.c` | Thread-local cache implementation |
| `src/seraphim/fpu_check.c` | Compiler FPU enforcement |
| `src/seraphim/pattern_opt.c` | Pattern-based optimization |
