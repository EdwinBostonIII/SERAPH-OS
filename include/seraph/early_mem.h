/**
 * @file early_mem.h
 * @brief MC26: Early Memory Initialization - Bootstrap Paging
 *
 * This module handles the critical task of setting up initial page tables
 * before the VMM is initialized. When the kernel starts, UEFI has left
 * identity mapping active, but we need:
 *
 *   1. IDENTITY MAP: Lower 4GB mapped 1:1 for boot compatibility
 *   2. HIGHER-HALF: Kernel code/data at SERAPH_KERNEL_BASE
 *   3. PHYSICAL MAP: All RAM at SERAPH_PHYS_MAP_BASE for phys_to_virt()
 *   4. RECURSIVE: PML4[510] points to PML4 for page table self-reference
 *
 * This bootstrap code runs with identity mapping, manually allocates
 * physical pages from the boot memory map, builds the page tables,
 * and switches CR3 to activate the new address space.
 *
 * After early_mem_init() completes:
 *   - seraph_phys_to_virt() works correctly
 *   - seraph_vmm_init() can be called safely
 *   - The kernel runs in higher-half address space
 */

#ifndef SERAPH_EARLY_MEM_H
#define SERAPH_EARLY_MEM_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/boot.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Early Memory Initialization Result
 *============================================================================*/

/**
 * @brief Result of early memory initialization
 */
typedef enum {
    SERAPH_EARLY_MEM_OK = 0,           /**< Success */
    SERAPH_EARLY_MEM_NO_MEMORY,        /**< Out of physical memory */
    SERAPH_EARLY_MEM_INVALID_BOOTINFO, /**< Boot info is invalid */
    SERAPH_EARLY_MEM_NO_MEMORY_MAP,    /**< No memory map in boot info */
} Seraph_EarlyMem_Result;

/*============================================================================
 * Early Memory Allocator State
 *============================================================================*/

/** Maximum number of pages that early_mem can track */
#define SERAPH_EARLY_MEM_MAX_PAGES 64

/**
 * @brief Early memory allocator state
 *
 * This simple bump allocator tracks pages allocated during early boot.
 * After PMM is initialized, these pages should be marked as used.
 */
typedef struct {
    uint64_t total_allocated;                          /**< Total pages allocated */
    uint64_t pml4_phys;                                /**< Physical address of PML4 */
    uint64_t allocated_pages[SERAPH_EARLY_MEM_MAX_PAGES]; /**< Array of allocated page addresses */
    bool     initialized;                              /**< Whether init completed */
} Seraph_EarlyMem_State;

/*============================================================================
 * Functions
 *============================================================================*/

/**
 * @brief Initialize early memory and set up initial page tables
 *
 * This function MUST be called before any VMM operations. It:
 *   1. Allocates pages from boot memory map
 *   2. Creates 4-level page tables with:
 *      - Identity mapping for first 4GB
 *      - Physical memory map at SERAPH_PHYS_MAP_BASE
 *      - Framebuffer mapping (if present)
 *      - Recursive mapping at PML4[510]
 *   3. Loads CR3 with new PML4
 *
 * After this function returns:
 *   - seraph_phys_to_virt() works correctly
 *   - Physical memory is accessible via higher-half mapping
 *   - boot_info->pml4_phys is updated with new PML4
 *
 * @param boot_info Boot information structure (will be updated)
 * @return Result code
 */
Seraph_EarlyMem_Result seraph_early_mem_init(Seraph_BootInfo* boot_info);

/**
 * @brief Get the early memory allocator state
 *
 * Used by PMM to know which pages were allocated during early boot.
 *
 * @return Pointer to early memory state
 */
const Seraph_EarlyMem_State* seraph_early_mem_get_state(void);

/**
 * @brief Allocate a physical page during early boot
 *
 * This is a simple bump allocator that finds free pages in the
 * boot memory map. Used internally by early_mem_init and can be
 * used by other early boot code before PMM is ready.
 *
 * @param boot_info Boot information with memory map
 * @return Physical address of allocated page, or 0 on failure
 */
uint64_t seraph_early_alloc_page(const Seraph_BootInfo* boot_info);

/**
 * @brief Get total physical memory size
 *
 * Scans the memory map to find the highest physical address.
 * Used to determine how much physical memory needs to be mapped.
 *
 * @param boot_info Boot information with memory map
 * @return Total physical memory size in bytes
 */
uint64_t seraph_early_get_total_memory(const Seraph_BootInfo* boot_info);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_EARLY_MEM_H */
