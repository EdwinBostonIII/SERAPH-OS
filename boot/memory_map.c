/**
 * @file memory_map.c
 * @brief MC23: UEFI Memory Map Capture
 *
 * Captures the UEFI memory map and converts it to Seraph format.
 * This is done just before ExitBootServices() to get the final map.
 *
 * Memory Map Overview:
 *   - UEFI provides a memory map describing all physical memory
 *   - Each descriptor has type, physical address, page count, attributes
 *   - The map must be captured IMMEDIATELY before ExitBootServices
 *   - The map key changes with any memory operation
 *
 * Conversion:
 *   - UEFI memory types map directly to Seraph types (same values)
 *   - We copy the map to our own buffer for kernel use
 *   - Descriptors are sorted by physical address
 */

#include "seraph/uefi_types.h"
#include "seraph/boot.h"
#include "uefi_crt.h"

/*============================================================================
 * Memory Map Buffer Size
 *============================================================================*/

/** Maximum size for memory map (should be enough for most systems) */
#define MAX_MEMORY_MAP_SIZE (16 * 1024)

/*============================================================================
 * Static Storage
 *============================================================================*/

/** Static buffer for memory map (to avoid allocations) */
static UINT8 memory_map_buffer[MAX_MEMORY_MAP_SIZE];

/*============================================================================
 * Memory Type Conversion
 *============================================================================*/

/**
 * @brief Convert UEFI memory type to Seraph type
 *
 * @param efi_type UEFI memory type
 * @return Seraph memory type
 */
static Seraph_Memory_Type convert_memory_type(EFI_MEMORY_TYPE efi_type) {
    switch (efi_type) {
        case EfiReservedMemoryType:
            return SERAPH_MEM_RESERVED;
        case EfiLoaderCode:
            return SERAPH_MEM_LOADER_CODE;
        case EfiLoaderData:
            return SERAPH_MEM_LOADER_DATA;
        case EfiBootServicesCode:
        case EfiBootServicesData:
            return SERAPH_MEM_BOOT_SERVICES;
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
            return SERAPH_MEM_RUNTIME_SERVICES;
        case EfiConventionalMemory:
            return SERAPH_MEM_CONVENTIONAL;
        case EfiUnusableMemory:
            return SERAPH_MEM_UNUSABLE;
        case EfiACPIReclaimMemory:
            return SERAPH_MEM_ACPI_RECLAIM;
        case EfiACPIMemoryNVS:
            return SERAPH_MEM_ACPI_NVS;
        case EfiMemoryMappedIO:
            return SERAPH_MEM_MMIO;
        case EfiMemoryMappedIOPortSpace:
            return SERAPH_MEM_MMIO_PORT;
        case EfiPalCode:
            return SERAPH_MEM_PAL_CODE;
        case EfiPersistentMemory:
            return SERAPH_MEM_PERSISTENT;
        default:
            return SERAPH_MEM_RESERVED;
    }
}

/**
 * @brief Get human-readable name for memory type
 *
 * @param type Memory type
 * @return Type name string
 */
static const CHAR16* memory_type_name(Seraph_Memory_Type type) {
    switch (type) {
        case SERAPH_MEM_RESERVED:         return L"Reserved";
        case SERAPH_MEM_LOADER_CODE:      return L"LoaderCode";
        case SERAPH_MEM_LOADER_DATA:      return L"LoaderData";
        case SERAPH_MEM_BOOT_SERVICES:    return L"BootServices";
        case SERAPH_MEM_RUNTIME_SERVICES: return L"RuntimeServices";
        case SERAPH_MEM_CONVENTIONAL:     return L"Conventional";
        case SERAPH_MEM_UNUSABLE:         return L"Unusable";
        case SERAPH_MEM_ACPI_RECLAIM:     return L"ACPIReclaim";
        case SERAPH_MEM_ACPI_NVS:         return L"ACPI_NVS";
        case SERAPH_MEM_MMIO:             return L"MMIO";
        case SERAPH_MEM_MMIO_PORT:        return L"MMIO_Port";
        case SERAPH_MEM_PAL_CODE:         return L"PALCode";
        case SERAPH_MEM_PERSISTENT:       return L"Persistent";
        case SERAPH_MEM_KERNEL:           return L"Kernel";
        case SERAPH_MEM_KERNEL_STACK:     return L"KernelStack";
        case SERAPH_MEM_BOOT_INFO:        return L"BootInfo";
        default:                          return L"Unknown";
    }
}

/*============================================================================
 * Public Functions
 *============================================================================*/

/**
 * @brief Get UEFI memory map
 *
 * Retrieves the current memory map from UEFI. The map_key is needed
 * for ExitBootServices.
 *
 * @param boot_services UEFI boot services
 * @param boot_info Boot info to fill
 * @param map_key Output: memory map key for ExitBootServices
 * @return EFI_SUCCESS or error code
 */
EFI_STATUS memory_map_get(
    EFI_BOOT_SERVICES* boot_services,
    Seraph_BootInfo* boot_info,
    UINTN* map_key)
{
    EFI_STATUS status;
    UINTN map_size = MAX_MEMORY_MAP_SIZE;
    UINTN descriptor_size;
    UINT32 descriptor_version;

    if (!boot_services || !boot_info || !map_key) {
        return EFI_INVALID_PARAMETER;
    }

    /* Get the memory map */
    status = boot_services->GetMemoryMap(
        &map_size,
        (EFI_MEMORY_DESCRIPTOR*)memory_map_buffer,
        map_key,
        &descriptor_size,
        &descriptor_version
    );

    if (EFI_ERROR(status)) {
        return status;
    }

    /* Calculate number of entries */
    UINTN entry_count = map_size / descriptor_size;

    /* Validate descriptor size before in-place conversion
     * UEFI descriptor size may be larger than ours due to padding/versioning */
    if (descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return EFI_INCOMPATIBLE_VERSION;
    }

    /* Ensure we can safely convert in place:
     * Our struct must be <= UEFI struct size to avoid overwriting */
    if (sizeof(Seraph_Memory_Descriptor) > descriptor_size) {
        /* UEFI descriptor is smaller than ours - cannot convert in place */
        return EFI_BUFFER_TOO_SMALL;
    }

    /* Convert to Seraph format in place (sizes validated above) */
    UINT8* src = memory_map_buffer;
    Seraph_Memory_Descriptor* dst = (Seraph_Memory_Descriptor*)memory_map_buffer;

    for (UINTN i = 0; i < entry_count; i++) {
        EFI_MEMORY_DESCRIPTOR* efi_desc = (EFI_MEMORY_DESCRIPTOR*)(src + i * descriptor_size);

        /* Build Seraph descriptor */
        Seraph_Memory_Descriptor seraph_desc;
        seraph_desc.type = convert_memory_type(efi_desc->Type);
        seraph_desc._pad = 0;
        seraph_desc.phys_start = efi_desc->PhysicalStart;
        seraph_desc.virt_start = efi_desc->VirtualStart;
        seraph_desc.page_count = efi_desc->NumberOfPages;
        seraph_desc.attribute = efi_desc->Attribute;

        /* Write to output (may overlap with source, but that's OK) */
        dst[i] = seraph_desc;
    }

    /* Fill boot info */
    boot_info->memory_map_base = (UINT64)memory_map_buffer;
    boot_info->memory_map_size = entry_count * sizeof(Seraph_Memory_Descriptor);
    boot_info->memory_desc_size = sizeof(Seraph_Memory_Descriptor);
    boot_info->memory_desc_version = 1;
    boot_info->memory_map_count = (UINT32)entry_count;

    return EFI_SUCCESS;
}

/**
 * @brief Print memory map
 *
 * @param con Console output
 * @param boot_info Boot info with memory map
 */
VOID memory_map_print(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con,
    const Seraph_BootInfo* boot_info)
{
    if (!con || !boot_info) return;

    efi_print(con, L"[MEMMAP] ");
    efi_print_uint64(con, boot_info->memory_map_count, 10);
    efi_print(con, L" entries:\r\n");

    UINT64 total_conventional = 0;
    UINT64 total_reserved = 0;
    UINT64 total_runtime = 0;

    for (UINT32 i = 0; i < boot_info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, i);
        if (!desc) continue;

        /* Accumulate statistics */
        UINT64 bytes = desc->page_count * 4096;
        switch (desc->type) {
            case SERAPH_MEM_CONVENTIONAL:
            case SERAPH_MEM_LOADER_CODE:
            case SERAPH_MEM_LOADER_DATA:
            case SERAPH_MEM_BOOT_SERVICES:
                total_conventional += bytes;
                break;
            case SERAPH_MEM_RUNTIME_SERVICES:
                total_runtime += bytes;
                break;
            default:
                total_reserved += bytes;
                break;
        }

        /* Print entry (only first few and summary) */
        if (i < 10 || desc->type == SERAPH_MEM_CONVENTIONAL) {
            efi_print(con, L"  ");
            efi_print_hex(con, desc->phys_start);
            efi_print(con, L" - ");
            efi_print_hex(con, desc->phys_start + bytes - 1);
            efi_print(con, L" ");
            efi_print(con, memory_type_name(desc->type));
            efi_print_newline(con);
        }
    }

    efi_print(con, L"  Conventional: ");
    efi_print_uint64(con, total_conventional / (1024 * 1024), 10);
    efi_print(con, L" MB\r\n");

    efi_print(con, L"  Runtime: ");
    efi_print_uint64(con, total_runtime / 1024, 10);
    efi_print(con, L" KB\r\n");
}

/**
 * @brief Find largest conventional memory region
 *
 * @param boot_info Boot info with memory map
 * @param base_out Output: physical address of region
 * @param size_out Output: size in bytes
 * @return TRUE if found, FALSE if no conventional memory
 */
BOOLEAN memory_map_find_largest(
    const Seraph_BootInfo* boot_info,
    UINT64* base_out,
    UINT64* size_out)
{
    if (!boot_info || !base_out || !size_out) {
        return FALSE;
    }

    UINT64 largest_base = 0;
    UINT64 largest_size = 0;

    for (UINT32 i = 0; i < boot_info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, i);
        if (!desc) continue;

        if (desc->type == SERAPH_MEM_CONVENTIONAL) {
            UINT64 size = desc->page_count * 4096;
            if (size > largest_size) {
                largest_size = size;
                largest_base = desc->phys_start;
            }
        }
    }

    if (largest_size == 0) {
        return FALSE;
    }

    *base_out = largest_base;
    *size_out = largest_size;
    return TRUE;
}

/**
 * @brief Calculate total conventional memory
 *
 * @param boot_info Boot info with memory map
 * @return Total bytes of conventional memory
 */
UINT64 memory_map_total_conventional(const Seraph_BootInfo* boot_info) {
    if (!boot_info) return 0;

    UINT64 total = 0;
    for (UINT32 i = 0; i < boot_info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, i);
        if (!desc) continue;

        if (desc->type == SERAPH_MEM_CONVENTIONAL ||
            desc->type == SERAPH_MEM_LOADER_CODE ||
            desc->type == SERAPH_MEM_LOADER_DATA ||
            desc->type == SERAPH_MEM_BOOT_SERVICES) {
            total += desc->page_count * 4096;
        }
    }
    return total;
}

/**
 * @brief Find memory for kernel allocation
 *
 * Finds a suitable region for allocating kernel memory.
 * Prefers high memory (above 1MB) to avoid legacy regions.
 *
 * @param boot_info Boot info with memory map
 * @param size Size needed
 * @param align Alignment (must be power of 2)
 * @param base_out Output: physical address
 * @return TRUE if found, FALSE if not enough memory
 */
BOOLEAN memory_map_find_for_kernel(
    const Seraph_BootInfo* boot_info,
    UINT64 size,
    UINT64 align,
    UINT64* base_out)
{
    if (!boot_info || !base_out || size == 0) {
        return FALSE;
    }

    /* Prefer memory above 1MB */
    for (UINT32 i = 0; i < boot_info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, i);
        if (!desc) continue;

        if (desc->type != SERAPH_MEM_CONVENTIONAL) continue;
        if (desc->phys_start < 0x100000) continue; /* Skip first 1MB */

        UINT64 region_size = desc->page_count * 4096;

        /* Align base address */
        UINT64 aligned_base = (desc->phys_start + align - 1) & ~(align - 1);
        if (aligned_base >= desc->phys_start + region_size) continue;

        UINT64 available = desc->phys_start + region_size - aligned_base;
        if (available >= size) {
            *base_out = aligned_base;
            return TRUE;
        }
    }

    return FALSE;
}
