/**
 * @file vmm.c
 * @brief MC18: Virtual Memory Manager Implementation
 *
 * Implements 4-level x86-64 paging with recursive mapping.
 *
 * Page Table Hierarchy:
 *   PML4 -> PDPT -> PD -> PT -> Page
 *
 * Recursive Mapping:
 *   PML4[510] points to PML4 itself, creating a recursive structure.
 *   This allows accessing any page table through specific virtual addresses:
 *
 *   PML4 (itself):  0xFFFFFF7FBFDFE000 + (510 * 8)
 *   PDPT[i]:        0xFFFFFF7FBFC00000 + (i * 4096)
 *   PD[i][j]:       0xFFFFFF7F80000000 + (i * 2MB) + (j * 4096)
 *   PT[i][j][k]:    0xFFFFFF0000000000 + (i * 1GB) + (j * 2MB) + (k * 4096)
 */

#include "seraph/vmm.h"
#include "seraph/void.h"
#include <string.h>

/*============================================================================
 * Recursive Mapping Constants
 *============================================================================*/

/** Recursive mapping slot in PML4 */
#define RECURSIVE_SLOT 510

/** Base address for accessing page tables through recursive mapping */
#define RECURSIVE_BASE ((uint64_t)RECURSIVE_SLOT << 39)

/** Virtual address of PML4 (via recursive mapping) */
#define PML4_VIRT_ADDR (0xFFFFFF7FBFDFE000ULL)

/*============================================================================
 * Page Table Entry Manipulation
 *============================================================================*/

/**
 * @brief Extract physical address from page table entry
 */
static inline uint64_t pte_get_addr(uint64_t pte) {
    return pte & SERAPH_PTE_ADDR_MASK;
}

/**
 * @brief Create a page table entry
 */
static inline uint64_t pte_create(uint64_t phys_addr, uint64_t flags) {
    return (phys_addr & SERAPH_PTE_ADDR_MASK) | (flags & ~SERAPH_PTE_ADDR_MASK);
}

/**
 * @brief Check if a page table entry is present
 */
static inline bool pte_is_present(uint64_t pte) {
    return (pte & SERAPH_PTE_PRESENT) != 0;
}

/**
 * @brief Check if a page table entry is a huge page
 */
static inline bool pte_is_huge(uint64_t pte) {
    return (pte & SERAPH_PTE_HUGE) != 0;
}

/*============================================================================
 * CR3/CR2 Access
 *============================================================================*/

/**
 * @brief Read CR3 (page table base)
 */
static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

/**
 * @brief Write CR3 (page table base)
 */
static inline void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

/**
 * @brief Read CR2 (faulting address)
 */
static inline uint64_t read_cr2(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

/*============================================================================
 * Internal Page Table Walking
 *============================================================================*/

/**
 * @brief Get virtual address of page table entry using recursive mapping
 *
 * For a given virtual address, returns the virtual address of its
 * page table entry at the specified level.
 *
 * @param virt Virtual address
 * @param level 4 = PML4, 3 = PDPT, 2 = PD, 1 = PT
 * @return Virtual address of the page table entry
 */
static uint64_t get_pte_virt_addr(uint64_t virt, int level) {
    uint64_t addr = virt;

    /* Extract indices */
    uint64_t pml4_idx = seraph_vmm_pml4_index(addr);
    uint64_t pdpt_idx = seraph_vmm_pdpt_index(addr);
    uint64_t pd_idx = seraph_vmm_pd_index(addr);
    uint64_t pt_idx = seraph_vmm_pt_index(addr);

    /* Build the recursive address based on level */
    uint64_t result;

    switch (level) {
        case 4: /* PML4 entry */
            result = 0xFFFFFF7FBFDFE000ULL + pml4_idx * 8;
            break;
        case 3: /* PDPT entry */
            result = 0xFFFFFF7FBFC00000ULL + pml4_idx * 4096 + pdpt_idx * 8;
            break;
        case 2: /* PD entry */
            result = 0xFFFFFF7F80000000ULL + pml4_idx * (512 * 4096) +
                     pdpt_idx * 4096 + pd_idx * 8;
            break;
        case 1: /* PT entry */
            result = 0xFFFFFF0000000000ULL + pml4_idx * (512ULL * 512 * 4096) +
                     pdpt_idx * (512 * 4096) + pd_idx * 4096 + pt_idx * 8;
            break;
        default:
            result = SERAPH_VOID_U64;
            break;
    }

    return result;
}

/**
 * @brief Allocate and zero a page table
 *
 * @param pmm Physical memory manager
 * @return Physical address of new page table, or SERAPH_VOID_U64
 */
static uint64_t alloc_page_table(Seraph_PMM* pmm) {
    uint64_t phys = seraph_pmm_alloc_page(pmm);
    if (SERAPH_IS_VOID_U64(phys)) {
        return SERAPH_VOID_U64;
    }

    /* Zero the page table using the physical memory map
     * The kernel sets up a direct mapping of all physical memory at
     * SERAPH_PHYS_MAP_BASE during early initialization. We use that
     * mapping to access the newly allocated physical page. */
    void* virt = seraph_phys_to_virt(phys);
    memset(virt, 0, 4096);

    return phys;
}

/**
 * @brief Ensure page table exists at the given level, creating if needed
 *
 * @param vmm VMM structure
 * @param virt Virtual address
 * @param level Level to ensure (3 = PDPT, 2 = PD, 1 = PT)
 * @return True if page table exists or was created
 */
static bool ensure_page_table(Seraph_VMM* vmm, uint64_t virt, int level) {
    if (level > 3) return true; /* PML4 always exists */

    /* First ensure the parent level exists */
    if (!ensure_page_table(vmm, virt, level + 1)) {
        return false;
    }

    /* Get the parent entry */
    uint64_t parent_pte_addr = get_pte_virt_addr(virt, level + 1);
    uint64_t parent_pte = *(volatile uint64_t*)parent_pte_addr;

    if (pte_is_present(parent_pte)) {
        return true; /* Already exists */
    }

    /* Allocate new page table */
    uint64_t new_pt_phys = alloc_page_table(vmm->pmm);
    if (SERAPH_IS_VOID_U64(new_pt_phys)) {
        return false;
    }

    /* Create entry with default flags */
    uint64_t flags = SERAPH_PTE_PRESENT | SERAPH_PTE_WRITABLE | SERAPH_PTE_USER;
    *(volatile uint64_t*)parent_pte_addr = pte_create(new_pt_phys, flags);

    /* Invalidate TLB */
    seraph_vmm_invlpg(virt);

    return true;
}

/*============================================================================
 * Initialization
 *============================================================================*/

void seraph_vmm_init(Seraph_VMM* vmm, Seraph_PMM* pmm, uint64_t pml4_phys) {
    if (!vmm || !pmm) return;

    vmm->pmm = pmm;
    vmm->pml4_phys = pml4_phys;
    vmm->recursive_index = RECURSIVE_SLOT;

    /* Access PML4 through recursive mapping */
    vmm->pml4 = (uint64_t*)PML4_VIRT_ADDR;
}

Seraph_Vbit seraph_vmm_create(Seraph_VMM* vmm, Seraph_PMM* pmm, bool copy_kernel) {
    if (!vmm || !pmm) return SERAPH_VBIT_VOID;

    /* Allocate PML4 */
    uint64_t pml4_phys = seraph_pmm_alloc_page(pmm);
    if (SERAPH_IS_VOID_U64(pml4_phys)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Zero the new PML4 using the physical memory map */
    uint64_t* pml4 = (uint64_t*)seraph_phys_to_virt(pml4_phys);
    memset(pml4, 0, 4096);

    /* Set up recursive mapping */
    pml4[RECURSIVE_SLOT] = pte_create(pml4_phys,
                                       SERAPH_PTE_PRESENT | SERAPH_PTE_WRITABLE);

    /* Copy kernel mappings if requested */
    if (copy_kernel) {
        uint64_t* current_pml4 = (uint64_t*)PML4_VIRT_ADDR;

        /* Copy entries for kernel space (higher half, entries 256-511) */
        for (int i = 256; i < 512; i++) {
            if (i != RECURSIVE_SLOT) {
                pml4[i] = current_pml4[i];
            }
        }
    }

    vmm->pmm = pmm;
    vmm->pml4_phys = pml4_phys;
    /* vmm->pml4 uses recursive mapping - once this address space is activated,
     * PML4_VIRT_ADDR will point to our PML4 through the recursive mapping.
     * The local 'pml4' variable (via physical map) was only for initial setup. */
    vmm->pml4 = (uint64_t*)PML4_VIRT_ADDR;
    vmm->recursive_index = RECURSIVE_SLOT;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Recursively free page tables at a given level
 *
 * @param vmm VMM structure
 * @param table_phys Physical address of the page table
 * @param level Current level (4=PML4, 3=PDPT, 2=PD, 1=PT)
 * @param is_user Only free user-space entries
 */
static void free_page_table_recursive(Seraph_VMM* vmm, uint64_t table_phys,
                                       int level, bool is_user) {
    if (SERAPH_IS_VOID_U64(table_phys)) return;
    if (level < 1) return;

    /* Access page table through physical memory map */
    uint64_t* table = (uint64_t*)seraph_phys_to_virt(table_phys);

    /* Determine entry range: for kernel-space preservation, skip higher-half entries
     * at the PML4 level (entries 256-511, except recursive slot) */
    int start = 0;
    int end = 512;
    if (level == 4 && is_user) {
        end = 256;  /* Only process user-space entries (lower half) */
    }

    for (int i = start; i < end; i++) {
        /* Skip recursive mapping slot */
        if (level == 4 && i == (int)vmm->recursive_index) continue;

        uint64_t entry = table[i];
        if (!pte_is_present(entry)) continue;

        /* If not a leaf (huge page), recurse into child table */
        if (level > 1 && !pte_is_huge(entry)) {
            uint64_t child_phys = pte_get_addr(entry);
            free_page_table_recursive(vmm, child_phys, level - 1, is_user);
        }
    }

    /* Free this page table itself (except PML4 which is freed by caller) */
    if (level < 4) {
        seraph_pmm_free_page(vmm->pmm, table_phys);
    }
}

void seraph_vmm_destroy(Seraph_VMM* vmm) {
    if (!vmm || !vmm->pmm) return;

    /* Walk and free all page tables recursively.
     * We only free user-space page tables (lower half) to preserve
     * kernel mappings that may be shared across address spaces. */
    if (vmm->pml4_phys != 0) {
        free_page_table_recursive(vmm, vmm->pml4_phys, 4, true);

        /* Finally free the PML4 itself */
        seraph_pmm_free_page(vmm->pmm, vmm->pml4_phys);
    }

    vmm->pml4 = NULL;
    vmm->pml4_phys = 0;
    vmm->pmm = NULL;
}

/*============================================================================
 * Mapping Operations
 *============================================================================*/

Seraph_Vbit seraph_vmm_map(Seraph_VMM* vmm, uint64_t virt, uint64_t phys,
                           uint64_t flags)
{
    if (!vmm || !vmm->pmm) return SERAPH_VBIT_VOID;

    /* Align addresses */
    virt = seraph_vmm_page_align_down(virt);
    phys = seraph_vmm_page_align_down(phys);

    /* Ensure canonical address */
    if (!seraph_vmm_is_canonical(virt)) {
        return SERAPH_VBIT_VOID;
    }

    /* Ensure page tables exist */
    if (!ensure_page_table(vmm, virt, 1)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Get PT entry address */
    uint64_t pte_addr = get_pte_virt_addr(virt, 1);
    uint64_t* pte = (uint64_t*)pte_addr;

    /* Check if already mapped */
    if (pte_is_present(*pte)) {
        uint64_t existing_phys = pte_get_addr(*pte);
        if (existing_phys != phys) {
            return SERAPH_VBIT_FALSE; /* Different mapping exists */
        }
        /* Same mapping, update flags */
    }

    /* Create the mapping */
    *pte = pte_create(phys, flags | SERAPH_PTE_PRESENT);

    /* Invalidate TLB entry */
    seraph_vmm_invlpg(virt);

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_vmm_map_range(Seraph_VMM* vmm, uint64_t virt, uint64_t phys,
                                  uint64_t page_count, uint64_t flags)
{
    if (!vmm || page_count == 0) return SERAPH_VBIT_VOID;

    for (uint64_t i = 0; i < page_count; i++) {
        Seraph_Vbit result = seraph_vmm_map(vmm,
                                            virt + i * SERAPH_VMM_PAGE_SIZE_4K,
                                            phys + i * SERAPH_VMM_PAGE_SIZE_4K,
                                            flags);
        if (!seraph_vbit_is_true(result)) {
            return result;
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_vmm_map_huge_2m(Seraph_VMM* vmm, uint64_t virt, uint64_t phys,
                                    uint64_t flags)
{
    if (!vmm || !vmm->pmm) return SERAPH_VBIT_VOID;

    /* Align to 2MB boundary */
    virt &= ~(SERAPH_VMM_PAGE_SIZE_2M - 1);
    phys &= ~(SERAPH_VMM_PAGE_SIZE_2M - 1);

    if (!seraph_vmm_is_canonical(virt)) {
        return SERAPH_VBIT_VOID;
    }

    /* Ensure PDPT and PD exist */
    if (!ensure_page_table(vmm, virt, 2)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Get PD entry (huge pages are at PD level) */
    uint64_t pde_addr = get_pte_virt_addr(virt, 2);
    uint64_t* pde = (uint64_t*)pde_addr;

    /* Create huge page mapping */
    *pde = pte_create(phys, flags | SERAPH_PTE_PRESENT | SERAPH_PTE_HUGE);

    /* Invalidate TLB */
    seraph_vmm_invlpg(virt);

    return SERAPH_VBIT_TRUE;
}

void seraph_vmm_unmap(Seraph_VMM* vmm, uint64_t virt) {
    if (!vmm) return;

    virt = seraph_vmm_page_align_down(virt);

    if (!seraph_vmm_is_canonical(virt)) return;

    /* Get PT entry */
    uint64_t pte_addr = get_pte_virt_addr(virt, 1);
    uint64_t* pte = (uint64_t*)pte_addr;

    /* Clear the entry */
    *pte = 0;

    /* Invalidate TLB */
    seraph_vmm_invlpg(virt);
}

void seraph_vmm_unmap_range(Seraph_VMM* vmm, uint64_t virt, uint64_t page_count) {
    if (!vmm || page_count == 0) return;

    for (uint64_t i = 0; i < page_count; i++) {
        seraph_vmm_unmap(vmm, virt + i * SERAPH_VMM_PAGE_SIZE_4K);
    }
}

/*============================================================================
 * Query Operations
 *============================================================================*/

uint64_t seraph_vmm_virt_to_phys(const Seraph_VMM* vmm, uint64_t virt) {
    if (!vmm) return SERAPH_VOID_U64;

    if (!seraph_vmm_is_canonical(virt)) {
        return SERAPH_VOID_U64;
    }

    /* Walk page tables */
    uint64_t pml4_entry_addr = get_pte_virt_addr(virt, 4);
    uint64_t pml4_entry = *(volatile uint64_t*)pml4_entry_addr;
    if (!pte_is_present(pml4_entry)) return SERAPH_VOID_U64;

    uint64_t pdpt_entry_addr = get_pte_virt_addr(virt, 3);
    uint64_t pdpt_entry = *(volatile uint64_t*)pdpt_entry_addr;
    if (!pte_is_present(pdpt_entry)) return SERAPH_VOID_U64;

    /* Check for 1GB huge page */
    if (pte_is_huge(pdpt_entry)) {
        uint64_t phys_base = pte_get_addr(pdpt_entry) & ~(SERAPH_VMM_PAGE_SIZE_1G - 1);
        uint64_t offset = virt & (SERAPH_VMM_PAGE_SIZE_1G - 1);
        return phys_base | offset;
    }

    uint64_t pd_entry_addr = get_pte_virt_addr(virt, 2);
    uint64_t pd_entry = *(volatile uint64_t*)pd_entry_addr;
    if (!pte_is_present(pd_entry)) return SERAPH_VOID_U64;

    /* Check for 2MB huge page */
    if (pte_is_huge(pd_entry)) {
        uint64_t phys_base = pte_get_addr(pd_entry) & ~(SERAPH_VMM_PAGE_SIZE_2M - 1);
        uint64_t offset = virt & (SERAPH_VMM_PAGE_SIZE_2M - 1);
        return phys_base | offset;
    }

    uint64_t pt_entry_addr = get_pte_virt_addr(virt, 1);
    uint64_t pt_entry = *(volatile uint64_t*)pt_entry_addr;
    if (!pte_is_present(pt_entry)) return SERAPH_VOID_U64;

    uint64_t phys_base = pte_get_addr(pt_entry);
    uint64_t offset = virt & (SERAPH_VMM_PAGE_SIZE_4K - 1);
    return phys_base | offset;
}

bool seraph_vmm_is_mapped(const Seraph_VMM* vmm, uint64_t virt) {
    return !SERAPH_IS_VOID_U64(seraph_vmm_virt_to_phys(vmm, virt));
}

uint64_t seraph_vmm_get_pte(const Seraph_VMM* vmm, uint64_t virt) {
    if (!vmm || !seraph_vmm_is_canonical(virt)) return 0;

    uint64_t pte_addr = get_pte_virt_addr(virt, 1);
    return *(volatile uint64_t*)pte_addr;
}

uint64_t seraph_vmm_get_flags(const Seraph_VMM* vmm, uint64_t virt) {
    uint64_t pte = seraph_vmm_get_pte(vmm, virt);
    return pte & ~SERAPH_PTE_ADDR_MASK;
}

/*============================================================================
 * TLB Management
 *============================================================================*/

void seraph_vmm_activate(const Seraph_VMM* vmm) {
    if (!vmm) return;
    write_cr3(vmm->pml4_phys);
}

void seraph_vmm_invlpg(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void seraph_vmm_flush_tlb(void) {
    uint64_t cr3 = read_cr3();
    write_cr3(cr3);
}

/*============================================================================
 * Page Fault Handling
 *============================================================================*/

/**
 * @brief Page fault error code bits
 */
#define PF_PRESENT  (1 << 0)  /**< Page was present (protection violation) */
#define PF_WRITE    (1 << 1)  /**< Write access caused fault */
#define PF_USER     (1 << 2)  /**< Fault occurred in user mode */
#define PF_RESERVED (1 << 3)  /**< Reserved bit was set in PTE */
#define PF_INSTR    (1 << 4)  /**< Instruction fetch caused fault */

Seraph_Vbit seraph_vmm_handle_page_fault(Seraph_VMM* vmm, uint64_t fault_addr,
                                          uint64_t error_code)
{
    if (!vmm || !vmm->pmm) return SERAPH_VBIT_VOID;

    /* Align fault address to page boundary */
    uint64_t page_addr = seraph_vmm_page_align_down(fault_addr);

    /* Check if address is canonical */
    if (!seraph_vmm_is_canonical(fault_addr)) {
        return SERAPH_VBIT_FALSE;  /* Invalid address - cannot handle */
    }

    /* Get current PTE */
    uint64_t pte = seraph_vmm_get_pte(vmm, page_addr);

    /*
     * Case 1: Page not present - Demand paging
     *
     * For demand paging, we'd need a VMA (virtual memory area) structure
     * tracking which regions should be backed. For now, we don't support
     * automatic demand paging - pages must be explicitly mapped.
     */
    if (!(error_code & PF_PRESENT)) {
        /* Page not present and not mapped - this is a true fault */
        return SERAPH_VBIT_FALSE;
    }

    /*
     * Case 2: Write to read-only page - possible Copy-on-Write (CoW)
     *
     * CoW pages are marked read-only but have a special flag indicating
     * they should be copied on write. We use the AVL bits (bits 9-11) for this.
     */
    #define PTE_COW (1ULL << 9)  /* Using AVL bit 9 for CoW marker */

    if ((error_code & PF_WRITE) && pte_is_present(pte)) {
        /* Check if this is a CoW page */
        if (pte & PTE_COW) {
            /* Get the physical address of the original page */
            uint64_t old_phys = pte_get_addr(pte);

            /* Allocate a new physical page */
            uint64_t new_phys = seraph_pmm_alloc_page(vmm->pmm);
            if (SERAPH_IS_VOID_U64(new_phys)) {
                return SERAPH_VBIT_FALSE;  /* Out of memory */
            }

            /* Copy the page contents */
            void* old_virt = seraph_phys_to_virt(old_phys);
            void* new_virt = seraph_phys_to_virt(new_phys);
            memcpy(new_virt, old_virt, SERAPH_VMM_PAGE_SIZE_4K);

            /* Update mapping: new page, writable, CoW flag cleared */
            uint64_t new_flags = (pte & ~SERAPH_PTE_ADDR_MASK & ~PTE_COW) |
                                 SERAPH_PTE_WRITABLE;
            uint64_t pte_vaddr = get_pte_virt_addr(page_addr, 1);
            *(volatile uint64_t*)pte_vaddr = pte_create(new_phys, new_flags);

            /* Invalidate TLB entry */
            seraph_vmm_invlpg(page_addr);

            return SERAPH_VBIT_TRUE;  /* CoW handled successfully */
        }

        /* Write to read-only page without CoW - protection violation */
        return SERAPH_VBIT_FALSE;
    }

    /*
     * Case 3: Reserved bit set - hardware error
     */
    if (error_code & PF_RESERVED) {
        return SERAPH_VBIT_FALSE;  /* Hardware error, cannot handle */
    }

    /*
     * Case 4: Instruction fetch on non-executable page
     */
    if (error_code & PF_INSTR) {
        /* Could implement W^X enforcement here */
        return SERAPH_VBIT_FALSE;
    }

    /* Unknown fault type - cannot handle */
    return SERAPH_VBIT_FALSE;
}
