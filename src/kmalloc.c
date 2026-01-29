/**
 * @file kmalloc.c
 * @brief MC19: Kernel Memory Allocator Implementation
 *
 * Implements a slab allocator for small allocations and a page allocator
 * for large allocations.
 *
 * Slab Allocator Design:
 *   - Each slab is one 4KB page
 *   - Slabs are grouped by size class (16, 32, 64, ... 2048 bytes)
 *   - Free objects within a slab form a linked list
 *   - Slabs are linked in partial/full lists per size class
 *
 * Large Allocations:
 *   - Allocations > 2048 bytes get whole pages
 *   - A header stores the size for freeing
 */

#include "seraph/kmalloc.h"
#include "seraph/void.h"
#include <string.h>

/*============================================================================
 * Global Allocator State
 *============================================================================*/

static Seraph_KMalloc g_kmalloc = {
    .vmm = NULL,
    .pmm = NULL,
    .heap_start = SERAPH_KHEAP_BASE,
    .heap_end = SERAPH_KHEAP_BASE,
    .heap_max = SERAPH_KSTACK_BASE - 0x100000000ULL, /* Leave 4GB for stacks */
    .large_alloc_count = 0,
    .total_allocated = 0,
    .initialized = false,
};

/*============================================================================
 * Large Allocation Header
 *============================================================================*/

/**
 * @brief Header for large (page-based) allocations
 */
typedef struct {
    uint64_t magic;      /**< Magic number for validation */
    uint64_t size;       /**< Allocation size in bytes */
    uint64_t pages;      /**< Number of pages */
    uint64_t _reserved;  /**< Alignment padding */
} Seraph_LargeHeader;

#define LARGE_HEADER_MAGIC 0x4C41524745484452ULL /* "LARGEHDR" */

/*============================================================================
 * Slab Management
 *============================================================================*/

/**
 * @brief Get slab header from an object pointer
 *
 * The slab header is at the start of the page containing the object.
 */
static inline Seraph_Slab* get_slab_from_object(void* obj) {
    return (Seraph_Slab*)((uint64_t)obj & ~0xFFFULL);
}

/**
 * @brief Get start of object area in a slab
 */
static inline void* slab_object_area(Seraph_Slab* slab) {
    /* Objects start after the header, aligned */
    uint64_t header_end = (uint64_t)slab + sizeof(Seraph_Slab);
    uint64_t aligned = (header_end + 15) & ~15ULL;
    return (void*)aligned;
}

/**
 * @brief Calculate how many objects fit in a slab
 */
static inline uint16_t slab_objects_per_page(uint32_t object_size) {
    uint64_t usable = 4096 - sizeof(Seraph_Slab) - 16; /* Header + alignment */
    return (uint16_t)(usable / object_size);
}

/**
 * @brief Allocate a new slab for a size class
 *
 * @param cache The slab cache
 * @return New slab, or NULL on failure
 */
static Seraph_Slab* slab_alloc_new(Seraph_SlabCache* cache) {
    if (!g_kmalloc.pmm) return NULL;

    /* Allocate a page for the slab */
    uint64_t phys = seraph_pmm_alloc_page(g_kmalloc.pmm);
    if (SERAPH_IS_VOID_U64(phys)) {
        return NULL;
    }

    /* Map it into kernel space */
    uint64_t virt = g_kmalloc.heap_end;
    g_kmalloc.heap_end += 4096;

    Seraph_Vbit result = seraph_vmm_map(g_kmalloc.vmm, virt, phys,
                                        SERAPH_PTE_PRESENT | SERAPH_PTE_WRITABLE | SERAPH_PTE_NX);
    if (!seraph_vbit_is_true(result)) {
        seraph_pmm_free_page(g_kmalloc.pmm, phys);
        g_kmalloc.heap_end -= 4096;
        return NULL;
    }

    /* Initialize the slab header */
    Seraph_Slab* slab = (Seraph_Slab*)virt;
    memset(slab, 0, sizeof(*slab));
    slab->object_size = (uint16_t)cache->object_size;
    slab->object_count = slab_objects_per_page(cache->object_size);
    slab->free_count = slab->object_count;
    slab->flags = 0;

    /* Build free list */
    void* obj_area = slab_object_area(slab);
    Seraph_SlabFreeObject* prev = NULL;

    for (uint16_t i = 0; i < slab->object_count; i++) {
        Seraph_SlabFreeObject* obj = (Seraph_SlabFreeObject*)((uint8_t*)obj_area + i * cache->object_size);
        obj->next = NULL;

        if (prev) {
            prev->next = obj;
        } else {
            slab->free_list = obj;
        }
        prev = obj;
    }

    /* Add to cache's partial list */
    slab->next = cache->partial;
    slab->prev = NULL;
    if (cache->partial) {
        cache->partial->prev = slab;
    }
    cache->partial = slab;
    cache->slab_count++;

    return slab;
}

/**
 * @brief Allocate an object from a slab
 */
static void* slab_alloc_object(Seraph_Slab* slab) {
    if (!slab || !slab->free_list) {
        return SERAPH_VOID_PTR;
    }

    /* Pop from free list */
    Seraph_SlabFreeObject* obj = slab->free_list;
    slab->free_list = obj->next;
    slab->free_count--;

    return obj;
}

/**
 * @brief Free an object back to its slab
 */
static void slab_free_object(Seraph_Slab* slab, void* obj) {
    if (!slab || !obj) return;

    /* Push onto free list */
    Seraph_SlabFreeObject* free_obj = (Seraph_SlabFreeObject*)obj;
    free_obj->next = slab->free_list;
    slab->free_list = free_obj;
    slab->free_count++;
}

/**
 * @brief Move slab between lists based on its fullness
 */
static void slab_update_list(Seraph_SlabCache* cache, Seraph_Slab* slab) {
    /* Remove from current list */
    if (slab->prev) {
        slab->prev->next = slab->next;
    }
    if (slab->next) {
        slab->next->prev = slab->prev;
    }

    /* Update list head if needed */
    if (cache->partial == slab) {
        cache->partial = slab->next;
    }
    if (cache->full == slab) {
        cache->full = slab->next;
    }

    /* Add to appropriate list */
    if (slab->free_count == 0) {
        /* Move to full list */
        slab->next = cache->full;
        slab->prev = NULL;
        if (cache->full) {
            cache->full->prev = slab;
        }
        cache->full = slab;
    } else {
        /* Move to partial list */
        slab->next = cache->partial;
        slab->prev = NULL;
        if (cache->partial) {
            cache->partial->prev = slab;
        }
        cache->partial = slab;
    }
}

/*============================================================================
 * Initialization
 *============================================================================*/

void seraph_kmalloc_init(Seraph_VMM* vmm, Seraph_PMM* pmm) {
    if (!vmm || !pmm) return;

    g_kmalloc.vmm = vmm;
    g_kmalloc.pmm = pmm;
    g_kmalloc.heap_start = SERAPH_KHEAP_BASE;
    g_kmalloc.heap_end = SERAPH_KHEAP_BASE;
    g_kmalloc.large_alloc_count = 0;
    g_kmalloc.total_allocated = 0;

    /* Initialize slab caches for each size class */
    for (int i = 0; i < SERAPH_KMALLOC_NUM_SLABS; i++) {
        Seraph_SlabCache* cache = &g_kmalloc.caches[i];
        cache->partial = NULL;
        cache->full = NULL;
        cache->object_size = SERAPH_KMALLOC_SIZE_CLASS(i);
        cache->slab_count = 0;
        cache->alloc_count = 0;
        cache->free_count = 0;
    }

    g_kmalloc.initialized = true;
}

bool seraph_kmalloc_is_initialized(void) {
    return g_kmalloc.initialized;
}

/*============================================================================
 * Basic Allocation
 *============================================================================*/

void* seraph_kmalloc(size_t size) {
    if (!g_kmalloc.initialized || size == 0) {
        return SERAPH_VOID_PTR;
    }

    /* Try slab allocation for small sizes */
    int class_idx = seraph_kmalloc_size_class_index(size);
    if (class_idx >= 0) {
        Seraph_SlabCache* cache = &g_kmalloc.caches[class_idx];

        /* Try to find a slab with free objects */
        Seraph_Slab* slab = cache->partial;

        /* If no partial slabs, allocate a new one */
        if (!slab) {
            slab = slab_alloc_new(cache);
            if (!slab) {
                return SERAPH_VOID_PTR;
            }
        }

        /* Allocate from the slab */
        void* obj = slab_alloc_object(slab);
        if (obj == SERAPH_VOID_PTR) {
            return SERAPH_VOID_PTR;
        }

        /* Update lists if slab is now full */
        if (slab->free_count == 0) {
            slab_update_list(cache, slab);
        }

        cache->alloc_count++;
        g_kmalloc.total_allocated += cache->object_size;

        return obj;
    }

    /* Large allocation: use page allocator */
    return seraph_kmalloc_pages((size + sizeof(Seraph_LargeHeader) + 4095) / 4096);
}

void* seraph_kcalloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return SERAPH_VOID_PTR;
    }

    /* Check for overflow */
    size_t total = count * size;
    if (total / count != size) {
        return SERAPH_VOID_PTR;
    }

    void* ptr = seraph_kmalloc(total);
    if (ptr != SERAPH_VOID_PTR) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* seraph_krealloc(void* ptr, size_t new_size) {
    if (ptr == NULL || ptr == SERAPH_VOID_PTR) {
        return seraph_kmalloc(new_size);
    }

    if (new_size == 0) {
        seraph_kfree(ptr);
        return SERAPH_VOID_PTR;
    }

    /* Get current size */
    size_t old_size = seraph_kmalloc_usable_size(ptr);
    if (old_size == 0) {
        return SERAPH_VOID_PTR;
    }

    /* If new size fits in current allocation, return same pointer */
    if (new_size <= old_size) {
        return ptr;
    }

    /* Allocate new block and copy */
    void* new_ptr = seraph_kmalloc(new_size);
    if (new_ptr != SERAPH_VOID_PTR) {
        memcpy(new_ptr, ptr, old_size);
        seraph_kfree(ptr);
    }

    return new_ptr;
}

void seraph_kfree(void* ptr) {
    if (ptr == NULL || ptr == SERAPH_VOID_PTR || !g_kmalloc.initialized) {
        return;
    }

    uint64_t addr = (uint64_t)ptr;

    /* Check if it's a slab allocation */
    if (addr >= SERAPH_KHEAP_BASE && addr < g_kmalloc.heap_end) {
        /* Check for large allocation header */
        Seraph_LargeHeader* header = (Seraph_LargeHeader*)((addr & ~0xFFFULL));
        if (header->magic == LARGE_HEADER_MAGIC) {
            /* Large allocation - free pages */
            seraph_kfree_pages(header, header->pages);
            return;
        }

        /* Slab allocation */
        Seraph_Slab* slab = get_slab_from_object(ptr);
        bool was_full = (slab->free_count == 0);

        slab_free_object(slab, ptr);

        /* Update list if slab was full */
        if (was_full) {
            int class_idx = seraph_kmalloc_size_class_index(slab->object_size);
            if (class_idx >= 0) {
                Seraph_SlabCache* cache = &g_kmalloc.caches[class_idx];
                slab_update_list(cache, slab);
                cache->free_count++;
            }
        }

        g_kmalloc.total_allocated -= seraph_kmalloc_round_size(slab->object_size);
    }
}

/*============================================================================
 * Page-Aligned Allocation
 *============================================================================*/

void* seraph_kmalloc_pages(size_t page_count) {
    if (!g_kmalloc.initialized || page_count == 0) {
        return SERAPH_VOID_PTR;
    }

    /* Allocate physical pages */
    uint64_t phys = seraph_pmm_alloc_pages(g_kmalloc.pmm, page_count);
    if (SERAPH_IS_VOID_U64(phys)) {
        return SERAPH_VOID_PTR;
    }

    /* Map into kernel space */
    uint64_t virt = g_kmalloc.heap_end;
    g_kmalloc.heap_end += page_count * 4096;

    for (size_t i = 0; i < page_count; i++) {
        Seraph_Vbit result = seraph_vmm_map(g_kmalloc.vmm,
                                            virt + i * 4096,
                                            phys + i * 4096,
                                            SERAPH_PTE_PRESENT | SERAPH_PTE_WRITABLE | SERAPH_PTE_NX);
        if (!seraph_vbit_is_true(result)) {
            /* Unmap what we've mapped and free physical memory */
            for (size_t j = 0; j < i; j++) {
                seraph_vmm_unmap(g_kmalloc.vmm, virt + j * 4096);
            }
            seraph_pmm_free_pages(g_kmalloc.pmm, phys, page_count);
            g_kmalloc.heap_end -= page_count * 4096;
            return SERAPH_VOID_PTR;
        }
    }

    /* Initialize header */
    Seraph_LargeHeader* header = (Seraph_LargeHeader*)virt;
    header->magic = LARGE_HEADER_MAGIC;
    header->size = page_count * 4096 - sizeof(Seraph_LargeHeader);
    header->pages = page_count;

    g_kmalloc.large_alloc_count++;
    g_kmalloc.total_allocated += page_count * 4096;

    /* Return pointer after header */
    return (void*)(virt + sizeof(Seraph_LargeHeader));
}

void seraph_kfree_pages(void* ptr, size_t page_count) {
    if (!ptr || ptr == SERAPH_VOID_PTR || !g_kmalloc.initialized || page_count == 0) {
        return;
    }

    uint64_t virt = (uint64_t)ptr & ~0xFFFULL;

    /* Translate to physical and free */
    uint64_t phys = seraph_vmm_virt_to_phys(g_kmalloc.vmm, virt);
    if (!SERAPH_IS_VOID_U64(phys)) {
        /* Unmap pages */
        for (size_t i = 0; i < page_count; i++) {
            seraph_vmm_unmap(g_kmalloc.vmm, virt + i * 4096);
        }

        /* Free physical pages */
        seraph_pmm_free_pages(g_kmalloc.pmm, phys, page_count);

        g_kmalloc.large_alloc_count--;
        g_kmalloc.total_allocated -= page_count * 4096;
    }
}

/*============================================================================
 * Aligned Allocation
 *============================================================================*/

void* seraph_kmalloc_aligned(size_t size, size_t align) {
    if (!g_kmalloc.initialized || size == 0 || align == 0) {
        return SERAPH_VOID_PTR;
    }

    /* Ensure alignment is power of 2 */
    if (align & (align - 1)) {
        return SERAPH_VOID_PTR;
    }

    /* For small alignments, regular kmalloc is fine (always 16-byte aligned) */
    if (align <= 16) {
        return seraph_kmalloc(size);
    }

    /* For larger alignments, allocate extra space */
    size_t total = size + align + sizeof(void*);
    void* raw = seraph_kmalloc(total);
    if (raw == SERAPH_VOID_PTR) {
        return SERAPH_VOID_PTR;
    }

    /* Align the pointer */
    uint64_t addr = (uint64_t)raw + sizeof(void*);
    uint64_t aligned = (addr + align - 1) & ~(align - 1);

    /* Store original pointer before aligned memory */
    ((void**)aligned)[-1] = raw;

    return (void*)aligned;
}

void seraph_kfree_aligned(void* ptr) {
    if (!ptr || ptr == SERAPH_VOID_PTR) {
        return;
    }

    /* Retrieve original pointer */
    void* raw = ((void**)ptr)[-1];
    seraph_kfree(raw);
}

/*============================================================================
 * DMA Allocation
 *============================================================================*/

void* seraph_kmalloc_dma(size_t size, uint64_t* phys_out) {
    if (!g_kmalloc.initialized || size == 0 || !phys_out) {
        return SERAPH_VOID_PTR;
    }

    size_t page_count = (size + 4095) / 4096;

    /* Allocate contiguous physical pages */
    uint64_t phys = seraph_pmm_alloc_pages(g_kmalloc.pmm, page_count);
    if (SERAPH_IS_VOID_U64(phys)) {
        return SERAPH_VOID_PTR;
    }

    /* Map with uncached flags for DMA */
    uint64_t virt = g_kmalloc.heap_end;
    g_kmalloc.heap_end += page_count * 4096;

    for (size_t i = 0; i < page_count; i++) {
        Seraph_Vbit result = seraph_vmm_map(g_kmalloc.vmm,
                                            virt + i * 4096,
                                            phys + i * 4096,
                                            SERAPH_PTE_PRESENT | SERAPH_PTE_WRITABLE |
                                            SERAPH_PTE_NOCACHE | SERAPH_PTE_NX);
        if (!seraph_vbit_is_true(result)) {
            /* Cleanup on failure */
            for (size_t j = 0; j < i; j++) {
                seraph_vmm_unmap(g_kmalloc.vmm, virt + j * 4096);
            }
            seraph_pmm_free_pages(g_kmalloc.pmm, phys, page_count);
            g_kmalloc.heap_end -= page_count * 4096;
            return SERAPH_VOID_PTR;
        }
    }

    *phys_out = phys;
    return (void*)virt;
}

void seraph_kfree_dma(void* ptr, size_t size) {
    if (!ptr || ptr == SERAPH_VOID_PTR || size == 0) {
        return;
    }

    seraph_kfree_pages(ptr, (size + 4095) / 4096);
}

/*============================================================================
 * Statistics and Debugging
 *============================================================================*/

void seraph_kmalloc_get_stats(Seraph_KMalloc_Stats* stats) {
    if (!stats) return;

    memset(stats, 0, sizeof(*stats));

    if (!g_kmalloc.initialized) return;

    stats->total_allocated = g_kmalloc.total_allocated;
    stats->heap_used = g_kmalloc.heap_end - g_kmalloc.heap_start;
    stats->page_allocations = g_kmalloc.large_alloc_count;

    for (int i = 0; i < SERAPH_KMALLOC_NUM_SLABS; i++) {
        Seraph_SlabCache* cache = &g_kmalloc.caches[i];
        stats->slab_allocations += cache->alloc_count;
        stats->slab_frees += cache->free_count;
        stats->total_slabs += cache->slab_count;

        /* Calculate available space */
        Seraph_Slab* slab = cache->partial;
        while (slab) {
            stats->total_available += slab->free_count * slab->object_size;
            slab = slab->next;
        }
    }
}

void seraph_kmalloc_print_stats(void) {
    /* In a real kernel, this would print to console */
    /* Placeholder for now */
}

bool seraph_kmalloc_verify(void) {
    if (!g_kmalloc.initialized) {
        return false;
    }

    /* Verify slab structures */
    for (int i = 0; i < SERAPH_KMALLOC_NUM_SLABS; i++) {
        Seraph_SlabCache* cache = &g_kmalloc.caches[i];

        /* Check partial list */
        Seraph_Slab* slab = cache->partial;
        while (slab) {
            if (slab->object_size != cache->object_size) {
                return false;
            }
            if (slab->free_count == 0) {
                return false; /* Should be in full list */
            }
            slab = slab->next;
        }

        /* Check full list */
        slab = cache->full;
        while (slab) {
            if (slab->object_size != cache->object_size) {
                return false;
            }
            if (slab->free_count != 0) {
                return false; /* Should be in partial list */
            }
            slab = slab->next;
        }
    }

    return true;
}

size_t seraph_kmalloc_usable_size(void* ptr) {
    if (!ptr || ptr == SERAPH_VOID_PTR || !g_kmalloc.initialized) {
        return 0;
    }

    uint64_t addr = (uint64_t)ptr;

    /* Check if it's in our heap range */
    if (addr < SERAPH_KHEAP_BASE || addr >= g_kmalloc.heap_end) {
        return 0;
    }

    /* Check for large allocation */
    Seraph_LargeHeader* header = (Seraph_LargeHeader*)((addr & ~0xFFFULL));
    if (header->magic == LARGE_HEADER_MAGIC) {
        return header->size;
    }

    /* Slab allocation */
    Seraph_Slab* slab = get_slab_from_object(ptr);
    return slab->object_size;
}
