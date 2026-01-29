# MC3: Semantic Byte

## What is a Semantic Byte? (Plain English)

A Semantic Byte is a "smart byte" that tracks which bits are known and which are VOID (unknown/error). It's 16 bits total: 8 bits of actual data + 8 bits of validity mask.

**Why this matters**: Sometimes you only know SOME bits of a value. Maybe a sensor gave you the high 4 bits but the low 4 bits are corrupt. A Semantic Byte lets you track this partial knowledge precisely.

**Example**:
```
Regular byte: 0xA5 (all 8 bits assumed valid)
Semantic byte: data=0xA5, mask=0xF0
  This means: "bits 4-7 are definitely 0xA (valid), bits 0-3 are unknown"
```

## Structure

```
┌────────────────────────────────────────┐
│ 15    14    13    12    11    10    9     8 │  <- Validity Mask
│  1     1     1     1     0     0     0     0 │     (1=valid, 0=VOID)
├────────────────────────────────────────┤
│  7     6     5     4     3     2     1     0 │  <- Value Bits
│  1     0     1     0     ?     ?     ?     ? │     (? = unknown)
└────────────────────────────────────────┘
```

In this example:
- Mask = 0xF0: high nibble is valid, low nibble is VOID
- Value = 0xA?: high nibble is 0xA, low nibble is garbage (ignore it)

## Validity Rules

| Mask Bit | Meaning |
|----------|---------|
| 1 | Corresponding value bit is valid |
| 0 | Corresponding value bit is VOID (unknown) |

## Operations

### Creation

```c
// Create from raw byte (all bits valid)
Seraph_SemanticByte sb = seraph_sbyte_from_u8(0xA5);
// mask=0xFF, value=0xA5

// Create with explicit mask
Seraph_SemanticByte sb = seraph_sbyte_create(0xA5, 0xF0);
// mask=0xF0, value=0xA5 (only high nibble valid)

// Create fully VOID
Seraph_SemanticByte sb = seraph_sbyte_void();
// mask=0x00, value=undefined
```

### Extraction

```c
// Extract to u8 (returns VOID_U8 if any bit is invalid)
uint8_t val = seraph_sbyte_to_u8(sb);

// Extract to u8 with default for invalid bits
uint8_t val = seraph_sbyte_to_u8_default(sb, 0x00);

// Check if fully valid
bool valid = seraph_sbyte_is_valid(sb);  // mask == 0xFF

// Check if fully VOID
bool void_all = seraph_sbyte_is_void(sb);  // mask == 0x00
```

### Bitwise Operations

All bitwise operations propagate the validity mask appropriately:

```c
Seraph_SemanticByte a = seraph_sbyte_create(0xFF, 0xF0);  // Valid high nibble
Seraph_SemanticByte b = seraph_sbyte_create(0x0F, 0x0F);  // Valid low nibble

// AND: output bit valid only if BOTH inputs valid
Seraph_SemanticByte c = seraph_sbyte_and(a, b);
// result: value=(0xFF & 0x0F)=0x0F, mask=(0xF0 & 0x0F)=0x00 (all VOID!)

// OR: output bit valid only if BOTH inputs valid
Seraph_SemanticByte d = seraph_sbyte_or(a, b);
// result: value=(0xFF | 0x0F)=0xFF, mask=(0xF0 & 0x0F)=0x00
```

**Important**: AND/OR operations require both inputs to be valid to produce a valid output. This is conservative - if you don't know both inputs, you can't know the output.

### Merge Operations

Sometimes you have partial information from multiple sources and want to combine them:

```c
Seraph_SemanticByte a = seraph_sbyte_create(0xA0, 0xF0);  // Know high nibble
Seraph_SemanticByte b = seraph_sbyte_create(0x05, 0x0F);  // Know low nibble

// Merge: combine valid bits from both (VOID if conflict)
Seraph_SemanticByte merged = seraph_sbyte_merge(a, b);
// result: value=0xA5, mask=0xFF (fully valid!)
```

Merge fails (returns VOID for conflicting bits) if both sources claim validity but disagree:

```c
Seraph_SemanticByte a = seraph_sbyte_create(0xF0, 0xF0);
Seraph_SemanticByte b = seraph_sbyte_create(0xA0, 0xF0);  // Disagrees!

Seraph_SemanticByte merged = seraph_sbyte_merge(a, b);
// result: mask=0x00 for high nibble (conflict!), plus whatever low nibble says
```

### Masking Out Bits

```c
// Set specific bits to VOID
Seraph_SemanticByte masked = seraph_sbyte_mask_out(sb, 0x0F);
// Low nibble becomes VOID regardless of original

// Keep only specific bits
Seraph_SemanticByte kept = seraph_sbyte_mask_keep(sb, 0xF0);
// Low nibble becomes VOID, high nibble unchanged
```

## Use Cases

### 1. Sensor Data with Errors

```c
// Sensor returns 8 bits, but ECC detected 2-bit error in positions 2-3
uint8_t raw_reading = read_sensor();
Seraph_SemanticByte reading = seraph_sbyte_create(raw_reading, 0xF3);
// Bits 2-3 marked as VOID due to detected error
```

### 2. Partial Protocol Parsing

```c
// Received packet header, but some fields are reserved/undefined
uint8_t header = receive_byte();
Seraph_SemanticByte parsed = seraph_sbyte_create(header, 0b11110011);
// Bits 2-3 are reserved in protocol, mark as VOID
```

### 3. Data Fusion

```c
// Two sensors measure overlapping bits of same value
Seraph_SemanticByte sensor1 = seraph_sbyte_create(temp_high, 0xF0);
Seraph_SemanticByte sensor2 = seraph_sbyte_create(temp_low, 0x0F);
Seraph_SemanticByte fused = seraph_sbyte_merge(sensor1, sensor2);
```

## Technical Specification

### Type Definition

```c
typedef struct {
    uint8_t mask;   // Validity mask (1=valid, 0=VOID)
    uint8_t value;  // Data value (only valid where mask=1)
} Seraph_SemanticByte;
```

### Invariant

For any valid Seraph_SemanticByte, the value bits should be 0 where mask is 0:
```c
(sb.value & ~sb.mask) == 0
```

This simplifies comparisons and ensures consistent representation.

### Comparison

Two Semantic Bytes are equal if they have the same mask AND the same value where the mask indicates validity:

```c
bool seraph_sbyte_eq(Seraph_SemanticByte a, Seraph_SemanticByte b) {
    if (a.mask != b.mask) return false;
    return (a.value & a.mask) == (b.value & b.mask);
}
```

## Design Rationale

### Why 16 bits instead of tagged union?

A tagged union (1 bit per value to indicate VOID-ness) would only need 9 bits but doesn't pack nicely. 16 bits (2 bytes) aligns well and allows efficient operations.

### Why mask=1 means valid (not mask=1 means VOID)?

Convention: 1 = present/valid is more intuitive. "All 1s mask = all bits valid" reads naturally.

### Relationship to VOID

A fully-VOID Semantic Byte (mask=0x00) corresponds to the VOID_U8 concept. A fully-valid Semantic Byte (mask=0xFF) is equivalent to a regular uint8_t.

The Semantic Byte provides finer granularity - VOID can apply to individual bits, not just the whole byte.

## Source Files

| File | Description |
|------|-------------|
| `src/sbyte.c` | Semantic byte operations, validity tracking, merge logic |
| `include/seraph/sbyte.h` | SemanticByte structure, bitwise operations |
