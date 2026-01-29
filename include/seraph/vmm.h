/**
 * @file vmm.h
 * @brief MC18: Virtual Memory Manager - 4-level x86-64 paging
 *
 * The Virtual Memory Manager (VMM) handles virtual-to-physical address
 * translation using x86-64's 4-level page tables (PML4, PDPT, PD, PT).
 *
 * Address Space Layout (48-bit canonical addresses):
 *
 *   0x0000_0000_0000_0000 - 0x0000_7FFF_FFFF_FFFF : User space (Volatile)
 *   0x0000_8000_0000_0000 - 0x0000_BFFF_FFFF_FFFF : Atlas (single-level store)
 *   0x0000_C000_0000_0000 - 0x0000_FFFF_FFFF_FFFF : Aether (DSM)
 *   0xFFFF_8000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF : Kernel space (higher half)
 *
 * Key Features:
 *   1. RECURSIVE MAPPING: PML4[510] points to PML4 itself, allowing
 *      page tables to be accessed through virtual addresses.
 *   2. HUGE PAGES: Support for 2MB and 1GB pages for efficiency.
 *   3. VOID SAFETY: All errors return SERAPH_VBIT_VOID or SERAPH_VOID_U64.
 *   4. NX SUPPORT: Proper No-Execute protection for data pages.
 */

#ifndef SERAPH_VMM_H
#define SERAPH_VMM_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/pmm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Page Table Entry Flags
 *============================================================================*/

/**
 * @brief Page table entry flags
 *
 * These flags are used in all levels of page tables (PML4, PDPT, PD, PT).
 * Some flags have different meanings at different levels.
 */
typedef enum {
    SERAPH_PTE_PRESENT     = (1ULL << 0),   /**< Page is present in memory */
    SERAPH_PTE_WRITABLE    = (1ULL << 1),   /**< Page is writable */
    SERAPH_PTE_USER        = (1ULL << 2),   /**< Page accessible from ring 3 */
    SERAPH_PTE_WRITETHROUGH = (1ULL << 3),  /**< Write-through caching */
    SERAPH_PTE_NOCACHE     = (1ULL << 4),   /**< Disable caching */
    SERAPH_PTE_ACCESSED    = (1ULL << 5),   /**< Page has been accessed */
    SERAPH_PTE_DIRTY       = (1ULL << 6),   /**< Page has been written (PT only) */
    SERAPH_PTE_HUGE        = (1ULL << 7),   /**< 2MB page (PD) or 1GB page (PDPT) */
    SERAPH_PTE_GLOBAL      = (1ULL << 8),   /**< Don't flush from TLB on CR3 load */
    SERAPH_PTE_NX          = (1ULL << 63),  /**< No-Execute (if NX enabled) */
} Seraph_PTE_Flags;

/** Physical address mask (bits 12-51) */
#define SERAPH_PTE_ADDR_MASK    0x000FFFFFFFFFF000ULL

/** Available bits for OS use (bits 9-11, 52-62) */
#define SERAPH_PTE_AVAIL_MASK   0x7FF0000000000E00ULL

/*============================================================================
 * Address Space Layout Constants
 *============================================================================*/

/** @name Address Space Regions */
/**@{*/
/** Volatile region (user space) */
#define SERAPH_VOLATILE_BASE    0x0000000000000000ULL
#define SERAPH_VOLATILE_END     0x00007FFFFFFFFFFFULL

/** Atlas region (single-level store) */
#define SERAPH_ATLAS_BASE       0x0000800000000000ULL
#define SERAPH_ATLAS_END        0x0000BFFFFFFFFFFFULL

/** Aether region (distributed shared memory) */
#ifndef SERAPH_AETHER_BASE
#define SERAPH_AETHER_BASE      0x0000C00000000000ULL
#endif
#ifndef SERAPH_AETHER_END
#define SERAPH_AETHER_END       0x0000FFFFFFFFFFFFULL
#endif

/** Kernel region (higher half) */
#define SERAPH_KERNEL_BASE      0xFFFF800000000000ULL
#define SERAPH_KERNEL_END       0xFFFFFFFFFFFFFFFFULL

/** Direct physical memory map in kernel space */
#define SERAPH_PHYS_MAP_BASE    0xFFFF800000000000ULL

/** Kernel heap start */
#define SERAPH_KHEAP_BASE       0xFFFFC00000000000ULL

/** Kernel stack region */
#define SERAPH_KSTACK_BASE      0xFFFFD00000000000ULL
/**@}*/

/** @name Page Table Constants */
/**@{*/
/** Number of entries per page table (512) */
#define SERAPH_VMM_ENTRIES_PER_TABLE  512

/** Recursive mapping index (PML4[510]) */
#define SERAPH_VMM_RECURSIVE_INDEX    510

/** Page size constants */
#define SERAPH_VMM_PAGE_SIZE_4K       0x1000ULL
#define SERAPH_VMM_PAGE_SIZE_2M       0x200000ULL
#define SERAPH_VMM_PAGE_SIZE_1G       0x40000000ULL
/**@}*/

/*============================================================================
 * VMM Structure
 *============================================================================*/

/**
 * @brief Virtual Memory Manager state
 *
 * Manages the current address space's page tables.
 */
typedef struct {
    uint64_t* pml4;            /**< Virtual address of PML4 (via recursive mapping) */
    uint64_t  pml4_phys;       /**< Physical address of PML4 */
    uint64_t  recursive_index; /**< PML4 entry used for recursive mapping (default 510) */
    Seraph_PMM* pmm;           /**< Physical memory manager for allocating tables */
} Seraph_VMM;

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * @brief Initialize VMM with existing page tables
 *
 * Called after boot when page tables are already set up by the bootloader.
 * Sets up the VMM structure to use the existing tables.
 *
 * @param vmm VMM structure to initialize
 * @param pmm Physical memory manager
 * @param pml4_phys Physical address of PML4 from boot info
 */
void seraph_vmm_init(Seraph_VMM* vmm, Seraph_PMM* pmm, uint64_t pml4_phys);

/**
 * @brief Create a new address space
 *
 * Allocates a new PML4 and optionally copies kernel mappings.
 *
 * @param vmm VMM structure to initialize
 * @param pmm Physical memory manager
 * @param copy_kernel If true, copy kernel-space mappings from current PML4
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_FALSE on allocation failure
 */
Seraph_Vbit seraph_vmm_create(Seraph_VMM* vmm, Seraph_PMM* pmm, bool copy_kernel);

/**
 * @brief Destroy an address space
 *
 * Frees all page tables. Does NOT free the mapped physical pages.
 *
 * @param vmm VMM structure to destroy
 */
void seraph_vmm_destroy(Seraph_VMM* vmm);

/*============================================================================
 * Mapping Operations
 *============================================================================*/

/**
 * @brief Map a virtual address to a physical address
 *
 * Creates page table entries as needed. Fails if the virtual address
 * is already mapped to a different physical address.
 *
 * @param vmm VMM structure
 * @param virt Virtual address (will be page-aligned)
 * @param phys Physical address (will be page-aligned)
 * @param flags Page flags (SERAPH_PTE_*)
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_FALSE on failure
 */
Seraph_Vbit seraph_vmm_map(Seraph_VMM* vmm, uint64_t virt, uint64_t phys,
                           uint64_t flags);

/**
 * @brief Map a range of pages
 *
 * Maps count pages starting at virt to physical addresses starting at phys.
 *
 * @param vmm VMM structure
 * @param virt Starting virtual address
 * @param phys Starting physical address
 * @param page_count Number of pages to map
 * @param flags Page flags (SERAPH_PTE_*)
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_FALSE on failure
 */
Seraph_Vbit seraph_vmm_map_range(Seraph_VMM* vmm, uint64_t virt, uint64_t phys,
                                  uint64_t page_count, uint64_t flags);

/**
 * @brief Map a 2MB huge page
 *
 * @param vmm VMM structure
 * @param virt Virtual address (must be 2MB-aligned)
 * @param phys Physical address (must be 2MB-aligned)
 * @param flags Page flags (SERAPH_PTE_HUGE will be added)
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_FALSE on failure
 */
Seraph_Vbit seraph_vmm_map_huge_2m(Seraph_VMM* vmm, uint64_t virt, uint64_t phys,
                                    uint64_t flags);

/**
 * @brief Unmap a virtual address
 *
 * Clears the page table entry. Does NOT free the physical page.
 *
 * @param vmm VMM structure
 * @param virt Virtual address to unmap
 */
void seraph_vmm_unmap(Seraph_VMM* vmm, uint64_t virt);

/**
 * @brief Unmap a range of pages
 *
 * @param vmm VMM structure
 * @param virt Starting virtual address
 * @param page_count Number of pages to unmap
 */
void seraph_vmm_unmap_range(Seraph_VMM* vmm, uint64_t virt, uint64_t page_count);

/*============================================================================
 * Query Operations
 *============================================================================*/

/**
 * @brief Translate virtual address to physical address
 *
 * Walks the page tables to find the physical address.
 *
 * @param vmm VMM structure
 * @param virt Virtual address to translate
 * @return Physical address, or SERAPH_VOID_U64 if not mapped
 */
uint64_t seraph_vmm_virt_to_phys(const Seraph_VMM* vmm, uint64_t virt);

/**
 * @brief Check if a virtual address is mapped
 *
 * @param vmm VMM structure
 * @param virt Virtual address to check
 * @return true if mapped, false otherwise
 */
bool seraph_vmm_is_mapped(const Seraph_VMM* vmm, uint64_t virt);

/**
 * @brief Get page table entry for a virtual address
 *
 * @param vmm VMM structure
 * @param virt Virtual address
 * @return Page table entry, or 0 if not mapped
 */
uint64_t seraph_vmm_get_pte(const Seraph_VMM* vmm, uint64_t virt);

/**
 * @brief Get flags for a mapped page
 *
 * @param vmm VMM structure
 * @param virt Virtual address
 * @return Page flags, or 0 if not mapped
 */
uint64_t seraph_vmm_get_flags(const Seraph_VMM* vmm, uint64_t virt);

/*============================================================================
 * TLB Management
 *============================================================================*/

/**
 * @brief Load page tables into CR3
 *
 * Activates this address space by loading its PML4 into CR3.
 *
 * @param vmm VMM structure
 */
void seraph_vmm_activate(const Seraph_VMM* vmm);

/**
 * @brief Invalidate TLB entry for a virtual address
 *
 * @param virt Virtual address to invalidate
 */
void seraph_vmm_invlpg(uint64_t virt);

/**
 * @brief Flush entire TLB
 *
 * Reloads CR3 to flush all non-global entries.
 */
void seraph_vmm_flush_tlb(void);

/*============================================================================
 * Page Fault Handling
 *============================================================================*/

/**
 * @brief Page fault error code bits
 *
 * Note: These are also defined in interrupts.h as Seraph_PF_Error_Bits.
 * Use include guards to avoid redefinition.
 */
#ifndef SERAPH_PF_ERROR_BITS_DEFINED
#define SERAPH_PF_ERROR_BITS_DEFINED
typedef enum {
    SERAPH_PF_PRESENT     = (1 << 0),  /**< Page was present (protection fault) */
    SERAPH_PF_WRITE       = (1 << 1),  /**< Caused by write access */
    SERAPH_PF_USER        = (1 << 2),  /**< Caused by user-mode access */
    SERAPH_PF_RESERVED    = (1 << 3),  /**< Reserved bit set in page table */
    SERAPH_PF_FETCH       = (1 << 4),  /**< Caused by instruction fetch */
    SERAPH_PF_PROTECTION  = (1 << 5),  /**< Protection key violation */
    SERAPH_PF_SHADOW      = (1 << 6),  /**< Shadow stack access */
} Seraph_PageFault_Error;
#define SERAPH_PF_PK SERAPH_PF_PROTECTION
#define SERAPH_PF_SS SERAPH_PF_SHADOW
#endif /* SERAPH_PF_ERROR_BITS_DEFINED */

/**
 * @brief Handle a page fault
 *
 * Called by the page fault exception handler. May allocate new pages
 * for demand paging or report fatal errors.
 *
 * @param vmm VMM structure
 * @param fault_addr Faulting virtual address (from CR2)
 * @param error_code Error code pushed by CPU
 * @return SERAPH_VBIT_TRUE if handled, SERAPH_VBIT_FALSE if fatal
 */
Seraph_Vbit seraph_vmm_handle_page_fault(Seraph_VMM* vmm, uint64_t fault_addr,
                                          uint64_t error_code);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Extract PML4 index from virtual address
 */
static inline uint64_t seraph_vmm_pml4_index(uint64_t virt) {
    return (virt >> 39) & 0x1FF;
}

/**
 * @brief Extract PDPT index from virtual address
 */
static inline uint64_t seraph_vmm_pdpt_index(uint64_t virt) {
    return (virt >> 30) & 0x1FF;
}

/**
 * @brief Extract PD index from virtual address
 */
static inline uint64_t seraph_vmm_pd_index(uint64_t virt) {
    return (virt >> 21) & 0x1FF;
}

/**
 * @brief Extract PT index from virtual address
 */
static inline uint64_t seraph_vmm_pt_index(uint64_t virt) {
    return (virt >> 12) & 0x1FF;
}

/**
 * @brief Align address down to page boundary
 */
static inline uint64_t seraph_vmm_page_align_down(uint64_t addr) {
    return addr & ~0xFFFULL;
}

/**
 * @brief Align address up to page boundary
 */
static inline uint64_t seraph_vmm_page_align_up(uint64_t addr) {
    return (addr + 0xFFFULL) & ~0xFFFULL;
}

/**
 * @brief Check if address is canonical (valid 48-bit sign-extended)
 */
static inline bool seraph_vmm_is_canonical(uint64_t addr) {
    uint64_t top = addr >> 47;
    return top == 0 || top == 0x1FFFF;
}

/*============================================================================
 * Physical/Virtual Address Conversion
 *============================================================================*/

/**
 * @brief Convert physical address to virtual address using physical map
 *
 * The kernel maintains a direct mapping of all physical memory at
 * SERAPH_PHYS_MAP_BASE. This function converts a physical address
 * to its virtual address in that mapping.
 *
 * IMPORTANT: This only works AFTER the kernel has set up the physical
 * memory map. During early boot (before page tables are initialized),
 * identity mapping is active and physical == virtual.
 *
 * @param phys Physical address
 * @return Virtual address in the physical map region
 */
static inline void* seraph_phys_to_virt(uint64_t phys) {
    return (void*)(SERAPH_PHYS_MAP_BASE + phys);
}

/**
 * @brief Convert virtual address (in physical map) to physical address
 *
 * Inverse of seraph_phys_to_virt(). Only valid for addresses in the
 * SERAPH_PHYS_MAP_BASE region.
 *
 * @param virt Virtual address in the physical map region
 * @return Physical address
 */
static inline uint64_t seraph_virt_to_phys_direct(const void* virt) {
    return (uint64_t)virt - SERAPH_PHYS_MAP_BASE;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_VMM_H */
