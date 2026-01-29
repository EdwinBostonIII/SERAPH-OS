/**
 * @file elf64_writer.c
 * @brief MC29: ELF64 Writer Implementation
 *
 * Generates ELF64 executables with SERAPH extensions.
 * This is the final stage of SERAPH's native compiler pipeline.
 *
 * The ELF writer produces binaries that are:
 * - Self-describing (embedded proofs and manifests)
 * - Verifiable (Merkle roots for all proofs)
 * - Secure (capability templates define access)
 *
 * Philosophy: The binary IS the specification.
 */

#include "seraph/seraphim/elf64_writer.h"
#include "seraph/seraphim/celestial_to_x64.h"
#include "seraph/seraphim/celestial_to_arm64.h"
#include "seraph/seraphim/celestial_to_riscv.h"
#include "seraph/seraphim/x64_encode.h"
#include "seraph/seraphim/arm64_encode.h"
#include "seraph/seraphim/riscv_encode.h"
#include "seraph/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Symbol Binding and Type Constants
 *============================================================================*/

#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

#define ELF64_ST_INFO(bind, type) (((bind) << 4) | ((type) & 0xF))

/*============================================================================
 * Relocation Types (x86-64)
 *============================================================================*/

#define R_X86_64_NONE      0
#define R_X86_64_64        1   /* Direct 64-bit */
#define R_X86_64_PC32      2   /* PC-relative 32-bit */
#define R_X86_64_PLT32     4   /* PLT-relative 32-bit */
#define R_X86_64_32        10  /* Direct 32-bit zero-extended */
#define R_X86_64_32S       11  /* Direct 32-bit sign-extended */

/*============================================================================
 * Writer Initialization
 *============================================================================*/

Seraph_Vbit seraph_elf_writer_init(Seraph_Elf_Writer* writer) {
    if (writer == NULL) return SERAPH_VBIT_FALSE;

    memset(writer, 0, sizeof(Seraph_Elf_Writer));

    /* Default base address for kernel (high address) */
    writer->base_addr = 0xFFFF800000100000ULL;  /* Kernel space */

    /* Initialize section name string table */
    writer->shstrtab_capacity = 256;
    writer->shstrtab = (char*)malloc(writer->shstrtab_capacity);
    if (writer->shstrtab == NULL) return SERAPH_VBIT_FALSE;
    writer->shstrtab[0] = '\0';  /* First byte is null */
    writer->shstrtab_size = 1;

    /* Initialize symbol string table */
    writer->strtab_capacity = 256;
    writer->strtab = (char*)malloc(writer->strtab_capacity);
    if (writer->strtab == NULL) {
        free(writer->shstrtab);
        return SERAPH_VBIT_FALSE;
    }
    writer->strtab[0] = '\0';
    writer->strtab_size = 1;

    /* Initialize manifest */
    writer->manifest.magic = SERAPH_MANIFEST_MAGIC;
    writer->manifest.version = SERAPH_MANIFEST_VERSION;

    /* Default to x86_64 */
    writer->machine_type = SERAPH_EM_X86_64;

    return SERAPH_VBIT_TRUE;
}

void seraph_elf_writer_free(Seraph_Elf_Writer* writer) {
    if (writer == NULL) return;

    /* Free sections */
    if (writer->sections != NULL) {
        for (size_t i = 0; i < writer->section_count; i++) {
            if (writer->sections[i].data != NULL) {
                free(writer->sections[i].data);
            }
        }
        free(writer->sections);
    }

    /* Free symbols */
    if (writer->symbols != NULL) {
        free(writer->symbols);
    }

    /* Free relocations */
    if (writer->relocs != NULL) {
        free(writer->relocs);
    }

    /* Free string tables */
    if (writer->shstrtab != NULL) {
        free(writer->shstrtab);
    }
    if (writer->strtab != NULL) {
        free(writer->strtab);
    }

    /* Free SERAPH extensions */
    if (writer->effects != NULL) {
        free(writer->effects);
    }
    if (writer->caps != NULL) {
        free(writer->caps);
    }

    /* Free code/data (if owned) */
    if (writer->code != NULL) {
        free(writer->code);
    }
    if (writer->rodata != NULL) {
        free(writer->rodata);
    }
    if (writer->data != NULL) {
        free(writer->data);
    }

    memset(writer, 0, sizeof(Seraph_Elf_Writer));
}

void seraph_elf_writer_set_base(Seraph_Elf_Writer* writer, uint64_t addr) {
    if (writer != NULL) {
        writer->base_addr = addr;
    }
}

void seraph_elf_writer_set_entry(Seraph_Elf_Writer* writer, uint64_t entry) {
    if (writer != NULL) {
        writer->entry_point = entry;
        writer->manifest.entry_point = entry;
    }
}

/*============================================================================
 * String Table Helpers
 *============================================================================*/

static size_t add_to_strtab(char** strtab, size_t* size, size_t* capacity,
                            const char* str) {
    if (str == NULL) return 0;

    size_t len = strlen(str) + 1;  /* Include null terminator */
    size_t offset = *size;

    /* Grow if needed */
    while (*size + len > *capacity) {
        size_t new_capacity = *capacity * 2;
        char* new_strtab = (char*)realloc(*strtab, new_capacity);
        if (new_strtab == NULL) return 0;
        *strtab = new_strtab;
        *capacity = new_capacity;
    }

    memcpy(*strtab + *size, str, len);
    *size += len;

    return offset;
}

/*============================================================================
 * Section Management
 *============================================================================*/

size_t seraph_elf_section_create(Seraph_Elf_Writer* writer,
                                  const char* name,
                                  uint32_t type,
                                  uint64_t flags) {
    if (writer == NULL) return SIZE_MAX;

    /* Grow section array if needed */
    if (writer->section_count >= writer->section_capacity) {
        size_t new_capacity = writer->section_capacity == 0 ? 16 :
                              writer->section_capacity * 2;
        Seraph_Elf_Section* new_sections = (Seraph_Elf_Section*)realloc(
            writer->sections, new_capacity * sizeof(Seraph_Elf_Section));
        if (new_sections == NULL) return SIZE_MAX;
        writer->sections = new_sections;
        writer->section_capacity = new_capacity;
    }

    size_t index = writer->section_count++;
    Seraph_Elf_Section* section = &writer->sections[index];

    memset(section, 0, sizeof(Seraph_Elf_Section));
    section->name = name;
    section->type = type;
    section->flags = flags;
    section->align = 1;

    /* Default alignment based on type */
    if (flags & SERAPH_SHF_EXEC) {
        section->align = 16;  /* Code alignment */
    } else if (flags & SERAPH_SHF_ALLOC) {
        section->align = 8;   /* Data alignment */
    }

    return index;
}

Seraph_Vbit seraph_elf_section_set_data(Seraph_Elf_Writer* writer,
                                         size_t section_index,
                                         const void* data,
                                         size_t size) {
    if (writer == NULL || section_index >= writer->section_count) {
        return SERAPH_VBIT_FALSE;
    }

    Seraph_Elf_Section* section = &writer->sections[section_index];

    /* Free existing data */
    if (section->data != NULL) {
        free(section->data);
    }

    /* Allocate and copy */
    section->data = (uint8_t*)malloc(size);
    if (section->data == NULL && size > 0) {
        return SERAPH_VBIT_FALSE;
    }

    if (data != NULL && size > 0) {
        memcpy(section->data, data, size);
    }

    section->size = size;
    section->capacity = size;

    return SERAPH_VBIT_TRUE;
}

size_t seraph_elf_section_append(Seraph_Elf_Writer* writer,
                                  size_t section_index,
                                  const void* data,
                                  size_t size) {
    if (writer == NULL || section_index >= writer->section_count) {
        return SIZE_MAX;
    }

    Seraph_Elf_Section* section = &writer->sections[section_index];
    size_t offset = section->size;

    /* Grow if needed */
    if (section->size + size > section->capacity) {
        size_t new_capacity = section->capacity == 0 ? 256 :
                              section->capacity * 2;
        while (new_capacity < section->size + size) {
            new_capacity *= 2;
        }

        uint8_t* new_data = (uint8_t*)realloc(section->data, new_capacity);
        if (new_data == NULL) return SIZE_MAX;
        section->data = new_data;
        section->capacity = new_capacity;
    }

    if (data != NULL && size > 0) {
        memcpy(section->data + section->size, data, size);
    }
    section->size += size;

    return offset;
}

/*============================================================================
 * Symbol Management
 *============================================================================*/

size_t seraph_elf_symbol_add(Seraph_Elf_Writer* writer,
                              const char* name,
                              uint64_t value,
                              uint64_t size,
                              uint8_t type,
                              uint8_t bind,
                              uint16_t section_index) {
    if (writer == NULL) return SIZE_MAX;

    /* Grow symbol array if needed */
    if (writer->symbol_count >= writer->symbol_capacity) {
        size_t new_capacity = writer->symbol_capacity == 0 ? 64 :
                              writer->symbol_capacity * 2;
        Seraph_Elf_Symbol* new_symbols = (Seraph_Elf_Symbol*)realloc(
            writer->symbols, new_capacity * sizeof(Seraph_Elf_Symbol));
        if (new_symbols == NULL) return SIZE_MAX;
        writer->symbols = new_symbols;
        writer->symbol_capacity = new_capacity;
    }

    size_t index = writer->symbol_count++;
    Seraph_Elf_Symbol* sym = &writer->symbols[index];

    sym->name = name;
    sym->value = value;
    sym->size = size;
    sym->type = type;
    sym->bind = bind;
    sym->section_index = section_index;

    return index;
}

/*============================================================================
 * Relocation Management
 *============================================================================*/

Seraph_Vbit seraph_elf_reloc_add(Seraph_Elf_Writer* writer,
                                  uint64_t offset,
                                  uint32_t type,
                                  uint32_t symbol_index,
                                  int64_t addend) {
    if (writer == NULL) return SERAPH_VBIT_FALSE;

    /* Grow relocation array if needed */
    if (writer->reloc_count >= writer->reloc_capacity) {
        size_t new_capacity = writer->reloc_capacity == 0 ? 64 :
                              writer->reloc_capacity * 2;
        Seraph_Elf_Reloc* new_relocs = (Seraph_Elf_Reloc*)realloc(
            writer->relocs, new_capacity * sizeof(Seraph_Elf_Reloc));
        if (new_relocs == NULL) return SERAPH_VBIT_FALSE;
        writer->relocs = new_relocs;
        writer->reloc_capacity = new_capacity;
    }

    Seraph_Elf_Reloc* reloc = &writer->relocs[writer->reloc_count++];
    reloc->offset = offset;
    reloc->type = type;
    reloc->symbol_index = symbol_index;
    reloc->addend = addend;

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * SERAPH Extensions
 *============================================================================*/

void seraph_elf_set_manifest(Seraph_Elf_Writer* writer,
                              const Seraph_Manifest* manifest) {
    if (writer != NULL && manifest != NULL) {
        memcpy(&writer->manifest, manifest, sizeof(Seraph_Manifest));
    }
}

void seraph_elf_add_effect(Seraph_Elf_Writer* writer,
                            const Seraph_Effect_Decl* decl) {
    if (writer == NULL || decl == NULL) return;

    /* Grow array if needed */
    size_t new_count = writer->effect_count + 1;
    Seraph_Effect_Decl* new_effects = (Seraph_Effect_Decl*)realloc(
        writer->effects, new_count * sizeof(Seraph_Effect_Decl));
    if (new_effects == NULL) return;

    writer->effects = new_effects;
    memcpy(&writer->effects[writer->effect_count], decl, sizeof(Seraph_Effect_Decl));
    writer->effect_count = new_count;
}

void seraph_elf_add_cap_template(Seraph_Elf_Writer* writer,
                                  const Seraph_Cap_Template* cap) {
    if (writer == NULL || cap == NULL) return;

    /* Grow array if needed */
    size_t new_count = writer->cap_count + 1;
    Seraph_Cap_Template* new_caps = (Seraph_Cap_Template*)realloc(
        writer->caps, new_count * sizeof(Seraph_Cap_Template));
    if (new_caps == NULL) return;

    writer->caps = new_caps;
    memcpy(&writer->caps[writer->cap_count], cap, sizeof(Seraph_Cap_Template));
    writer->cap_count = new_count;
}

void seraph_elf_set_proofs(Seraph_Elf_Writer* writer,
                            const Seraph_Proof_Table* proofs) {
    if (writer != NULL) {
        writer->proofs = proofs;
    }
}

/*============================================================================
 * Code and Data
 *============================================================================*/

void seraph_elf_set_code(Seraph_Elf_Writer* writer,
                          const uint8_t* code,
                          size_t size) {
    if (writer == NULL) return;

    if (writer->code != NULL) {
        free(writer->code);
    }

    writer->code = (uint8_t*)malloc(size);
    if (writer->code != NULL && code != NULL) {
        memcpy(writer->code, code, size);
    }
    writer->code_size = size;
}

void seraph_elf_set_rodata(Seraph_Elf_Writer* writer,
                            const uint8_t* rodata,
                            size_t size) {
    if (writer == NULL) return;

    if (writer->rodata != NULL) {
        free(writer->rodata);
    }

    writer->rodata = (uint8_t*)malloc(size);
    if (writer->rodata != NULL && rodata != NULL) {
        memcpy(writer->rodata, rodata, size);
    }
    writer->rodata_size = size;
}

void seraph_elf_set_data(Seraph_Elf_Writer* writer,
                          const uint8_t* data,
                          size_t size) {
    if (writer == NULL) return;

    if (writer->data != NULL) {
        free(writer->data);
    }

    writer->data = (uint8_t*)malloc(size);
    if (writer->data != NULL && data != NULL) {
        memcpy(writer->data, data, size);
    }
    writer->data_size = size;
}

void seraph_elf_set_bss(Seraph_Elf_Writer* writer, size_t size) {
    if (writer != NULL) {
        writer->bss_size = size;
    }
}

/*============================================================================
 * ELF Generation
 *============================================================================*/

/**
 * @brief Calculate aligned offset
 */
static size_t align_up(size_t value, size_t align) {
    if (align == 0) return value;
    return (value + align - 1) & ~(align - 1);
}

/**
 * @brief Build section headers and calculate layout
 */
static void build_layout(Seraph_Elf_Writer* writer,
                         uint64_t* text_addr,
                         uint64_t* rodata_addr,
                         uint64_t* data_addr,
                         uint64_t* bss_addr,
                         size_t* file_size) {
    uint64_t vaddr = writer->base_addr;
    size_t offset = sizeof(Seraph_Elf64_Ehdr);

    /* Program headers come first */
    size_t phdr_count = 3;  /* PHDR, LOAD (text+rodata), LOAD (data+bss) */
    if (writer->manifest.magic == SERAPH_MANIFEST_MAGIC) {
        phdr_count++;  /* SERAPH segment */
    }
    offset += phdr_count * sizeof(Seraph_Elf64_Phdr);
    offset = align_up(offset, 16);

    /* .text - starts after headers, so add offset to base address */
    *text_addr = writer->base_addr + offset;  /* Actual load address accounts for headers */
    size_t text_offset = offset;
    (void)text_offset;  /* Used in file layout */
    offset += writer->code_size;
    vaddr = align_up(writer->base_addr + offset, 4096);  /* Next segment boundary */

    /* .rodata */
    offset = align_up(offset, 16);
    *rodata_addr = vaddr;
    size_t rodata_offset = offset;
    (void)rodata_offset;  /* Used in file layout */
    offset += writer->rodata_size;
    vaddr += align_up(writer->rodata_size, 4096);

    /* .data */
    offset = align_up(offset, 16);
    *data_addr = vaddr;
    size_t data_offset = offset;
    (void)data_offset;  /* Used in file layout */
    offset += writer->data_size;

    /* .bss (no file data, just address) */
    *bss_addr = vaddr + writer->data_size;

    /* SERAPH sections (not loaded, just metadata) */
    offset = align_up(offset, 8);

    /* Section headers at end */
    offset = align_up(offset, 8);
    *file_size = offset;
}

size_t seraph_elf_calculate_size(Seraph_Elf_Writer* writer) {
    if (writer == NULL) return 0;

    uint64_t text_addr, rodata_addr, data_addr, bss_addr;
    size_t file_size;
    build_layout(writer, &text_addr, &rodata_addr, &data_addr, &bss_addr, &file_size);

    /* Add section headers and string tables */
    size_t shdr_count = 10;  /* null, .text, .rodata, .data, .bss, .shstrtab,
                                .seraph.manifest, .seraph.proofs, .seraph.effects, .seraph.caps */
    file_size += shdr_count * sizeof(Seraph_Elf64_Shdr);
    file_size += writer->shstrtab_size + 256;  /* Extra for section names */

    /* SERAPH sections */
    file_size += sizeof(Seraph_Manifest);
    file_size += writer->effect_count * sizeof(Seraph_Effect_Decl);
    file_size += writer->cap_count * sizeof(Seraph_Cap_Template);

    return file_size;
}

Seraph_Vbit seraph_elf_write_file(Seraph_Elf_Writer* writer,
                                   const char* filename) {
    if (writer == NULL || filename == NULL) return SERAPH_VBIT_FALSE;

    FILE* f = fopen(filename, "wb");
    if (f == NULL) return SERAPH_VBIT_FALSE;

    /* Calculate layout */
    uint64_t text_addr, rodata_addr, data_addr, bss_addr;
    size_t file_size_estimate;
    build_layout(writer, &text_addr, &rodata_addr, &data_addr, &bss_addr,
                 &file_size_estimate);

    /* Build section name string table */
    size_t shstrtab_null = 0;  /* Index 0 is always null in ELF strtab */
    (void)shstrtab_null;
    size_t shstrtab_text = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                          &writer->shstrtab_capacity, ".text");
    size_t shstrtab_rodata = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                            &writer->shstrtab_capacity, ".rodata");
    size_t shstrtab_data = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                          &writer->shstrtab_capacity, ".data");
    size_t shstrtab_bss = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                         &writer->shstrtab_capacity, ".bss");
    size_t shstrtab_shstrtab = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                              &writer->shstrtab_capacity, ".shstrtab");
    size_t shstrtab_manifest = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                              &writer->shstrtab_capacity, ".seraph.manifest");
    size_t shstrtab_proofs = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                            &writer->shstrtab_capacity, ".seraph.proofs");
    size_t shstrtab_effects = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                             &writer->shstrtab_capacity, ".seraph.effects");
    size_t shstrtab_caps = add_to_strtab(&writer->shstrtab, &writer->shstrtab_size,
                                          &writer->shstrtab_capacity, ".seraph.caps");

    /* File layout:
     * - ELF header
     * - Program headers
     * - .text
     * - .rodata
     * - .data
     * - .seraph.manifest
     * - .seraph.proofs (empty placeholder)
     * - .seraph.effects
     * - .seraph.caps
     * - .shstrtab
     * - Section headers
     */

    size_t offset = sizeof(Seraph_Elf64_Ehdr);
    size_t phdr_count = 4;  /* PHDR, LOAD x2, SERAPH */
    size_t phdr_offset = offset;
    offset += phdr_count * sizeof(Seraph_Elf64_Phdr);
    offset = align_up(offset, 16);

    size_t text_offset = offset;
    offset += writer->code_size;
    offset = align_up(offset, 16);

    size_t rodata_offset = offset;
    offset += writer->rodata_size;
    offset = align_up(offset, 16);

    size_t data_offset = offset;
    offset += writer->data_size;
    offset = align_up(offset, 8);

    size_t manifest_offset = offset;
    offset += sizeof(Seraph_Manifest);
    offset = align_up(offset, 8);

    size_t proofs_offset = offset;
    size_t proofs_size = 0;  /* Placeholder */
    offset += proofs_size;

    size_t effects_offset = offset;
    size_t effects_size = writer->effect_count * sizeof(Seraph_Effect_Decl);
    offset += effects_size;

    size_t caps_offset = offset;
    size_t caps_size = writer->cap_count * sizeof(Seraph_Cap_Template);
    offset += caps_size;
    offset = align_up(offset, 8);

    size_t shstrtab_offset = offset;
    offset += writer->shstrtab_size;
    offset = align_up(offset, 8);

    size_t shdr_offset = offset;
    size_t shdr_count = 10;

    /* === Write ELF Header === */
    Seraph_Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));

    ehdr.e_ident[0] = 0x7F;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = SERAPH_ELFCLASS64;
    ehdr.e_ident[5] = SERAPH_ELFDATA2LSB;
    ehdr.e_ident[6] = SERAPH_EV_CURRENT;
    ehdr.e_ident[7] = 0;  /* ELFOSABI_NONE */

    ehdr.e_type = SERAPH_ET_EXEC;
    ehdr.e_machine = writer->machine_type;
    ehdr.e_version = SERAPH_EV_CURRENT;
    /* Entry point is base_addr + text_offset (code starts after headers) */
    ehdr.e_entry = writer->base_addr + text_offset;
    ehdr.e_phoff = phdr_offset;
    ehdr.e_shoff = shdr_offset;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Seraph_Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Seraph_Elf64_Phdr);
    ehdr.e_phnum = phdr_count;
    ehdr.e_shentsize = sizeof(Seraph_Elf64_Shdr);
    ehdr.e_shnum = shdr_count;
    ehdr.e_shstrndx = 5;  /* .shstrtab section index */

    fwrite(&ehdr, sizeof(ehdr), 1, f);

    /* === Write Program Headers === */
    Seraph_Elf64_Phdr phdr;

    /* PHDR segment */
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = SERAPH_PT_PHDR;
    phdr.p_flags = SERAPH_PF_R;
    phdr.p_offset = phdr_offset;
    phdr.p_vaddr = writer->base_addr + phdr_offset;
    phdr.p_paddr = phdr.p_vaddr;
    phdr.p_filesz = phdr_count * sizeof(Seraph_Elf64_Phdr);
    phdr.p_memsz = phdr.p_filesz;
    phdr.p_align = 8;
    fwrite(&phdr, sizeof(phdr), 1, f);

    /* LOAD segment (text + rodata) */
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = SERAPH_PT_LOAD;
    phdr.p_flags = SERAPH_PF_R | SERAPH_PF_X;
    phdr.p_offset = 0;
    phdr.p_vaddr = writer->base_addr;
    phdr.p_paddr = phdr.p_vaddr;
    phdr.p_filesz = rodata_offset + writer->rodata_size;
    phdr.p_memsz = phdr.p_filesz;
    phdr.p_align = 0x1000;
    fwrite(&phdr, sizeof(phdr), 1, f);

    /* LOAD segment (data + bss) */
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = SERAPH_PT_LOAD;
    phdr.p_flags = SERAPH_PF_R | SERAPH_PF_W;
    phdr.p_offset = data_offset;
    phdr.p_vaddr = data_addr;
    phdr.p_paddr = phdr.p_vaddr;
    phdr.p_filesz = writer->data_size;
    phdr.p_memsz = writer->data_size + writer->bss_size;
    phdr.p_align = 0x1000;
    fwrite(&phdr, sizeof(phdr), 1, f);

    /* SERAPH segment (metadata) */
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = SERAPH_PT_SERAPH;
    phdr.p_flags = SERAPH_PF_R;
    phdr.p_offset = manifest_offset;
    phdr.p_vaddr = 0;  /* Not loaded */
    phdr.p_paddr = 0;
    phdr.p_filesz = sizeof(Seraph_Manifest) + effects_size + caps_size;
    phdr.p_memsz = 0;
    phdr.p_align = 8;
    fwrite(&phdr, sizeof(phdr), 1, f);

    /* Pad to text section */
    size_t current = sizeof(ehdr) + phdr_count * sizeof(Seraph_Elf64_Phdr);
    while (current < text_offset) {
        uint8_t zero = 0;
        fwrite(&zero, 1, 1, f);
        current++;
    }

    /* === Write .text === */
    if (writer->code != NULL && writer->code_size > 0) {
        fwrite(writer->code, writer->code_size, 1, f);
    }
    current = text_offset + writer->code_size;

    /* Pad to rodata */
    while (current < rodata_offset) {
        uint8_t zero = 0;
        fwrite(&zero, 1, 1, f);
        current++;
    }

    /* === Write .rodata === */
    if (writer->rodata != NULL && writer->rodata_size > 0) {
        fwrite(writer->rodata, writer->rodata_size, 1, f);
    }
    current = rodata_offset + writer->rodata_size;

    /* Pad to data */
    while (current < data_offset) {
        uint8_t zero = 0;
        fwrite(&zero, 1, 1, f);
        current++;
    }

    /* === Write .data === */
    if (writer->data != NULL && writer->data_size > 0) {
        fwrite(writer->data, writer->data_size, 1, f);
    }
    current = data_offset + writer->data_size;

    /* Pad to manifest */
    while (current < manifest_offset) {
        uint8_t zero = 0;
        fwrite(&zero, 1, 1, f);
        current++;
    }

    /* === Write .seraph.manifest === */
    fwrite(&writer->manifest, sizeof(Seraph_Manifest), 1, f);
    current = manifest_offset + sizeof(Seraph_Manifest);

    /* === Write .seraph.effects === */
    if (writer->effects != NULL && effects_size > 0) {
        fwrite(writer->effects, effects_size, 1, f);
    }
    current += effects_size;

    /* === Write .seraph.caps === */
    if (writer->caps != NULL && caps_size > 0) {
        fwrite(writer->caps, caps_size, 1, f);
    }
    current += caps_size;

    /* Pad to shstrtab */
    while (current < shstrtab_offset) {
        uint8_t zero = 0;
        fwrite(&zero, 1, 1, f);
        current++;
    }

    /* === Write .shstrtab === */
    fwrite(writer->shstrtab, writer->shstrtab_size, 1, f);
    current = shstrtab_offset + writer->shstrtab_size;

    /* Pad to section headers */
    while (current < shdr_offset) {
        uint8_t zero = 0;
        fwrite(&zero, 1, 1, f);
        current++;
    }

    /* === Write Section Headers === */
    Seraph_Elf64_Shdr shdr;

    /* [0] NULL section */
    memset(&shdr, 0, sizeof(shdr));
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [1] .text */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_text;
    shdr.sh_type = SERAPH_SHT_PROGBITS;
    shdr.sh_flags = SERAPH_SHF_ALLOC | SERAPH_SHF_EXEC;
    shdr.sh_addr = text_addr;
    shdr.sh_offset = text_offset;
    shdr.sh_size = writer->code_size;
    shdr.sh_addralign = 16;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [2] .rodata */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_rodata;
    shdr.sh_type = SERAPH_SHT_PROGBITS;
    shdr.sh_flags = SERAPH_SHF_ALLOC;
    shdr.sh_addr = rodata_addr;
    shdr.sh_offset = rodata_offset;
    shdr.sh_size = writer->rodata_size;
    shdr.sh_addralign = 16;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [3] .data */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_data;
    shdr.sh_type = SERAPH_SHT_PROGBITS;
    shdr.sh_flags = SERAPH_SHF_ALLOC | SERAPH_SHF_WRITE;
    shdr.sh_addr = data_addr;
    shdr.sh_offset = data_offset;
    shdr.sh_size = writer->data_size;
    shdr.sh_addralign = 8;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [4] .bss */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_bss;
    shdr.sh_type = SERAPH_SHT_NOBITS;
    shdr.sh_flags = SERAPH_SHF_ALLOC | SERAPH_SHF_WRITE;
    shdr.sh_addr = bss_addr;
    shdr.sh_offset = 0;
    shdr.sh_size = writer->bss_size;
    shdr.sh_addralign = 8;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [5] .shstrtab */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_shstrtab;
    shdr.sh_type = SERAPH_SHT_STRTAB;
    shdr.sh_offset = shstrtab_offset;
    shdr.sh_size = writer->shstrtab_size;
    shdr.sh_addralign = 1;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [6] .seraph.manifest */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_manifest;
    shdr.sh_type = SERAPH_SHT_MANIFEST;
    shdr.sh_offset = manifest_offset;
    shdr.sh_size = sizeof(Seraph_Manifest);
    shdr.sh_addralign = 8;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [7] .seraph.proofs */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_proofs;
    shdr.sh_type = SERAPH_SHT_PROOFS;
    shdr.sh_offset = proofs_offset;
    shdr.sh_size = proofs_size;
    shdr.sh_addralign = 8;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [8] .seraph.effects */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_effects;
    shdr.sh_type = SERAPH_SHT_EFFECTS;
    shdr.sh_offset = effects_offset;
    shdr.sh_size = effects_size;
    shdr.sh_entsize = sizeof(Seraph_Effect_Decl);
    shdr.sh_addralign = 8;
    fwrite(&shdr, sizeof(shdr), 1, f);

    /* [9] .seraph.caps */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = (uint32_t)shstrtab_caps;
    shdr.sh_type = SERAPH_SHT_CAPS;
    shdr.sh_offset = caps_offset;
    shdr.sh_size = caps_size;
    shdr.sh_entsize = sizeof(Seraph_Cap_Template);
    shdr.sh_addralign = 8;
    fwrite(&shdr, sizeof(shdr), 1, f);

    fclose(f);
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_elf_write_buffer(Seraph_Elf_Writer* writer,
                                     uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t* written_out) {
    if (!writer || !buffer || buffer_size == 0) {
        if (written_out) *written_out = 0;
        return SERAPH_VBIT_VOID;
    }

    /* Calculate total size needed */
    size_t total_size = seraph_elf_calculate_size(writer);
    if (total_size > buffer_size) {
        if (written_out) *written_out = 0;
        return SERAPH_VBIT_FALSE;  /* Buffer too small */
    }

    /* Write to memory buffer instead of file */
    size_t offset = 0;

    /* Write ELF header */
    if (offset + sizeof(Elf64_Ehdr) > buffer_size) goto error;
    memcpy(buffer + offset, &writer->header, sizeof(Elf64_Ehdr));
    offset += sizeof(Elf64_Ehdr);

    /* Write program headers */
    size_t phdr_size = writer->phdr_count * sizeof(Elf64_Phdr);
    if (offset + phdr_size > buffer_size) goto error;
    memcpy(buffer + offset, writer->phdrs, phdr_size);
    offset += phdr_size;

    /* Write sections */
    for (size_t i = 0; i < writer->section_count; i++) {
        Seraph_Elf_Section* sec = &writer->sections[i];
        if (sec->data && sec->size > 0) {
            /* Align to section alignment */
            while (offset % sec->align != 0 && offset < buffer_size) {
                buffer[offset++] = 0;
            }
            if (offset + sec->size > buffer_size) goto error;
            memcpy(buffer + offset, sec->data, sec->size);
            offset += sec->size;
        }
    }

    /* Write section headers */
    size_t shdr_size = writer->section_count * sizeof(Elf64_Shdr);
    if (offset + shdr_size > buffer_size) goto error;
    memcpy(buffer + offset, writer->shdrs, shdr_size);
    offset += shdr_size;

    if (written_out) *written_out = offset;
    return SERAPH_VBIT_TRUE;

error:
    if (written_out) *written_out = 0;
    return SERAPH_VBIT_FALSE;
}

/*============================================================================
 * High-Level API
 *============================================================================*/

Seraph_Vbit seraph_elf_from_celestial(Celestial_Module* module,
                                       const Seraph_Proof_Table* proofs,
                                       const char* filename) {
    if (!module || !filename) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Vbit result = SERAPH_VBIT_FALSE;

    /* Create arena for temporary allocations
     * Need significant space for large modules:
     * - Each function needs ~130KB for block info (4096 blocks × 32 bytes)
     * - Plus live intervals, register allocator state, etc.
     * - 48 functions × 150KB ≈ 7MB, use 32MB to be safe */
    Seraph_Arena arena;
    if (!seraph_vbit_is_true(seraph_arena_create(&arena, 32 * 1024 * 1024, 0,
                                                  SERAPH_ARENA_FLAG_ZERO_ON_ALLOC))) {
        return SERAPH_VBIT_FALSE;
    }

    /* Initialize x64 code buffer */
    X64_Buffer code_buf;
    if (!seraph_vbit_is_true(x64_buf_init(&code_buf, 64 * 1024))) {
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    /* Compile Celestial IR to x86-64 machine code */
    result = celestial_compile_module(module, &code_buf, &arena);
    if (!seraph_vbit_is_true(result)) {
        x64_buf_free(&code_buf);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    /* Initialize ELF writer */
    Seraph_Elf_Writer writer;
    if (!seraph_vbit_is_true(seraph_elf_writer_init(&writer))) {
        x64_buf_free(&code_buf);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    /* Configure writer */
    seraph_elf_writer_set_base(&writer, 0x400000);  /* Standard executable base */

    /* Set the generated code */
    seraph_elf_set_code(&writer, code_buf.code, code_buf.size);

    /* Find entry point (_start or main) */
    uint64_t entry_point = 0x400000;  /* Text section starts at base */
    for (Celestial_Function* fn = module->functions; fn; fn = fn->next) {
        if (fn->name && (strcmp(fn->name, "_start") == 0 ||
                         strcmp(fn->name, "main") == 0)) {
            /* Entry point is at the start of the function
             * For proper calculation, we'd need function offsets from compilation
             * For now, use default
             */
            break;
        }
    }
    seraph_elf_writer_set_entry(&writer, entry_point);

    /* Set proofs if provided */
    if (proofs) {
        seraph_elf_set_proofs(&writer, proofs);
    }

    /* Build SERAPH manifest */
    Seraph_Manifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    manifest.magic = SERAPH_MANIFEST_MAGIC;
    manifest.version = SERAPH_MANIFEST_VERSION;
    manifest.kernel_min_version = 1;
    manifest.entry_point = entry_point;
    manifest.stack_size = 64 * 1024;      /* 64 KB stack */
    manifest.heap_size = 1024 * 1024;     /* 1 MB heap */
    manifest.chronon_budget = 1000000;    /* 1M time units */
    manifest.cap_template_count = 0;
    manifest.atlas_region_count = 0;
    manifest.aether_node_count = 0;
    /* Compute proof merkle root from proof table if available.
     * The merkle root is a hash of all proof entries, allowing
     * efficient verification that no proofs have been tampered with.
     * For now, use a simple hash of the proof data. */
    if (proofs && proofs->entries && proofs->count > 0) {
        /* Simple FNV-1a hash of proof data as placeholder for full merkle tree */
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (size_t i = 0; i < proofs->count; i++) {
            const uint8_t* data = (const uint8_t*)&proofs->entries[i];
            for (size_t j = 0; j < sizeof(proofs->entries[i]); j++) {
                hash ^= data[j];
                hash *= 0x100000001b3ULL;
            }
        }
        /* Store hash in merkle root (repeated to fill 32 bytes) */
        for (int i = 0; i < 4; i++) {
            memcpy(manifest.proof_merkle_root + i * 8, &hash, 8);
        }
    } else {
        memset(manifest.proof_merkle_root, 0, sizeof(manifest.proof_merkle_root));
    }

    seraph_elf_set_manifest(&writer, &manifest);

    /* Add effect declarations for each function */
    uint32_t func_id = 0;
    for (Celestial_Function* fn = module->functions; fn; fn = fn->next) {
        Seraph_Effect_Decl effect_decl;
        effect_decl.function_id = func_id++;
        effect_decl.declared_effects = fn->declared_effects;
        /* Verified effects should match declared effects after type checking.
         * The checker validates that function bodies don't exceed declared effects.
         * If we reach code generation, verification has already passed. */
        effect_decl.verified_effects = fn->declared_effects;

        /* Required capabilities are computed from effect analysis:
         * - PERSIST effect requires SERAPH_CAP_ATLAS
         * - NETWORK effect requires SERAPH_CAP_AETHER
         * - IO effect requires SERAPH_CAP_IO */
        effect_decl.required_caps = 0;
        if (fn->declared_effects & SERAPH_EFFECT_PERSIST) {
            effect_decl.required_caps |= SERAPH_CAP_PERSIST_BIT;
        }
        if (fn->declared_effects & SERAPH_EFFECT_NETWORK) {
            effect_decl.required_caps |= SERAPH_CAP_NETWORK_BIT;
        }
        if (fn->declared_effects & SERAPH_EFFECT_IO) {
            effect_decl.required_caps |= SERAPH_CAP_IO_BIT;
        }
        seraph_elf_add_effect(&writer, &effect_decl);
    }

    /* Write the ELF file */
    result = seraph_elf_write_file(&writer, filename);

    /* Cleanup */
    seraph_elf_writer_free(&writer);
    x64_buf_free(&code_buf);
    seraph_arena_destroy(&arena);

    return result;
}

Seraph_Vbit seraph_elf_from_celestial_target(Celestial_Module* module,
                                              const Seraph_Proof_Table* proofs,
                                              Seraph_Elf_Target target,
                                              const char* filename) {
    if (!module || !filename) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Vbit result = SERAPH_VBIT_FALSE;

    /* Create arena for temporary allocations
     * Need significant space for large modules:
     * - Each function needs ~130KB for block info (4096 blocks × 32 bytes)
     * - Plus live intervals, register allocator state, etc.
     * - 48 functions × 150KB ≈ 7MB, use 32MB to be safe */
    Seraph_Arena arena;
    if (!seraph_vbit_is_true(seraph_arena_create(&arena, 32 * 1024 * 1024, 0,
                                                  SERAPH_ARENA_FLAG_ZERO_ON_ALLOC))) {
        return SERAPH_VBIT_FALSE;
    }

    /* Code buffer and machine type based on target */
    uint8_t* code_data = NULL;
    size_t code_size = 0;
    uint16_t machine_type = SERAPH_EM_X86_64;

    switch (target) {
        case SERAPH_ELF_TARGET_X64: {
            /* x64 backend */
            X64_Buffer code_buf;
            if (!seraph_vbit_is_true(x64_buf_init(&code_buf, 64 * 1024))) {
                seraph_arena_destroy(&arena);
                return SERAPH_VBIT_FALSE;
            }

            result = celestial_compile_module(module, &code_buf, &arena);
            if (!seraph_vbit_is_true(result)) {
                x64_buf_free(&code_buf);
                seraph_arena_destroy(&arena);
                return SERAPH_VBIT_FALSE;
            }

            /* Copy code to persistent buffer */
            code_size = code_buf.size;
            code_data = (uint8_t*)malloc(code_size);
            if (code_data && code_buf.code) {
                memcpy(code_data, code_buf.code, code_size);
            }
            x64_buf_free(&code_buf);
            machine_type = SERAPH_EM_X86_64;
            break;
        }

        case SERAPH_ELF_TARGET_ARM64: {
            /* ARM64 backend */
            void* buf_mem = seraph_arena_alloc(&arena, 64 * 1024, 4);
            if (!buf_mem) {
                seraph_arena_destroy(&arena);
                return SERAPH_VBIT_FALSE;
            }

            ARM64_Buffer code_buf;
            arm64_buffer_init(&code_buf, buf_mem, 64 * 1024);

            ARM64_Context ctx;
            arm64_context_init(&ctx, &code_buf, module, &arena);
            arm64_compile_module(&ctx);

            /* Copy code to persistent buffer */
            code_size = arm64_buffer_pos(&code_buf) * sizeof(uint32_t);
            code_data = (uint8_t*)malloc(code_size);
            if (code_data && code_buf.data) {
                memcpy(code_data, code_buf.data, code_size);
            }
            machine_type = SERAPH_EM_AARCH64;
            break;
        }

        case SERAPH_ELF_TARGET_RISCV64: {
            /* RISC-V backend */
            void* buf_mem = seraph_arena_alloc(&arena, 64 * 1024, 4);
            if (!buf_mem) {
                seraph_arena_destroy(&arena);
                return SERAPH_VBIT_FALSE;
            }

            RV_Buffer code_buf;
            rv_buffer_init(&code_buf, buf_mem, 64 * 1024);

            RV_Context ctx;
            rv_context_init(&ctx, &code_buf, module, &arena);
            rv_compile_module(&ctx);

            /* Copy code to persistent buffer */
            code_size = rv_buffer_pos(&code_buf) * sizeof(uint32_t);
            code_data = (uint8_t*)malloc(code_size);
            if (code_data && code_buf.data) {
                memcpy(code_data, code_buf.data, code_size);
            }
            machine_type = SERAPH_EM_RISCV;
            break;
        }

        default:
            seraph_arena_destroy(&arena);
            return SERAPH_VBIT_FALSE;
    }

    if (!code_data) {
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    /* Initialize ELF writer */
    Seraph_Elf_Writer writer;
    if (!seraph_vbit_is_true(seraph_elf_writer_init(&writer))) {
        free(code_data);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    /* Configure writer for target */
    seraph_elf_writer_set_base(&writer, 0x400000);

    /* Set the generated code */
    seraph_elf_set_code(&writer, code_data, code_size);
    free(code_data);  /* Writer makes its own copy */

    /* Set entry point */
    uint64_t entry_point = 0x400000;  /* Text section starts at base */
    seraph_elf_writer_set_entry(&writer, entry_point);

    /* Set proofs if provided */
    if (proofs) {
        seraph_elf_set_proofs(&writer, proofs);
    }

    /* Build SERAPH manifest */
    Seraph_Manifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    manifest.magic = SERAPH_MANIFEST_MAGIC;
    manifest.version = SERAPH_MANIFEST_VERSION;
    manifest.kernel_min_version = 1;
    manifest.entry_point = entry_point;
    manifest.stack_size = 64 * 1024;
    manifest.heap_size = 1024 * 1024;
    manifest.chronon_budget = 1000000;

    seraph_elf_set_manifest(&writer, &manifest);

    /* Add effect declarations */
    uint32_t func_id = 0;
    for (Celestial_Function* fn = module->functions; fn; fn = fn->next) {
        Seraph_Effect_Decl effect_decl;
        effect_decl.function_id = func_id++;
        effect_decl.declared_effects = fn->declared_effects;
        effect_decl.verified_effects = fn->declared_effects;
        effect_decl.required_caps = 0;
        seraph_elf_add_effect(&writer, &effect_decl);
    }

    /* Set the machine type for ELF header */
    writer.machine_type = machine_type;

    /* Write the ELF file */
    result = seraph_elf_write_file(&writer, filename);

    /* Cleanup */
    seraph_elf_writer_free(&writer);
    seraph_arena_destroy(&arena);

    return result;
}
