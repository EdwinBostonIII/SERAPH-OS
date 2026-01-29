# MC1: VBIT Three-Valued Logic

## What is VBIT? (Plain English)

Normal computers use binary logic: TRUE (1) or FALSE (0). SERAPH adds a third value: VOID (unknown/error). This is called Kleene three-valued logic, and it makes error handling automatic at the bit level.

**Why this matters**: When you AND something with FALSE, you always get FALSE (even if the other value is VOID). But if you AND TRUE with VOID, you get VOID - because you can't know the answer without knowing both values.

**Example**: Imagine you're checking two conditions:
- "Is the user logged in?" → TRUE
- "Does the user have permission?" → VOID (permission system error)

With regular AND: `TRUE && VOID = ???` (crash or undefined)
With VBIT AND: `TRUE && VOID = VOID` (error propagates cleanly)

But if the first condition was FALSE:
- "Is the user logged in?" → FALSE

Then: `FALSE && VOID = FALSE` (we know the answer regardless of permission check!)

## The Three Values

| Value | Meaning | Bit Pattern |
|-------|---------|-------------|
| FALSE | Definitely false | 0x00 |
| TRUE | Definitely true | 0x01 |
| VOID | Unknown/error | 0xFF |

## Truth Tables

### NOT (Negation)
| A | NOT A |
|---|-------|
| FALSE | TRUE |
| TRUE | FALSE |
| VOID | VOID |

*Intuition*: If we don't know A, we can't know NOT A.

### AND (Conjunction)
| A | B | A AND B |
|---|---|---------|
| FALSE | FALSE | FALSE |
| FALSE | TRUE | FALSE |
| FALSE | VOID | FALSE |
| TRUE | FALSE | FALSE |
| TRUE | TRUE | TRUE |
| TRUE | VOID | VOID |
| VOID | FALSE | FALSE |
| VOID | TRUE | VOID |
| VOID | VOID | VOID |

*Intuition*: If either operand is FALSE, the result is FALSE (FALSE dominates). Otherwise, VOID propagates.

### OR (Disjunction)
| A | B | A OR B |
|---|---|--------|
| FALSE | FALSE | FALSE |
| FALSE | TRUE | TRUE |
| FALSE | VOID | VOID |
| TRUE | FALSE | TRUE |
| TRUE | TRUE | TRUE |
| TRUE | VOID | TRUE |
| VOID | FALSE | VOID |
| VOID | TRUE | TRUE |
| VOID | VOID | VOID |

*Intuition*: If either operand is TRUE, the result is TRUE (TRUE dominates). Otherwise, VOID propagates.

### XOR (Exclusive Or)
| A | B | A XOR B |
|---|---|---------|
| FALSE | FALSE | FALSE |
| FALSE | TRUE | TRUE |
| FALSE | VOID | VOID |
| TRUE | FALSE | TRUE |
| TRUE | TRUE | FALSE |
| TRUE | VOID | VOID |
| VOID | FALSE | VOID |
| VOID | TRUE | VOID |
| VOID | VOID | VOID |

*Intuition*: XOR needs both values to compute the result. Any VOID propagates.

### IMPLIES (Material Implication)
| A | B | A → B |
|---|---|-------|
| FALSE | FALSE | TRUE |
| FALSE | TRUE | TRUE |
| FALSE | VOID | TRUE |
| TRUE | FALSE | FALSE |
| TRUE | TRUE | TRUE |
| TRUE | VOID | VOID |
| VOID | FALSE | VOID |
| VOID | TRUE | TRUE |
| VOID | VOID | VOID |

*Intuition*: "A implies B" is FALSE only when A is TRUE and B is FALSE. If A is FALSE, implication is TRUE regardless of B.

### IFF (If and Only If / Equivalence)
| A | B | A ↔ B |
|---|---|-------|
| FALSE | FALSE | TRUE |
| FALSE | TRUE | FALSE |
| FALSE | VOID | VOID |
| TRUE | FALSE | FALSE |
| TRUE | TRUE | TRUE |
| TRUE | VOID | VOID |
| VOID | FALSE | VOID |
| VOID | TRUE | VOID |
| VOID | VOID | VOID |

*Intuition*: Need both values to determine equivalence.

## Bit Arrays

SERAPH supports arrays of VBITs for working with multiple logical values at once.

### Representation
A VBIT array uses 2 bits per logical value:
- Bit 0 (value): 0 = false, 1 = true
- Bit 1 (validity): 0 = valid, 1 = VOID

Packed into bytes: `[validity7|value7|validity6|value6|...|validity0|value0]`

### Operations
All logic operations can be applied to entire arrays using SIMD instructions. This is critical for:
- Graphics (pixel masks)
- Databases (query filtering)
- Simulation (particle states)

## Use Cases

### 1. Database NULL Handling
SQL has three-valued logic for NULLs. VBIT provides the same semantics at the hardware level.

```c
Seraph_Vbit age_check = (user.age > 18);  // Might be VOID if age unknown
Seraph_Vbit has_license = user.has_license;  // TRUE, FALSE, or VOID

Seraph_Vbit can_drive = seraph_vbit_and(age_check, has_license);
// VOID if either is unknown, FALSE if either is FALSE, TRUE only if both TRUE
```

### 2. Partial Information Systems
When sensors fail or data is missing:

```c
Seraph_Vbit sensor_readings[100];
// Some sensors return VOID (failed), some return actual readings

Seraph_Vbit all_ok = seraph_vbit_all_true(sensor_readings, 100);
// VOID if any sensor failed, FALSE if any sensor returned FALSE, TRUE only if all TRUE
```

### 3. Short-Circuit Optimization
VBIT AND/OR naturally short-circuit when possible:
- `FALSE AND anything = FALSE` (don't evaluate second operand)
- `TRUE OR anything = TRUE` (don't evaluate second operand)

## Technical Specification

### Type Definition
```c
typedef enum {
    SERAPH_VBIT_FALSE = 0x00,
    SERAPH_VBIT_TRUE  = 0x01,
    SERAPH_VBIT_VOID  = 0xFF
} Seraph_Vbit;
```

### Core Functions
```c
Seraph_Vbit seraph_vbit_not(Seraph_Vbit a);
Seraph_Vbit seraph_vbit_and(Seraph_Vbit a, Seraph_Vbit b);
Seraph_Vbit seraph_vbit_or(Seraph_Vbit a, Seraph_Vbit b);
Seraph_Vbit seraph_vbit_xor(Seraph_Vbit a, Seraph_Vbit b);
Seraph_Vbit seraph_vbit_implies(Seraph_Vbit a, Seraph_Vbit b);
Seraph_Vbit seraph_vbit_iff(Seraph_Vbit a, Seraph_Vbit b);
```

### Comparison Functions
```c
// Compare regular values, returning VBIT result
Seraph_Vbit seraph_vbit_eq_u64(uint64_t a, uint64_t b);   // a == b?
Seraph_Vbit seraph_vbit_lt_u64(uint64_t a, uint64_t b);   // a < b?
Seraph_Vbit seraph_vbit_le_u64(uint64_t a, uint64_t b);   // a <= b?
Seraph_Vbit seraph_vbit_gt_u64(uint64_t a, uint64_t b);   // a > b?
Seraph_Vbit seraph_vbit_ge_u64(uint64_t a, uint64_t b);   // a >= b?
```

### Conversion Functions
```c
bool seraph_vbit_to_bool(Seraph_Vbit v, bool default_val);
Seraph_Vbit seraph_vbit_from_bool(bool b);
```

## Design Rationale

### Why 0xFF for VOID instead of 0x02?
- Matches the VOID pattern in integers (all bits set)
- Easy to detect with single comparison
- Provides large gap between valid values (0, 1) and VOID (255)

### Why Kleene logic instead of Łukasiewicz?
Kleene three-valued logic has the property that `FALSE AND x = FALSE` and `TRUE OR x = TRUE` regardless of x. This allows short-circuit evaluation and matches programmer intuition.

Łukasiewicz logic treats VOID more symmetrically but doesn't allow these optimizations.

### Relationship to IEEE 754 NaN
NaN in floating-point is similar conceptually (unknown value) but:
- Only works for floats, not integers or booleans
- Has complex propagation rules
- Has multiple NaN representations

VBIT provides the same concept with simpler, consistent semantics across all types.

## Source Files

| File | Description |
|------|-------------|
| `src/vbit.c` | Three-valued logic operations, SIMD batch operations |
| `include/seraph/vbit.h` | Vbit type, truth tables, comparison functions |
