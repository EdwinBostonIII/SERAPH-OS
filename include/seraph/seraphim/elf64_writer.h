/**
 * @file elf64_writer.h
 * @brief MC29: ELF64 Writer with SERAPH Extensions
 *
 * Generates ELF64 executables from Celestial IR with SERAPH-specific sections.
 *
 * SERAPH ELF Extensions:
 *
 * 1. .seraph.proofs - Embedded compile-time proofs
 *    Contains Merkle roots of proof trees for:
 *    - Bounds checking proofs
 *    - VOID propagation proofs
 *    - Effect contract proofs
 *    - Capability permission proofs
 *
 * 2. .seraph.effects - Function effect declarations
 *    For each function: declared effects, verified effects, effect mask
 *    Enables runtime verification without recompilation
 *
 * 3. .seraph.caps - Capability templates
 *    Initial capability configurations for Sovereign creation
 *    Defines what memory regions are accessible
 *
 * 4. .seraph.manifest - Sovereign metadata
 *    - Required kernel version
 *    - Resource requirements (memory, Chronon budget)
 *    - Substrate dependencies (Atlas regions, Aether nodes)
 *    - Entry point information
 *
 * The kernel validates these sections before execution. A SERAPH binary
 * without valid proofs CANNOT execute. This is security by construction.
 *
 * Philosophy: The binary itself is a certificate of correctness.
 */

#ifndef SERAPH_SERAPHIM_ELF64_WRITER_H
#define SERAPH_SERAPHIM_ELF64_WRITER_H

#include <stdint.h>
#include <stddef.h>
#include "seraph/vbit.h"
#include "seraph/seraphim/celestial_ir.h"
#include "seraph/seraphim/proofs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * ELF64 Constants (NIH - no elf.h dependency)
 *============================================================================*/

/* ELF Magic */
#define SERAPH_ELF_MAGIC     "\x7F" "ELF"
#define SERAPH_ELFCLASS64    2
#define SERAPH_ELFDATA2LSB   1
#define SERAPH_EV_CURRENT    1

/* ELF Types */
#define SERAPH_ET_EXEC       2   /* Executable */
#define SERAPH_ET_DYN        3   /* Shared object (PIE) */

/* Machine Types */
#define SERAPH_EM_X86_64     62
#define SERAPH_EM_AARCH64    183
#define SERAPH_EM_RISCV      243

/* ELF Target Architecture */
typedef enum {
    SERAPH_ELF_TARGET_X64 = 0,
    SERAPH_ELF_TARGET_ARM64,
    SERAPH_ELF_TARGET_RISCV64,
} Seraph_Elf_Target;

/* Section Types */
#define SERAPH_SHT_NULL      0
#define SERAPH_SHT_PROGBITS  1
#define SERAPH_SHT_SYMTAB    2
#define SERAPH_SHT_STRTAB    3
#define SERAPH_SHT_RELA      4
#define SERAPH_SHT_NOBITS    8
#define SERAPH_SHT_NOTE      7

/* SERAPH custom section types (using OS-specific range) */
#define SERAPH_SHT_PROOFS    0x60000001   /* .seraph.proofs */
#define SERAPH_SHT_EFFECTS   0x60000002   /* .seraph.effects */
#define SERAPH_SHT_CAPS      0x60000003   /* .seraph.caps */
#define SERAPH_SHT_MANIFEST  0x60000004   /* .seraph.manifest */

/* Section Flags */
#define SERAPH_SHF_WRITE     (1 << 0)
#define SERAPH_SHF_ALLOC     (1 << 1)
#define SERAPH_SHF_EXEC      (1 << 2)

/* Program Header Types */
#define SERAPH_PT_NULL       0
#define SERAPH_PT_LOAD       1
#define SERAPH_PT_NOTE       4
#define SERAPH_PT_PHDR       6

/* SERAPH custom program header type */
#define SERAPH_PT_SERAPH     0x60000000   /* SERAPH metadata segment */

/* Program Header Flags */
#define SERAPH_PF_X          1
#define SERAPH_PF_W          2
#define SERAPH_PF_R          4

/*============================================================================
 * ELF64 Structures
 *============================================================================*/

/**
 * @brief ELF64 File Header
 */
typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Seraph_Elf64_Ehdr;

/**
 * @brief ELF64 Section Header
 */
typedef struct __attribute__((packed)) {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Seraph_Elf64_Shdr;

/**
 * @brief ELF64 Program Header
 */
typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Seraph_Elf64_Phdr;

/**
 * @brief ELF64 Symbol
 */
typedef struct __attribute__((packed)) {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Seraph_Elf64_Sym;

/**
 * @brief ELF64 Relocation with Addend
 */
typedef struct __attribute__((packed)) {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Seraph_Elf64_Rela;

/*============================================================================
 * SERAPH Extension Structures
 *============================================================================*/

/**
 * @brief SERAPH Manifest Header
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;              /**< SERAPH_MANIFEST_MAGIC */
    uint32_t version;            /**< Manifest format version */
    uint32_t kernel_min_version; /**< Minimum kernel version required */
    uint32_t flags;              /**< Sovereign creation flags */

    uint64_t entry_point;        /**< Entry point virtual address */
    uint64_t stack_size;         /**< Required stack size */
    uint64_t heap_size;          /**< Required heap size */
    uint64_t chronon_budget;     /**< Initial Chronon budget */

    uint32_t atlas_region_count; /**< Number of Atlas regions needed */
    uint32_t aether_node_count;  /**< Number of Aether nodes */
    uint32_t cap_template_count; /**< Number of capability templates */

    uint8_t  proof_merkle_root[32]; /**< Merkle root of all proofs */
} Seraph_Manifest;

#define SERAPH_MANIFEST_MAGIC    0x5345524D   /* "SERM" */
#define SERAPH_MANIFEST_VERSION  1

/**
 * @brief SERAPH Effect Declaration
 */
typedef struct __attribute__((packed)) {
    uint32_t function_id;        /**< Function index */
    uint32_t declared_effects;   /**< Effects declared by programmer */
    uint32_t verified_effects;   /**< Effects verified by compiler */
    uint32_t required_caps;      /**< Required capability permissions */
} Seraph_Effect_Decl;

/**
 * @brief SERAPH Capability Template
 */
typedef struct __attribute__((packed)) {
    uint64_t base;               /**< Base address (or offset) */
    uint64_t length;             /**< Region length */
    uint32_t permissions;        /**< Permission flags */
    uint32_t flags;              /**< Additional flags */
} Seraph_Cap_Template;

/*============================================================================
 * ELF Writer Context
 *============================================================================*/

/**
 * @brief Section being built
 */
typedef struct {
    const char* name;
    uint32_t    type;
    uint64_t    flags;
    uint8_t*    data;
    size_t      size;
    size_t      capacity;
    uint64_t    addr;            /**< Virtual address (0 if not loaded) */
    uint64_t    align;
    uint32_t    link;
    uint32_t    info;
    uint64_t    entsize;
} Seraph_Elf_Section;

/**
 * @brief Symbol being built
 */
typedef struct {
    const char* name;
    uint64_t    value;
    uint64_t    size;
    uint8_t     type;            /**< STT_* */
    uint8_t     bind;            /**< STB_* */
    uint16_t    section_index;
} Seraph_Elf_Symbol;

/**
 * @brief Relocation entry
 */
typedef struct {
    uint64_t offset;
    uint32_t type;
    uint32_t symbol_index;
    int64_t  addend;
} Seraph_Elf_Reloc;

/**
 * @brief ELF64 Writer Context
 */
typedef struct {
    /* Sections */
    Seraph_Elf_Section* sections;
    size_t              section_count;
    size_t              section_capacity;

    /* Symbols */
    Seraph_Elf_Symbol*  symbols;
    size_t              symbol_count;
    size_t              symbol_capacity;

    /* Relocations */
    Seraph_Elf_Reloc*   relocs;
    size_t              reloc_count;
    size_t              reloc_capacity;

    /* String tables */
    char*               shstrtab;       /**< Section name string table */
    size_t              shstrtab_size;
    size_t              shstrtab_capacity;

    char*               strtab;         /**< Symbol string table */
    size_t              strtab_size;
    size_t              strtab_capacity;

    /* Configuration */
    uint64_t            base_addr;      /**< Base load address */
    uint64_t            entry_point;    /**< Entry point address */
    int                 is_pie;         /**< Position-independent executable? */

    /* SERAPH extensions */
    Seraph_Manifest     manifest;
    Seraph_Effect_Decl* effects;
    size_t              effect_count;
    Seraph_Cap_Template* caps;
    size_t              cap_count;
    const Seraph_Proof_Table* proofs;

    /* Target architecture */
    uint16_t            machine_type;     /**< ELF machine type */

    /* Code buffer (from backend) */
    uint8_t*            code;
    size_t              code_size;

    /* Data sections */
    uint8_t*            rodata;
    size_t              rodata_size;
    uint8_t*            data;
    size_t              data_size;
    size_t              bss_size;
} Seraph_Elf_Writer;

/*============================================================================
 * Writer Creation and Management
 *============================================================================*/

/**
 * @brief Initialize ELF writer
 *
 * @param writer Writer to initialize
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_elf_writer_init(Seraph_Elf_Writer* writer);

/**
 * @brief Free ELF writer resources
 *
 * @param writer Writer to free
 */
void seraph_elf_writer_free(Seraph_Elf_Writer* writer);

/**
 * @brief Set base load address
 *
 * @param writer The writer
 * @param addr Base address (typically 0x400000 for executables)
 */
void seraph_elf_writer_set_base(Seraph_Elf_Writer* writer, uint64_t addr);

/**
 * @brief Set entry point
 *
 * @param writer The writer
 * @param entry Entry point virtual address
 */
void seraph_elf_writer_set_entry(Seraph_Elf_Writer* writer, uint64_t entry);

/*============================================================================
 * Section Management
 *============================================================================*/

/**
 * @brief Create a new section
 *
 * @param writer The writer
 * @param name Section name
 * @param type Section type (SHT_*)
 * @param flags Section flags (SHF_*)
 * @return Section index, or SIZE_MAX on error
 */
size_t seraph_elf_section_create(Seraph_Elf_Writer* writer,
                                  const char* name,
                                  uint32_t type,
                                  uint64_t flags);

/**
 * @brief Set section data
 *
 * @param writer The writer
 * @param section_index Section to modify
 * @param data Data to set
 * @param size Data size
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_elf_section_set_data(Seraph_Elf_Writer* writer,
                                         size_t section_index,
                                         const void* data,
                                         size_t size);

/**
 * @brief Append data to section
 *
 * @param writer The writer
 * @param section_index Section to modify
 * @param data Data to append
 * @param size Data size
 * @return Offset where data was placed
 */
size_t seraph_elf_section_append(Seraph_Elf_Writer* writer,
                                  size_t section_index,
                                  const void* data,
                                  size_t size);

/*============================================================================
 * Symbol Management
 *============================================================================*/

/**
 * @brief Add a symbol
 *
 * @param writer The writer
 * @param name Symbol name
 * @param value Symbol value (address)
 * @param size Symbol size
 * @param type Symbol type (function, object, etc.)
 * @param bind Symbol binding (local, global, weak)
 * @param section_index Section containing symbol
 * @return Symbol index, or SIZE_MAX on error
 */
size_t seraph_elf_symbol_add(Seraph_Elf_Writer* writer,
                              const char* name,
                              uint64_t value,
                              uint64_t size,
                              uint8_t type,
                              uint8_t bind,
                              uint16_t section_index);

/*============================================================================
 * Relocation Management
 *============================================================================*/

/**
 * @brief Add a relocation
 *
 * @param writer The writer
 * @param offset Offset in section to relocate
 * @param type Relocation type
 * @param symbol_index Symbol being referenced
 * @param addend Addend value
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_elf_reloc_add(Seraph_Elf_Writer* writer,
                                  uint64_t offset,
                                  uint32_t type,
                                  uint32_t symbol_index,
                                  int64_t addend);

/*============================================================================
 * SERAPH Extension Functions
 *============================================================================*/

/**
 * @brief Set SERAPH manifest
 *
 * @param writer The writer
 * @param manifest Manifest data
 */
void seraph_elf_set_manifest(Seraph_Elf_Writer* writer,
                              const Seraph_Manifest* manifest);

/**
 * @brief Add effect declaration
 *
 * @param writer The writer
 * @param decl Effect declaration
 */
void seraph_elf_add_effect(Seraph_Elf_Writer* writer,
                            const Seraph_Effect_Decl* decl);

/**
 * @brief Add capability template
 *
 * @param writer The writer
 * @param cap Capability template
 */
void seraph_elf_add_cap_template(Seraph_Elf_Writer* writer,
                                  const Seraph_Cap_Template* cap);

/**
 * @brief Set proof table for embedding
 *
 * @param writer The writer
 * @param proofs Proof table
 */
void seraph_elf_set_proofs(Seraph_Elf_Writer* writer,
                            const Seraph_Proof_Table* proofs);

/*============================================================================
 * Code and Data
 *============================================================================*/

/**
 * @brief Set code (.text section)
 *
 * @param writer The writer
 * @param code Code bytes
 * @param size Code size
 */
void seraph_elf_set_code(Seraph_Elf_Writer* writer,
                          const uint8_t* code,
                          size_t size);

/**
 * @brief Set read-only data (.rodata section)
 *
 * @param writer The writer
 * @param rodata Read-only data
 * @param size Data size
 */
void seraph_elf_set_rodata(Seraph_Elf_Writer* writer,
                            const uint8_t* rodata,
                            size_t size);

/**
 * @brief Set initialized data (.data section)
 *
 * @param writer The writer
 * @param data Initialized data
 * @param size Data size
 */
void seraph_elf_set_data(Seraph_Elf_Writer* writer,
                          const uint8_t* data,
                          size_t size);

/**
 * @brief Set BSS size (.bss section)
 *
 * @param writer The writer
 * @param size BSS size
 */
void seraph_elf_set_bss(Seraph_Elf_Writer* writer, size_t size);

/*============================================================================
 * Output Generation
 *============================================================================*/

/**
 * @brief Write ELF to file
 *
 * @param writer The writer
 * @param filename Output filename
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_elf_write_file(Seraph_Elf_Writer* writer,
                                   const char* filename);

/**
 * @brief Write ELF to buffer
 *
 * @param writer The writer
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param written_out Output: bytes written
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_elf_write_buffer(Seraph_Elf_Writer* writer,
                                     uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t* written_out);

/**
 * @brief Calculate total ELF size
 *
 * @param writer The writer
 * @return Total size needed for ELF file
 */
size_t seraph_elf_calculate_size(Seraph_Elf_Writer* writer);

/*============================================================================
 * High-Level API
 *============================================================================*/

/**
 * @brief Generate ELF from Celestial IR module (x64 default)
 *
 * Performs full compilation pipeline:
 * 1. Generate x64 machine code from Celestial IR
 * 2. Build ELF sections
 * 3. Add SERAPH extensions
 * 4. Write output
 *
 * @param module Celestial IR module
 * @param proofs Proof table
 * @param filename Output filename
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_elf_from_celestial(Celestial_Module* module,
                                       const Seraph_Proof_Table* proofs,
                                       const char* filename);

/**
 * @brief Generate ELF from Celestial IR module with target selection
 *
 * Performs full compilation pipeline for the specified architecture:
 * 1. Generate native machine code from Celestial IR
 * 2. Build ELF sections with correct machine type
 * 3. Add SERAPH extensions
 * 4. Write output
 *
 * @param module Celestial IR module
 * @param proofs Proof table
 * @param target Target architecture
 * @param filename Output filename
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_elf_from_celestial_target(Celestial_Module* module,
                                              const Seraph_Proof_Table* proofs,
                                              Seraph_Elf_Target target,
                                              const char* filename);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SERAPHIM_ELF64_WRITER_H */
