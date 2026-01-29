# MC4: Entropic Arithmetic

## What is Entropic Arithmetic? (Plain English)

Entropic Arithmetic is SERAPH's integer math system that gives you control over what happens when numbers overflow. Instead of silently wrapping (like C) or crashing (like some languages), you choose the behavior:

1. **VOID Mode**: Overflow returns VOID (safest - you know when something went wrong)
2. **WRAP Mode**: Overflow wraps around like C (fastest - for when you want modular arithmetic)
3. **SATURATE Mode**: Overflow clamps to max/min (useful for graphics, audio, control systems)

## The Problem

In standard C, integer overflow is:
- **Undefined behavior** for signed integers (compiler can do anything!)
- **Wraps around** for unsigned integers (often unexpected)

This causes bugs:
```c
int32_t a = 2000000000;
int32_t b = 2000000000;
int32_t c = a + b;  // UNDEFINED BEHAVIOR in C!
```

## The Three Modes

### VOID Mode (SERAPH_ARITH_VOID)

When an operation would overflow, return VOID instead.

```c
uint32_t a = 0xFFFFFFFE;
uint32_t b = 10;
uint32_t c = seraph_add_u32(a, b, SERAPH_ARITH_VOID);
// c = VOID (0xFFFFFFFF) because 0xFFFFFFFE + 10 overflows
```

**Best for**: Safety-critical code where you need to know if overflow occurred.

### WRAP Mode (SERAPH_ARITH_WRAP)

When an operation would overflow, wrap around (modular arithmetic).

```c
uint32_t a = 0xFFFFFFFE;
uint32_t b = 10;
uint32_t c = seraph_add_u32(a, b, SERAPH_ARITH_WRAP);
// c = 8 (wrapped around)
```

**Best for**: Hash functions, checksums, performance-critical code where wrapping is intentional.

### SATURATE Mode (SERAPH_ARITH_SATURATE)

When an operation would overflow, clamp to the maximum (or minimum) representable value.

```c
uint32_t a = 0xFFFFFFFE;
uint32_t b = 10;
uint32_t c = seraph_add_u32(a, b, SERAPH_ARITH_SATURATE);
// c = 0xFFFFFFFE (saturated at max, but max is VOID, so actually 0xFFFFFFFE - 1)
```

Wait, there's a subtlety: Since VOID uses the maximum value (0xFF...FF), saturation stops one short of VOID to keep values distinguishable from errors.

**Best for**: Graphics (color values), audio (sample values), control systems (actuator limits).

## Operations

### Addition
```c
uint64_t seraph_add_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode);
int64_t seraph_add_i64(int64_t a, int64_t b, Seraph_ArithMode mode);
```

### Subtraction
```c
uint64_t seraph_sub_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode);
int64_t seraph_sub_i64(int64_t a, int64_t b, Seraph_ArithMode mode);
```

### Multiplication
```c
uint64_t seraph_mul_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode);
int64_t seraph_mul_i64(int64_t a, int64_t b, Seraph_ArithMode mode);
```

### Division
```c
uint64_t seraph_div_u64(uint64_t a, uint64_t b, Seraph_ArithMode mode);
int64_t seraph_div_i64(int64_t a, int64_t b, Seraph_ArithMode mode);
```

Division by zero ALWAYS returns VOID regardless of mode (there's no sensible wrap or saturate behavior).

### Negation
```c
int64_t seraph_neg_i64(int64_t a, Seraph_ArithMode mode);
```

Note: Negating INT64_MIN would overflow (because there's no positive equivalent). In VOID mode, returns VOID. In SATURATE mode, returns INT64_MAX.

### Absolute Value
```c
uint64_t seraph_abs_i64(int64_t a, Seraph_ArithMode mode);
```

Returns unsigned to handle the INT64_MIN case properly.

## Overflow Detection

Sometimes you need to do an operation AND know if it overflowed:

```c
bool overflow;
uint64_t result = seraph_add_u64_checked(a, b, &overflow);
if (overflow) {
    // Handle overflow case
}
```

## Comparison Operations

Comparisons work with VOID:

```c
Seraph_Vbit seraph_eq_u64(uint64_t a, uint64_t b);   // Returns VOID if either is VOID
Seraph_Vbit seraph_lt_u64(uint64_t a, uint64_t b);   // Returns VOID if either is VOID
// etc.
```

## Signed vs Unsigned

SERAPH provides both signed and unsigned variants for all operations. The key differences:

| Operation | Unsigned | Signed |
|-----------|----------|--------|
| Overflow up | Wraps to 0 | Wraps to MIN |
| Overflow down | Wraps to MAX | Wraps to MAX |
| Division | Floor division | Truncation toward zero |
| Right shift | Logical (fills 0s) | Arithmetic (fills sign) |

## Performance Considerations

### WRAP Mode
Fastest - often just the raw CPU instruction.

### VOID Mode
Requires overflow check before/after operation. Modern CPUs have overflow flags, so this is often just one additional branch.

### SATURATE Mode
Requires comparison and conditional assignment. Most expensive, but SIMD instructions often have built-in saturation.

## SIMD Batch Operations

For performance-critical code, SERAPH provides SIMD versions:

```c
// Add 4 x 64-bit values with saturation (AVX2)
void seraph_add_4x64_saturate(const uint64_t* a, const uint64_t* b, uint64_t* dst);

// Add 8 x 32-bit values with VOID check (AVX2)
void seraph_add_8x32_void(const uint32_t* a, const uint32_t* b, uint32_t* dst);
```

## Technical Specification

### Mode Enum
```c
typedef enum {
    SERAPH_ARITH_VOID     = 0,  // Overflow returns VOID
    SERAPH_ARITH_WRAP     = 1,  // Overflow wraps around
    SERAPH_ARITH_SATURATE = 2   // Overflow clamps to limit
} Seraph_ArithMode;
```

### Saturation Limits

Because VOID uses the maximum bit pattern, saturation must stop one value short:

| Type | Max (for saturation) | VOID |
|------|---------------------|------|
| uint8_t | 0xFE (254) | 0xFF (255) |
| uint16_t | 0xFFFE | 0xFFFF |
| uint32_t | 0xFFFFFFFE | 0xFFFFFFFF |
| uint64_t | 0xFFFFFFFFFFFFFFFE | 0xFFFFFFFFFFFFFFFF |
| int8_t | 126, -128 | -1 |
| int16_t | 32766, -32768 | -1 |
| int32_t | 2147483646, -2147483648 | -1 |
| int64_t | 9223372036854775806, -9223372036854775808 | -1 |

For signed types, the minimum value CAN be used since VOID is -1 (maximum unsigned), not the minimum signed.

## Example: Safe Integer Parsing

```c
int64_t parse_int_safe(const char* str) {
    int64_t result = 0;
    int sign = 1;

    if (*str == '-') { sign = -1; str++; }

    while (*str >= '0' && *str <= '9') {
        int64_t digit = *str - '0';

        // Multiply by 10, checking for overflow
        result = seraph_mul_i64(result, 10, SERAPH_ARITH_VOID);
        if (SERAPH_IS_VOID_I64(result)) return SERAPH_VOID_I64;

        // Add digit, checking for overflow
        result = seraph_add_i64(result, digit * sign, SERAPH_ARITH_VOID);
        if (SERAPH_IS_VOID_I64(result)) return SERAPH_VOID_I64;

        str++;
    }

    return result;
}
```

## Source Files

| File | Description |
|------|-------------|
| `src/entropic.c` | VOID/WRAP/SATURATE arithmetic, overflow detection |
| `include/seraph/entropic.h` | Arithmetic mode enum, checked operations |
