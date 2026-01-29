/**
 * @file pmm.h
 * @brief MC17: Physical Memory Manager - Bitmap-based page allocator
 *
 * The Physical Memory Manager (PMM) is responsible for tracking which
 * physical pages are free or allocated. It uses a simple bitmap where
 * each bit represents one 4KB page: 0 = free, 1 = allocated.
 *
 * Design Principles:
 *   1. SIMPLICITY: Bitmap is the simplest O(n) allocator that works
 *   2. PERFORMANCE: Use uint64_t words for efficient scanning
 *   3. VOID SAFETY: All errors return SERAPH_VOID_U64
 *   4. HINT OPTIMIZATION: Track last allocation for locality
 *
 * Memory Layout:
 *   The bitmap is stored at a fixed location in the primordial arena.
 *   For 4GB RAM: 4GB / 4KB / 8 = 128KB bitmap
 *   For 64GB RAM: 64GB / 4KB / 8 = 2MB bitmap
 */

#ifndef SERAPH_PMM_H
#define SERAPH_PMM_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/boot.h"
#include "seraph/void.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Page size in bytes (4KB) */
#define SERAPH_PMM_PAGE_SIZE      4096

/** Page size shift (log2(4096) = 12) */
#define SERAPH_PMM_PAGE_SHIFT     12

/** Bits per bitmap word */
#define SERAPH_PMM_BITS_PER_WORD  64

/*============================================================================
 * PMM Structure
 *============================================================================*/

/**
 * @brief Physical Memory Manager state
 *
 * Tracks physical page allocation using a bitmap. Each bit in the bitmap
 * corresponds to one physical page (4KB). A set bit means the page is
 * allocated, a clear bit means it's free.
 */
typedef struct {
    uint64_t* bitmap;          /**< Bitmap array (bit set = page allocated) */
    uint64_t  bitmap_size;     /**< Size of bitmap in bytes */
    uint64_t  total_pages;     /**< Total number of pages managed */
    uint64_t  free_pages;      /**< Number of currently free pages */
    uint64_t  base_address;    /**< Lowest physical address managed */
    uint64_t  top_address;     /**< Highest physical address managed */
    uint64_t  last_alloc;      /**< Hint: word index of last allocation */
    uint64_t  bitmap_phys;     /**< Physical address of bitmap itself */
} Seraph_PMM;

/*============================================================================
 * PMM Initialization
 *============================================================================*/

/**
 * @brief Initialize PMM from boot memory map
 *
 * Scans the memory map from boot info to determine available RAM,
 * allocates the bitmap from the primordial arena, and marks all
 * reserved regions as allocated.
 *
 * @param pmm PMM structure to initialize
 * @param boot_info Boot information from UEFI bootloader
 */
void seraph_pmm_init(Seraph_PMM* pmm, const Seraph_BootInfo* boot_info);

/**
 * @brief Initialize PMM with explicit parameters (for testing)
 *
 * @param pmm PMM structure to initialize
 * @param bitmap_buffer Pre-allocated bitmap buffer
 * @param bitmap_size Size of bitmap in bytes
 * @param base_address Lowest physical address to manage
 * @param top_address Highest physical address to manage
 */
void seraph_pmm_init_manual(Seraph_PMM* pmm, uint64_t* bitmap_buffer,
                            uint64_t bitmap_size, uint64_t base_address,
                            uint64_t top_address);

/*============================================================================
 * Single Page Operations
 *============================================================================*/

/**
 * @brief Allocate a single physical page
 *
 * Finds and marks the first free page as allocated.
 * Uses the last_alloc hint for locality.
 *
 * @param pmm PMM structure
 * @return Physical address of allocated page, or SERAPH_VOID_U64 if no memory
 */
uint64_t seraph_pmm_alloc_page(Seraph_PMM* pmm);

/**
 * @brief Free a single physical page
 *
 * Marks the page as free. Does nothing if the page is already free
 * or outside the managed range.
 *
 * @param pmm PMM structure
 * @param phys_addr Physical address of page to free (must be page-aligned)
 */
void seraph_pmm_free_page(Seraph_PMM* pmm, uint64_t phys_addr);

/*============================================================================
 * Contiguous Page Operations
 *============================================================================*/

/**
 * @brief Allocate contiguous physical pages
 *
 * Finds and marks a contiguous range of free pages as allocated.
 * This is O(n) worst case as it may need to scan the entire bitmap.
 *
 * @param pmm PMM structure
 * @param count Number of contiguous pages needed
 * @return Physical address of first page, or SERAPH_VOID_U64 if not available
 */
uint64_t seraph_pmm_alloc_pages(Seraph_PMM* pmm, uint64_t count);

/**
 * @brief Allocate contiguous pages at specific alignment
 *
 * Like seraph_pmm_alloc_pages but the returned address will be aligned
 * to align_pages pages. Useful for 2MB huge pages (align_pages=512).
 *
 * @param pmm PMM structure
 * @param count Number of contiguous pages needed
 * @param align_pages Alignment in pages (must be power of 2)
 * @return Physical address of first page, or SERAPH_VOID_U64 if not available
 */
uint64_t seraph_pmm_alloc_pages_aligned(Seraph_PMM* pmm, uint64_t count,
                                        uint64_t align_pages);

/**
 * @brief Free contiguous physical pages
 *
 * Marks a range of pages as free.
 *
 * @param pmm PMM structure
 * @param phys_addr Physical address of first page (must be page-aligned)
 * @param count Number of pages to free
 */
void seraph_pmm_free_pages(Seraph_PMM* pmm, uint64_t phys_addr, uint64_t count);

/*============================================================================
 * Query Operations
 *============================================================================*/

/**
 * @brief Check if a page is allocated
 *
 * @param pmm PMM structure
 * @param phys_addr Physical address to check (must be page-aligned)
 * @return true if page is allocated, false if free or out of range
 */
bool seraph_pmm_is_allocated(const Seraph_PMM* pmm, uint64_t phys_addr);

/**
 * @brief Mark a range of pages as allocated
 *
 * Used during initialization to reserve memory regions.
 *
 * @param pmm PMM structure
 * @param phys_addr Physical address of first page (must be page-aligned)
 * @param count Number of pages to mark as allocated
 */
void seraph_pmm_mark_allocated(Seraph_PMM* pmm, uint64_t phys_addr, uint64_t count);

/**
 * @brief Mark a range of pages as free
 *
 * Used during initialization or for returning memory to the pool.
 *
 * @param pmm PMM structure
 * @param phys_addr Physical address of first page (must be page-aligned)
 * @param count Number of pages to mark as free
 */
void seraph_pmm_mark_free(Seraph_PMM* pmm, uint64_t phys_addr, uint64_t count);

/*============================================================================
 * Statistics
 *============================================================================*/

/**
 * @brief Get number of free pages
 *
 * @param pmm PMM structure
 * @return Number of free pages
 */
static inline uint64_t seraph_pmm_get_free_pages(const Seraph_PMM* pmm) {
    return pmm ? pmm->free_pages : 0;
}

/**
 * @brief Get total number of managed pages
 *
 * @param pmm PMM structure
 * @return Total number of pages
 */
static inline uint64_t seraph_pmm_get_total_pages(const Seraph_PMM* pmm) {
    return pmm ? pmm->total_pages : 0;
}

/**
 * @brief Get free memory in bytes
 *
 * @param pmm PMM structure
 * @return Free memory in bytes
 */
static inline uint64_t seraph_pmm_get_free_memory(const Seraph_PMM* pmm) {
    return pmm ? pmm->free_pages * SERAPH_PMM_PAGE_SIZE : 0;
}

/**
 * @brief Get total managed memory in bytes
 *
 * @param pmm PMM structure
 * @return Total managed memory in bytes
 */
static inline uint64_t seraph_pmm_get_total_memory(const Seraph_PMM* pmm) {
    return pmm ? pmm->total_pages * SERAPH_PMM_PAGE_SIZE : 0;
}

/*============================================================================
 * Debug Utilities
 *============================================================================*/

/**
 * @brief Print PMM statistics (for debugging)
 *
 * @param pmm PMM structure
 */
void seraph_pmm_print_stats(const Seraph_PMM* pmm);

/**
 * @brief Dump bitmap region (for debugging)
 *
 * @param pmm PMM structure
 * @param start_page Starting page index
 * @param count Number of pages to dump
 */
void seraph_pmm_dump_region(const Seraph_PMM* pmm, uint64_t start_page,
                            uint64_t count);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_PMM_H */
