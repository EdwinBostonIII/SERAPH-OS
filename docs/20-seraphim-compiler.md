# MC26: Seraphim - The SERAPH Language Compiler

## Overview

Seraphim is SERAPH's systems programming language and its compiler. It operates in two modes:

1. **Native Compilation**: Generates machine code directly via Celestial IR for x64, ARM64, and RISC-V. No external compilers needed.

2. **C Transpilation**: Generates C code using SERAPH-BUILD primitives. Useful for debugging and bootstrap.

Both paths maintain SERAPH's NIH (Not Invented Here) philosophy - zero external dependencies for the native path, only a C compiler for transpilation.

**Note**: For detailed documentation on the native pipeline, see:
- [21-celestial-ir.md](21-celestial-ir.md) - Intermediate representation
- [22-native-code-generation.md](22-native-code-generation.md) - Machine code backends
- [23-elf64-writer.md](23-elf64-writer.md) - ELF executable generation
- [24-seraphic-driver.md](24-seraphic-driver.md) - Compiler CLI

## Plain English Explanation

Imagine you want to write programs for SERAPH. You could write raw C and carefully use all the SERAPH functions correctly - checking for VOID, using capabilities, managing substrates. But that's tedious and error-prone.

Seraphim lets you write in a cleaner syntax:
```seraphim
fn safe_divide(a: u64, b: u64) -> ??u64 {
    if b == 0 { return VOID; }
    return a / b;
}
```

The compiler transforms this into C that uses SERAPH primitives:
```c
uint64_t seraphim_safe_divide(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID(a)) return SERAPH_VOID_U64;
    if (SERAPH_IS_VOID(b)) return SERAPH_VOID_U64;
    if (b == 0) return SERAPH_VOID_U64;
    return seraph_div_u64_void(a, b);
}
```

The magic is that Seraphim **proves** your code is safe at compile time. It tracks:
- **Effects**: What your function CAN do (read files? use network? persist data?)
- **Capabilities**: What memory your function CAN access
- **VOID propagation**: Where errors can occur and how they flow

These proofs are embedded in the binary. When SERAPH loads your program, it verifies the proofs. If valid, the code runs at full native speed with zero runtime safety checks.

## Architecture

### Compilation Pipeline

Seraphim has two compilation paths:

```
┌────────────────────────────────────────────────────────────────────────────┐
│                        SERAPHIM COMPILATION PIPELINE                        │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                            │
│   .seraph source file                                                      │
│          │                                                                 │
│          ▼                                                                 │
│   ┌─────────────┐                                                          │
│   │   LEXER     │  Converts text to tokens                                 │
│   │  (lexer.c)  │  Handles: keywords, operators, literals                  │
│   └──────┬──────┘                                                          │
│          │ Token stream                                                    │
│          ▼                                                                 │
│   ┌─────────────┐                                                          │
│   │   PARSER    │  Builds Abstract Syntax Tree                             │
│   │ (parser.c)  │  Recursive descent, Arena allocation                     │
│   └──────┬──────┘                                                          │
│          │ AST                                                             │
│          ▼                                                                 │
│   ┌─────────────┐                                                          │
│   │TYPE CHECKER │  Annotates AST with types                                │
│   │(checker.c)  │  Substrate tracking, VOID handling                       │
│   └──────┬──────┘                                                          │
│          │ Typed AST                                                       │
│          ▼                                                                 │
│   ┌─────────────┐                                                          │
│   │EFFECT CHECK │  Infers and validates effects                            │
│   │(effects.c)  │  PURE ⊂ VOID ⊂ PERSIST/NETWORK/TIMER ⊂ IO               │
│   └──────┬──────┘                                                          │
│          │                                                                 │
│          ├─────────────────────────────────────────────────────────────┐   │
│          │                                                             │   │
│          ▼ NATIVE PATH                                                 │   │
│   ┌─────────────┐                                                      │   │
│   │ AST→IR      │  Convert to Celestial IR (SSA)                       │   │
│   │(ast_to_ir)  │  VOID-aware, capability-aware                        │   │
│   └──────┬──────┘                                                      │   │
│          │                                                             │   │
│          ▼                                                             │   │
│   ┌─────────────┐                                                      │   │
│   │ OPTIMIZER   │  Constant folding, DCE                               │   │
│   │(celestial)  │  Respects SERAPH semantics                           │   │
│   └──────┬──────┘                                                      │   │
│          │                                                             │   │
│          ▼                                                             │   │
│   ┌─────────────┐                                                      │   │
│   │ BACKEND     │  x64/ARM64/RISC-V lowering                          │   │
│   │(celestial)  │  Register alloc, instruction select                  │   │
│   └──────┬──────┘                                                      │   │
│          │                                                             │   │
│          ▼                                                             │   │
│   ┌─────────────┐                                                      │   │
│   │ELF WRITER   │  Package as ELF64                                    │   │
│   │(elf64)      │  + SERAPH proof sections                             │   │
│   └──────┬──────┘                                                      │   │
│          │                                                             │   │
│          ▼                                                             │   │
│   Native ELF binary                                                    │   │
│   (No external compiler!)                                              │   │
│                                                                        │   │
│          ▼ C TRANSPILE PATH ◄──────────────────────────────────────────┘   │
│   ┌─────────────┐                                                          │
│   │ C CODEGEN   │  Emits C code using seraph.h                             │
│   │(codegen.c)  │  VOID checks, Galactic ops                               │
│   └──────┬──────┘                                                          │
│          │                                                                 │
│          ▼                                                                 │
│   .c file → gcc/clang → Native binary                                      │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

## Language Features

### 1. VOID Semantics

VOID is a bit pattern (0xFF...FF), not a type wrapper. Zero runtime cost.

```seraphim
// VOID propagation: bubble up to caller
fn get_nested(obj: &Object) -> ??u64 {
    let field = obj.get_field("data")??;  // Returns VOID if no field
    let value = field.as_u64()??;          // Returns VOID if not u64
    return value;
}

// VOID coalescing: provide default
fn get_or_zero(ptr: &u64) -> u64 {
    return *ptr ?? 0;
}

// VOID assertion: panic with message
fn must_exist(ptr: &u64) -> u64 {
    return *ptr !! "value was unexpectedly VOID";
}
```

### 2. Effect System

Effects declare WHAT a function CAN do. Compile-time enforcement.

```seraphim
// PURE: Total function, no side effects
fn add(a: u64, b: u64) -> u64 pure {
    return a + b;
}

// VOID: May return VOID
fn safe_divide(a: u64, b: u64) -> ??u64 {
    if b == 0 { return VOID; }
    return a / b;
}

// PERSIST: Uses Atlas (persistent storage)
fn save_config(config: &Config) effects(PERSIST) {
    atlas::write("config", config);
}

// NETWORK: Uses Aether (distributed memory)
fn fetch_data(key: &str) -> ??Data effects(NETWORK, VOID) {
    return aether::get(key);
}

// Combined effects
fn sync_state(state: &State) effects(PERSIST, NETWORK, VOID) {
    let remote = fetch_remote()??;
    save_local(remote);
}
```

**Effect Hierarchy:**
```
         ┌─────┐
         │ IO  │  ← Kernel only
         └──┬──┘
            │
    ┌───────┼───────┐
    │       │       │
    ▼       ▼       ▼
┌───────┐┌───────┐┌───────┐
│NETWORK││PERSIST││TIMER  │
└───┬───┘└───┬───┘└───┬───┘
    │        │        │
    └────────┼────────┘
             │
             ▼
         ┌──────┐
         │ VOID │  ← May return VOID
         └──┬───┘
            │
            ▼
         ┌──────┐
         │ PURE │  ← Total function
         └──────┘
```

### 3. Substrate-Aware Types

Types know WHERE data lives.

```seraphim
// VOLATILE: RAM only (default, fast, lost on crash)
fn process_local(data: &volatile [u8]) { ... }

// ATLAS: Persistent storage
fn load_config() -> &atlas Config effects(PERSIST) {
    return atlas::genesis::<Config>();
}

// AETHER: Network/remote
fn fetch_remote(node: NodeId) -> ??&aether [u8] effects(NETWORK, VOID) {
    return aether::get(node, "key");
}

// Substrate elevation blocks
fn persist_data(local: &volatile Data) effects(PERSIST) {
    persist {
        let saved = local.clone();  // Allocated in Atlas
        atlas::set_root(saved);
    }
}
```

### 4. Galactic Numbers (Native)

256-bit fixed-point with automatic differentiation as first-class types.

```seraphim
// Literal suffixes: s (scalar), d (dual), g (galactic)
let x = 3.14g;           // Galactic: 256-bit with derivative
let y = x * x;           // y.primal = 9.8596, y.tangent = 6.28

// Automatic differentiation in physics
fn simulate(pos: galactic, vel: galactic, acc: galactic, dt: galactic)
    -> (galactic, galactic)
{
    let new_vel = vel + acc * dt;
    let new_pos = pos + new_vel * dt;
    return (new_pos, new_vel);
    // Derivatives tracked automatically through entire computation
}
```

### 5. Capability-Based Memory

References carry capabilities with bounds and permissions.

```seraphim
fn sum_array(data: &[u64]) -> u64 {
    // Compiler infers: needs READ permission
    // Compiler proves: all accesses in bounds
    let mut sum: u64 = 0;
    for i in 0..data.len() {
        sum = sum + data[i];  // Proven in-bounds
    }
    return sum;
}
```

### 6. Pipe Operator

Left-to-right data flow.

```seraphim
fn process(input: &str) -> ??Config {
    return input
        |> trim
        |> parse_json??
        |> extract_config??;
}
```

## C Code Generation Patterns

### Pattern 1: VOID Propagation

```seraphim
fn foo() -> ??u64 {
    let x = bar()??;
    return x + 1;
}
```

Generates:
```c
uint64_t seraphim_foo(void) {
    uint64_t x = seraphim_bar();
    if (SERAPH_IS_VOID(x)) return SERAPH_VOID_U64;
    return seraph_add_u64_void(x, 1);
}
```

### Pattern 2: VOID Coalescing

```seraphim
let x = bar() ?? 0;
```

Generates:
```c
uint64_t _tmp_1 = seraphim_bar();
uint64_t x = SERAPH_IS_VOID(_tmp_1) ? 0 : _tmp_1;
```

### Pattern 3: Galactic Operations

```seraphim
let g = 3.14g;
let result = g * g;
```

Generates:
```c
Seraph_Galactic g = seraph_galactic_from_double(3.14);
Seraph_Galactic result = seraph_galactic_mul(g, g);
```

### Pattern 4: Persist Block

```seraphim
persist {
    let data = create_data();
}
```

Generates:
```c
{
    Seraph_Atlas_Transaction* _tx = seraph_atlas_begin(g_atlas);
    /* Allocations use Atlas */
    Data* data = (Data*)seraph_atlas_alloc(sizeof(Data));
    *data = seraphim_create_data_atlas(_tx);
    seraph_atlas_commit(g_atlas, _tx);
}
```

### Pattern 5: Capability Slice

```seraphim
fn sum(data: &[u64]) -> u64 { ... }
```

Generates:
```c
uint64_t seraphim_sum(Seraph_Capability data_cap) {
    if (seraph_capability_is_void(data_cap)) return SERAPH_VOID_U64;
    uint64_t* data = (uint64_t*)data_cap.base;
    size_t len = data_cap.length / sizeof(uint64_t);
    /* Body uses capability for bounds */
}
```

## Proof-Carrying Code

### Proof Types

```c
typedef enum {
    PROOF_BOUNDS,      /* Array access proven in-bounds */
    PROOF_EFFECT,      /* Function uses only declared effects */
    PROOF_PERMISSION,  /* Capability has required permissions */
    PROOF_ALIASING,    /* No aliasing violations */
    PROOF_GENERATION   /* Capability generation is valid */
} Seraph_Proof_Kind;
```

### Proof Embedding

Generated C includes proof section:
```c
/* === SERAPHIM PROOF SECTION === */
static const Seraph_Proof _seraphim_proofs[] = {
    { PROOF_EFFECT, 0, 0, {0x01, 0x01, ...} },
    { PROOF_BOUNDS, 0, 5, {...} },
    { PROOF_PERMISSION, 1, 3, {...} },
};
static const size_t _seraphim_proof_count = 3;

/* Proof accessor for kernel verifier */
const Seraph_Proof* seraphim_get_proofs(size_t* count) {
    *count = _seraphim_proof_count;
    return _seraphim_proofs;
}
```

### Verification Flow

1. Program requests load
2. Kernel reads proof section
3. Kernel verifier checks each proof against code
4. If all proofs valid: map code, run at full speed
5. If any proof invalid: reject load

## Implementation Structure

```
SERAPH-BUILD/
├── include/seraph/seraphim/
│   ├── token.h         # Token types
│   ├── lexer.h         # Lexer interface
│   ├── ast.h           # AST nodes
│   ├── parser.h        # Parser interface
│   ├── types.h         # Type system
│   ├── effects.h       # Effect system
│   ├── checker.h       # Type/effect checker
│   ├── proofs.h        # Proof structures
│   ├── codegen.h       # C code generator
│   └── compiler.h      # Main interface
│
├── src/seraphim/
│   ├── token.c
│   ├── lexer.c
│   ├── ast.c
│   ├── parser.c
│   ├── types.c
│   ├── effects.c
│   ├── checker.c
│   ├── proofs.c
│   ├── codegen.c
│   └── main.c          # CLI entry point
│
├── tests/
│   ├── test_seraphim_lexer.c
│   ├── test_seraphim_parser.c
│   ├── test_seraphim_checker.c
│   ├── test_seraphim_codegen.c
│   └── test_seraphim_integration.c
│
└── examples/seraphim/
    ├── hello.seraph
    ├── void_handling.seraph
    ├── galactic_math.seraph
    └── atlas_persist.seraph
```

## Grammar (EBNF)

```ebnf
program        = { item }
item           = function | struct | enum | const | use | foreign

function       = 'fn' IDENT generic? '(' params? ')' return_type? effects? block
generic        = '<' generic_param { ',' generic_param } '>'
params         = param { ',' param }
param          = IDENT ':' type
return_type    = '->' type
effects        = 'effects' '(' effect { ',' effect } ')' | '[' effect ']'
effect         = 'pure' | 'PURE' | 'VOID' | 'PERSIST' | 'NETWORK' | 'TIMER' | 'IO'

type           = primitive | reference | slice | array | tuple | void_type | named
primitive      = 'u8' | 'u16' | 'u32' | 'u64' | 'i8' | 'i16' | 'i32' | 'i64'
               | 'bool' | 'char' | 'scalar' | 'dual' | 'galactic'
reference      = '&' ['mut'] [substrate] type
substrate      = 'volatile' | 'atlas' | 'aether'
slice          = '[' type ']'
void_type      = '??' type

statement      = let_stmt | return_stmt | if_stmt | for_stmt | while_stmt
               | persist_stmt | aether_stmt | recover_stmt | expr_stmt
let_stmt       = 'let' ['mut'] IDENT [':' type] '=' expr ';'
persist_stmt   = 'persist' block
recover_stmt   = 'recover' block 'else' block

expr           = void_expr | pipe_expr | binary_expr | unary_expr | call_expr
               | index_expr | field_expr | lambda_expr | literal | ident
void_expr      = expr '??' [expr]      /* propagation or coalescing */
void_assert    = expr '!!' STRING
pipe_expr      = expr '|>' expr
lambda_expr    = '|' params? '|' expr

literal        = INT | FLOAT | STRING | 'true' | 'false' | 'VOID'
               | INT 'u' | INT 'i' | FLOAT 's' | FLOAT 'd' | FLOAT 'g'
```

## Integration with SERAPH-BUILD

The Seraphim compiler uses existing SERAPH primitives:

| Seraphim Concept | SERAPH Primitive |
|-----------------|------------------|
| VOID values | `SERAPH_IS_VOID()`, `SERAPH_VOID_*` |
| Entropic math | `seraph_add_*_void()`, etc. |
| Galactic numbers | `Seraph_Galactic`, `seraph_galactic_*()` |
| Capabilities | `Seraph_Capability` |
| Memory allocation | `Seraph_Arena`, `seraph_arena_alloc()` |
| Persistent storage | `Seraph_Atlas`, `seraph_atlas_*()` |
| Network access | `Seraph_Aether`, `seraph_aether_*()` |
| Time operations | `Seraph_Chronon`, `seraph_chronon_*()` |

## Test Coverage

Target: ~260 tests across all components

- Lexer: ~30 tests (all token types)
- Parser: ~50 tests (all grammar rules)
- Type System: ~40 tests (types, substrates, VOID)
- Effect System: ~30 tests (inference, checking)
- Code Generator: ~60 tests (all patterns)
- Proof Generator: ~30 tests (all proof types)
- Integration: ~20 tests (end-to-end compilation)

## Example: Complete Program

```seraphim
// hello.seraph - A complete Seraphim program

// Pure function: total, no effects
fn add(a: u64, b: u64) -> u64 pure {
    return a + b;
}

// VOID-returning function
fn safe_sqrt(x: galactic) -> ??galactic {
    if x.primal < 0.0g {
        return VOID;
    }
    return x.sqrt();
}

// Persistent data
struct Config : atlas {
    name: String,
    value: u64
}

// Main with multiple effects
fn main() effects(PERSIST, VOID) {
    // Use Galactic for physics
    let g = 9.81g;
    let t = 2.0g;
    let distance = 0.5g * g * t * t;

    // VOID handling
    let root = safe_sqrt(distance) ?? 0.0g;

    // Persist data
    persist {
        let config = Config {
            name: "gravity_test",
            value: 42
        };
        atlas::set_root(config);
    }

    return 0;
}
```

## FPU Enforcement (Zero-FPU Integration)

Seraphim integrates with SERAPH's Zero-FPU architecture to ensure kernel code never generates floating-point instructions.

### Enforcement Levels

```c
/* Set via compiler flag or API */
void seraph_fpu_set_enforcement(int level);

/* Level 0: Allow FPU (userspace default) */
/* Level 1: Warn on FPU usage */
/* Level 2: Error on FPU usage (kernel default) */
```

### Compile-Time Checks

The compiler performs two levels of FPU detection:

1. **IR-Level**: Checks for calls to known floating-point functions (sinf, cosf, sqrtf, etc.)
2. **Assembly-Level**: Scans generated assembly for FPU mnemonics (fld, addss, vmulpd, etc.)

### Pattern Optimization

The compiler recognizes mathematical patterns and replaces them with Zero-FPU optimized versions:

| Pattern | Replacement | Benefit |
|---------|-------------|---------|
| `sin(x); cos(x);` | `sincos(x, &s, &c);` | Single computation |
| `x*x + y*y` | `hypot(x,y)²` | Overflow protection |
| `x * 4` | `x << 2` | Strength reduction |
| `x % 8` | `x & 7` | Bitwise for power-of-2 |

### Integer-Only Functions

Functions with the `[integer_only]` attribute or `q16_`/`q64_` prefix are enforced:

```seraphim
[integer_only]
fn kernel_sin(angle: u32) -> u32 {
    return q16_sin(angle);  // OK: uses Zero-FPU
    // return sinf(angle);  // ERROR: would use FPU
}
```

### Reporting

```c
void seraph_fpu_print_violations(void);
/* Output:
   [FPU-CHECK] Found 2 floating-point violations:
     error: kernel_math: call to sinf
     error: kernel_math: fmul instruction
   [FPU-CHECK] Total: 2 errors, 0 warnings
*/
```

## Summary

Seraphim represents a new approach to systems programming:

1. **C Transpiler**: Outputs readable C using existing SERAPH primitives
2. **VOID-Native**: Errors as bit patterns, zero-cost propagation
3. **Effect System**: Compile-time capability enforcement
4. **Substrate-Aware**: Types know where data lives
5. **Proof-Carrying**: Safety proven at compile time, verified at load time
6. **Zero External Dependencies**: Pure C, uses only SERAPH-BUILD

The result is a language that combines C's performance with Rust's safety guarantees, without the borrow checker complexity or runtime overhead.

## Source Files

| File | Description |
|------|-------------|
| `src/seraphim/seraphic.c` | Compiler driver CLI |
| `src/seraphim/lexer.c` | Tokenization |
| `src/seraphim/parser.c` | Recursive descent parser |
| `src/seraphim/ast.c` | AST node construction |
| `src/seraphim/types.c` | Type system |
| `src/seraphim/checker.c` | Type/effect checker |
| `src/seraphim/effects.c` | Effect inference |
| `src/seraphim/proofs.c` | Proof generation |
| `src/seraphim/codegen.c` | C code generator |
| `src/seraphim/celestial_ir.c` | Celestial IR (native path) |
| `src/seraphim/elf64_writer.c` | ELF64 generation |
| `src/seraphim/fpu_check.c` | FPU instruction detection |
| `src/seraphim/pattern_opt.c` | Pattern-based optimization |
| `include/seraph/seraphim/` | Header files for all components |
