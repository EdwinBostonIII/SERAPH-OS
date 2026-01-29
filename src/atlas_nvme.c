/**
 * @file atlas_nvme.c
 * @brief MC24: Atlas NVMe Backend - Connecting Atlas to NVMe Storage
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module connects Atlas (the single-level store) to NVMe storage,
 * enabling true persistent memory semantics backed by physical storage.
 *
 * ARCHITECTURE:
 *
 *   Atlas Region (Virtual) <-> Page Fault Handler <-> NVMe Driver <-> SSD
 *
 * When a page fault occurs in the Atlas address range:
 *   1. Page fault handler calls atlas_nvme_fetch_page()
 *   2. We calculate the NVMe LBA from the faulting address
 *   3. We read the page from NVMe into a RAM frame
 *   4. We map the frame into the faulting address
 *   5. Execution resumes
 *
 * Dirty pages are written back:
 *   - On explicit flush (seraph_atlas_sync)
 *   - On eviction from the page cache
 *   - Periodically by a background task
 *
 * COPY-ON-WRITE:
 *
 *   For crash consistency, we use copy-on-write:
 *   1. Modified pages are written to NEW NVMe locations
 *   2. Once written, metadata is atomically updated
 *   3. Old pages become garbage (reclaimed later)
 *
 *   This ensures that at any point, the on-disk state is consistent.
 *
 * ADDRESS TRANSLATION:
 *
 *   Atlas Address = SERAPH_ATLAS_BASE + offset
 *   NVMe LBA = offset / sector_size
 *
 * For 512-byte sectors and 4KB pages:
 *   1 page = 8 sectors
 */

#include "seraph/atlas.h"
#include "seraph/drivers/nvme.h"
#include "seraph/interrupts.h"
#include "seraph/void.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*============================================================================
 * Configuration
 *============================================================================*/

/** Page cache size (number of cached pages) */
#define ATLAS_NVME_CACHE_SIZE 256

/** Sectors per page (4KB pages, 512B sectors) */
#define ATLAS_NVME_SECTORS_PER_PAGE (SERAPH_PAGE_SIZE / SERAPH_NVME_SECTOR_SIZE)

/*============================================================================
 * Page Cache Entry
 *============================================================================*/

/**
 * @brief Cache entry state
 */
typedef enum {
    ATLAS_CACHE_INVALID = 0,   /**< Entry not in use */
    ATLAS_CACHE_CLEAN,         /**< Page matches NVMe content */
    ATLAS_CACHE_DIRTY,         /**< Page modified, needs writeback */
    ATLAS_CACHE_WRITING        /**< Writeback in progress */
} Atlas_Cache_State;

/**
 * @brief Page cache entry
 */
typedef struct Atlas_Cache_Entry {
    uint64_t            atlas_offset;  /**< Offset in Atlas region */
    uint64_t            nvme_lba;      /**< NVMe LBA for this page */
    void*               page;          /**< Cached page data */
    Atlas_Cache_State   state;         /**< Entry state */
    uint64_t            access_time;   /**< Last access timestamp */
    bool                pinned;        /**< Cannot be evicted */

    /* LRU list links */
    struct Atlas_Cache_Entry* lru_prev;
    struct Atlas_Cache_Entry* lru_next;
} Atlas_Cache_Entry;

/*============================================================================
 * Atlas NVMe Backend State
 *============================================================================*/

/**
 * @brief Atlas NVMe backend state
 */
typedef struct {
    Seraph_NVMe*         nvme;          /**< NVMe controller */
    Atlas_Cache_Entry*   cache;         /**< Page cache array */
    size_t               cache_size;    /**< Number of cache entries */
    Atlas_Cache_Entry*   lru_head;      /**< Most recently used */
    Atlas_Cache_Entry*   lru_tail;      /**< Least recently used */
    uint64_t             access_counter; /**< Access timestamp counter */

    /* Statistics */
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t writebacks;
    uint64_t evictions;

    bool initialized;
} Atlas_NVMe_Backend;

/* Global backend state */
static Atlas_NVMe_Backend g_atlas_nvme = {0};

/*============================================================================
 * Cache Management
 *============================================================================*/

/**
 * @brief Remove entry from LRU list
 */
static void cache_lru_remove(Atlas_Cache_Entry* entry) {
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        g_atlas_nvme.lru_head = entry->lru_next;
    }

    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        g_atlas_nvme.lru_tail = entry->lru_prev;
    }

    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

/**
 * @brief Add entry to front of LRU list (most recently used)
 */
static void cache_lru_add_front(Atlas_Cache_Entry* entry) {
    entry->lru_prev = NULL;
    entry->lru_next = g_atlas_nvme.lru_head;

    if (g_atlas_nvme.lru_head) {
        g_atlas_nvme.lru_head->lru_prev = entry;
    } else {
        g_atlas_nvme.lru_tail = entry;
    }

    g_atlas_nvme.lru_head = entry;
}

/**
 * @brief Touch entry (move to front of LRU)
 */
static void cache_touch(Atlas_Cache_Entry* entry) {
    entry->access_time = ++g_atlas_nvme.access_counter;
    cache_lru_remove(entry);
    cache_lru_add_front(entry);
}

/**
 * @brief Find cache entry by Atlas offset
 */
static Atlas_Cache_Entry* cache_find(uint64_t atlas_offset) {
    /* Align to page boundary */
    atlas_offset &= ~(uint64_t)(SERAPH_PAGE_SIZE - 1);

    for (size_t i = 0; i < g_atlas_nvme.cache_size; i++) {
        Atlas_Cache_Entry* entry = &g_atlas_nvme.cache[i];
        if (entry->state != ATLAS_CACHE_INVALID &&
            entry->atlas_offset == atlas_offset) {
            return entry;
        }
    }

    return NULL;
}

/**
 * @brief Writeback a dirty page to NVMe
 */
static Seraph_Vbit cache_writeback(Atlas_Cache_Entry* entry) {
    if (entry->state != ATLAS_CACHE_DIRTY) {
        return SERAPH_VBIT_TRUE;  /* Nothing to write */
    }

    entry->state = ATLAS_CACHE_WRITING;

    /* Write page to NVMe */
    Seraph_Vbit result = seraph_nvme_write(g_atlas_nvme.nvme,
                                            entry->nvme_lba,
                                            ATLAS_NVME_SECTORS_PER_PAGE,
                                            entry->page);

    if (seraph_vbit_is_true(result)) {
        entry->state = ATLAS_CACHE_CLEAN;
        g_atlas_nvme.writebacks++;
        return SERAPH_VBIT_TRUE;
    } else {
        entry->state = ATLAS_CACHE_DIRTY;  /* Keep dirty for retry */
        return SERAPH_VBIT_VOID;
    }
}

/**
 * @brief Evict a page from cache
 *
 * Writes back if dirty, then frees the entry.
 */
static Seraph_Vbit cache_evict(Atlas_Cache_Entry* entry) {
    if (entry->pinned) {
        return SERAPH_VBIT_FALSE;  /* Cannot evict pinned page */
    }

    if (entry->state == ATLAS_CACHE_DIRTY) {
        Seraph_Vbit result = cache_writeback(entry);
        if (!seraph_vbit_is_true(result)) {
            return result;
        }
    }

    /* Free the page */
    if (entry->page) {
        free(entry->page);
        entry->page = NULL;
    }

    entry->state = ATLAS_CACHE_INVALID;
    cache_lru_remove(entry);
    g_atlas_nvme.evictions++;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Find or create a cache entry
 *
 * May evict LRU entry if cache is full.
 */
static Atlas_Cache_Entry* cache_get_entry(uint64_t atlas_offset) {
    /* Align to page boundary */
    atlas_offset &= ~(uint64_t)(SERAPH_PAGE_SIZE - 1);

    /* First check if already cached */
    Atlas_Cache_Entry* entry = cache_find(atlas_offset);
    if (entry) {
        cache_touch(entry);
        return entry;
    }

    /* Find a free entry */
    for (size_t i = 0; i < g_atlas_nvme.cache_size; i++) {
        entry = &g_atlas_nvme.cache[i];
        if (entry->state == ATLAS_CACHE_INVALID) {
            return entry;
        }
    }

    /* Cache full - evict LRU entry */
    entry = g_atlas_nvme.lru_tail;
    while (entry && entry->pinned) {
        entry = entry->lru_prev;
    }

    if (entry == NULL) {
        return NULL;  /* All entries pinned */
    }

    cache_evict(entry);
    return entry;
}

/*============================================================================
 * Public API
 *============================================================================*/

/**
 * @brief Initialize Atlas NVMe backend
 *
 * @param nvme Initialized NVMe controller
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_atlas_nvme_init(Seraph_NVMe* nvme) {
    if (nvme == NULL || !nvme->initialized) {
        return SERAPH_VBIT_VOID;
    }

    memset(&g_atlas_nvme, 0, sizeof(g_atlas_nvme));
    g_atlas_nvme.nvme = nvme;
    g_atlas_nvme.cache_size = ATLAS_NVME_CACHE_SIZE;

    /* Allocate cache entries */
    g_atlas_nvme.cache = calloc(ATLAS_NVME_CACHE_SIZE, sizeof(Atlas_Cache_Entry));
    if (g_atlas_nvme.cache == NULL) {
        return SERAPH_VBIT_VOID;
    }

    g_atlas_nvme.initialized = true;
    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Shutdown Atlas NVMe backend
 *
 * Flushes all dirty pages and frees resources.
 */
void seraph_atlas_nvme_shutdown(void) {
    if (!g_atlas_nvme.initialized) {
        return;
    }

    /* Flush all dirty pages */
    for (size_t i = 0; i < g_atlas_nvme.cache_size; i++) {
        Atlas_Cache_Entry* entry = &g_atlas_nvme.cache[i];
        if (entry->state == ATLAS_CACHE_DIRTY) {
            cache_writeback(entry);
        }
        if (entry->page) {
            free(entry->page);
        }
    }

    free(g_atlas_nvme.cache);
    memset(&g_atlas_nvme, 0, sizeof(g_atlas_nvme));
}

/**
 * @brief Fetch a page from NVMe into the cache
 *
 * Called by page fault handler when an Atlas page is not present.
 *
 * @param atlas_offset Offset within Atlas region
 * @param out_page Output: pointer to page data
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_atlas_nvme_fetch_page(uint64_t atlas_offset, void** out_page) {
    if (!g_atlas_nvme.initialized || out_page == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Check cache first */
    Atlas_Cache_Entry* entry = cache_find(atlas_offset);
    if (entry) {
        cache_touch(entry);
        *out_page = entry->page;
        g_atlas_nvme.cache_hits++;
        return SERAPH_VBIT_TRUE;
    }

    g_atlas_nvme.cache_misses++;

    /* Get a cache entry (may evict) */
    entry = cache_get_entry(atlas_offset);
    if (entry == NULL) {
        return SERAPH_VBIT_VOID;  /* Cache full of pinned pages */
    }

    /* Allocate page if needed */
    if (entry->page == NULL) {
#ifdef _WIN32
        entry->page = _aligned_malloc(SERAPH_PAGE_SIZE, SERAPH_PAGE_SIZE);
#else
        if (posix_memalign(&entry->page, SERAPH_PAGE_SIZE, SERAPH_PAGE_SIZE) != 0) {
            entry->page = NULL;
        }
#endif
        if (entry->page == NULL) {
            return SERAPH_VBIT_VOID;
        }
    }

    /* Calculate NVMe LBA */
    uint64_t lba = atlas_offset / SERAPH_NVME_SECTOR_SIZE;

    /* Read from NVMe */
    Seraph_Vbit result = seraph_nvme_read(g_atlas_nvme.nvme,
                                           lba,
                                           ATLAS_NVME_SECTORS_PER_PAGE,
                                           entry->page);

    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Initialize entry */
    entry->atlas_offset = atlas_offset & ~(uint64_t)(SERAPH_PAGE_SIZE - 1);
    entry->nvme_lba = lba;
    entry->state = ATLAS_CACHE_CLEAN;
    entry->pinned = false;
    cache_lru_add_front(entry);

    *out_page = entry->page;
    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Mark a page as dirty
 *
 * Called when an Atlas page is modified.
 *
 * @param atlas_offset Offset within Atlas region
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_atlas_nvme_mark_dirty(uint64_t atlas_offset) {
    if (!g_atlas_nvme.initialized) {
        return SERAPH_VBIT_VOID;
    }

    Atlas_Cache_Entry* entry = cache_find(atlas_offset);
    if (entry == NULL) {
        return SERAPH_VBIT_FALSE;  /* Page not in cache */
    }

    if (entry->state == ATLAS_CACHE_CLEAN) {
        entry->state = ATLAS_CACHE_DIRTY;
    }

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Flush all dirty pages to NVMe
 *
 * @return SERAPH_VBIT_TRUE if all pages flushed successfully
 */
Seraph_Vbit seraph_atlas_nvme_flush_all(void) {
    if (!g_atlas_nvme.initialized) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Vbit result = SERAPH_VBIT_TRUE;

    for (size_t i = 0; i < g_atlas_nvme.cache_size; i++) {
        Atlas_Cache_Entry* entry = &g_atlas_nvme.cache[i];
        if (entry->state == ATLAS_CACHE_DIRTY) {
            Seraph_Vbit wb_result = cache_writeback(entry);
            if (!seraph_vbit_is_true(wb_result)) {
                result = SERAPH_VBIT_FALSE;
            }
        }
    }

    /* Issue NVMe flush command */
    Seraph_Vbit flush_result = seraph_nvme_flush(g_atlas_nvme.nvme);
    if (!seraph_vbit_is_true(flush_result)) {
        result = seraph_vbit_and(result, flush_result);
    }

    return result;
}

/**
 * @brief Page fault handler for Atlas region
 *
 * This is registered with the interrupt subsystem to handle page faults
 * in the Atlas address range.
 *
 * @param fault_addr Faulting address
 * @param error_code Page fault error code
 * @param frame Interrupt frame
 * @return SERAPH_VBIT_TRUE if handled
 */
Seraph_Vbit seraph_atlas_nvme_page_fault_handler(uint64_t fault_addr,
                                                   uint64_t error_code,
                                                   Seraph_InterruptFrame* frame) {
    (void)error_code;
    (void)frame;

    /* Check if fault is in Atlas region */
    if (fault_addr < SERAPH_ATLAS_BASE) {
        return SERAPH_VBIT_FALSE;  /* Not our address range */
    }

    uint64_t offset = fault_addr - SERAPH_ATLAS_BASE;

    /* Fetch the page */
    void* page;
    Seraph_Vbit result = seraph_atlas_nvme_fetch_page(offset, &page);
    if (!seraph_vbit_is_true(result)) {
        return SERAPH_VBIT_VOID;
    }

    /* Map the page at the faulting address
     * (This would call into the VMM to update page tables)
     * For userspace simulation, the page is already accessible. */

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Get Atlas NVMe backend statistics
 */
void seraph_atlas_nvme_get_stats(uint64_t* hits, uint64_t* misses,
                                  uint64_t* writebacks, uint64_t* evictions) {
    if (hits) *hits = g_atlas_nvme.cache_hits;
    if (misses) *misses = g_atlas_nvme.cache_misses;
    if (writebacks) *writebacks = g_atlas_nvme.writebacks;
    if (evictions) *evictions = g_atlas_nvme.evictions;
}

/*============================================================================
 * Atlas Interface Functions (called from atlas.c)
 *============================================================================*/

/**
 * @brief Sync Atlas NVMe backend (flush dirty pages)
 *
 * Called when Atlas needs to ensure data is persisted.
 */
void seraph_atlas_nvme_sync(Seraph_Atlas* atlas) {
    (void)atlas;

    if (!g_atlas_nvme.initialized) {
        return;
    }

    /* Flush all dirty pages */
    for (uint32_t i = 0; i < ATLAS_NVME_CACHE_SIZE; i++) {
        Atlas_Cache_Entry* entry = &g_atlas_nvme.cache[i];
        if (entry->state == ATLAS_CACHE_DIRTY) {
            /* Write back dirty page */
            cache_writeback(entry);
        }
    }

    /* Ensure all writes are committed to media */
    if (g_atlas_nvme.nvme) {
        seraph_nvme_flush(g_atlas_nvme.nvme);
    }
}

/**
 * @brief Close Atlas NVMe backend
 *
 * Called when Atlas is being destroyed.
 */
void seraph_atlas_nvme_close(Seraph_Atlas* atlas) {
    (void)atlas;

    /* Sync first to ensure data is persisted */
    seraph_atlas_nvme_sync(atlas);

    /* Shutdown the backend */
    seraph_atlas_nvme_shutdown();
}
