# MC29: ELF64 Writer with SERAPH Extensions

## Overview

The ELF64 Writer generates native executable files from compiled Celestial IR. It produces standard ELF64 binaries with SERAPH-specific extensions for proof-carrying code.

**Philosophy**: The binary itself is a certificate of correctness.

## Plain English Explanation

After the compiler generates machine code, we need to wrap it in a format the operating system can load. ELF (Executable and Linkable Format) is the standard format on Linux and SERAPH.

But SERAPH goes further. Our ELF files contain:
1. The actual machine code (like any ELF)
2. Compile-time safety proofs embedded in special sections
3. Capability templates defining what memory the program can access
4. Effect declarations showing what each function can do

When SERAPH loads a binary, it **verifies** the proofs. If valid, the code runs at full speed. If not, the binary is rejected. No code execution without proof.

## ELF64 Structure

### Standard ELF Sections

| Section | Purpose |
|---------|---------|
| `.text` | Executable code |
| `.rodata` | Read-only data (strings, constants) |
| `.data` | Initialized writable data |
| `.bss` | Zero-initialized data (no file space) |
| `.shstrtab` | Section name string table |
| `.symtab` | Symbol table |
| `.strtab` | Symbol string table |

### SERAPH Custom Sections

| Section | Type | Purpose |
|---------|------|---------|
| `.seraph.manifest` | `0x60000004` | Sovereign metadata and requirements |
| `.seraph.proofs` | `0x60000001` | Merkle roots of safety proofs |
| `.seraph.effects` | `0x60000002` | Function effect declarations |
| `.seraph.caps` | `0x60000003` | Capability templates |

### SERAPH Custom Program Header

| Type | Value | Purpose |
|------|-------|---------|
| `PT_SERAPH` | `0x60000000` | Points to SERAPH metadata segment |

## SERAPH Manifest

The manifest is the binary's "ID card":

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;              /* SERAPH_MANIFEST_MAGIC (0x5345524D = "SERM") */
    uint32_t version;            /* Manifest format version */
    uint32_t kernel_min_version; /* Minimum kernel version required */
    uint32_t flags;              /* Sovereign creation flags */

    uint64_t entry_point;        /* Entry point virtual address */
    uint64_t stack_size;         /* Required stack size */
    uint64_t heap_size;          /* Required heap size */
    uint64_t chronon_budget;     /* Initial Chronon budget */

    uint32_t atlas_region_count; /* Number of Atlas regions needed */
    uint32_t aether_node_count;  /* Number of Aether nodes */
    uint32_t cap_template_count; /* Number of capability templates */

    uint8_t  proof_merkle_root[32]; /* Merkle root of all proofs */
} Seraph_Manifest;
```

### Manifest Fields Explained

- **magic**: Identifies this as a SERAPH manifest
- **kernel_min_version**: Binary won't load on older kernels
- **stack_size**: Kernel allocates this much stack (default: 64 KB)
- **heap_size**: Kernel allocates this much heap (default: 1 MB)
- **chronon_budget**: Initial time budget in Chronon units (default: 1M)
- **proof_merkle_root**: Hash of all embedded proofs for quick verification

## Effect Declarations

Each function has an effect declaration:

```c
typedef struct __attribute__((packed)) {
    uint32_t function_id;        /* Function index */
    uint32_t declared_effects;   /* Effects declared by programmer */
    uint32_t verified_effects;   /* Effects verified by compiler */
    uint32_t required_caps;      /* Required capability permissions */
} Seraph_Effect_Decl;
```

### Effect Flags

| Flag | Value | Meaning |
|------|-------|---------|
| `EFFECT_VOID` | 0x01 | May return VOID |
| `EFFECT_READ` | 0x02 | Reads memory |
| `EFFECT_WRITE` | 0x04 | Writes memory |
| `EFFECT_PERSIST` | 0x08 | Uses Atlas |
| `EFFECT_NETWORK` | 0x10 | Uses Aether |
| `EFFECT_TIMER` | 0x20 | Uses Chronon |
| `EFFECT_ALLOC` | 0x40 | Allocates memory |
| `EFFECT_PANIC` | 0x80 | May panic |

## Capability Templates

Define initial capabilities for the Sovereign:

```c
typedef struct __attribute__((packed)) {
    uint64_t base;               /* Base address (or offset) */
    uint64_t length;             /* Region length */
    uint32_t permissions;        /* Permission flags */
    uint32_t flags;              /* Additional flags */
} Seraph_Cap_Template;
```

### Permission Flags

| Flag | Value | Meaning |
|------|-------|---------|
| `CAP_PERM_READ` | 0x01 | Can read through capability |
| `CAP_PERM_WRITE` | 0x02 | Can write through capability |
| `CAP_PERM_EXEC` | 0x04 | Can execute through capability |
| `CAP_PERM_SHARE` | 0x08 | Can derive child capabilities |

## ELF Writer API

### Initialization

```c
Seraph_Vbit seraph_elf_writer_init(Seraph_Elf_Writer* writer);
void seraph_elf_writer_free(Seraph_Elf_Writer* writer);
```

### Configuration

```c
/* Set base load address (typically 0x400000 for executables) */
void seraph_elf_writer_set_base(Seraph_Elf_Writer* writer, uint64_t addr);

/* Set entry point */
void seraph_elf_writer_set_entry(Seraph_Elf_Writer* writer, uint64_t entry);
```

### Section Management

```c
/* Create a new section */
size_t seraph_elf_section_create(Seraph_Elf_Writer* writer,
                                  const char* name,
                                  uint32_t type,
                                  uint64_t flags);

/* Set section data */
Seraph_Vbit seraph_elf_section_set_data(Seraph_Elf_Writer* writer,
                                         size_t section_index,
                                         const void* data,
                                         size_t size);

/* Append data to section */
size_t seraph_elf_section_append(Seraph_Elf_Writer* writer,
                                  size_t section_index,
                                  const void* data,
                                  size_t size);
```

### Symbol Management

```c
size_t seraph_elf_symbol_add(Seraph_Elf_Writer* writer,
                              const char* name,
                              uint64_t value,
                              uint64_t size,
                              uint8_t type,
                              uint8_t bind,
                              uint16_t section_index);
```

Symbol types:
- `STT_FUNC` (2): Function
- `STT_OBJECT` (1): Data object

Symbol bindings:
- `STB_LOCAL` (0): Not visible outside file
- `STB_GLOBAL` (1): Visible everywhere
- `STB_WEAK` (2): Lower priority than global

### SERAPH Extensions

```c
/* Set the manifest */
void seraph_elf_set_manifest(Seraph_Elf_Writer* writer,
                              const Seraph_Manifest* manifest);

/* Add effect declaration */
void seraph_elf_add_effect(Seraph_Elf_Writer* writer,
                            const Seraph_Effect_Decl* decl);

/* Add capability template */
void seraph_elf_add_cap_template(Seraph_Elf_Writer* writer,
                                  const Seraph_Cap_Template* cap);

/* Set proof table for embedding */
void seraph_elf_set_proofs(Seraph_Elf_Writer* writer,
                            const Seraph_Proof_Table* proofs);
```

### Code and Data

```c
/* Set .text section */
void seraph_elf_set_code(Seraph_Elf_Writer* writer,
                          const uint8_t* code,
                          size_t size);

/* Set .rodata section */
void seraph_elf_set_rodata(Seraph_Elf_Writer* writer,
                            const uint8_t* rodata,
                            size_t size);

/* Set .data section */
void seraph_elf_set_data(Seraph_Elf_Writer* writer,
                          const uint8_t* data,
                          size_t size);

/* Set .bss size */
void seraph_elf_set_bss(Seraph_Elf_Writer* writer, size_t size);
```

### Output

```c
/* Write to file */
Seraph_Vbit seraph_elf_write_file(Seraph_Elf_Writer* writer,
                                   const char* filename);

/* Write to buffer */
Seraph_Vbit seraph_elf_write_buffer(Seraph_Elf_Writer* writer,
                                     uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t* written_out);

/* Calculate total size needed */
size_t seraph_elf_calculate_size(Seraph_Elf_Writer* writer);
```

## High-Level API

The simplest way to generate an ELF:

```c
/* From Celestial IR to ELF (x64 default) */
Seraph_Vbit seraph_elf_from_celestial(Celestial_Module* module,
                                       const Seraph_Proof_Table* proofs,
                                       const char* filename);

/* With target architecture selection */
Seraph_Vbit seraph_elf_from_celestial_target(Celestial_Module* module,
                                              const Seraph_Proof_Table* proofs,
                                              Seraph_Elf_Target target,
                                              const char* filename);
```

Target architectures:
- `SERAPH_ELF_TARGET_X64`: x86-64 (machine type 62)
- `SERAPH_ELF_TARGET_ARM64`: ARM64 (machine type 183)
- `SERAPH_ELF_TARGET_RISCV64`: RISC-V 64 (machine type 243)

## ELF File Layout

```
┌───────────────────────────────────────┐
│           ELF Header (64 bytes)        │
├───────────────────────────────────────┤
│         Program Headers                │
│   - PT_PHDR (self-reference)          │
│   - PT_LOAD (.text + .rodata)         │
│   - PT_LOAD (.data + .bss)            │
│   - PT_SERAPH (SERAPH metadata)       │
├───────────────────────────────────────┤
│              .text                     │  ← Executable code
├───────────────────────────────────────┤
│             .rodata                    │  ← String constants
├───────────────────────────────────────┤
│              .data                     │  ← Initialized data
├───────────────────────────────────────┤
│        .seraph.manifest               │  ← Sovereign metadata
├───────────────────────────────────────┤
│        .seraph.proofs                 │  ← Safety proofs
├───────────────────────────────────────┤
│        .seraph.effects                │  ← Effect declarations
├───────────────────────────────────────┤
│        .seraph.caps                   │  ← Capability templates
├───────────────────────────────────────┤
│            .shstrtab                  │  ← Section name strings
├───────────────────────────────────────┤
│         Section Headers               │  ← Section metadata
└───────────────────────────────────────┘
```

## Default Configuration

The high-level API uses these defaults:

```c
/* Base address for kernel binaries */
writer->base_addr = 0xFFFF800000100000ULL;  /* High kernel space */

/* Base address for user binaries */
writer->base_addr = 0x400000;  /* Standard executable base */

/* Default manifest values */
manifest.stack_size = 64 * 1024;      /* 64 KB stack */
manifest.heap_size = 1024 * 1024;     /* 1 MB heap */
manifest.chronon_budget = 1000000;    /* 1M time units */
```

## Compilation Pipeline Integration

```
compile_to_native(opts, source, source_len)
    │
    ├── seraph_lexer_init() + seraph_lexer_tokenize()
    ├── seraph_parser_init() + seraph_parse_module()
    ├── type_check()
    │
    ├── ir_convert_module()           # AST → Celestial IR
    ├── celestial_verify_module()      # Verify IR well-formed
    │
    ├── celestial_fold_constants()     # Optimization pass
    ├── celestial_eliminate_dead_code()# Optimization pass
    │
    └── seraph_elf_from_celestial_target()
            │
            ├── Create arena and code buffer
            ├── celestial_compile_module()  # IR → Machine code
            │       ├── x64 backend
            │       ├── arm64 backend
            │       └── riscv backend
            │
            ├── seraph_elf_writer_init()
            ├── seraph_elf_writer_set_base()
            ├── seraph_elf_set_code()
            ├── seraph_elf_writer_set_entry()
            ├── seraph_elf_set_manifest()
            ├── seraph_elf_add_effect() for each function
            │
            └── seraph_elf_write_file()
```

## Verification at Load Time

When SERAPH kernel loads a binary:

1. **Header Check**: Verify ELF magic, class, endianness
2. **SERAPH Check**: Look for PT_SERAPH program header
3. **Manifest Check**: Verify magic, version, kernel compatibility
4. **Proof Verification**: Compute proof hashes, compare to Merkle root
5. **Effect Validation**: Ensure declared effects are compatible
6. **Capability Setup**: Create initial capabilities from templates

If any check fails, the binary is rejected. No exceptions.

## NIH Compliance

The ELF writer has **zero external dependencies**:

- No `libelf`
- No `libbfd`
- No `objcopy`

All ELF structures are defined locally:

```c
/* We define our own ELF structures */
typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    ...
} Seraph_Elf64_Ehdr;
```

## Example Usage

```c
/* Create module from AST */
Celestial_Module* ir = ir_convert_module(ast, types, arena);

/* Verify IR */
if (!seraph_vbit_is_true(celestial_verify_module(ir))) {
    printf("IR verification failed\n");
    return 1;
}

/* Optimize */
celestial_fold_constants(ir);
celestial_eliminate_dead_code(ir);

/* Generate ELF for x86-64 */
Seraph_Proof_Table proofs = {0};  /* TODO: Generate proofs */
if (!seraph_vbit_is_true(
        seraph_elf_from_celestial_target(ir, &proofs,
                                          SERAPH_ELF_TARGET_X64,
                                          "output.elf"))) {
    printf("ELF generation failed\n");
    return 1;
}

printf("Generated output.elf\n");
```

## Summary

The ELF64 Writer is the final stage of SERAPH's native compiler:

1. **Standard ELF64**: Compatible with Linux tools
2. **SERAPH Extensions**: Proofs, effects, capabilities embedded
3. **Multi-Architecture**: x64, ARM64, RISC-V support
4. **Self-Describing**: Binaries carry their own safety certificates
5. **NIH Pure**: No external libraries

The result is binaries that are both standards-compliant and security-hardened by construction.

## Source Files

| File | Description |
|------|-------------|
| `src/seraphim/elf64_writer.c` | ELF64 generation, section management, SERAPH extensions |
| `include/seraph/seraphim/elf64_writer.h` | ELF writer API, manifest structures |
