/**
 * @file boot.h
 * @brief MC15: Seraph Boot Information Structure
 *
 * Passed from UEFI bootloader to kernel. Contains all information
 * needed to initialize the kernel: framebuffer, memory map, ACPI, etc.
 *
 * This structure is the contract between the bootloader and kernel.
 * The bootloader fills it out before jumping to the kernel entry point.
 * The kernel uses it to set up memory management, graphics, and hardware.
 */

#ifndef SERAPH_BOOT_H
#define SERAPH_BOOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Boot Magic and Version
 *============================================================================*/

/** Magic number: "SERAPHTB" in little-endian ASCII */
#define SERAPH_BOOT_MAGIC   0x5345524150484254ULL

/** Current boot protocol version */
#define SERAPH_BOOT_VERSION 1

/*============================================================================
 * Framebuffer Pixel Formats
 *============================================================================*/

/** @name Framebuffer Formats */
/**@{*/
#define SERAPH_FB_FORMAT_BGRA8 0  /**< Blue-Green-Red-Alpha, 8 bits each */
#define SERAPH_FB_FORMAT_RGBA8 1  /**< Red-Green-Blue-Alpha, 8 bits each */
#define SERAPH_FB_FORMAT_BGR8  2  /**< Blue-Green-Red, 8 bits each (no alpha) */
#define SERAPH_FB_FORMAT_RGB8  3  /**< Red-Green-Blue, 8 bits each (no alpha) */
/**@}*/

/*============================================================================
 * Memory Region Types
 *============================================================================*/

/**
 * @brief Memory region types (UEFI-compatible)
 *
 * These match the UEFI memory type values for easy conversion.
 * The kernel uses these to understand what memory is available.
 */
typedef enum {
    SERAPH_MEM_RESERVED         = 0,   /**< Reserved, do not use */
    SERAPH_MEM_LOADER_CODE      = 1,   /**< UEFI boot services code */
    SERAPH_MEM_LOADER_DATA      = 2,   /**< UEFI boot services data */
    SERAPH_MEM_BOOT_SERVICES    = 3,   /**< UEFI boot services */
    SERAPH_MEM_RUNTIME_SERVICES = 4,   /**< UEFI runtime services (preserve!) */
    SERAPH_MEM_CONVENTIONAL     = 7,   /**< Free memory for kernel use */
    SERAPH_MEM_UNUSABLE         = 8,   /**< Memory with errors */
    SERAPH_MEM_ACPI_RECLAIM     = 9,   /**< ACPI tables (can reclaim after parsing) */
    SERAPH_MEM_ACPI_NVS         = 10,  /**< ACPI NVS memory (preserve!) */
    SERAPH_MEM_MMIO             = 11,  /**< Memory-mapped I/O */
    SERAPH_MEM_MMIO_PORT        = 12,  /**< Memory-mapped I/O port space */
    SERAPH_MEM_PAL_CODE         = 13,  /**< PAL code (IA-64 only) */
    SERAPH_MEM_PERSISTENT       = 14,  /**< Persistent memory (NVDIMM) */
    SERAPH_MEM_KERNEL           = 0x80000000,  /**< Kernel image (custom type) */
    SERAPH_MEM_KERNEL_STACK     = 0x80000001,  /**< Kernel stack (custom type) */
    SERAPH_MEM_BOOT_INFO        = 0x80000002,  /**< Boot info struct (custom type) */
} Seraph_Memory_Type;

/*============================================================================
 * Memory Descriptor
 *============================================================================*/

/**
 * @brief Memory descriptor (matches UEFI memory descriptor layout)
 *
 * Describes a contiguous region of physical memory.
 * An array of these is passed to the kernel in the boot info.
 */
typedef struct __attribute__((packed)) {
    uint32_t type;          /**< Seraph_Memory_Type */
    uint32_t _pad;          /**< Padding for alignment */
    uint64_t phys_start;    /**< Physical start address (page-aligned) */
    uint64_t virt_start;    /**< Virtual start address (for runtime services) */
    uint64_t page_count;    /**< Number of 4KB pages */
    uint64_t attribute;     /**< Memory attributes (cacheability, etc.) */
} Seraph_Memory_Descriptor;

/*============================================================================
 * Memory Attributes
 *============================================================================*/

/** @name Memory Attributes */
/**@{*/
#define SERAPH_MEM_ATTR_UC           0x0000000000000001ULL  /**< Uncacheable */
#define SERAPH_MEM_ATTR_WC           0x0000000000000002ULL  /**< Write-combining */
#define SERAPH_MEM_ATTR_WT           0x0000000000000004ULL  /**< Write-through */
#define SERAPH_MEM_ATTR_WB           0x0000000000000008ULL  /**< Write-back */
#define SERAPH_MEM_ATTR_UCE          0x0000000000000010ULL  /**< Uncacheable, exported */
#define SERAPH_MEM_ATTR_WP           0x0000000000001000ULL  /**< Write-protected */
#define SERAPH_MEM_ATTR_RP           0x0000000000002000ULL  /**< Read-protected */
#define SERAPH_MEM_ATTR_XP           0x0000000000004000ULL  /**< Execute-protected */
#define SERAPH_MEM_ATTR_NV           0x0000000000008000ULL  /**< Non-volatile */
#define SERAPH_MEM_ATTR_MORE_RELIABLE 0x0000000000010000ULL /**< More reliable */
#define SERAPH_MEM_ATTR_RO           0x0000000000020000ULL  /**< Read-only */
#define SERAPH_MEM_ATTR_SP           0x0000000000040000ULL  /**< Specific-purpose */
#define SERAPH_MEM_ATTR_CPU_CRYPTO   0x0000000000080000ULL  /**< CPU crypto capable */
#define SERAPH_MEM_ATTR_RUNTIME      0x8000000000000000ULL  /**< Runtime services */
/**@}*/

/*============================================================================
 * Boot Flags
 *============================================================================*/

/** @name Boot Flags */
/**@{*/
#define SERAPH_BOOT_FLAG_FRAMEBUFFER  0x00000001  /**< Framebuffer is valid */
#define SERAPH_BOOT_FLAG_ACPI_V1      0x00000002  /**< ACPI 1.0 RSDP found */
#define SERAPH_BOOT_FLAG_ACPI_V2      0x00000004  /**< ACPI 2.0+ RSDP found */
#define SERAPH_BOOT_FLAG_SMBIOS       0x00000008  /**< SMBIOS found */
#define SERAPH_BOOT_FLAG_SERIAL       0x00000010  /**< Serial console available */
#define SERAPH_BOOT_FLAG_EFI_RUNTIME  0x00000020  /**< EFI runtime services available */
/**@}*/

/*============================================================================
 * Boot Information Structure
 *============================================================================*/

/**
 * @brief Boot information structure passed to kernel
 *
 * This structure is the bridge between bootloader and kernel. It must
 * remain stable across versions (new fields at end, no removals).
 *
 * The bootloader allocates this structure in memory that won't be
 * reclaimed, fills it out, and passes its physical address to the kernel.
 */
typedef struct __attribute__((packed)) {
    /*--- Header ---*/
    uint64_t magic;                     /**< SERAPH_BOOT_MAGIC */
    uint32_t version;                   /**< SERAPH_BOOT_VERSION */
    uint32_t flags;                     /**< SERAPH_BOOT_FLAG_* */

    /*--- Framebuffer ---*/
    uint64_t framebuffer_base;          /**< Physical address of framebuffer */
    uint64_t framebuffer_size;          /**< Size in bytes */
    uint32_t fb_width;                  /**< Width in pixels */
    uint32_t fb_height;                 /**< Height in pixels */
    uint32_t fb_stride;                 /**< Bytes per scan line */
    uint32_t fb_format;                 /**< SERAPH_FB_FORMAT_* */

    /*--- Memory Map ---*/
    uint64_t memory_map_base;           /**< Physical address of memory map array */
    uint64_t memory_map_size;           /**< Total size in bytes */
    uint64_t memory_desc_size;          /**< Size of each descriptor */
    uint32_t memory_desc_version;       /**< Descriptor version (UEFI) */
    uint32_t memory_map_count;          /**< Number of descriptors */

    /*--- Kernel Location ---*/
    uint64_t kernel_phys_base;          /**< Physical address where kernel is loaded */
    uint64_t kernel_virt_base;          /**< Virtual address kernel expects */
    uint64_t kernel_size;               /**< Size of kernel image */

    /*--- ACPI/SMBIOS ---*/
    uint64_t rsdp_address;              /**< Physical address of RSDP */
    uint64_t smbios_address;            /**< Physical address of SMBIOS entry point */

    /*--- Initial Allocations ---*/
    uint64_t stack_phys;                /**< Physical address of kernel stack */
    uint64_t stack_size;                /**< Size of kernel stack */
    uint64_t primordial_arena_phys;     /**< Physical address of early arena */
    uint64_t primordial_arena_size;     /**< Size of early arena */

    /*--- Page Tables (set up by bootloader) ---*/
    uint64_t pml4_phys;                 /**< Physical address of PML4 */

    /*--- Future expansion ---*/
    uint8_t _reserved[120];             /**< Reserved for future use */
} Seraph_BootInfo;

/*============================================================================
 * Boot Info Validation
 *============================================================================*/

/**
 * @brief Validate boot info structure
 *
 * @param info Pointer to boot info structure
 * @return Non-zero if valid, zero if invalid
 */
static inline int seraph_boot_info_valid(const Seraph_BootInfo* info) {
    if (!info) return 0;
    if (info->magic != SERAPH_BOOT_MAGIC) return 0;
    if (info->version != SERAPH_BOOT_VERSION) return 0;
    return 1;
}

/**
 * @brief Check if framebuffer is available
 *
 * @param info Pointer to boot info structure
 * @return Non-zero if framebuffer is valid
 */
static inline int seraph_boot_has_framebuffer(const Seraph_BootInfo* info) {
    return info && (info->flags & SERAPH_BOOT_FLAG_FRAMEBUFFER) &&
           info->framebuffer_base != 0;
}

/**
 * @brief Check if ACPI is available
 *
 * @param info Pointer to boot info structure
 * @return Non-zero if ACPI RSDP is valid
 */
static inline int seraph_boot_has_acpi(const Seraph_BootInfo* info) {
    return info && (info->flags & (SERAPH_BOOT_FLAG_ACPI_V1 | SERAPH_BOOT_FLAG_ACPI_V2)) &&
           info->rsdp_address != 0;
}

/**
 * @brief Get memory descriptor by index
 *
 * @param info Boot info structure
 * @param index Descriptor index
 * @return Pointer to descriptor, or NULL if out of bounds
 */
static inline const Seraph_Memory_Descriptor* seraph_boot_get_memory_desc(
    const Seraph_BootInfo* info, uint32_t index)
{
    if (!info || index >= info->memory_map_count) return 0;
    return (const Seraph_Memory_Descriptor*)(
        info->memory_map_base + (index * info->memory_desc_size));
}

/**
 * @brief Calculate total conventional memory
 *
 * @param info Boot info structure
 * @return Total bytes of conventional (usable) memory
 */
static inline uint64_t seraph_boot_total_conventional_memory(const Seraph_BootInfo* info) {
    if (!info) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(info, i);
        if (desc && desc->type == SERAPH_MEM_CONVENTIONAL) {
            total += desc->page_count * 4096;
        }
    }
    return total;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_BOOT_H */
