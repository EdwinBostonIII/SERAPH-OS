/**
 * @file elf64_loader.c
 * @brief MC21: ELF64 Parser and Loader Implementation
 *
 * Implements ELF64 validation and loading for x86-64 kernel images.
 */

#include "elf64_loader.h"
#include "uefi_crt.h"

/*============================================================================
 * ELF64 Identification Constants
 *============================================================================*/

#define EI_MAG0       0   /* File identification */
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4   /* File class */
#define EI_DATA       5   /* Data encoding */
#define EI_VERSION    6   /* File version */
#define EI_OSABI      7   /* OS/ABI identification */
#define EI_ABIVERSION 8   /* ABI version */

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

/*============================================================================
 * Validation Functions
 *============================================================================*/

BOOLEAN elf64_is_valid(const VOID* buffer, UINTN size) {
    if (!buffer || size < sizeof(Elf64_Ehdr)) {
        return FALSE;
    }

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;

    /* Check magic number */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return FALSE;
    }

    /* Check class (64-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return FALSE;
    }

    /* Check data encoding (little-endian) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return FALSE;
    }

    /* Check machine type (x86-64) */
    if (ehdr->e_machine != EM_X86_64) {
        return FALSE;
    }

    /* Check file type (executable or shared object/PIE) */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return FALSE;
    }

    /* Check that program headers exist */
    if (ehdr->e_phnum == 0 || ehdr->e_phoff == 0) {
        return FALSE;
    }

    /* Verify program headers are within buffer */
    UINT64 phdr_end = ehdr->e_phoff + (UINT64)ehdr->e_phnum * ehdr->e_phentsize;
    if (phdr_end > size) {
        return FALSE;
    }

    return TRUE;
}

UINT64 elf64_get_entry_point(const VOID* buffer) {
    if (!buffer) return 0;
    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;
    return ehdr->e_entry;
}

UINTN elf64_get_phdr_count(const VOID* buffer) {
    if (!buffer) return 0;
    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;
    return ehdr->e_phnum;
}

const Elf64_Phdr* elf64_get_phdr(const VOID* buffer, UINTN index) {
    if (!buffer) return NULL;

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;
    if (index >= ehdr->e_phnum) return NULL;

    const UINT8* base = (const UINT8*)buffer;
    return (const Elf64_Phdr*)(base + ehdr->e_phoff + index * ehdr->e_phentsize);
}

/*============================================================================
 * Size Calculation
 *============================================================================*/

EFI_STATUS elf64_calculate_size(
    const VOID* buffer,
    UINTN buffer_size,
    UINT64* virt_base_out,
    UINT64* virt_size_out)
{
    if (!buffer || !virt_base_out || !virt_size_out) {
        return EFI_INVALID_PARAMETER;
    }

    if (!elf64_is_valid(buffer, buffer_size)) {
        return EFI_LOAD_ERROR;
    }

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;
    UINT64 virt_min = ~0ULL;
    UINT64 virt_max = 0;
    BOOLEAN found_loadable = FALSE;

    /* Scan PT_LOAD segments */
    for (UINTN i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* phdr = elf64_get_phdr(buffer, i);
        if (!phdr) continue;

        if (phdr->p_type == PT_LOAD && phdr->p_memsz > 0) {
            found_loadable = TRUE;

            if (phdr->p_vaddr < virt_min) {
                virt_min = phdr->p_vaddr;
            }

            UINT64 seg_end = phdr->p_vaddr + phdr->p_memsz;
            if (seg_end > virt_max) {
                virt_max = seg_end;
            }
        }
    }

    if (!found_loadable) {
        return EFI_NOT_FOUND;
    }

    *virt_base_out = virt_min;
    *virt_size_out = virt_max - virt_min;

    return EFI_SUCCESS;
}

/*============================================================================
 * Loading Functions
 *============================================================================*/

EFI_STATUS elf64_load(
    const VOID* buffer,
    UINTN buffer_size,
    EFI_BOOT_SERVICES* boot_services,
    Elf64_LoadResult* result)
{
    if (!buffer || !boot_services || !result) {
        return EFI_INVALID_PARAMETER;
    }

    if (!elf64_is_valid(buffer, buffer_size)) {
        return EFI_LOAD_ERROR;
    }

    /* Calculate size requirements */
    UINT64 virt_base, virt_size;
    EFI_STATUS status = elf64_calculate_size(buffer, buffer_size,
                                              &virt_base, &virt_size);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* Allocate memory for the kernel at its expected physical address */
    EFI_PHYSICAL_ADDRESS phys_base = virt_base;  /* Load at vaddr as physical */
    UINTN pages = (virt_size + 4095) / 4096;

    /* Try to allocate at the exact address the kernel expects */
    status = boot_services->AllocatePages(
        AllocateAddress,
        EfiLoaderData,
        pages,
        &phys_base
    );

    /* If that fails, fall back to any available memory */
    if (EFI_ERROR(status)) {
        phys_base = 0;
        status = boot_services->AllocatePages(
            AllocateAnyPages,
            EfiLoaderData,
            pages,
            &phys_base
        );
    }

    if (EFI_ERROR(status)) {
        return status;
    }

    /* Zero the allocated memory */
    efi_memset((VOID*)phys_base, 0, pages * 4096);

    /* Load at the allocated address */
    status = elf64_load_at(buffer, buffer_size, phys_base, result);

    if (EFI_ERROR(status)) {
        boot_services->FreePages(phys_base, pages);
        return status;
    }

    return EFI_SUCCESS;
}

EFI_STATUS elf64_load_at(
    const VOID* buffer,
    UINTN buffer_size,
    UINT64 phys_base,
    Elf64_LoadResult* result)
{
    if (!buffer || !result) {
        return EFI_INVALID_PARAMETER;
    }

    if (!elf64_is_valid(buffer, buffer_size)) {
        return EFI_LOAD_ERROR;
    }

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;
    const UINT8* file_data = (const UINT8*)buffer;

    /* Calculate virtual address range */
    UINT64 virt_base, virt_size;
    EFI_STATUS status = elf64_calculate_size(buffer, buffer_size,
                                              &virt_base, &virt_size);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* Initialize result */
    efi_memset(result, 0, sizeof(*result));
    result->entry_point = ehdr->e_entry;
    result->virt_base = virt_base;
    result->virt_top = virt_base + virt_size;
    result->phys_base = phys_base;
    result->total_size = virt_size;
    result->segment_count = 0;
    result->segments = NULL;

    /* Count loadable segments */
    UINTN load_count = 0;
    for (UINTN i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* phdr = elf64_get_phdr(buffer, i);
        if (phdr && phdr->p_type == PT_LOAD && phdr->p_memsz > 0) {
            load_count++;
        }
    }

    /* Copy PT_LOAD segments */
    for (UINTN i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* phdr = elf64_get_phdr(buffer, i);
        if (!phdr || phdr->p_type != PT_LOAD || phdr->p_memsz == 0) {
            continue;
        }

        /* Calculate physical address for this segment */
        UINT64 offset_from_base = phdr->p_vaddr - virt_base;
        UINT64 dest_addr = phys_base + offset_from_base;

        /* Validate file data offset */
        if (phdr->p_offset + phdr->p_filesz > buffer_size) {
            return EFI_LOAD_ERROR;
        }

        /* Copy file data */
        if (phdr->p_filesz > 0) {
            efi_memcpy((VOID*)dest_addr,
                      file_data + phdr->p_offset,
                      phdr->p_filesz);
        }

        /* Zero BSS (memsz > filesz) */
        if (phdr->p_memsz > phdr->p_filesz) {
            efi_memset((VOID*)(dest_addr + phdr->p_filesz),
                      0,
                      phdr->p_memsz - phdr->p_filesz);
        }

        result->segment_count++;
    }

    return EFI_SUCCESS;
}

EFI_STATUS elf64_unload(
    EFI_BOOT_SERVICES* boot_services,
    Elf64_LoadResult* result)
{
    if (!boot_services || !result) {
        return EFI_INVALID_PARAMETER;
    }

    if (result->phys_base != 0 && result->total_size > 0) {
        UINTN pages = (result->total_size + 4095) / 4096;
        boot_services->FreePages(result->phys_base, pages);
    }

    efi_memset(result, 0, sizeof(*result));
    return EFI_SUCCESS;
}

/*============================================================================
 * Debug Helpers
 *============================================================================*/

VOID elf64_print_header(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con,
    const VOID* buffer)
{
    if (!con || !buffer) return;

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;

    efi_print(con, L"ELF64 Header:\r\n");

    efi_print(con, L"  Type: ");
    efi_print_uint64(con, ehdr->e_type, 10);
    efi_print_newline(con);

    efi_print(con, L"  Machine: ");
    efi_print_uint64(con, ehdr->e_machine, 10);
    efi_print_newline(con);

    efi_print(con, L"  Entry: ");
    efi_print_hex(con, ehdr->e_entry);
    efi_print_newline(con);

    efi_print(con, L"  Program Headers: ");
    efi_print_uint64(con, ehdr->e_phnum, 10);
    efi_print(con, L" at offset ");
    efi_print_hex(con, ehdr->e_phoff);
    efi_print_newline(con);
}

VOID elf64_print_phdrs(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con,
    const VOID* buffer)
{
    if (!con || !buffer) return;

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buffer;

    efi_print(con, L"Program Headers:\r\n");

    for (UINTN i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* phdr = elf64_get_phdr(buffer, i);
        if (!phdr) continue;

        efi_print(con, L"  [");
        efi_print_uint64(con, i, 10);
        efi_print(con, L"] Type: ");

        switch (phdr->p_type) {
            case PT_NULL:    efi_print(con, L"NULL"); break;
            case PT_LOAD:    efi_print(con, L"LOAD"); break;
            case PT_DYNAMIC: efi_print(con, L"DYNAMIC"); break;
            case PT_INTERP:  efi_print(con, L"INTERP"); break;
            case PT_NOTE:    efi_print(con, L"NOTE"); break;
            case PT_PHDR:    efi_print(con, L"PHDR"); break;
            default:
                efi_print_hex(con, phdr->p_type);
                break;
        }

        if (phdr->p_type == PT_LOAD) {
            efi_print(con, L" VAddr:");
            efi_print_hex(con, phdr->p_vaddr);
            efi_print(con, L" MemSz:");
            efi_print_hex(con, phdr->p_memsz);
            efi_print(con, L" Flags:");
            if (phdr->p_flags & PF_R) efi_print(con, L"R");
            if (phdr->p_flags & PF_W) efi_print(con, L"W");
            if (phdr->p_flags & PF_X) efi_print(con, L"X");
        }

        efi_print_newline(con);
    }
}
