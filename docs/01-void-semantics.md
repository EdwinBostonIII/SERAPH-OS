# MC0: VOID Semantics

## What is VOID? (Plain English)

VOID is SERAPH's way of saying "this value doesn't exist" or "something went wrong." Unlike traditional error handling (exceptions, error codes, null pointers), VOID is a special bit pattern that automatically "infects" any calculation it touches.

**Analogy**: Imagine you're baking a cake. If one ingredient is spoiled (VOID), the whole cake is spoiled. You don't need to check each ingredient at every step - the spoilage naturally propagates to the final result.

**In code terms**: If you try to divide by zero, instead of crashing, you get VOID. If you then add 5 to that VOID, you get VOID. If you multiply that by 100, you still get VOID. The error bubbles up automatically until something checks for it and handles it.

## Why VOID? (The Problem It Solves)

Traditional approaches have problems:

| Approach | Problem |
|----------|---------|
| Null pointers | Crash when dereferenced (billion dollar mistake) |
| Exceptions | Control flow becomes unpredictable, easy to forget |
| Error codes | Easy to ignore, clutters code with checks |
| NaN (IEEE 754) | Only works for floats, not integers |

VOID solves all these:
- **Can't crash** - VOID is a valid bit pattern, not an invalid address
- **Can't ignore** - VOID propagates through all operations
- **Works everywhere** - Same pattern for integers, pointers, booleans
- **Zero overhead** - Just a comparison, no exceptions or unwinding

## The VOID Bit Pattern

VOID is represented as "all 1s" - every bit set to 1:

| Type | VOID Value | Hex |
|------|------------|-----|
| u8 | 255 | 0xFF |
| u16 | 65,535 | 0xFFFF |
| u32 | 4,294,967,295 | 0xFFFFFFFF |
| u64 | 18,446,744,073,709,551,615 | 0xFFFFFFFFFFFFFFFF |
| i8 | -1 | 0xFF |
| i16 | -1 | 0xFFFF |
| i32 | -1 | 0xFFFFFFFF |
| i64 | -1 | 0xFFFFFFFFFFFFFFFF |

### Why All 1s?

1. **Fast detection**: Compare against -1 (single instruction)
2. **SIMD friendly**: Can check multiple values at once with vector comparison
3. **Distinctive**: Unlikely to be a valid value in most contexts
4. **Consistent**: Same pattern regardless of type width

## VOID Propagation Rules

### Rule 1: Unary Operations
If the input is VOID, the output is VOID.
```
-VOID = VOID
~VOID = VOID
abs(VOID) = VOID
```

### Rule 2: Binary Operations
If either input is VOID, the output is VOID.
```
VOID + 5 = VOID
5 + VOID = VOID
VOID + VOID = VOID
```

### Rule 3: Comparisons
Comparing with VOID returns VOID (unknown).
```
VOID < 5 = VOID (unknown)
VOID == VOID = VOID (not TRUE!)
```

### Rule 4: Error Conditions
Operations that would produce undefined behavior return VOID instead.
```
5 / 0 = VOID
x << 64 = VOID (for 64-bit x)
sqrt(-1) = VOID (for real numbers)
```

## Checking for VOID

### `SERAPH_IS_VOID(x)`
Returns true if x is VOID. This is a type-generic macro that works for all integer types.

```c
int result = divide(10, 0);  // Returns VOID
if (SERAPH_IS_VOID(result)) {
    // Handle the error
}
```

### `SERAPH_EXISTS(x)`
Returns true if x is NOT VOID (syntactic sugar for `!SERAPH_IS_VOID(x)`).

```c
int result = divide(10, 2);  // Returns 5
if (SERAPH_EXISTS(result)) {
    // Use the valid result
}
```

### `SERAPH_UNWRAP_OR(x, default)`
Returns x if it exists, otherwise returns the default value.

```c
int result = SERAPH_UNWRAP_OR(divide(10, 0), 0);  // Returns 0
int result = SERAPH_UNWRAP_OR(divide(10, 2), 0);  // Returns 5
```

## SIMD Batch Checking

For performance-critical code, SERAPH provides SIMD operations to check multiple values at once:

### AVX2 (4 × 64-bit values)
```c
uint64_t values[4] = {1, VOID_U64, 3, 4};
uint64_t mask = seraph_void_check_4x64(values);
// mask bits indicate which values are VOID
```

### SSE2 (2 × 64-bit values)
```c
uint64_t values[2] = {VOID_U64, 5};
uint64_t mask = seraph_void_check_2x64(values);
```

## Technical Specification

### Constants
```c
#define SERAPH_VOID_U8   ((uint8_t)0xFF)
#define SERAPH_VOID_U16  ((uint16_t)0xFFFF)
#define SERAPH_VOID_U32  ((uint32_t)0xFFFFFFFF)
#define SERAPH_VOID_U64  ((uint64_t)0xFFFFFFFFFFFFFFFFULL)
#define SERAPH_VOID_I8   ((int8_t)-1)
#define SERAPH_VOID_I16  ((int16_t)-1)
#define SERAPH_VOID_I32  ((int32_t)-1)
#define SERAPH_VOID_I64  ((int64_t)-1)
```

### Detection Macros
```c
#define SERAPH_IS_VOID_U8(x)  ((x) == SERAPH_VOID_U8)
#define SERAPH_IS_VOID_U16(x) ((x) == SERAPH_VOID_U16)
#define SERAPH_IS_VOID_U32(x) ((x) == SERAPH_VOID_U32)
#define SERAPH_IS_VOID_U64(x) ((x) == SERAPH_VOID_U64)
// ... and signed variants
```

### Propagation Functions
```c
// Unary: propagate VOID through operation
uint64_t seraph_void_unary_u64(uint64_t x, uint64_t (*op)(uint64_t));

// Binary: propagate VOID through binary operation
uint64_t seraph_void_binary_u64(uint64_t a, uint64_t b,
                                 uint64_t (*op)(uint64_t, uint64_t));
```

## Design Rationale

### Why not use a separate "valid" flag?
A separate flag doubles memory usage and requires checking two things. VOID is self-describing - the value itself tells you if it's valid.

### Why not use a tagged union?
Tagged unions require branching on every access. VOID uses the value space itself, with propagation being automatic arithmetic.

### Why -1 for signed integers?
-1 has all bits set in two's complement, matching the unsigned pattern. This means the same machine code works for both signed and unsigned VOID checks.

### What about legitimate -1 values?
In SERAPH's type system, -1 is rarely a valid domain value. For the rare cases where it is, use a larger type (i16 for i8's domain) or a wrapper type with explicit validity.

## Examples

### Safe Division
```c
int32_t safe_divide(int32_t a, int32_t b) {
    if (SERAPH_IS_VOID(a) || SERAPH_IS_VOID(b) || b == 0) {
        return SERAPH_VOID_I32;
    }
    return a / b;
}
```

### Error Propagation Chain
```c
int32_t calculate(int32_t x, int32_t y, int32_t z) {
    int32_t step1 = safe_divide(x, y);  // Might be VOID
    int32_t step2 = safe_add(step1, z); // Propagates VOID
    int32_t step3 = safe_mul(step2, 2); // Still propagates
    return step3;  // VOID if any step failed
}
```

### Batch Processing with SIMD
```c
void process_batch(uint64_t* data, size_t count) {
    for (size_t i = 0; i + 4 <= count; i += 4) {
        uint64_t mask = seraph_void_check_4x64(&data[i]);
        if (mask) {
            // At least one VOID in this batch
            handle_voids(&data[i], mask);
        }
    }
}
```

## Source Files

| File | Description |
|------|-------------|
| `src/void.c` | VOID propagation, SIMD batch checking |
| `include/seraph/void.h` | VOID constants, detection macros |
