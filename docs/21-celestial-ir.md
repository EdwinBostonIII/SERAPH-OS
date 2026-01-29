# MC28: Celestial IR - SERAPH's Native Intermediate Representation

## Overview

Celestial IR is SERAPH's custom intermediate representation (IR) for native code compilation. Unlike LLVM IR which is designed for general-purpose compilation, Celestial IR is purpose-built for SERAPH's unique semantics:

- **VOID-First**: Every value carries VOID infection potential
- **Capability-Aware**: Memory access through bounded, revocable capabilities
- **Substrate-Conscious**: Operations know their memory substrate context
- **Effect-Tracked**: Every operation has explicit side effects
- **Galactic-Native**: 256-bit hyper-dual numbers are first-class citizens

## Plain English Explanation

When you compile a Seraphim program, it goes through this pipeline:

```
Source Code → Lexer → Parser → AST → Type Checker → Celestial IR → Machine Code → ELF
```

Celestial IR sits in the middle. It's a simplified form of your program that's:
1. Closer to machine code (no fancy syntax)
2. In SSA form (each variable assigned exactly once)
3. Rich with SERAPH metadata (VOID tracking, effects, substrates)

Think of it like assembly language, but with SERAPH superpowers built in.

## Architecture

### Type System

```c
typedef enum {
    /* Primitive types */
    CIR_TYPE_VOID,          /* The VOID type itself (unit type) */
    CIR_TYPE_BOOL,          /* Boolean (1 bit logical) */
    CIR_TYPE_I8,            /* 8-bit signed integer */
    CIR_TYPE_I16,           /* 16-bit signed integer */
    CIR_TYPE_I32,           /* 32-bit signed integer */
    CIR_TYPE_I64,           /* 64-bit signed integer */
    CIR_TYPE_U8,            /* 8-bit unsigned integer */
    CIR_TYPE_U16,           /* 16-bit unsigned integer */
    CIR_TYPE_U32,           /* 32-bit unsigned integer */
    CIR_TYPE_U64,           /* 64-bit unsigned integer */

    /* SERAPH-specific primitive types */
    CIR_TYPE_SCALAR,        /* Q64.64 fixed-point (128 bits = 16 bytes) */
    CIR_TYPE_DUAL,          /* Dual number (value + derivative = 32 bytes) */
    CIR_TYPE_GALACTIC,      /* Hyper-dual quaternion (512 bits = 64 bytes) */

    /* Compound types */
    CIR_TYPE_CAPABILITY,    /* Capability (base + length + gen + perms = 32 bytes) */
    CIR_TYPE_STRUCT,        /* User-defined struct */
    CIR_TYPE_ARRAY,         /* Fixed-size array */
    CIR_TYPE_SLICE,         /* Dynamic slice (cap + length) */
    CIR_TYPE_STR,           /* String (ptr + length fat pointer) */
    CIR_TYPE_ENUM,          /* Tagged union (discriminant + payload) */
    CIR_TYPE_FUNCTION,      /* Function pointer type */

    /* Special types */
    CIR_TYPE_VOIDABLE,      /* Type that may contain VOID value */
    CIR_TYPE_SUBSTRATE,     /* Substrate context handle */
} Celestial_Type_Kind;
```

### Type Sizes

| Type | Size (bytes) | Description |
|------|-------------|-------------|
| BOOL | 1 | Boolean value |
| I8/U8 | 1 | 8-bit integer |
| I16/U16 | 2 | 16-bit integer |
| I32/U32 | 4 | 32-bit integer |
| I64/U64 | 8 | 64-bit integer |
| SCALAR | 16 | Q64.64 fixed-point |
| DUAL | 32 | Value + derivative |
| GALACTIC | 64 | Hyper-dual quaternion |
| CAPABILITY | 32 | base(8) + length(8) + generation(8) + perms(8) |

### Value Kinds

Values in Celestial IR can be:

```c
typedef enum {
    CIR_VALUE_CONST,        /* Compile-time constant */
    CIR_VALUE_VREG,         /* Virtual register (SSA) */
    CIR_VALUE_PARAM,        /* Function parameter */
    CIR_VALUE_GLOBAL,       /* Global variable */
    CIR_VALUE_VOID_CONST,   /* The VOID constant for a type */
    CIR_VALUE_STRING,       /* String constant reference */
    CIR_VALUE_FNPTR,        /* Function pointer reference */
} Celestial_Value_Kind;
```

Each value carries:
- `type`: The Celestial type
- `id`: Unique identifier for register allocation
- `may_be_void`: Vbit tracking if value might be VOID

## Instruction Categories

### 1. Arithmetic (VOID-propagating)

```
CIR_ADD     - Add (VOID if overflow or operand VOID)
CIR_SUB     - Subtract
CIR_MUL     - Multiply
CIR_DIV     - Divide (VOID if divisor zero)
CIR_MOD     - Modulo (VOID if divisor zero)
CIR_NEG     - Negate
```

Division and modulo operations **always** set `CIR_EFFECT_VOID` because they may produce VOID if the divisor is zero.

### 2. Bitwise Operations

```
CIR_AND     - Bitwise AND
CIR_OR      - Bitwise OR
CIR_XOR     - Bitwise XOR
CIR_NOT     - Bitwise NOT
CIR_SHL     - Shift left
CIR_SHR     - Shift right (logical)
CIR_SAR     - Shift right (arithmetic)
```

### 3. Comparison Operations

```
CIR_EQ      - Equal
CIR_NE      - Not equal
CIR_LT      - Less than (signed)
CIR_LE      - Less or equal
CIR_GT      - Greater than
CIR_GE      - Greater or equal
CIR_ULT     - Less than (unsigned)
CIR_ULE     - Less or equal (unsigned)
CIR_UGT     - Greater than (unsigned)
CIR_UGE     - Greater or equal (unsigned)
```

Comparisons produce a Vbit (three-valued boolean), not a regular bool. Comparing with VOID produces VOID.

### 4. VOID Operations (SERAPH-specific)

```
CIR_VOID_TEST       - Test if value is VOID → Vbit
CIR_VOID_PROP       - Propagate VOID (?? operator)
CIR_VOID_ASSERT     - Assert non-VOID (!! operator)
CIR_VOID_COALESCE   - VOID coalescing (value ?? default)
CIR_VOID_CONST      - Load VOID constant for type
```

These implement SERAPH's core error handling:
- `VOID_TEST`: Returns TRUE if value is VOID, FALSE otherwise
- `VOID_PROP`: If VOID, immediately return VOID from function
- `VOID_ASSERT`: If VOID, execute UD2 (panic)
- `VOID_COALESCE`: If VOID, use default value instead

### 5. Capability Operations (SERAPH-specific)

```
CIR_CAP_CREATE      - Create capability (base, len, gen, perms)
CIR_CAP_LOAD        - Load through capability (bounds-checked)
CIR_CAP_STORE       - Store through capability (bounds-checked)
CIR_CAP_CHECK       - Check capability validity → Vbit
CIR_CAP_NARROW      - Narrow capability bounds
CIR_CAP_SPLIT       - Split capability into two
CIR_CAP_REVOKE      - Increment generation (revoke)
```

Capability loads return VOID if:
- Capability is revoked (generation mismatch)
- Access is out of bounds
- Permission check fails

Capability stores silently fail under the same conditions (no side effects).

### 6. Memory Operations

```
CIR_LOAD            - Load from volatile memory (raw)
CIR_STORE           - Store to volatile memory (raw)
CIR_ALLOCA          - Stack allocation
CIR_MEMCPY          - Memory copy
CIR_MEMSET          - Memory set
```

### 7. Substrate Operations (SERAPH-specific)

```
CIR_SUBSTRATE_ENTER   - Enter substrate context
CIR_SUBSTRATE_EXIT    - Exit substrate context
CIR_ATLAS_LOAD        - Load from Atlas (persistent)
CIR_ATLAS_STORE       - Store to Atlas (journaled)
CIR_ATLAS_BEGIN       - Begin Atlas transaction
CIR_ATLAS_COMMIT      - Commit Atlas transaction
CIR_ATLAS_ROLLBACK    - Rollback Atlas transaction
CIR_AETHER_LOAD       - Load from Aether (network)
CIR_AETHER_STORE      - Store to Aether (write-back)
CIR_AETHER_SYNC       - Synchronize Aether state
```

### 8. Control Flow

```
CIR_JUMP            - Unconditional jump
CIR_BRANCH          - Conditional branch (on Vbit)
CIR_SWITCH          - Multi-way branch
CIR_CALL            - Function call
CIR_CALL_INDIRECT   - Indirect call through function pointer
CIR_TAIL_CALL       - Tail call
CIR_RETURN          - Return from function
```

### 9. Galactic Operations (SERAPH-specific)

```
CIR_GALACTIC_ADD      - Galactic addition (preserves derivatives)
CIR_GALACTIC_MUL      - Galactic multiplication (chain rule)
CIR_GALACTIC_DIV      - Galactic division (quotient rule)
CIR_GALACTIC_PREDICT  - Extrapolate using derivatives
CIR_GALACTIC_EXTRACT  - Extract component (w, x, y, z)
CIR_GALACTIC_INSERT   - Insert component
```

### 10. Type Conversions

```
CIR_TRUNC           - Truncate to smaller type
CIR_ZEXT            - Zero-extend to larger type
CIR_SEXT            - Sign-extend to larger type
CIR_BITCAST         - Reinterpret bits as different type
CIR_TO_SCALAR       - Convert int to Scalar
CIR_FROM_SCALAR     - Convert Scalar to int
CIR_TO_GALACTIC     - Promote Scalar to Galactic
CIR_FROM_GALACTIC   - Extract Scalar from Galactic
```

### 11. Struct/Array Operations

```
CIR_EXTRACTFIELD    - Extract struct field
CIR_INSERTFIELD     - Insert struct field
CIR_EXTRACTELEM     - Extract array element
CIR_INSERTELEM      - Insert array element
CIR_GEP             - Get element pointer
```

## Effect System

Every instruction tracks its effects:

```c
typedef enum {
    CIR_EFFECT_NONE     = 0,
    CIR_EFFECT_VOID     = (1 << 0),   /* May produce VOID */
    CIR_EFFECT_READ     = (1 << 1),   /* Reads memory */
    CIR_EFFECT_WRITE    = (1 << 2),   /* Writes memory */
    CIR_EFFECT_PERSIST  = (1 << 3),   /* Accesses Atlas */
    CIR_EFFECT_NETWORK  = (1 << 4),   /* Accesses Aether */
    CIR_EFFECT_TIMER    = (1 << 5),   /* Uses Chronon */
    CIR_EFFECT_ALLOC    = (1 << 6),   /* Allocates memory */
    CIR_EFFECT_PANIC    = (1 << 7),   /* May panic */
    CIR_EFFECT_DIVERGE  = (1 << 8),   /* May not terminate */

    /* Composite effects */
    CIR_EFFECT_PURE     = CIR_EFFECT_NONE,
    CIR_EFFECT_IO       = CIR_EFFECT_READ | CIR_EFFECT_WRITE,
} Celestial_Effect;
```

Functions declare their effects, and the verifier ensures actual effects don't exceed declared effects.

## Module Structure

```c
struct Celestial_Module {
    const char*          name;
    size_t               name_len;

    /* Functions */
    Celestial_Function*  functions;
    size_t               function_count;

    /* Global variables */
    Celestial_Value**    globals;
    size_t               global_count;

    /* Types */
    Celestial_Type**     types;
    size_t               type_count;

    /* String constants (for rodata section) */
    Celestial_String_Const* strings;
    size_t                  string_count;

    /* Arena for allocations */
    Seraph_Arena*        arena;
};
```

## Function Structure

```c
struct Celestial_Function {
    const char*          name;
    size_t               name_len;
    Celestial_Type*      type;          /* Function type */

    /* Parameters */
    Celestial_Value**    params;
    size_t               param_count;

    /* Blocks */
    Celestial_Block*     entry;         /* Entry block */
    Celestial_Block*     blocks;        /* All blocks (linked list) */
    size_t               block_count;

    /* Effect declaration */
    uint32_t             declared_effects;

    /* For SSA construction */
    uint32_t             next_vreg_id;
    uint32_t             next_block_id;

    /* Linked list in module */
    Celestial_Function*  next;
};
```

## Basic Block Structure

```c
struct Celestial_Block {
    uint32_t             id;            /* Block ID */
    const char*          name;          /* Optional name */

    /* Instructions */
    Celestial_Instr*     first;         /* First instruction */
    Celestial_Instr*     last;          /* Last instruction (terminator) */
    size_t               instr_count;

    /* Control flow graph */
    Celestial_Block**    preds;         /* Predecessor blocks */
    size_t               pred_count;
    Celestial_Block**    succs;         /* Successor blocks */
    size_t               succ_count;

    /* Substrate context at block entry */
    Celestial_Substrate_Kind substrate;

    /* Linked list in function */
    Celestial_Block*     next;
    Celestial_Block*     prev;

    /* For dominance analysis */
    Celestial_Block*     idom;          /* Immediate dominator */
    uint32_t             dom_depth;
};
```

## Builder API

The builder provides a fluent API for constructing IR:

```c
/* Initialize builder */
void celestial_builder_init(Celestial_Builder* builder, Celestial_Module* mod);

/* Position at end of block */
void celestial_builder_position(Celestial_Builder* builder, Celestial_Block* block);

/* Arithmetic */
Celestial_Value* celestial_build_add(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name);

/* VOID operations */
Celestial_Value* celestial_build_void_test(Celestial_Builder* b,
                                            Celestial_Value* value,
                                            const char* name);

/* Control flow */
void celestial_build_branch(Celestial_Builder* b,
                            Celestial_Value* cond,
                            Celestial_Block* then_block,
                            Celestial_Block* else_block);
```

## Optimization Passes

### Constant Folding

Evaluates constant expressions at compile time:

```c
int celestial_fold_constants(Celestial_Module* mod);
```

Before:
```
%1 = add i64 2, 3
%2 = mul i64 %1, 4
```

After:
```
%1 = const i64 5
%2 = const i64 20
```

### Dead Code Elimination

Removes unused instructions with no side effects:

```c
int celestial_eliminate_dead_code(Celestial_Module* mod);
```

Respects SERAPH semantics - instructions with `CIR_EFFECT_VOID` are kept even if their results are unused (they might panic).

## Verification

The verifier checks:

1. All types are valid
2. All values have types
3. All blocks end with terminators
4. Effect declarations match actual effects
5. Substrate contexts are properly nested
6. SSA form is maintained

```c
Seraph_Vbit celestial_verify_module(Celestial_Module* mod);
```

## String Constant Handling

String literals go through escape sequence processing:

| Escape | Character |
|--------|-----------|
| `\n` | Newline (0x0A) |
| `\r` | Carriage return (0x0D) |
| `\t` | Tab (0x09) |
| `\\` | Backslash |
| `\"` | Double quote |
| `\'` | Single quote |
| `\0` | Null byte |
| `\xNN` | Hex byte |

String constants are stored in the module's string table and emitted to the `.rodata` section.

## Example IR Output

```
define fn add(i64 %0, i64 %1) -> i64 effects(PURE) {
entry:
    %2 = add i64 %0, %1
    ret i64 %2
}

define fn safe_divide(i64 %0, i64 %1) -> ??i64 effects(VOID) {
entry:
    %2 = eq i64 %1, 0
    br %2, zero_block, ok_block

zero_block:
    %3 = void_const i64
    ret i64 %3

ok_block:
    %4 = div i64 %0, %1
    ret i64 %4
}
```

## Integration with Backends

Celestial IR is lowered to machine code by architecture-specific backends:

- **x64**: `celestial_to_x64.c` → `x64_encode.c`
- **ARM64**: `celestial_to_arm64.c` → `arm64_encode.c`
- **RISC-V**: `celestial_to_riscv.c` → `riscv_encode.c`

Each backend:
1. Performs register allocation
2. Lowers each IR instruction to machine instructions
3. Handles calling conventions
4. Generates VOID checking sequences
5. Resolves labels and branches

## Summary

Celestial IR embodies SERAPH's worldview:

1. **Safety first**: VOID propagation is built into every arithmetic operation
2. **Capabilities everywhere**: Memory access is always through capabilities
3. **Effects are explicit**: No hidden side effects
4. **Substrates matter**: Different code paths for volatile/persistent/distributed memory
5. **Derivatives flow**: Galactic operations preserve automatic differentiation

The IR bridges high-level Seraphim code and low-level machine code while preserving all of SERAPH's safety guarantees.

## Source Files

| File | Description |
|------|-------------|
| `src/seraphim/celestial_ir.c` | IR construction, verification, SSA form |
| `src/seraphim/celestial_opt.c` | Constant folding, dead code elimination |
| `src/seraphim/ast_to_ir.c` | AST to Celestial IR lowering |
| `include/seraph/seraphim/celestial_ir.h` | IR types, opcodes, builder API |
