/**
 * @file kmalloc.h
 * @brief MC19: Kernel Memory Allocator API
 *
 * The kernel allocator provides dynamic memory allocation for the kernel.
 * It builds on top of the PMM (physical) and VMM (virtual) to provide
 * a familiar malloc-like interface.
 *
 * Design:
 *   1. SLAB ALLOCATOR: Small allocations (<= 2048 bytes) use slab caches
 *      with power-of-two size classes: 16, 32, 64, 128, 256, 512, 1024, 2048
 *   2. PAGE ALLOCATOR: Large allocations (> 2048 bytes) get whole pages
 *   3. VOID SAFETY: Returns SERAPH_VOID_PTR on failure, never crashes
 *   4. ARENA INTEGRATION: Can use Spectral Arenas for bulk allocations
 *
 * Size Classes:
 *   - 16-byte: tiny structs, nodes
 *   - 32-byte: small buffers
 *   - 64-byte: cache-line aligned objects
 *   - 128-byte: medium structs
 *   - 256-byte: medium buffers
 *   - 512-byte: larger structs
 *   - 1024-byte: large structs
 *   - 2048-byte: maximum slab size
 *
 * Thread Safety:
 *   The current implementation is NOT thread-safe. For multi-core support,
 *   per-CPU caches would be needed.
 */

#ifndef SERAPH_KMALLOC_H
#define SERAPH_KMALLOC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vmm.h"
#include "seraph/pmm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Number of slab size classes */
#define SERAPH_KMALLOC_NUM_SLABS  8

/** Minimum allocation size */
#define SERAPH_KMALLOC_MIN_SIZE   16

/** Maximum slab allocation size */
#define SERAPH_KMALLOC_MAX_SLAB   2048

/** Slab size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 */
#define SERAPH_KMALLOC_SIZE_CLASS(idx) (16 << (idx))

/*============================================================================
 * Slab Structures
 *============================================================================*/

/**
 * @brief Free object within a slab
 *
 * When an object is free, it's part of a linked list.
 */
typedef struct Seraph_SlabFreeObject {
    struct Seraph_SlabFreeObject* next;
} Seraph_SlabFreeObject;

/**
 * @brief Slab header (stored at start of each slab page)
 *
 * A slab is a single page (4KB) divided into objects of a fixed size.
 */
typedef struct Seraph_Slab {
    struct Seraph_Slab*     next;        /**< Next slab in list */
    struct Seraph_Slab*     prev;        /**< Previous slab in list */
    Seraph_SlabFreeObject*  free_list;   /**< List of free objects */
    uint16_t                object_size; /**< Size of each object */
    uint16_t                object_count;/**< Total objects in slab */
    uint16_t                free_count;  /**< Number of free objects */
    uint16_t                flags;       /**< Slab flags */
} Seraph_Slab;

/**
 * @brief Slab cache for a specific size class
 */
typedef struct {
    Seraph_Slab*  partial;       /**< Slabs with some free objects */
    Seraph_Slab*  full;          /**< Slabs with no free objects */
    uint32_t      object_size;   /**< Size of objects in this cache */
    uint32_t      slab_count;    /**< Total number of slabs */
    uint64_t      alloc_count;   /**< Total allocations made */
    uint64_t      free_count;    /**< Total frees made */
} Seraph_SlabCache;

/*============================================================================
 * Kernel Allocator State
 *============================================================================*/

/**
 * @brief Kernel allocator global state
 */
typedef struct {
    Seraph_VMM*       vmm;                            /**< Virtual memory manager */
    Seraph_PMM*       pmm;                            /**< Physical memory manager */
    Seraph_SlabCache  caches[SERAPH_KMALLOC_NUM_SLABS]; /**< Size-class caches */
    uint64_t          heap_start;                     /**< Start of kernel heap */
    uint64_t          heap_end;                       /**< Current end of heap */
    uint64_t          heap_max;                       /**< Maximum heap address */
    uint64_t          large_alloc_count;              /**< Large allocation count */
    uint64_t          total_allocated;                /**< Total bytes allocated */
    bool              initialized;                    /**< Is allocator ready? */
} Seraph_KMalloc;

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * @brief Initialize the kernel memory allocator
 *
 * Must be called after PMM and VMM are initialized.
 *
 * @param vmm Virtual memory manager
 * @param pmm Physical memory manager
 */
void seraph_kmalloc_init(Seraph_VMM* vmm, Seraph_PMM* pmm);

/**
 * @brief Check if allocator is initialized
 *
 * @return true if initialized and ready
 */
bool seraph_kmalloc_is_initialized(void);

/*============================================================================
 * Basic Allocation
 *============================================================================*/

/**
 * @brief Allocate kernel memory
 *
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or SERAPH_VOID_PTR on failure
 */
void* seraph_kmalloc(size_t size);

/**
 * @brief Allocate zeroed kernel memory
 *
 * @param count Number of elements
 * @param size Size of each element
 * @return Pointer to zeroed memory, or SERAPH_VOID_PTR on failure
 */
void* seraph_kcalloc(size_t count, size_t size);

/**
 * @brief Reallocate kernel memory
 *
 * @param ptr Pointer to existing allocation (or NULL)
 * @param new_size New size in bytes
 * @return Pointer to reallocated memory, or SERAPH_VOID_PTR on failure
 */
void* seraph_krealloc(void* ptr, size_t new_size);

/**
 * @brief Free kernel memory
 *
 * @param ptr Pointer to memory to free (NULL is safe)
 */
void seraph_kfree(void* ptr);

/*============================================================================
 * Page-Aligned Allocation
 *============================================================================*/

/**
 * @brief Allocate page-aligned memory
 *
 * Allocates whole pages for large allocations.
 *
 * @param page_count Number of 4KB pages to allocate
 * @return Pointer to allocated pages, or SERAPH_VOID_PTR on failure
 */
void* seraph_kmalloc_pages(size_t page_count);

/**
 * @brief Free page-aligned memory
 *
 * @param ptr Pointer to pages to free
 * @param page_count Number of pages (must match allocation)
 */
void seraph_kfree_pages(void* ptr, size_t page_count);

/*============================================================================
 * Aligned Allocation
 *============================================================================*/

/**
 * @brief Allocate memory with specific alignment
 *
 * @param size Number of bytes to allocate
 * @param align Alignment requirement (must be power of 2)
 * @return Pointer to aligned memory, or SERAPH_VOID_PTR on failure
 */
void* seraph_kmalloc_aligned(size_t size, size_t align);

/**
 * @brief Free aligned memory
 *
 * @param ptr Pointer returned by seraph_kmalloc_aligned
 */
void seraph_kfree_aligned(void* ptr);

/*============================================================================
 * Special Allocations
 *============================================================================*/

/**
 * @brief Allocate memory for DMA (physically contiguous, uncached)
 *
 * @param size Number of bytes
 * @param phys_out Output: physical address of allocation
 * @return Virtual address of allocation, or SERAPH_VOID_PTR on failure
 */
void* seraph_kmalloc_dma(size_t size, uint64_t* phys_out);

/**
 * @brief Free DMA memory
 *
 * @param ptr Virtual address from seraph_kmalloc_dma
 * @param size Size of allocation
 */
void seraph_kfree_dma(void* ptr, size_t size);

/*============================================================================
 * Statistics and Debugging
 *============================================================================*/

/**
 * @brief Allocator statistics
 */
typedef struct {
    uint64_t total_allocated;      /**< Total bytes currently allocated */
    uint64_t total_available;      /**< Total bytes available in slabs */
    uint64_t slab_allocations;     /**< Number of slab allocations */
    uint64_t slab_frees;           /**< Number of slab frees */
    uint64_t page_allocations;     /**< Number of page allocations */
    uint64_t page_frees;           /**< Number of page frees */
    uint64_t total_slabs;          /**< Total number of slab pages */
    uint64_t heap_used;            /**< Bytes used in heap region */
} Seraph_KMalloc_Stats;

/**
 * @brief Get allocator statistics
 *
 * @param stats Output structure to fill
 */
void seraph_kmalloc_get_stats(Seraph_KMalloc_Stats* stats);

/**
 * @brief Print allocator statistics (for debugging)
 */
void seraph_kmalloc_print_stats(void);

/**
 * @brief Verify heap integrity (for debugging)
 *
 * @return true if heap is valid, false if corruption detected
 */
bool seraph_kmalloc_verify(void);

/*============================================================================
 * Allocation Size Query
 *============================================================================*/

/**
 * @brief Get usable size of an allocation
 *
 * Returns the actual usable size of an allocation, which may be
 * larger than the requested size due to size class rounding.
 *
 * @param ptr Pointer to allocation
 * @return Usable size in bytes, or 0 if ptr is invalid
 */
size_t seraph_kmalloc_usable_size(void* ptr);

/*============================================================================
 * Internal Size Class Utilities
 *============================================================================*/

/**
 * @brief Get size class index for a given size
 *
 * @param size Requested size
 * @return Index into size class array, or -1 if too large for slabs
 */
static inline int seraph_kmalloc_size_class_index(size_t size) {
    if (size <= 16)   return 0;
    if (size <= 32)   return 1;
    if (size <= 64)   return 2;
    if (size <= 128)  return 3;
    if (size <= 256)  return 4;
    if (size <= 512)  return 5;
    if (size <= 1024) return 6;
    if (size <= 2048) return 7;
    return -1; /* Too large for slab allocation */
}

/**
 * @brief Round size up to nearest size class
 *
 * @param size Requested size
 * @return Size class size
 */
static inline size_t seraph_kmalloc_round_size(size_t size) {
    if (size <= 16)   return 16;
    if (size <= 32)   return 32;
    if (size <= 64)   return 64;
    if (size <= 128)  return 128;
    if (size <= 256)  return 256;
    if (size <= 512)  return 512;
    if (size <= 1024) return 1024;
    if (size <= 2048) return 2048;
    /* Round up to page size */
    return (size + 4095) & ~4095ULL;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_KMALLOC_H */
