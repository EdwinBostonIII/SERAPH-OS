/**
 * @file elf64_loader.h
 * @brief MC21: ELF64 Parser and Loader
 *
 * Parses and loads ELF64 kernel images from disk into memory.
 *
 * ELF64 Overview:
 *   - ELF header at offset 0 contains magic number and architecture info
 *   - Program headers describe loadable segments (PT_LOAD)
 *   - We copy PT_LOAD segments to their specified virtual addresses
 *   - Entry point is specified in the ELF header
 *
 * This loader:
 *   1. Validates ELF64 header (magic, architecture, 64-bit)
 *   2. Iterates program headers to find PT_LOAD segments
 *   3. Allocates physical memory for each segment
 *   4. Copies segment data from file to memory
 *   5. Returns entry point and memory requirements
 */

#ifndef SERAPH_ELF64_LOADER_H
#define SERAPH_ELF64_LOADER_H

#include "seraph/uefi_types.h"

/*============================================================================
 * ELF64 Constants
 *============================================================================*/

/** ELF Magic number */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" in little-endian */

/** ELF Class (32/64 bit) */
#define ELFCLASS64 2

/** ELF Data encoding (endianness) */
#define ELFDATA2LSB 1  /* Little-endian */

/** ELF Object file types */
#define ET_EXEC 2  /* Executable */
#define ET_DYN  3  /* Shared object (PIE) */

/** ELF Machine types */
#define EM_X86_64 62  /* AMD x86-64 */

/** Program header types */
#define PT_NULL    0  /* Unused entry */
#define PT_LOAD    1  /* Loadable segment */
#define PT_DYNAMIC 2  /* Dynamic linking info */
#define PT_INTERP  3  /* Interpreter path */
#define PT_NOTE    4  /* Auxiliary info */
#define PT_PHDR    6  /* Program header table */

/** Program header flags */
#define PF_X 0x1  /* Execute */
#define PF_W 0x2  /* Write */
#define PF_R 0x4  /* Read */

/*============================================================================
 * ELF64 Structures
 *============================================================================*/

/**
 * @brief ELF64 File Header
 */
typedef struct __attribute__((packed)) {
    UINT8  e_ident[16];   /**< ELF identification */
    UINT16 e_type;        /**< Object file type */
    UINT16 e_machine;     /**< Machine type */
    UINT32 e_version;     /**< ELF version */
    UINT64 e_entry;       /**< Entry point virtual address */
    UINT64 e_phoff;       /**< Program header offset */
    UINT64 e_shoff;       /**< Section header offset */
    UINT32 e_flags;       /**< Processor flags */
    UINT16 e_ehsize;      /**< ELF header size */
    UINT16 e_phentsize;   /**< Program header entry size */
    UINT16 e_phnum;       /**< Program header count */
    UINT16 e_shentsize;   /**< Section header entry size */
    UINT16 e_shnum;       /**< Section header count */
    UINT16 e_shstrndx;    /**< Section name string table index */
} Elf64_Ehdr;

/**
 * @brief ELF64 Program Header
 */
typedef struct __attribute__((packed)) {
    UINT32 p_type;        /**< Segment type */
    UINT32 p_flags;       /**< Segment flags */
    UINT64 p_offset;      /**< Offset in file */
    UINT64 p_vaddr;       /**< Virtual address */
    UINT64 p_paddr;       /**< Physical address (unused) */
    UINT64 p_filesz;      /**< Size in file */
    UINT64 p_memsz;       /**< Size in memory */
    UINT64 p_align;       /**< Alignment */
} Elf64_Phdr;

/*============================================================================
 * Loader Structures
 *============================================================================*/

/**
 * @brief Information about a loaded ELF segment
 */
typedef struct {
    UINT64 vaddr;         /**< Virtual address */
    UINT64 paddr;         /**< Physical address where loaded */
    UINT64 memsz;         /**< Size in memory */
    UINT64 filesz;        /**< Size from file */
    UINT32 flags;         /**< PF_R, PF_W, PF_X */
} Elf64_LoadedSegment;

/**
 * @brief Result of loading an ELF64 kernel
 */
typedef struct {
    UINT64             entry_point;      /**< Virtual entry point address */
    UINT64             virt_base;        /**< Lowest virtual address */
    UINT64             virt_top;         /**< Highest virtual address + 1 */
    UINT64             phys_base;        /**< Physical base where loaded */
    UINT64             total_size;       /**< Total size in memory */
    UINTN              segment_count;    /**< Number of loaded segments */
    Elf64_LoadedSegment* segments;       /**< Array of loaded segments */
} Elf64_LoadResult;

/*============================================================================
 * Validation Functions
 *============================================================================*/

/**
 * @brief Check if buffer contains valid ELF64 header
 *
 * @param buffer Buffer containing at least sizeof(Elf64_Ehdr) bytes
 * @param size Size of buffer
 * @return TRUE if valid ELF64 x86-64 executable
 */
BOOLEAN elf64_is_valid(const VOID* buffer, UINTN size);

/**
 * @brief Get entry point from ELF64 header
 *
 * @param buffer Buffer containing ELF64 file
 * @return Entry point virtual address
 */
UINT64 elf64_get_entry_point(const VOID* buffer);

/**
 * @brief Get program header count
 *
 * @param buffer Buffer containing ELF64 file
 * @return Number of program headers
 */
UINTN elf64_get_phdr_count(const VOID* buffer);

/**
 * @brief Get program header by index
 *
 * @param buffer Buffer containing ELF64 file
 * @param index Program header index
 * @return Pointer to program header, or NULL if invalid
 */
const Elf64_Phdr* elf64_get_phdr(const VOID* buffer, UINTN index);

/*============================================================================
 * Loading Functions
 *============================================================================*/

/**
 * @brief Calculate memory requirements for loading ELF64
 *
 * Scans PT_LOAD segments to determine total memory needed.
 *
 * @param buffer Buffer containing ELF64 file
 * @param buffer_size Size of buffer
 * @param virt_base_out Output: lowest virtual address
 * @param virt_size_out Output: total virtual size needed
 * @return EFI_SUCCESS or error code
 */
EFI_STATUS elf64_calculate_size(
    const VOID* buffer,
    UINTN buffer_size,
    UINT64* virt_base_out,
    UINT64* virt_size_out
);

/**
 * @brief Load ELF64 file into memory
 *
 * Allocates memory and copies PT_LOAD segments.
 * The caller must provide memory allocation functions.
 *
 * @param buffer Buffer containing ELF64 file
 * @param buffer_size Size of buffer
 * @param boot_services EFI boot services for memory allocation
 * @param result Output: load result information
 * @return EFI_SUCCESS or error code
 */
EFI_STATUS elf64_load(
    const VOID* buffer,
    UINTN buffer_size,
    EFI_BOOT_SERVICES* boot_services,
    Elf64_LoadResult* result
);

/**
 * @brief Load ELF64 file at specified physical address
 *
 * Like elf64_load but uses pre-allocated memory at specified address.
 *
 * @param buffer Buffer containing ELF64 file
 * @param buffer_size Size of buffer
 * @param phys_base Physical address to load at
 * @param result Output: load result information
 * @return EFI_SUCCESS or error code
 */
EFI_STATUS elf64_load_at(
    const VOID* buffer,
    UINTN buffer_size,
    UINT64 phys_base,
    Elf64_LoadResult* result
);

/**
 * @brief Free memory allocated by elf64_load
 *
 * @param boot_services EFI boot services
 * @param result Load result from elf64_load
 * @return EFI_SUCCESS or error code
 */
EFI_STATUS elf64_unload(
    EFI_BOOT_SERVICES* boot_services,
    Elf64_LoadResult* result
);

/*============================================================================
 * Debug Helpers
 *============================================================================*/

/**
 * @brief Print ELF64 header information
 *
 * @param con Console output
 * @param buffer Buffer containing ELF64 file
 */
VOID elf64_print_header(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con,
    const VOID* buffer
);

/**
 * @brief Print program headers
 *
 * @param con Console output
 * @param buffer Buffer containing ELF64 file
 */
VOID elf64_print_phdrs(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con,
    const VOID* buffer
);

#endif /* SERAPH_ELF64_LOADER_H */
