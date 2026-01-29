/**
 * @file aether.c
 * @brief MC28: Aether - Distributed Shared Memory Implementation
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This implementation provides a userspace simulation of Aether's
 * distributed shared memory system. In a real kernel implementation,
 * this would integrate with the page fault handler for transparent
 * remote memory access.
 *
 * For userspace testing, we simulate:
 * - Multiple nodes as separate memory regions
 * - Network fetches as direct memory copies with optional failure injection
 * - Coherence protocol with directory-based tracking
 * - Global generations for cluster-wide revocation
 */

#include "seraph/aether.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Thread-Local VOID Context
 *============================================================================*/

#if defined(_MSC_VER)
    #define THREAD_LOCAL __declspec(thread)
#else
    #define THREAD_LOCAL _Thread_local
#endif

/** Thread-local VOID reason */
static THREAD_LOCAL Seraph_Aether_Void_Reason g_void_reason = SERAPH_AETHER_VOID_NONE;

/** Thread-local VOID address */
static THREAD_LOCAL uint64_t g_void_addr = 0;

/**
 * @brief Set VOID context for current thread
 */
static void set_void_context(Seraph_Aether_Void_Reason reason, uint64_t addr) {
    g_void_reason = reason;
    g_void_addr = addr;
}

Seraph_Aether_Void_Reason seraph_aether_get_void_reason(void) {
    return g_void_reason;
}

uint64_t seraph_aether_get_void_addr(void) {
    return g_void_addr;
}

void seraph_aether_clear_void_context(void) {
    g_void_reason = SERAPH_AETHER_VOID_NONE;
    g_void_addr = 0;
}

/*============================================================================
 * Internal Helper Functions
 *============================================================================*/

/**
 * @brief Find simulated node by ID
 */
static Seraph_Aether_Sim_Node* find_sim_node(Seraph_Aether* aether, uint16_t node_id) {
    if (aether == NULL || aether->sim_nodes == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < aether->sim_node_count; i++) {
        if (aether->sim_nodes[i].node_id == node_id) {
            return &aether->sim_nodes[i];
        }
    }
    return NULL;
}

/**
 * @brief Get local memory pointer for an offset on a simulated node
 */
static void* get_sim_memory(Seraph_Aether_Sim_Node* node, uint64_t offset, size_t size) {
    if (node == NULL || node->memory == NULL) {
        return NULL;
    }
    if (offset + size > node->memory_size) {
        return NULL;
    }
    return (uint8_t*)node->memory + offset;
}

/**
 * @brief Initialize cache structure
 */
static Seraph_Vbit init_cache(Seraph_Aether_Cache* cache, size_t capacity) {
    if (cache == NULL) {
        return SERAPH_VBIT_VOID;
    }

    cache->entries = (Seraph_Aether_Cache_Entry*)calloc(
        capacity, sizeof(Seraph_Aether_Cache_Entry));
    if (cache->entries == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    cache->capacity = capacity;
    cache->count = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Destroy cache structure
 */
static void destroy_cache(Seraph_Aether_Cache* cache) {
    if (cache == NULL) {
        return;
    }

    /* Free cached pages and their vector clocks */
    if (cache->entries != NULL) {
        for (size_t i = 0; i < cache->capacity; i++) {
            if (cache->entries[i].valid) {
                if (cache->entries[i].local_page != NULL) {
                    free(cache->entries[i].local_page);
                }
                /* Destroy the page's vector clock */
                seraph_sparse_vclock_destroy(&cache->entries[i].vclock);
            }
        }
        free(cache->entries);
    }

    cache->entries = NULL;
    cache->capacity = 0;
    cache->count = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
}

/**
 * @brief Move cache entry to front of LRU list
 */
static void cache_lru_touch(Seraph_Aether_Cache* cache, Seraph_Aether_Cache_Entry* entry) {
    if (cache == NULL || entry == NULL) {
        return;
    }

    /* Already at head? */
    if (cache->lru_head == entry) {
        return;
    }

    /* Remove from current position */
    if (entry->lru_prev != NULL) {
        entry->lru_prev->lru_next = entry->lru_next;
    }
    if (entry->lru_next != NULL) {
        entry->lru_next->lru_prev = entry->lru_prev;
    }
    if (cache->lru_tail == entry) {
        cache->lru_tail = entry->lru_prev;
    }

    /* Insert at head */
    entry->lru_prev = NULL;
    entry->lru_next = cache->lru_head;
    if (cache->lru_head != NULL) {
        cache->lru_head->lru_prev = entry;
    }
    cache->lru_head = entry;

    if (cache->lru_tail == NULL) {
        cache->lru_tail = entry;
    }
}

/**
 * @brief Remove entry from LRU list
 */
static void cache_lru_remove(Seraph_Aether_Cache* cache, Seraph_Aether_Cache_Entry* entry) {
    if (cache == NULL || entry == NULL) {
        return;
    }

    if (entry->lru_prev != NULL) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        cache->lru_head = entry->lru_next;
    }

    if (entry->lru_next != NULL) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        cache->lru_tail = entry->lru_prev;
    }

    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

/**
 * @brief Find empty cache slot or evict LRU
 */
static Seraph_Aether_Cache_Entry* cache_find_slot(Seraph_Aether_Cache* cache) {
    if (cache == NULL || cache->entries == NULL) {
        return NULL;
    }

    /* Find empty slot */
    for (size_t i = 0; i < cache->capacity; i++) {
        if (!cache->entries[i].valid) {
            return &cache->entries[i];
        }
    }

    /* Evict LRU entry */
    Seraph_Aether_Cache_Entry* victim = cache->lru_tail;
    if (victim != NULL) {
        cache_lru_remove(cache, victim);
        if (victim->local_page != NULL) {
            free(victim->local_page);
            victim->local_page = NULL;
        }
        victim->valid = false;
        cache->count--;
        return victim;
    }

    return NULL;
}

/**
 * @brief Initialize directory for a simulated node
 */
static Seraph_Vbit init_directory(Seraph_Aether_Sim_Node* node, size_t capacity) {
    if (node == NULL) {
        return SERAPH_VBIT_VOID;
    }

    node->directory = (Seraph_Aether_Directory_Entry*)calloc(
        capacity, sizeof(Seraph_Aether_Directory_Entry));
    if (node->directory == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    node->directory_capacity = capacity;
    node->directory_count = 0;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Destroy directory
 */
static void destroy_directory(Seraph_Aether_Sim_Node* node) {
    if (node == NULL) {
        return;
    }
    if (node->directory != NULL) {
        free(node->directory);
        node->directory = NULL;
    }
    node->directory_capacity = 0;
    node->directory_count = 0;
}

/*============================================================================
 * Initialization API
 *============================================================================*/

Seraph_Vbit seraph_aether_init(
    Seraph_Aether* aether,
    uint16_t node_id,
    uint16_t node_count
) {
    if (aether == NULL) {
        return SERAPH_VBIT_VOID;
    }

    memset(aether, 0, sizeof(Seraph_Aether));

    aether->local_node_id = node_id;
    aether->node_count = node_count;

    /* Initialize cache */
    Seraph_Vbit result = init_cache(&aether->cache, SERAPH_AETHER_MAX_CACHE_ENTRIES);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Allocate simulated nodes array */
    aether->sim_nodes = (Seraph_Aether_Sim_Node*)calloc(
        SERAPH_AETHER_MAX_SIM_NODES, sizeof(Seraph_Aether_Sim_Node));
    if (aether->sim_nodes == NULL) {
        destroy_cache(&aether->cache);
        return SERAPH_VBIT_FALSE;
    }
    aether->sim_node_count = 0;

    aether->initialized = true;
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_aether_init_default(Seraph_Aether* aether) {
    return seraph_aether_init(aether, 0, 1);
}

void seraph_aether_destroy(Seraph_Aether* aether) {
    if (aether == NULL) {
        return;
    }

    /* Destroy cache (including vector clocks in cache entries) */
    destroy_cache(&aether->cache);

    /* Destroy simulated nodes */
    if (aether->sim_nodes != NULL) {
        for (size_t i = 0; i < aether->sim_node_count; i++) {
            if (aether->sim_nodes[i].memory != NULL) {
                free(aether->sim_nodes[i].memory);
            }
            /* Destroy node's vector clock */
            seraph_sparse_vclock_destroy(&aether->sim_nodes[i].vclock);
            destroy_directory(&aether->sim_nodes[i]);
        }
        free(aether->sim_nodes);
    }

    memset(aether, 0, sizeof(Seraph_Aether));
}

uint16_t seraph_aether_get_local_node_id(const Seraph_Aether* aether) {
    if (aether == NULL) {
        return 0xFFFF;  /* VOID node ID */
    }
    return aether->local_node_id;
}

bool seraph_aether_is_local(const Seraph_Aether* aether, uint64_t addr) {
    if (aether == NULL) {
        return false;
    }
    if (!seraph_aether_is_aether_addr(addr)) {
        return false;
    }
    return seraph_aether_get_node(addr) == aether->local_node_id;
}

/*============================================================================
 * Simulated Node Management
 *============================================================================*/

Seraph_Vbit seraph_aether_add_sim_node(
    Seraph_Aether* aether,
    uint16_t node_id,
    size_t memory_size
) {
    if (aether == NULL || !aether->initialized) {
        return SERAPH_VBIT_VOID;
    }

    if (aether->sim_node_count >= SERAPH_AETHER_MAX_SIM_NODES) {
        return SERAPH_VBIT_FALSE;
    }

    /* Check if node already exists */
    if (find_sim_node(aether, node_id) != NULL) {
        return SERAPH_VBIT_FALSE;  /* Already exists */
    }

    Seraph_Aether_Sim_Node* node = &aether->sim_nodes[aether->sim_node_count];
    node->node_id = node_id;
    node->memory_size = memory_size;
    node->next_alloc_offset = 0;
    node->generation = 1;  /* Start at generation 1 */
    node->online = true;
    node->injected_failure = SERAPH_AETHER_VOID_NONE;

    /* Allocate memory for this node */
    node->memory = calloc(1, memory_size);
    if (node->memory == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    /* Initialize node's vector clock for causality tracking */
    Seraph_Vbit vclock_result = seraph_sparse_vclock_init(&node->vclock, node_id);
    if (!seraph_vbit_is_true(vclock_result)) {
        free(node->memory);
        node->memory = NULL;
        return vclock_result;
    }

    /* Initialize directory */
    Seraph_Vbit result = init_directory(node, 256);
    if (!seraph_vbit_is_true(result)) {
        seraph_sparse_vclock_destroy(&node->vclock);
        free(node->memory);
        node->memory = NULL;
        return result;
    }

    aether->sim_node_count++;
    return SERAPH_VBIT_TRUE;
}

void seraph_aether_set_node_online(
    Seraph_Aether* aether,
    uint16_t node_id,
    bool online
) {
    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node != NULL) {
        node->online = online;
    }
}

void seraph_aether_inject_failure(
    Seraph_Aether* aether,
    uint16_t node_id,
    Seraph_Aether_Void_Reason reason
) {
    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node != NULL) {
        node->injected_failure = reason;
    }
}

void seraph_aether_clear_failure(
    Seraph_Aether* aether,
    uint16_t node_id
) {
    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node != NULL) {
        node->injected_failure = SERAPH_AETHER_VOID_NONE;
    }
}

/*============================================================================
 * Memory Operations
 *============================================================================*/

uint64_t seraph_aether_alloc(Seraph_Aether* aether, size_t size) {
    return seraph_aether_alloc_on_node(aether, aether->local_node_id, size);
}

uint64_t seraph_aether_alloc_on_node(
    Seraph_Aether* aether,
    uint16_t node_id,
    size_t size
) {
    if (aether == NULL || !aether->initialized || size == 0) {
        return SERAPH_VOID_U64;
    }

    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node == NULL || !node->online) {
        set_void_context(SERAPH_AETHER_VOID_UNREACHABLE,
                         seraph_aether_make_addr(node_id, 0));
        return SERAPH_VOID_U64;
    }

    /* Page-align the size */
    size_t aligned_size = (size + SERAPH_AETHER_PAGE_SIZE - 1) &
                          ~((size_t)(SERAPH_AETHER_PAGE_SIZE - 1));

    /* Check if we have space */
    if (node->next_alloc_offset + aligned_size > node->memory_size) {
        set_void_context(SERAPH_AETHER_VOID_NONE,
                         seraph_aether_make_addr(node_id, node->next_alloc_offset));
        return SERAPH_VOID_U64;  /* Out of memory */
    }

    uint64_t offset = node->next_alloc_offset;
    node->next_alloc_offset += aligned_size;

    return seraph_aether_make_addr(node_id, offset);
}

void seraph_aether_free(Seraph_Aether* aether, uint64_t addr, size_t size) {
    /* In this simple implementation, we don't actually free.
     * A real implementation would use a proper allocator with free lists. */
    (void)aether;
    (void)addr;
    (void)size;
}

/**
 * @brief Fetch page from simulated remote node
 */
static Seraph_Aether_Fetch_Result fetch_from_sim_node(
    Seraph_Aether* aether,
    uint16_t node_id,
    uint64_t offset
) {
    Seraph_Aether_Fetch_Result result;
    memset(&result, 0, sizeof(result));

    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node == NULL) {
        result.status = SERAPH_AETHER_UNREACHABLE;
        result.reason = SERAPH_AETHER_VOID_UNREACHABLE;
        return result;
    }

    /* Check for injected failure */
    if (node->injected_failure != SERAPH_AETHER_VOID_NONE) {
        result.status = SERAPH_AETHER_REMOTE_ERROR;
        result.reason = node->injected_failure;
        return result;
    }

    /* Check if node is online */
    if (!node->online) {
        result.status = SERAPH_AETHER_UNREACHABLE;
        result.reason = SERAPH_AETHER_VOID_NODE_CRASHED;
        return result;
    }

    /* Get source memory */
    uint64_t page_offset = seraph_aether_page_align(offset);
    void* src = get_sim_memory(node, page_offset, SERAPH_AETHER_PAGE_SIZE);
    if (src == NULL) {
        result.status = SERAPH_AETHER_NOT_FOUND;
        result.reason = SERAPH_AETHER_VOID_NONE;
        return result;
    }

    /* Allocate local page copy */
    void* local_page = malloc(SERAPH_AETHER_PAGE_SIZE);
    if (local_page == NULL) {
        result.status = SERAPH_AETHER_OOM;
        result.reason = SERAPH_AETHER_VOID_NONE;
        return result;
    }

    /* Copy data */
    memcpy(local_page, src, SERAPH_AETHER_PAGE_SIZE);

    result.status = SERAPH_AETHER_OK;
    result.page = local_page;
    result.generation = node->generation;
    result.reason = SERAPH_AETHER_VOID_NONE;

    aether->remote_fetches++;

    return result;
}

Seraph_Aether_Fetch_Result seraph_aether_read(
    Seraph_Aether* aether,
    uint64_t addr,
    void* dest,
    size_t size
) {
    Seraph_Aether_Fetch_Result result;
    memset(&result, 0, sizeof(result));

    if (aether == NULL || !aether->initialized || dest == NULL || size == 0) {
        result.status = SERAPH_AETHER_INVALID_ADDR;
        return result;
    }

    if (!seraph_aether_is_aether_addr(addr)) {
        result.status = SERAPH_AETHER_INVALID_ADDR;
        return result;
    }

    uint16_t node_id = seraph_aether_get_node(addr);
    uint64_t offset = seraph_aether_get_offset(addr);
    uint64_t page_addr = seraph_aether_page_align(addr);
    uint64_t page_off = seraph_aether_page_offset(addr);

    /* Check cache first */
    Seraph_Aether_Cache_Entry* cached = seraph_aether_cache_lookup(aether, addr);
    if (cached != NULL && cached->valid) {
        /* Cache hit */
        aether->cache_hits++;
        cache_lru_touch(&aether->cache, cached);

        /* Copy from cached page */
        uint8_t* src = (uint8_t*)cached->local_page + page_off;
        size_t copy_size = size;
        if (page_off + size > SERAPH_AETHER_PAGE_SIZE) {
            copy_size = SERAPH_AETHER_PAGE_SIZE - page_off;
        }
        memcpy(dest, src, copy_size);

        result.status = SERAPH_AETHER_OK;
        result.generation = cached->generation;
        return result;
    }

    /* Cache miss */
    aether->cache_misses++;

    /* Check if local node */
    if (node_id == aether->local_node_id) {
        Seraph_Aether_Sim_Node* local_node = find_sim_node(aether, node_id);
        if (local_node != NULL) {
            void* src = get_sim_memory(local_node, offset, size);
            if (src != NULL) {
                memcpy(dest, src, size);
                result.status = SERAPH_AETHER_OK;
                result.generation = local_node->generation;
                return result;
            }
        }
    }

    /* Fetch from remote */
    Seraph_Aether_Fetch_Result fetch_result = fetch_from_sim_node(
        aether, node_id, offset);

    if (fetch_result.status != SERAPH_AETHER_OK) {
        set_void_context(fetch_result.reason, addr);
        return fetch_result;
    }

    /* Insert into cache */
    seraph_aether_cache_insert(
        aether, page_addr, fetch_result.page,
        node_id, fetch_result.generation);

    /* Copy requested data */
    uint8_t* src = (uint8_t*)fetch_result.page + page_off;
    size_t copy_size = size;
    if (page_off + size > SERAPH_AETHER_PAGE_SIZE) {
        copy_size = SERAPH_AETHER_PAGE_SIZE - page_off;
    }
    memcpy(dest, src, copy_size);

    result.status = SERAPH_AETHER_OK;
    result.generation = fetch_result.generation;
    return result;
}

Seraph_Aether_Fetch_Result seraph_aether_write(
    Seraph_Aether* aether,
    uint64_t addr,
    const void* src,
    size_t size
) {
    Seraph_Aether_Fetch_Result result;
    memset(&result, 0, sizeof(result));

    if (aether == NULL || !aether->initialized || src == NULL || size == 0) {
        result.status = SERAPH_AETHER_INVALID_ADDR;
        return result;
    }

    if (!seraph_aether_is_aether_addr(addr)) {
        result.status = SERAPH_AETHER_INVALID_ADDR;
        return result;
    }

    uint16_t node_id = seraph_aether_get_node(addr);
    uint64_t offset = seraph_aether_get_offset(addr);

    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node == NULL) {
        result.status = SERAPH_AETHER_UNREACHABLE;
        result.reason = SERAPH_AETHER_VOID_UNREACHABLE;
        set_void_context(SERAPH_AETHER_VOID_UNREACHABLE, addr);
        return result;
    }

    /* Check for injected failure */
    if (node->injected_failure != SERAPH_AETHER_VOID_NONE) {
        result.status = SERAPH_AETHER_REMOTE_ERROR;
        result.reason = node->injected_failure;
        set_void_context(node->injected_failure, addr);
        return result;
    }

    if (!node->online) {
        result.status = SERAPH_AETHER_UNREACHABLE;
        result.reason = SERAPH_AETHER_VOID_NODE_CRASHED;
        set_void_context(SERAPH_AETHER_VOID_NODE_CRASHED, addr);
        return result;
    }

    /* Get destination memory */
    void* dest = get_sim_memory(node, offset, size);
    if (dest == NULL) {
        result.status = SERAPH_AETHER_NOT_FOUND;
        return result;
    }

    /* Invalidate any cached copies (coherence) */
    seraph_aether_broadcast_invalidation(aether, offset, node->generation + 1);

    /* Write to node memory */
    memcpy(dest, src, size);

    /* Increment generation on write */
    node->generation++;

    /* Increment node's vector clock on write (causality tracking) */
    seraph_sparse_vclock_increment(&node->vclock);

    /* Update local cache if present */
    Seraph_Aether_Cache_Entry* cached = seraph_aether_cache_lookup(aether, addr);
    if (cached != NULL && cached->valid) {
        uint64_t page_off = seraph_aether_page_offset(addr);
        memcpy((uint8_t*)cached->local_page + page_off, src, size);
        cached->dirty = true;
        cached->generation = node->generation;
        /* Update cache entry's vector clock */
        seraph_sparse_vclock_increment(&cached->vclock);
    }

    result.status = SERAPH_AETHER_OK;
    result.generation = node->generation;
    /* Copy the node's current vector clock to result */
    seraph_sparse_vclock_copy(&result.vclock, &node->vclock);
    return result;
}

Seraph_Vbit seraph_aether_read_vbit(
    Seraph_Aether* aether,
    uint64_t addr,
    void* dest,
    size_t size
) {
    Seraph_Aether_Fetch_Result result = seraph_aether_read(aether, addr, dest, size);
    if (result.status == SERAPH_AETHER_OK) {
        return SERAPH_VBIT_TRUE;
    }
    return SERAPH_VBIT_VOID;
}

Seraph_Vbit seraph_aether_write_vbit(
    Seraph_Aether* aether,
    uint64_t addr,
    const void* src,
    size_t size
) {
    Seraph_Aether_Fetch_Result result = seraph_aether_write(aether, addr, src, size);
    if (result.status == SERAPH_AETHER_OK) {
        return SERAPH_VBIT_TRUE;
    }
    return SERAPH_VBIT_VOID;
}

/*============================================================================
 * Cache Operations
 *============================================================================*/

Seraph_Aether_Cache_Entry* seraph_aether_cache_lookup(
    Seraph_Aether* aether,
    uint64_t addr
) {
    if (aether == NULL || aether->cache.entries == NULL) {
        return NULL;
    }

    uint64_t page_addr = seraph_aether_page_align(addr);

    for (size_t i = 0; i < aether->cache.capacity; i++) {
        if (aether->cache.entries[i].valid &&
            aether->cache.entries[i].aether_addr == page_addr) {
            return &aether->cache.entries[i];
        }
    }

    return NULL;
}

Seraph_Aether_Cache_Entry* seraph_aether_cache_insert(
    Seraph_Aether* aether,
    uint64_t addr,
    void* page,
    uint16_t owner_node,
    uint64_t generation
) {
    if (aether == NULL || page == NULL) {
        return NULL;
    }

    uint64_t page_addr = seraph_aether_page_align(addr);

    /* Check if already cached */
    Seraph_Aether_Cache_Entry* existing = seraph_aether_cache_lookup(aether, addr);
    if (existing != NULL) {
        /* Update existing entry */
        if (existing->local_page != page) {
            free(existing->local_page);
            existing->local_page = page;
        }
        existing->generation = generation;
        existing->fetch_time = 0;
        existing->dirty = false;
        /* Keep existing vclock, just update it on next operation */
        cache_lru_touch(&aether->cache, existing);
        return existing;
    }

    /* Find or create slot */
    Seraph_Aether_Cache_Entry* slot = cache_find_slot(&aether->cache);
    if (slot == NULL) {
        free(page);
        return NULL;
    }

    slot->aether_addr = page_addr;
    slot->local_page = page;
    slot->owner_node = owner_node;
    slot->generation = generation;
    slot->fetch_time = 0;
    slot->dirty = false;
    slot->valid = true;

    /* Initialize vector clock for this cached page */
    if (!seraph_vbit_is_true(seraph_sparse_vclock_init(&slot->vclock, aether->local_node_id))) {
        free(page);
        slot->valid = false;
        return NULL;
    }

    aether->cache.count++;
    cache_lru_touch(&aether->cache, slot);

    return slot;
}

void seraph_aether_cache_invalidate(Seraph_Aether* aether, uint64_t addr) {
    if (aether == NULL) {
        return;
    }

    Seraph_Aether_Cache_Entry* entry = seraph_aether_cache_lookup(aether, addr);
    if (entry != NULL && entry->valid) {
        cache_lru_remove(&aether->cache, entry);
        if (entry->local_page != NULL) {
            free(entry->local_page);
            entry->local_page = NULL;
        }
        /* Destroy the page's vector clock */
        seraph_sparse_vclock_destroy(&entry->vclock);
        entry->valid = false;
        aether->cache.count--;
        aether->invalidations_received++;
    }
}

uint32_t seraph_aether_cache_flush(Seraph_Aether* aether) {
    if (aether == NULL || aether->cache.entries == NULL) {
        return 0;
    }

    uint32_t flushed = 0;

    for (size_t i = 0; i < aether->cache.capacity; i++) {
        Seraph_Aether_Cache_Entry* entry = &aether->cache.entries[i];
        if (entry->valid && entry->dirty) {
            /* Write dirty page back to owner */
            Seraph_Aether_Sim_Node* owner = find_sim_node(aether, entry->owner_node);
            if (owner != NULL && owner->online) {
                uint64_t offset = seraph_aether_get_offset(entry->aether_addr);
                void* dest = get_sim_memory(owner, offset, SERAPH_AETHER_PAGE_SIZE);
                if (dest != NULL) {
                    memcpy(dest, entry->local_page, SERAPH_AETHER_PAGE_SIZE);
                    entry->dirty = false;
                    flushed++;
                }
            }
        }
    }

    return flushed;
}

void seraph_aether_cache_clear(Seraph_Aether* aether) {
    if (aether == NULL || aether->cache.entries == NULL) {
        return;
    }

    for (size_t i = 0; i < aether->cache.capacity; i++) {
        if (aether->cache.entries[i].valid) {
            if (aether->cache.entries[i].local_page != NULL) {
                free(aether->cache.entries[i].local_page);
                aether->cache.entries[i].local_page = NULL;
            }
            aether->cache.entries[i].valid = false;
        }
    }

    aether->cache.count = 0;
    aether->cache.lru_head = NULL;
    aether->cache.lru_tail = NULL;
}

void seraph_aether_cache_stats(
    const Seraph_Aether* aether,
    uint64_t* hits,
    uint64_t* misses
) {
    if (aether == NULL) {
        if (hits) *hits = 0;
        if (misses) *misses = 0;
        return;
    }
    if (hits) *hits = aether->cache_hits;
    if (misses) *misses = aether->cache_misses;
}

/*============================================================================
 * Generation and Revocation
 *============================================================================*/

uint64_t seraph_aether_get_generation(Seraph_Aether* aether, uint64_t addr) {
    if (aether == NULL || !seraph_aether_is_aether_addr(addr)) {
        return SERAPH_VOID_U64;
    }

    uint16_t node_id = seraph_aether_get_node(addr);
    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node == NULL) {
        return SERAPH_VOID_U64;
    }

    return node->generation;
}

Seraph_Vbit seraph_aether_check_generation(
    Seraph_Aether* aether,
    uint64_t addr,
    uint64_t expected_gen
) {
    uint64_t current_gen = seraph_aether_get_generation(aether, addr);
    if (current_gen == SERAPH_VOID_U64) {
        return SERAPH_VBIT_VOID;
    }

    /* Unpack the expected global generation */
    Seraph_Aether_Global_Gen expected = seraph_aether_unpack_global_gen(expected_gen);

    /* Check node matches */
    uint16_t node_id = seraph_aether_get_node(addr);
    if (expected.node_id != node_id) {
        return SERAPH_VBIT_FALSE;
    }

    /* Check generation */
    if (expected.local_gen != current_gen) {
        set_void_context(SERAPH_AETHER_VOID_GENERATION, addr);
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_aether_revoke(Seraph_Aether* aether, uint64_t addr) {
    if (aether == NULL || !seraph_aether_is_aether_addr(addr)) {
        return SERAPH_VBIT_VOID;
    }

    uint16_t node_id = seraph_aether_get_node(addr);
    uint64_t offset = seraph_aether_get_offset(addr);

    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node == NULL || !node->online) {
        set_void_context(SERAPH_AETHER_VOID_UNREACHABLE, addr);
        return SERAPH_VBIT_VOID;
    }

    /* Increment generation (revoke all capabilities) */
    node->generation++;

    /* Broadcast invalidation to all cachers */
    seraph_aether_broadcast_invalidation(aether, offset, node->generation);

    return SERAPH_VBIT_TRUE;
}

uint64_t seraph_aether_get_global_gen(Seraph_Aether* aether, uint64_t addr) {
    if (aether == NULL || !seraph_aether_is_aether_addr(addr)) {
        return SERAPH_VOID_U64;
    }

    uint16_t node_id = seraph_aether_get_node(addr);
    uint64_t local_gen = seraph_aether_get_generation(aether, addr);
    if (local_gen == SERAPH_VOID_U64) {
        return SERAPH_VOID_U64;
    }

    return seraph_aether_pack_global_gen(node_id, local_gen);
}

/*============================================================================
 * Coherence Protocol
 *============================================================================*/

Seraph_Aether_Response seraph_aether_handle_read_request(
    Seraph_Aether* aether,
    uint16_t requester_node,
    uint64_t offset
) {
    Seraph_Aether_Response resp;
    memset(&resp, 0, sizeof(resp));

    if (aether == NULL) {
        resp.status = SERAPH_AETHER_RESP_ERROR;
        return resp;
    }

    Seraph_Aether_Sim_Node* local_node = find_sim_node(aether, aether->local_node_id);
    if (local_node == NULL) {
        resp.status = SERAPH_AETHER_RESP_ERROR;
        return resp;
    }

    /* Get or create directory entry */
    Seraph_Aether_Directory_Entry* entry = seraph_aether_get_directory_entry(
        aether, aether->local_node_id, offset);

    if (entry == NULL) {
        resp.status = SERAPH_AETHER_RESP_NOT_FOUND;
        return resp;
    }

    /* Add requester to sharers */
    seraph_aether_directory_add_sharer(entry, requester_node);
    entry->state = SERAPH_AETHER_PAGE_SHARED;

    /* Get page data */
    void* page_data = get_sim_memory(local_node, offset, SERAPH_AETHER_PAGE_SIZE);

    resp.status = SERAPH_AETHER_RESP_OK;
    resp.page_data = page_data;
    resp.data_size = SERAPH_AETHER_PAGE_SIZE;
    resp.generation = local_node->generation;

    return resp;
}

Seraph_Aether_Response seraph_aether_handle_write_request(
    Seraph_Aether* aether,
    uint16_t requester_node,
    uint64_t offset,
    const void* data,
    size_t size
) {
    Seraph_Aether_Response resp;
    memset(&resp, 0, sizeof(resp));

    if (aether == NULL || data == NULL) {
        resp.status = SERAPH_AETHER_RESP_ERROR;
        return resp;
    }

    Seraph_Aether_Sim_Node* local_node = find_sim_node(aether, aether->local_node_id);
    if (local_node == NULL) {
        resp.status = SERAPH_AETHER_RESP_ERROR;
        return resp;
    }

    /* Get directory entry */
    Seraph_Aether_Directory_Entry* entry = seraph_aether_get_directory_entry(
        aether, aether->local_node_id, offset);

    if (entry != NULL) {
        /* Invalidate all current sharers */
        for (uint16_t i = 0; i < entry->sharer_count; i++) {
            if (entry->sharers[i] != requester_node) {
                /* Would send invalidation message in real implementation */
                aether->invalidations_sent++;
            }
        }
        entry->sharer_count = 0;
        entry->state = SERAPH_AETHER_PAGE_EXCLUSIVE;
        entry->exclusive_owner = requester_node;
    }

    /* Apply the write */
    void* dest = get_sim_memory(local_node, offset, size);
    if (dest != NULL) {
        memcpy(dest, data, size);
    }

    /* Increment generation */
    local_node->generation++;

    resp.status = SERAPH_AETHER_RESP_OK;
    resp.generation = local_node->generation;

    return resp;
}

void seraph_aether_handle_invalidate(
    Seraph_Aether* aether,
    uint64_t addr,
    uint64_t new_generation
) {
    if (aether == NULL) {
        return;
    }

    /* Invalidate our cached copy */
    seraph_aether_cache_invalidate(aether, addr);

    (void)new_generation;  /* Would update expected generation */
}

void seraph_aether_broadcast_invalidation(
    Seraph_Aether* aether,
    uint64_t offset,
    uint64_t new_generation
) {
    if (aether == NULL) {
        return;
    }

    /* In simulation, we directly invalidate all node caches */
    uint64_t addr = seraph_aether_make_addr(aether->local_node_id, offset);

    /* For each cached copy not on owner, invalidate */
    for (size_t i = 0; i < aether->cache.capacity; i++) {
        Seraph_Aether_Cache_Entry* entry = &aether->cache.entries[i];
        if (entry->valid) {
            uint64_t cached_offset = seraph_aether_get_offset(entry->aether_addr);
            if (cached_offset == offset && entry->owner_node == aether->local_node_id) {
                /* Update generation instead of invalidating for local cache */
                entry->generation = new_generation;
            }
        }
    }

    aether->invalidations_sent++;
    (void)addr;
}

/*============================================================================
 * Directory Operations
 *============================================================================*/

Seraph_Aether_Directory_Entry* seraph_aether_get_directory_entry(
    Seraph_Aether* aether,
    uint16_t node_id,
    uint64_t offset
) {
    if (aether == NULL) {
        return NULL;
    }

    Seraph_Aether_Sim_Node* node = find_sim_node(aether, node_id);
    if (node == NULL || node->directory == NULL) {
        return NULL;
    }

    uint64_t page_offset = seraph_aether_page_align(offset);

    /* Find existing entry */
    for (size_t i = 0; i < node->directory_count; i++) {
        if (node->directory[i].valid && node->directory[i].offset == page_offset) {
            return &node->directory[i];
        }
    }

    /* Create new entry if space available */
    if (node->directory_count < node->directory_capacity) {
        Seraph_Aether_Directory_Entry* entry = &node->directory[node->directory_count++];
        entry->offset = page_offset;
        entry->state = SERAPH_AETHER_PAGE_INVALID;
        entry->exclusive_owner = 0;
        entry->sharer_count = 0;
        entry->generation = node->generation;
        entry->valid = true;
        return entry;
    }

    return NULL;
}

void seraph_aether_directory_add_sharer(
    Seraph_Aether_Directory_Entry* entry,
    uint16_t node_id
) {
    if (entry == NULL) {
        return;
    }

    /* Check if already a sharer */
    for (uint16_t i = 0; i < entry->sharer_count; i++) {
        if (entry->sharers[i] == node_id) {
            return;  /* Already sharing */
        }
    }

    /* Add to sharers list */
    if (entry->sharer_count < SERAPH_AETHER_MAX_SHARERS) {
        entry->sharers[entry->sharer_count++] = node_id;
    }
}

void seraph_aether_directory_remove_sharer(
    Seraph_Aether_Directory_Entry* entry,
    uint16_t node_id
) {
    if (entry == NULL) {
        return;
    }

    for (uint16_t i = 0; i < entry->sharer_count; i++) {
        if (entry->sharers[i] == node_id) {
            /* Remove by shifting remaining entries */
            for (uint16_t j = i; j < entry->sharer_count - 1; j++) {
                entry->sharers[j] = entry->sharers[j + 1];
            }
            entry->sharer_count--;
            return;
        }
    }
}

/*============================================================================
 * Statistics
 *============================================================================*/

void seraph_aether_get_stats(
    const Seraph_Aether* aether,
    uint64_t* cache_hits,
    uint64_t* cache_misses,
    uint64_t* remote_fetches,
    uint64_t* invalidations_sent,
    uint64_t* invalidations_received
) {
    if (aether == NULL) {
        if (cache_hits) *cache_hits = 0;
        if (cache_misses) *cache_misses = 0;
        if (remote_fetches) *remote_fetches = 0;
        if (invalidations_sent) *invalidations_sent = 0;
        if (invalidations_received) *invalidations_received = 0;
        return;
    }

    if (cache_hits) *cache_hits = aether->cache_hits;
    if (cache_misses) *cache_misses = aether->cache_misses;
    if (remote_fetches) *remote_fetches = aether->remote_fetches;
    if (invalidations_sent) *invalidations_sent = aether->invalidations_sent;
    if (invalidations_received) *invalidations_received = aether->invalidations_received;
}

void seraph_aether_reset_stats(Seraph_Aether* aether) {
    if (aether == NULL) {
        return;
    }

    aether->cache_hits = 0;
    aether->cache_misses = 0;
    aether->remote_fetches = 0;
    aether->invalidations_sent = 0;
    aether->invalidations_received = 0;
}

/*============================================================================
 * Vector Clock Operations for Coherence
 *============================================================================*/

Seraph_Sparse_VClock* seraph_aether_get_page_vclock(
    Seraph_Aether* aether,
    uint64_t addr
) {
    if (aether == NULL || !seraph_aether_is_aether_addr(addr)) {
        return NULL;
    }

    Seraph_Aether_Cache_Entry* cached = seraph_aether_cache_lookup(aether, addr);
    if (cached != NULL && cached->valid) {
        return &cached->vclock;
    }

    return NULL;
}

Seraph_Sparse_VClock_Order seraph_aether_compare_page_causality(
    Seraph_Aether* aether,
    uint64_t addr_a,
    uint64_t addr_b
) {
    if (aether == NULL) {
        return SERAPH_SPARSE_VCLOCK_VOID;
    }

    Seraph_Sparse_VClock* vclock_a = seraph_aether_get_page_vclock(aether, addr_a);
    Seraph_Sparse_VClock* vclock_b = seraph_aether_get_page_vclock(aether, addr_b);

    if (vclock_a == NULL || vclock_b == NULL) {
        return SERAPH_SPARSE_VCLOCK_VOID;
    }

    return seraph_sparse_vclock_compare(vclock_a, vclock_b);
}

Seraph_Vbit seraph_aether_page_happened_before(
    Seraph_Aether* aether,
    uint64_t addr_a,
    uint64_t addr_b
) {
    if (aether == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Sparse_VClock* vclock_a = seraph_aether_get_page_vclock(aether, addr_a);
    Seraph_Sparse_VClock* vclock_b = seraph_aether_get_page_vclock(aether, addr_b);

    if (vclock_a == NULL || vclock_b == NULL) {
        return SERAPH_VBIT_VOID;
    }

    return seraph_sparse_vclock_happened_before(vclock_a, vclock_b);
}

Seraph_Vbit seraph_aether_detect_conflict(
    Seraph_Aether* aether,
    uint64_t addr,
    const Seraph_Sparse_VClock* other_vclock
) {
    if (aether == NULL || other_vclock == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Sparse_VClock* local_vclock = seraph_aether_get_page_vclock(aether, addr);
    if (local_vclock == NULL) {
        return SERAPH_VBIT_VOID;
    }

    return seraph_sparse_vclock_is_concurrent(local_vclock, other_vclock);
}

uint64_t seraph_aether_vclock_tick(Seraph_Aether* aether) {
    if (aether == NULL || !aether->initialized) {
        return SERAPH_VOID_U64;
    }

    /* Find local node's clock */
    Seraph_Aether_Sim_Node* local_node = find_sim_node(aether, aether->local_node_id);
    if (local_node == NULL) {
        return SERAPH_VOID_U64;
    }

    return seraph_sparse_vclock_increment(&local_node->vclock);
}

Seraph_Vbit seraph_aether_vclock_merge(
    Seraph_Aether* aether,
    const Seraph_Sparse_VClock* received
) {
    if (aether == NULL || received == NULL || !aether->initialized) {
        return SERAPH_VBIT_VOID;
    }

    /* Find local node's clock */
    Seraph_Aether_Sim_Node* local_node = find_sim_node(aether, aether->local_node_id);
    if (local_node == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Merge received clock into local clock */
    Seraph_Vbit result = seraph_sparse_vclock_merge(&local_node->vclock, received);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Increment local timestamp after receive */
    uint64_t ts = seraph_sparse_vclock_increment(&local_node->vclock);
    if (SERAPH_IS_VOID_U64(ts)) {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}
