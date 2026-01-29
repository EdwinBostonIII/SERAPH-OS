/**
 * @file pmm.c
 * @brief MC17: Physical Memory Manager Implementation
 *
 * Bitmap-based physical page allocator. Each bit represents one 4KB page:
 *   - Bit set (1) = page is allocated
 *   - Bit clear (0) = page is free
 *
 * The bitmap is stored in the primordial arena provided by the bootloader.
 * This ensures we can track memory before kmalloc is initialized.
 */

#include "seraph/pmm.h"
#include "seraph/void.h"
#include "seraph/early_mem.h"
#include <string.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Check if a memory region is usable after ExitBootServices
 *
 * SERAPH memory types (converted from UEFI by bootloader):
 *   - SERAPH_MEM_LOADER_CODE (1): Bootloader code, becomes free
 *   - SERAPH_MEM_LOADER_DATA (2): Bootloader data, becomes free
 *   - SERAPH_MEM_BOOT_SERVICES (3): UEFI boot services, becomes free
 *   - SERAPH_MEM_CONVENTIONAL (7): Always free
 *
 * Note: Type 4 is RUNTIME_SERVICES - must NOT be used!
 */
static inline bool pmm_is_usable_memory(uint32_t type) {
    return type == SERAPH_MEM_LOADER_CODE ||
           type == SERAPH_MEM_LOADER_DATA ||
           type == SERAPH_MEM_BOOT_SERVICES ||
           type == SERAPH_MEM_CONVENTIONAL;
}

/**
 * @brief Convert physical address to page index
 */
static inline uint64_t addr_to_page(const Seraph_PMM* pmm, uint64_t addr) {
    if (addr < pmm->base_address) return SERAPH_VOID_U64;
    return (addr - pmm->base_address) >> SERAPH_PMM_PAGE_SHIFT;
}

/**
 * @brief Convert page index to physical address
 */
static inline uint64_t page_to_addr(const Seraph_PMM* pmm, uint64_t page) {
    return pmm->base_address + (page << SERAPH_PMM_PAGE_SHIFT);
}

/**
 * @brief Get bitmap word index for a page
 */
static inline uint64_t page_to_word(uint64_t page) {
    return page / SERAPH_PMM_BITS_PER_WORD;
}

/**
 * @brief Get bit position within word for a page
 */
static inline uint64_t page_to_bit(uint64_t page) {
    return page % SERAPH_PMM_BITS_PER_WORD;
}

/**
 * @brief Set a bit in the bitmap (mark page as allocated)
 */
static inline void bitmap_set(Seraph_PMM* pmm, uint64_t page) {
    uint64_t word = page_to_word(page);
    uint64_t bit = page_to_bit(page);
    pmm->bitmap[word] |= (1ULL << bit);
}

/**
 * @brief Clear a bit in the bitmap (mark page as free)
 */
static inline void bitmap_clear(Seraph_PMM* pmm, uint64_t page) {
    uint64_t word = page_to_word(page);
    uint64_t bit = page_to_bit(page);
    pmm->bitmap[word] &= ~(1ULL << bit);
}

/**
 * @brief Test if a bit is set (page is allocated)
 */
static inline bool bitmap_test(const Seraph_PMM* pmm, uint64_t page) {
    uint64_t word = page_to_word(page);
    uint64_t bit = page_to_bit(page);
    return (pmm->bitmap[word] & (1ULL << bit)) != 0;
}

/**
 * @brief Find first clear bit in a word, starting from a given bit
 *
 * @param word The bitmap word
 * @param start Starting bit position (0-63)
 * @return Bit position of first clear bit, or 64 if none found
 */
static inline uint64_t find_first_clear_bit(uint64_t word, uint64_t start) {
    /* Create mask for bits >= start */
    uint64_t mask = ~0ULL;
    if (start > 0) {
        mask <<= start;
    }

    /* Find first clear bit */
    uint64_t inverted = ~word & mask;
    if (inverted == 0) return 64;

    /* Count trailing zeros to find the bit position */
    return __builtin_ctzll(inverted);
}

/*============================================================================
 * Initialization
 *============================================================================*/

void seraph_pmm_init(Seraph_PMM* pmm, const Seraph_BootInfo* boot_info) {
    if (!pmm || !boot_info) return;

    /* Clear PMM structure */
    memset(pmm, 0, sizeof(*pmm));

    /*------------------------------------------------------------------------
     * Step 1: Find memory range
     *------------------------------------------------------------------------*/
    uint64_t lowest_addr = SERAPH_VOID_U64;
    uint64_t highest_addr = 0;
    uint64_t total_conventional = 0;

    for (uint32_t i = 0; i < boot_info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, i);
        if (!desc) continue;

        uint64_t start = desc->phys_start;
        uint64_t end = start + desc->page_count * SERAPH_PMM_PAGE_SIZE;

        /* Track range for all memory types */
        if (start < lowest_addr) lowest_addr = start;
        if (end > highest_addr) highest_addr = end;

        /* Count usable memory (conventional + boot services after ExitBootServices) */
        if (pmm_is_usable_memory(desc->type)) {
            total_conventional += desc->page_count * SERAPH_PMM_PAGE_SIZE;
        }
    }

    /* Align to page boundaries */
    lowest_addr = lowest_addr & ~(SERAPH_PMM_PAGE_SIZE - 1);
    highest_addr = (highest_addr + SERAPH_PMM_PAGE_SIZE - 1) & ~(SERAPH_PMM_PAGE_SIZE - 1);

    /*------------------------------------------------------------------------
     * Step 2: Calculate bitmap requirements
     *------------------------------------------------------------------------*/
    uint64_t memory_range = highest_addr - lowest_addr;
    uint64_t total_pages = memory_range >> SERAPH_PMM_PAGE_SHIFT;
    uint64_t bitmap_bytes = (total_pages + 7) / 8;

    /* Round bitmap size up to 8 bytes for alignment */
    bitmap_bytes = (bitmap_bytes + 7) & ~7ULL;

    /*------------------------------------------------------------------------
     * Step 3: Place bitmap in primordial arena
     *------------------------------------------------------------------------*/
    if (bitmap_bytes > boot_info->primordial_arena_size) {
        /* Not enough space - this is a fatal error in real code */
        return;
    }

    pmm->bitmap = (uint64_t*)boot_info->primordial_arena_phys;
    pmm->bitmap_size = bitmap_bytes;
    pmm->bitmap_phys = boot_info->primordial_arena_phys;
    pmm->total_pages = total_pages;
    pmm->free_pages = 0;
    pmm->base_address = lowest_addr;
    pmm->top_address = highest_addr;
    pmm->last_alloc = 0;

    /*------------------------------------------------------------------------
     * Step 4: Initialize bitmap - mark all pages as allocated
     *------------------------------------------------------------------------*/
    memset(pmm->bitmap, 0xFF, bitmap_bytes);

    /*------------------------------------------------------------------------
     * Step 5: Free conventional memory regions
     *------------------------------------------------------------------------*/
    for (uint32_t i = 0; i < boot_info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, i);
        if (!desc) continue;

        /* Free all usable memory types (conventional + boot services) */
        if (pmm_is_usable_memory(desc->type)) {
            seraph_pmm_mark_free(pmm, desc->phys_start, desc->page_count);
        }
    }

    /*------------------------------------------------------------------------
     * Step 6: Reserve memory used by kernel and boot structures
     *------------------------------------------------------------------------*/

    /* Reserve first 1MB (legacy BIOS area) */
    if (pmm->base_address < 0x100000) {
        uint64_t pages_to_reserve = (0x100000 - pmm->base_address) >> SERAPH_PMM_PAGE_SHIFT;
        seraph_pmm_mark_allocated(pmm, pmm->base_address, pages_to_reserve);
    }

    /* Reserve kernel memory */
    if (boot_info->kernel_phys_base != 0 && boot_info->kernel_size != 0) {
        uint64_t kernel_pages = (boot_info->kernel_size + SERAPH_PMM_PAGE_SIZE - 1) >> SERAPH_PMM_PAGE_SHIFT;
        seraph_pmm_mark_allocated(pmm, boot_info->kernel_phys_base, kernel_pages);
    }

    /* Reserve stack */
    if (boot_info->stack_phys != 0 && boot_info->stack_size != 0) {
        uint64_t stack_pages = (boot_info->stack_size + SERAPH_PMM_PAGE_SIZE - 1) >> SERAPH_PMM_PAGE_SHIFT;
        seraph_pmm_mark_allocated(pmm, boot_info->stack_phys, stack_pages);
    }

    /* Reserve primordial arena (where bitmap lives) */
    if (boot_info->primordial_arena_phys != 0 && boot_info->primordial_arena_size != 0) {
        uint64_t arena_pages = (boot_info->primordial_arena_size + SERAPH_PMM_PAGE_SIZE - 1) >> SERAPH_PMM_PAGE_SHIFT;
        seraph_pmm_mark_allocated(pmm, boot_info->primordial_arena_phys, arena_pages);
    }

    /* Reserve framebuffer */
    if (boot_info->framebuffer_base != 0 && boot_info->framebuffer_size != 0) {
        uint64_t fb_pages = (boot_info->framebuffer_size + SERAPH_PMM_PAGE_SIZE - 1) >> SERAPH_PMM_PAGE_SHIFT;
        seraph_pmm_mark_allocated(pmm, boot_info->framebuffer_base, fb_pages);
    }

}

void seraph_pmm_init_manual(Seraph_PMM* pmm, uint64_t* bitmap_buffer,
                            uint64_t bitmap_size, uint64_t base_address,
                            uint64_t top_address)
{
    if (!pmm || !bitmap_buffer) return;

    pmm->bitmap = bitmap_buffer;
    pmm->bitmap_size = bitmap_size;
    pmm->bitmap_phys = (uint64_t)bitmap_buffer; /* Assume identity mapping */
    pmm->base_address = base_address;
    pmm->top_address = top_address;
    pmm->total_pages = (top_address - base_address) >> SERAPH_PMM_PAGE_SHIFT;
    pmm->free_pages = pmm->total_pages;
    pmm->last_alloc = 0;

    /* Start with all pages free */
    memset(bitmap_buffer, 0, bitmap_size);
}

/*============================================================================
 * Single Page Operations
 *============================================================================*/

uint64_t seraph_pmm_alloc_page(Seraph_PMM* pmm) {
    if (!pmm || !pmm->bitmap || pmm->free_pages == 0) {
        return SERAPH_VOID_U64;
    }

    uint64_t total_words = (pmm->total_pages + SERAPH_PMM_BITS_PER_WORD - 1) / SERAPH_PMM_BITS_PER_WORD;

    /* Start searching from last_alloc hint */
    uint64_t start_word = pmm->last_alloc;

    /* Search from hint to end */
    for (uint64_t w = start_word; w < total_words; w++) {
        uint64_t word = pmm->bitmap[w];
        if (word == ~0ULL) continue; /* All bits set - no free pages here */

        /* Find first clear bit */
        uint64_t bit = find_first_clear_bit(word, 0);
        if (bit < 64) {
            uint64_t page = w * SERAPH_PMM_BITS_PER_WORD + bit;
            if (page < pmm->total_pages) {
                bitmap_set(pmm, page);
                pmm->free_pages--;
                pmm->last_alloc = w;
                return page_to_addr(pmm, page);
            }
        }
    }

    /* Wrap around: search from beginning to hint */
    for (uint64_t w = 0; w < start_word; w++) {
        uint64_t word = pmm->bitmap[w];
        if (word == ~0ULL) continue;

        uint64_t bit = find_first_clear_bit(word, 0);
        if (bit < 64) {
            uint64_t page = w * SERAPH_PMM_BITS_PER_WORD + bit;
            if (page < pmm->total_pages) {
                bitmap_set(pmm, page);
                pmm->free_pages--;
                pmm->last_alloc = w;
                return page_to_addr(pmm, page);
            }
        }
    }

    /* No free pages found */
    return SERAPH_VOID_U64;
}

void seraph_pmm_free_page(Seraph_PMM* pmm, uint64_t phys_addr) {
    if (!pmm || !pmm->bitmap) return;

    /* Align to page boundary */
    phys_addr &= ~(SERAPH_PMM_PAGE_SIZE - 1);

    uint64_t page = addr_to_page(pmm, phys_addr);
    if (SERAPH_IS_VOID_U64(page) || page >= pmm->total_pages) return;

    /* Only free if currently allocated */
    if (bitmap_test(pmm, page)) {
        bitmap_clear(pmm, page);
        pmm->free_pages++;
    }
}

/*============================================================================
 * Contiguous Page Operations
 *============================================================================*/

uint64_t seraph_pmm_alloc_pages(Seraph_PMM* pmm, uint64_t count) {
    if (!pmm || !pmm->bitmap || count == 0 || pmm->free_pages < count) {
        return SERAPH_VOID_U64;
    }

    /* For single page, use the simpler function */
    if (count == 1) {
        return seraph_pmm_alloc_page(pmm);
    }

    /* Search for contiguous free pages */
    uint64_t run_start = 0;
    uint64_t run_length = 0;

    for (uint64_t page = 0; page < pmm->total_pages; page++) {
        if (!bitmap_test(pmm, page)) {
            /* Page is free */
            if (run_length == 0) {
                run_start = page;
            }
            run_length++;

            if (run_length >= count) {
                /* Found enough contiguous pages */
                for (uint64_t i = 0; i < count; i++) {
                    bitmap_set(pmm, run_start + i);
                }
                pmm->free_pages -= count;
                pmm->last_alloc = page_to_word(run_start);
                return page_to_addr(pmm, run_start);
            }
        } else {
            /* Page is allocated - reset run */
            run_length = 0;
        }
    }

    /* Not enough contiguous pages */
    return SERAPH_VOID_U64;
}

uint64_t seraph_pmm_alloc_pages_aligned(Seraph_PMM* pmm, uint64_t count,
                                        uint64_t align_pages)
{
    if (!pmm || !pmm->bitmap || count == 0 || align_pages == 0) {
        return SERAPH_VOID_U64;
    }

    /* Ensure align_pages is power of 2 */
    if (align_pages & (align_pages - 1)) {
        return SERAPH_VOID_U64;
    }

    if (pmm->free_pages < count) {
        return SERAPH_VOID_U64;
    }

    uint64_t align_mask = align_pages - 1;

    /* Search for aligned contiguous free pages */
    for (uint64_t page = 0; page < pmm->total_pages; page++) {
        /* Skip to next aligned page */
        if (page & align_mask) {
            page = (page + align_pages) & ~align_mask;
            if (page >= pmm->total_pages) break;
        }

        /* Check if we have enough contiguous pages starting here */
        bool all_free = true;
        for (uint64_t i = 0; i < count && all_free; i++) {
            if (page + i >= pmm->total_pages || bitmap_test(pmm, page + i)) {
                all_free = false;
            }
        }

        if (all_free) {
            /* Allocate the pages */
            for (uint64_t i = 0; i < count; i++) {
                bitmap_set(pmm, page + i);
            }
            pmm->free_pages -= count;
            pmm->last_alloc = page_to_word(page);
            return page_to_addr(pmm, page);
        }
    }

    return SERAPH_VOID_U64;
}

void seraph_pmm_free_pages(Seraph_PMM* pmm, uint64_t phys_addr, uint64_t count) {
    if (!pmm || !pmm->bitmap || count == 0) return;

    phys_addr &= ~(SERAPH_PMM_PAGE_SIZE - 1);
    uint64_t start_page = addr_to_page(pmm, phys_addr);

    if (SERAPH_IS_VOID_U64(start_page)) return;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t page = start_page + i;
        if (page >= pmm->total_pages) break;

        if (bitmap_test(pmm, page)) {
            bitmap_clear(pmm, page);
            pmm->free_pages++;
        }
    }
}

/*============================================================================
 * Query Operations
 *============================================================================*/

bool seraph_pmm_is_allocated(const Seraph_PMM* pmm, uint64_t phys_addr) {
    if (!pmm || !pmm->bitmap) return true; /* Assume allocated if invalid */

    phys_addr &= ~(SERAPH_PMM_PAGE_SIZE - 1);
    uint64_t page = addr_to_page(pmm, phys_addr);

    if (SERAPH_IS_VOID_U64(page) || page >= pmm->total_pages) {
        return true; /* Out of range = allocated */
    }

    return bitmap_test(pmm, page);
}

void seraph_pmm_mark_allocated(Seraph_PMM* pmm, uint64_t phys_addr, uint64_t count) {
    if (!pmm || !pmm->bitmap || count == 0) return;

    phys_addr &= ~(SERAPH_PMM_PAGE_SIZE - 1);
    uint64_t start_page = addr_to_page(pmm, phys_addr);

    if (SERAPH_IS_VOID_U64(start_page)) return;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t page = start_page + i;
        if (page >= pmm->total_pages) break;

        if (!bitmap_test(pmm, page)) {
            bitmap_set(pmm, page);
            pmm->free_pages--;
        }
    }
}

void seraph_pmm_mark_free(Seraph_PMM* pmm, uint64_t phys_addr, uint64_t count) {
    if (!pmm || !pmm->bitmap || count == 0) return;

    phys_addr &= ~(SERAPH_PMM_PAGE_SIZE - 1);
    uint64_t start_page = addr_to_page(pmm, phys_addr);

    if (SERAPH_IS_VOID_U64(start_page)) return;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t page = start_page + i;
        if (page >= pmm->total_pages) break;

        if (bitmap_test(pmm, page)) {
            bitmap_clear(pmm, page);
            pmm->free_pages++;
        }
    }
}

/*============================================================================
 * Debug Utilities
 *============================================================================*/

void seraph_pmm_print_stats(const Seraph_PMM* pmm) {
    if (!pmm) return;

    /* In a real kernel, this would print to console */
    /* For now, it's a placeholder */
    (void)pmm;
}

void seraph_pmm_dump_region(const Seraph_PMM* pmm, uint64_t start_page,
                            uint64_t count)
{
    if (!pmm || !pmm->bitmap) return;

    /* Placeholder for debug output */
    (void)start_page;
    (void)count;
}
