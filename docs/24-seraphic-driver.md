# MC30: Seraphic - The SERAPH Compiler Driver

## Overview

`seraphic` is the command-line interface to the Seraphim compiler. It orchestrates the complete compilation pipeline from source code to native executable.

## Plain English Explanation

When you type `seraphic hello.srph`, you're asking the compiler to:

1. Read your source file
2. Parse it into an internal representation
3. Check types and effects
4. Generate intermediate representation (Celestial IR)
5. Optimize the IR
6. Generate native machine code
7. Wrap it in an ELF executable

The result is a self-contained binary that can run directly on SERAPH (or Linux for testing).

## Usage

```
seraphic [options] <source-file>
```

### Options

| Option | Description |
|--------|-------------|
| `-o <file>` | Output file (default: a.out) |
| `--emit-ir` | Output Celestial IR instead of executable |
| `--emit-asm` | Output assembly-like listing |
| `--emit-c` | Output C code (transpilation mode) |
| `-O<n>` | Optimization level (0-3) |
| `-g` | Include debug information |
| `-v`, `--verbose` | Verbose output |
| `--target=<t>` | Target architecture (x64, arm64, riscv64) |
| `--help`, `-h` | Show help |
| `--version`, `-V` | Show version |

### Target Architectures

| Target | Aliases | Description |
|--------|---------|-------------|
| `x64` | `x86_64` | Intel/AMD 64-bit |
| `arm64` | `aarch64` | ARM 64-bit |
| `riscv64` | - | RISC-V 64-bit |

## Examples

### Compile to Native Executable

```bash
# Default (x86-64)
seraphic hello.srph -o hello

# ARM64 target
seraphic hello.srph --target=arm64 -o hello.arm

# RISC-V target
seraphic hello.srph --target=riscv64 -o hello.rv

# With optimizations
seraphic -O2 hello.srph -o hello.opt
```

### Output Celestial IR

```bash
seraphic --emit-ir hello.srph -o hello.cir
```

Output looks like:
```
define fn main() -> i32 effects(PURE) {
entry:
    %0 = const i32 0
    ret i32 %0
}
```

### Transpile to C

```bash
seraphic --emit-c hello.srph -o hello.c
```

This generates C code that uses SERAPH primitives:
```c
#include "seraph.h"

int32_t seraphim_main(void) {
    return 0;
}
```

### Verbose Compilation

```bash
seraphic -v hello.srph
```

Output:
```
Compiling 'hello.srph' (42 bytes)
Lexed 15 tokens
Parsed AST successfully
Generated Celestial IR
Constant folding: 3 instructions folded
Dead code elimination: 1 instructions removed
Successfully compiled to 'a.out'
```

## Compilation Pipeline

```
┌──────────────────────────────────────────────────────────────────────┐
│                      SERAPHIC COMPILATION PIPELINE                    │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   hello.srph                                                         │
│        │                                                             │
│        ▼                                                             │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │  FRONTEND                                                    │   │
│   │                                                              │   │
│   │   Source → Lexer → Parser → Type Checker → Effect Checker   │   │
│   │                                                              │   │
│   │   Output: Typed, effect-annotated AST                       │   │
│   └─────────────────────────────────────────────────────────────┘   │
│        │                                                             │
│        ▼                                                             │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │  IR GENERATION (ast_to_ir.c)                                 │   │
│   │                                                              │   │
│   │   AST → Celestial IR (SSA form)                             │   │
│   │                                                              │   │
│   │   Output: Module with functions, blocks, instructions        │   │
│   └─────────────────────────────────────────────────────────────┘   │
│        │                                                             │
│        ├────────────────────────────────────────────────────────────┤
│        │                      --emit-ir exits here                   │
│        ▼                                                             │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │  OPTIMIZATION                                                │   │
│   │                                                              │   │
│   │   celestial_fold_constants()                                │   │
│   │   celestial_eliminate_dead_code()                           │   │
│   │                                                              │   │
│   └─────────────────────────────────────────────────────────────┘   │
│        │                                                             │
│        ▼                                                             │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │  BACKEND (per architecture)                                  │   │
│   │                                                              │   │
│   │   ┌────────────────┬────────────────┬────────────────┐     │   │
│   │   │      x64       │     ARM64      │    RISC-V      │     │   │
│   │   ├────────────────┼────────────────┼────────────────┤     │   │
│   │   │celestial_to_x64│celestial_to_arm│celestial_to_rv │     │   │
│   │   │  x64_encode    │  arm64_encode  │  riscv_encode  │     │   │
│   │   └────────────────┴────────────────┴────────────────┘     │   │
│   │                                                              │   │
│   │   Output: Native machine code bytes                         │   │
│   └─────────────────────────────────────────────────────────────┘   │
│        │                                                             │
│        ▼                                                             │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │  ELF WRITER (elf64_writer.c)                                 │   │
│   │                                                              │   │
│   │   Code + Manifest + Effects + Proofs → ELF64 binary         │   │
│   │                                                              │   │
│   └─────────────────────────────────────────────────────────────┘   │
│        │                                                             │
│        ▼                                                             │
│   a.out (or specified output file)                                   │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

## Code Organization

```
src/seraphim/
├── seraphic.c           # Compiler driver (main entry point)
│
├── lexer.c              # Tokenization
├── parser.c             # Recursive descent parser
├── ast.c                # AST construction
│
├── types.c              # Type system
├── checker.c            # Type checking
├── effects.c            # Effect inference
│
├── ast_to_ir.c          # AST → Celestial IR
├── celestial_ir.c       # IR construction and verification
│
├── celestial_to_x64.c   # IR → x86-64
├── celestial_to_arm64.c # IR → ARM64
├── celestial_to_riscv.c # IR → RISC-V
│
├── x64_encode.c         # x86-64 instruction encoding
├── arm64_encode.c       # ARM64 instruction encoding
├── riscv_encode.c       # RISC-V instruction encoding
│
├── elf64_writer.c       # ELF generation
├── proofs.c             # Proof generation
│
└── codegen.c            # C code generation (--emit-c)
```

## Internal Architecture

### Options Structure

```c
typedef enum {
    TARGET_X64,
    TARGET_ARM64,
    TARGET_RISCV64,
} Seraphic_Target;

typedef enum {
    OUTPUT_EXECUTABLE,
    OUTPUT_OBJECT,
    OUTPUT_IR,
    OUTPUT_ASM,
    OUTPUT_C,
} Seraphic_Output_Type;

typedef struct {
    const char*         input_file;
    const char*         output_file;
    Seraphic_Target     target;
    Seraphic_Output_Type output_type;
    int                 opt_level;
    int                 debug_info;
    int                 verbose;
    int                 show_help;
    int                 show_version;
} Seraphic_Options;
```

### Main Entry Point

```c
int main(int argc, char** argv) {
    Seraphic_Options opts = {0};
    opts.target = TARGET_X64;
    opts.output_type = OUTPUT_EXECUTABLE;
    opts.output_file = "a.out";

    if (parse_args(argc, argv, &opts) != 0)
        return 1;

    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    char* source = read_file(opts.input_file, &source_len);
    if (!source) {
        fprintf(stderr, "Error: Could not read file '%s'\n", opts.input_file);
        return 1;
    }

    Seraph_Vbit result;
    if (opts.output_type == OUTPUT_C) {
        result = compile_to_c(&opts, source, source_len);
    } else {
        result = compile_to_native(&opts, source, source_len);
    }

    return seraph_vbit_is_true(result) ? 0 : 1;
}
```

### Native Compilation Path

```c
static Seraph_Vbit compile_to_native(Seraphic_Options* opts,
                                      const char* source,
                                      size_t source_len) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 4 * 1024 * 1024, 0, 0);

    /* Lex */
    Seraph_Lexer lexer;
    seraph_lexer_init(&lexer, source, source_len, opts->input_file, &arena);
    seraph_lexer_tokenize(&lexer);

    /* Parse */
    Seraph_Parser parser;
    seraph_parser_init(&parser, &lexer, &arena);
    Seraph_AST_Node* module_ast = seraph_parse_module(&parser);

    /* Generate Celestial IR */
    Celestial_Module* ir_module = ir_convert_module(module_ast, &type_ctx, &arena);

    /* Verify IR */
    celestial_verify_module(ir_module);

    /* Optimize */
    celestial_fold_constants(ir_module);
    celestial_eliminate_dead_code(ir_module);

    /* Output IR if requested */
    if (opts->output_type == OUTPUT_IR) {
        FILE* ir_out = fopen(opts->output_file, "w");
        celestial_print_module(ir_module, ir_out);
        fclose(ir_out);
        return SERAPH_VBIT_TRUE;
    }

    /* Generate ELF */
    Seraph_Elf_Target elf_target;
    switch (opts->target) {
        case TARGET_X64:    elf_target = SERAPH_ELF_TARGET_X64; break;
        case TARGET_ARM64:  elf_target = SERAPH_ELF_TARGET_ARM64; break;
        case TARGET_RISCV64: elf_target = SERAPH_ELF_TARGET_RISCV64; break;
    }

    seraph_elf_from_celestial_target(ir_module, &proofs,
                                      elf_target, opts->output_file);

    seraph_arena_destroy(&arena);
    return SERAPH_VBIT_TRUE;
}
```

### C Transpilation Path

```c
static Seraph_Vbit compile_to_c(Seraphic_Options* opts,
                                 const char* source,
                                 size_t source_len) {
    Seraph_Arena arena;
    seraph_arena_create(&arena, 1024 * 1024, 0, 0);

    /* Lex and Parse (same as native) */
    Seraph_Lexer lexer;
    Seraph_Parser parser;
    ...

    /* Generate C code */
    FILE* output = fopen(opts->output_file, "w");
    Seraph_Codegen codegen;
    seraph_codegen_init(&codegen, output, &arena);
    seraph_codegen_set_options(&codegen, SERAPH_CODEGEN_OPT_DEBUG);
    seraph_codegen_module(&codegen, module_ast);

    fclose(output);
    seraph_arena_destroy(&arena);
    return SERAPH_VBIT_TRUE;
}
```

## Self-Hosting Plan

The Seraphim compiler is designed for eventual self-hosting:

### Stage 0 (Current)
`seraphic` is written in C and compiles Seraphim code.

### Stage 1 (Future)
`seraphic.srph` (Seraphim source) is compiled by Stage 0 C compiler.

### Stage 2 (Verification)
`seraphic.srph` is compiled by Stage 1 binary. If output matches Stage 1, bootstrap is verified.

## NIH Compliance

Seraphic has **zero external dependencies**:

- No LLVM
- No GCC libraries
- No libelf
- No libc (for kernel mode)

The only requirement is a C compiler for the initial bootstrap (Stage 0).

## Error Handling

### Lexer Errors
```
hello.srph:5:12: error: unexpected character '$'
    let x = $invalid;
            ^
```

### Parser Errors
```
hello.srph:10:5: error: expected ';' after expression
    let y = 42
        ^
```

### Type Errors
```
hello.srph:15:9: error: type mismatch: expected 'u64', found 'bool'
    let z: u64 = true;
                 ^^^^
```

### Effect Errors
```
hello.srph:20:5: error: function 'save' calls 'atlas::write' but lacks PERSIST effect
    atlas::write("key", data);
    ^
```

## Memory Management

Compilation uses arenas for memory allocation:

```c
/* Create arena for compilation */
Seraph_Arena arena;
seraph_arena_create(&arena, 4 * 1024 * 1024, 0, 0);

/* All allocations go through arena */
Seraph_AST_Node* node = seraph_arena_alloc(&arena, sizeof(Seraph_AST_Node), 8);

/* Single cleanup at end */
seraph_arena_destroy(&arena);
```

Benefits:
1. Fast allocation (bump pointer)
2. No fragmentation
3. Single deallocation
4. Cache-friendly sequential layout

## Future Work

1. **Incremental compilation**: Only recompile changed functions
2. **LTO (Link-Time Optimization)**: Cross-module inlining
3. **Debug info**: DWARF generation for debugger support
4. **Profiling**: PGO (Profile-Guided Optimization)
5. **Self-hosting**: Compiler written in Seraphim

## Summary

`seraphic` is the front door to SERAPH development:

1. **Single binary**: No toolchain to install
2. **Multi-target**: x64, ARM64, RISC-V from one source
3. **Two modes**: Native compilation or C transpilation
4. **NIH pure**: No external dependencies
5. **Production-ready**: Proper error handling, optimization

Run `seraphic --help` to get started.

## Source Files

| File | Description |
|------|-------------|
| `src/seraphim/seraphic.c` | Compiler driver, command-line parsing, pipeline orchestration |
| `src/seraphim/lexer.c` | Tokenization |
| `src/seraphim/parser.c` | Recursive descent parser, AST construction |
| `src/seraphim/checker.c` | Type checking, effect inference |
| `src/seraphim/codegen.c` | C code generation (transpilation mode) |
| `include/seraph/seraphim/compiler.h` | Compiler interface |
