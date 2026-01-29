# MC2: Bit Operations

## What are VOID-Aware Bit Operations? (Plain English)

Normal bit operations (AND, OR, shifts) can produce garbage or undefined results when given invalid inputs. SERAPH's bit operations return VOID instead of crashing or producing garbage.

**Example**: If you try to shift a 64-bit number left by 70 bits, normal C gives you undefined behavior. SERAPH returns VOID - clearly signaling "this operation doesn't make sense."

## Byte Ordering (LSB-First)

SERAPH uses LSB-first (Little-Endian) byte ordering, which is native to x86-64:
- Bit 0 is the Least Significant Bit (LSB)
- Bit 63 is the Most Significant Bit (MSB)
- Multi-byte values have lowest address = lowest byte

```
Byte:     [B0][B1][B2][B3][B4][B5][B6][B7]
Bits:     0-7 8-15 16-23 24-31 ...
```

## Core Operations

### Bit Get/Set/Clear

| Operation | Description | Returns |
|-----------|-------------|---------|
| `seraph_bit_get(x, pos)` | Get bit at position | 0, 1, or VOID |
| `seraph_bit_set(x, pos)` | Set bit to 1 | Result or VOID |
| `seraph_bit_clear(x, pos)` | Clear bit to 0 | Result or VOID |
| `seraph_bit_toggle(x, pos)` | Flip bit | Result or VOID |

All operations return VOID if:
- Input `x` is VOID
- Position `pos` is out of range for the type

### Bit Extract/Insert

| Operation | Description |
|-----------|-------------|
| `seraph_bits_extract(x, start, len)` | Extract `len` bits starting at `start` |
| `seraph_bits_insert(x, val, start, len)` | Insert `len` bits of `val` at `start` |

Returns VOID if range is invalid (extends past bit width).

### Shifts and Rotates

| Operation | Description |
|-----------|-------------|
| `seraph_shl(x, n)` | Shift left by n bits |
| `seraph_shr(x, n)` | Shift right by n bits |
| `seraph_sar(x, n)` | Arithmetic shift right (preserves sign) |
| `seraph_rol(x, n)` | Rotate left |
| `seraph_ror(x, n)` | Rotate right |

All return VOID if shift amount >= type width.

### Population and Leading/Trailing Operations

| Operation | Description |
|-----------|-------------|
| `seraph_popcount(x)` | Count bits set to 1 |
| `seraph_clz(x)` | Count leading zeros |
| `seraph_ctz(x)` | Count trailing zeros |
| `seraph_ffs(x)` | Find first set bit (1-indexed) |
| `seraph_fls(x)` | Find last set bit (1-indexed) |

These return VOID if input is VOID. `clz`/`ctz` return VOID if input is zero (undefined in C).

## Bit Ranges

SERAPH provides a `Seraph_BitRange` type for specifying contiguous bit regions:

```c
typedef struct {
    uint8_t start;  // Starting bit position
    uint8_t length; // Number of bits
} Seraph_BitRange;
```

### Range Operations

```c
// Check if range is valid for 64-bit value
bool seraph_bitrange_valid_64(Seraph_BitRange range);

// Extract bits using range
uint64_t seraph_bitrange_extract(uint64_t x, Seraph_BitRange range);

// Insert bits using range
uint64_t seraph_bitrange_insert(uint64_t x, uint64_t val, Seraph_BitRange range);
```

## Masks

SERAPH provides efficient mask generation:

```c
// Generate mask with n consecutive 1s starting at bit 0
uint64_t seraph_mask_low(uint8_t n);   // e.g., n=4 → 0x0F

// Generate mask with n consecutive 1s ending at bit 63
uint64_t seraph_mask_high(uint8_t n);  // e.g., n=4 → 0xF0...0

// Generate mask for specific range
uint64_t seraph_mask_range(uint8_t start, uint8_t len);
```

## SIMD Operations

For performance-critical batch operations:

```c
// Population count for 4x64-bit values (AVX2)
void seraph_popcount_4x64(const uint64_t* src, uint8_t* dst);

// Leading zero count for 4x64-bit values
void seraph_clz_4x64(const uint64_t* src, uint8_t* dst);
```

## Technical Specification

### Shift Validation
```c
static inline uint64_t seraph_shl_u64(uint64_t x, uint32_t n) {
    if (SERAPH_IS_VOID_U64(x) || n >= 64) return SERAPH_VOID_U64;
    return x << n;
}
```

### Safe Extract
```c
static inline uint64_t seraph_bits_extract_u64(uint64_t x, uint8_t start, uint8_t len) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
    if (start >= 64 || len == 0 || (start + len) > 64) return SERAPH_VOID_U64;
    return (x >> start) & ((1ULL << len) - 1);
}
```

## Design Rationale

### Why VOID on Invalid Shift?
C says shifting by >= bit width is undefined behavior. Many bugs come from forgetting this. SERAPH makes it safe by returning VOID.

### Why Not Clamp/Saturate Shifts?
Clamping (treating shift by 70 as shift by 63) hides bugs. The programmer should know they did something wrong. VOID makes the error visible.

### Why Separate Logical and Arithmetic Shift Right?
- Logical shift (`shr`): Fills with zeros - for unsigned values
- Arithmetic shift (`sar`): Fills with sign bit - for signed values

Both are useful; conflating them causes bugs.

## Source Files

| File | Description |
|------|-------------|
| `src/bit.c` | VOID-aware bit operations, shifts, rotates, population count |
| `include/seraph/bit.h` | Bit manipulation API, masks, ranges |
