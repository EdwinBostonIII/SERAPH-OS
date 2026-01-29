/**
 * @file aether.h
 * @brief MC28: Aether - Distributed Shared Memory
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * "There is no network. There is only memory that happens to be far away."
 *
 * Aether makes the network transparent. A pointer can reference memory on
 * another machine, and accessing it works exactly like accessing local
 * memory - just slower. Network failures don't throw exceptions; they
 * return VOID.
 *
 * KEY INNOVATIONS:
 * 1. TRANSPARENT ACCESS: Remote memory accessed via pointers, not RPC
 * 2. VOID OVER NETWORK: Network failures return VOID, not exceptions
 * 3. GLOBAL GENERATIONS: Capability revocation works across the cluster
 * 4. COHERENT CACHING: Modified pages invalidate remote caches automatically
 * 5. CHRONON CAUSALITY: Distributed operations ordered by vector clocks
 *
 * ADDRESS LAYOUT:
 *   [63:48] Node ID (16 bits) - 65,536 nodes max
 *   [47:0]  Local Offset (48 bits) - 256 TB per node
 *
 * ADDRESS SPACE:
 *   0x0000_0000_0000_0000 - 0x0000_7FFF_FFFF_FFFF: Volatile (local RAM)
 *   0x0000_8000_0000_0000 - 0x0000_BFFF_FFFF_FFFF: Atlas (persistent)
 *   0x0000_C000_0000_0000 - 0x0000_FFFF_FFFF_FFFF: Aether (distributed)
 */

#ifndef SERAPH_AETHER_H
#define SERAPH_AETHER_H

#include "void.h"
#include "vbit.h"
#include "chronon.h"
#include "capability.h"
#include "vector_clock.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Address Space Constants
 *============================================================================*/

/** Aether address range: 0xC000... to 0xFFFF... (about 64TB total) */
#ifndef SERAPH_AETHER_BASE
#define SERAPH_AETHER_BASE       ((uint64_t)0x0000C00000000000ULL)
#endif
#ifndef SERAPH_AETHER_END
#define SERAPH_AETHER_END        ((uint64_t)0x0000FFFFFFFFFFFFULL)
#endif

/**
 * Address encoding within Aether range:
 * The total Aether space is ~64TB (2^46 bytes).
 * We encode node ID in upper bits of the offset within this range.
 *
 * Layout (relative to AETHER_BASE):
 *   [45:32] Node ID (14 bits) - 16,384 nodes max
 *   [31:0]  Local Offset (32 bits) - 4 GB per node
 */
#define SERAPH_AETHER_NODE_BITS  14
#define SERAPH_AETHER_NODE_SHIFT 32
#define SERAPH_AETHER_NODE_MASK  ((uint64_t)0x00003FFF00000000ULL)

/** Local offset is in bits 31-0 */
#define SERAPH_AETHER_OFFSET_BITS 32
#define SERAPH_AETHER_OFFSET_MASK ((uint64_t)0x00000000FFFFFFFFULL)

/** Maximum nodes in cluster (2^14) */
#define SERAPH_AETHER_MAX_NODES  16384

/** Maximum addressable per node (4 GB) */
#define SERAPH_AETHER_MAX_OFFSET ((uint64_t)0x00000000FFFFFFFFULL)

/** Page size for Aether operations */
#define SERAPH_AETHER_PAGE_SIZE  4096

/** Maximum cache entries (per node simulation) */
#define SERAPH_AETHER_MAX_CACHE_ENTRIES 256

/** Maximum sharers per page (for coherence directory) */
#define SERAPH_AETHER_MAX_SHARERS 64

/** Maximum simulated nodes for userspace testing */
#define SERAPH_AETHER_MAX_SIM_NODES 16

/** Default timeout for network operations (ms) */
#define SERAPH_AETHER_TIMEOUT_MS 5000

/*============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief Fetch status codes
 */
typedef enum Seraph_Aether_Fetch_Status {
    SERAPH_AETHER_OK = 0,           /**< Success */
    SERAPH_AETHER_UNREACHABLE,      /**< Cannot reach node */
    SERAPH_AETHER_TIMEOUT,          /**< Request timed out */
    SERAPH_AETHER_REMOTE_ERROR,     /**< Remote node reported error */
    SERAPH_AETHER_OOM,              /**< Out of memory */
    SERAPH_AETHER_INVALID_ADDR,     /**< Invalid Aether address */
    SERAPH_AETHER_PERMISSION,       /**< Permission denied */
    SERAPH_AETHER_GENERATION,       /**< Stale generation */
    SERAPH_AETHER_CORRUPTION,       /**< Data integrity failure */
    SERAPH_AETHER_NOT_FOUND,        /**< Page not found */
    SERAPH_AETHER_RDMA_ERROR,       /**< RDMA operation failed */
    SERAPH_AETHER_TCP_ERROR,        /**< TCP operation failed */
} Seraph_Aether_Fetch_Status;

/**
 * @brief VOID reason codes for network failures
 *
 * All network failures collapse to VOID, but these codes provide
 * diagnostic information when needed.
 */
typedef enum Seraph_Aether_Void_Reason {
    SERAPH_AETHER_VOID_NONE = 0,    /**< No failure */
    SERAPH_AETHER_VOID_TIMEOUT,     /**< Request timed out */
    SERAPH_AETHER_VOID_UNREACHABLE, /**< Cannot reach node */
    SERAPH_AETHER_VOID_PARTITION,   /**< Network partition detected */
    SERAPH_AETHER_VOID_NODE_CRASHED,/**< Node is known to be down */
    SERAPH_AETHER_VOID_PERMISSION,  /**< Remote node denied access */
    SERAPH_AETHER_VOID_GENERATION,  /**< Capability revoked on remote */
    SERAPH_AETHER_VOID_CORRUPTION,  /**< Data integrity check failed */
} Seraph_Aether_Void_Reason;

/**
 * @brief Page coherence state (on owner node)
 */
typedef enum Seraph_Aether_Page_State {
    SERAPH_AETHER_PAGE_INVALID = 0, /**< No valid copies exist */
    SERAPH_AETHER_PAGE_EXCLUSIVE,   /**< One node has writable copy */
    SERAPH_AETHER_PAGE_SHARED,      /**< Multiple nodes have read-only copies */
} Seraph_Aether_Page_State;

/**
 * @brief Transport type for connections
 */
typedef enum Seraph_Aether_Transport {
    SERAPH_AETHER_TRANSPORT_SIMULATED = 0, /**< Simulated (userspace testing) */
    SERAPH_AETHER_TRANSPORT_RDMA,          /**< RDMA (InfiniBand, RoCE) */
    SERAPH_AETHER_TRANSPORT_TCP,           /**< TCP/IP fallback */
} Seraph_Aether_Transport;

/**
 * @brief Request types for Aether protocol
 */
typedef enum Seraph_Aether_Request_Type {
    SERAPH_AETHER_REQ_PAGE = 0,     /**< Request page data */
    SERAPH_AETHER_REQ_WRITE,        /**< Request write permission */
    SERAPH_AETHER_REQ_REVOKE,       /**< Revoke capability */
    SERAPH_AETHER_REQ_INVALIDATE,   /**< Invalidate cached copy */
    SERAPH_AETHER_REQ_GENERATION,   /**< Query current generation */
} Seraph_Aether_Request_Type;

/**
 * @brief Response status codes
 */
typedef enum Seraph_Aether_Response_Status {
    SERAPH_AETHER_RESP_OK = 0,      /**< Request succeeded */
    SERAPH_AETHER_RESP_ERROR,       /**< Request failed */
    SERAPH_AETHER_RESP_DENIED,      /**< Permission denied */
    SERAPH_AETHER_RESP_NOT_FOUND,   /**< Page not found */
    SERAPH_AETHER_RESP_STALE,       /**< Generation is stale */
} Seraph_Aether_Response_Status;

/*============================================================================
 * Core Structures
 *============================================================================*/

/**
 * @brief Global generation - (Node ID, Local Generation) packed
 */
typedef struct Seraph_Aether_Global_Gen {
    uint16_t node_id;               /**< Node that owns the allocation */
    uint64_t local_gen;             /**< Generation on that node */
} Seraph_Aether_Global_Gen;

/**
 * @brief Fetch result from remote node
 *
 * Includes the page's vector clock for causality tracking.
 */
typedef struct Seraph_Aether_Fetch_Result {
    Seraph_Aether_Fetch_Status status; /**< Status code */
    void* page;                        /**< Fetched page data (if success) */
    uint64_t generation;               /**< Current generation */
    Seraph_Aether_Void_Reason reason;  /**< Detailed failure reason */
    Seraph_Sparse_VClock vclock;        /**< Page's vector clock */
} Seraph_Aether_Fetch_Result;

/**
 * @brief Cache entry for remote pages
 *
 * Each cached page carries a vector clock that tracks its causal history.
 * The vclock enables detection of concurrent writes and proper ordering
 * of coherence operations across the distributed system.
 */
typedef struct Seraph_Aether_Cache_Entry {
    uint64_t aether_addr;           /**< Original Aether address (page-aligned) */
    void* local_page;               /**< Local cached copy */
    uint16_t owner_node;            /**< Node that owns this page */
    uint64_t generation;            /**< Generation when fetched */
    uint64_t fetch_time;            /**< Chronon timestamp of fetch */
    Seraph_Sparse_VClock vclock;     /**< Vector clock for causality tracking */
    bool dirty;                     /**< Has local copy been modified? */
    bool valid;                     /**< Is cache entry valid? */
    struct Seraph_Aether_Cache_Entry* lru_prev; /**< LRU list previous */
    struct Seraph_Aether_Cache_Entry* lru_next; /**< LRU list next */
} Seraph_Aether_Cache_Entry;

/**
 * @brief Directory entry for coherence tracking (on owner node)
 *
 * The directory tracks which nodes have cached copies of each page.
 * The vclock represents the current causal state of the page, updated
 * on each write operation. Sharers receive this clock with page data.
 */
typedef struct Seraph_Aether_Directory_Entry {
    uint64_t offset;                /**< Page offset on owner node */
    Seraph_Aether_Page_State state; /**< Current coherence state */
    uint16_t exclusive_owner;       /**< Node with exclusive copy */
    uint16_t sharer_count;          /**< Number of sharing nodes */
    uint16_t sharers[SERAPH_AETHER_MAX_SHARERS]; /**< Nodes with shared copies */
    uint64_t generation;            /**< Current generation */
    Seraph_Sparse_VClock vclock;     /**< Vector clock for page causality */
    bool valid;                     /**< Is entry valid? */
} Seraph_Aether_Directory_Entry;

/**
 * @brief Protocol request structure
 *
 * Requests carry the sender's vector clock for causality tracking.
 * On receive, the recipient merges this clock with their local clock.
 */
typedef struct Seraph_Aether_Request {
    Seraph_Aether_Request_Type type; /**< Request type */
    uint64_t offset;                 /**< Page offset */
    uint64_t generation;             /**< Expected generation (for validation) */
    size_t data_size;                /**< Size of attached data */
    Seraph_Chronon sender_time;      /**< Sender's Chronon timestamp */
    uint64_t message_id;             /**< For duplicate detection */
    Seraph_Sparse_VClock sender_vclock; /**< Sender's vector clock */
} Seraph_Aether_Request;

/**
 * @brief Protocol response structure
 *
 * Responses include the page's current vector clock, which the requester
 * uses to track causality and detect conflicts with concurrent operations.
 */
typedef struct Seraph_Aether_Response {
    Seraph_Aether_Response_Status status; /**< Response status */
    uint64_t generation;                  /**< Current generation */
    void* page_data;                      /**< Page data (if applicable) */
    size_t data_size;                     /**< Size of page data */
    Seraph_Chronon responder_time;        /**< Responder's Chronon timestamp */
    Seraph_Sparse_VClock page_vclock;      /**< Page's vector clock */
} Seraph_Aether_Response;

/**
 * @brief Simulated node state (for userspace testing)
 *
 * Each node maintains its own vector clock that tracks its local causal time.
 * This clock is incremented on local events and merged on message receive.
 */
typedef struct Seraph_Aether_Sim_Node {
    uint16_t node_id;                /**< This node's ID */
    void* memory;                    /**< Allocated memory for this node */
    size_t memory_size;              /**< Size of allocated memory */
    uint64_t next_alloc_offset;      /**< Next allocation offset */
    uint64_t generation;             /**< Current generation counter */
    Seraph_Sparse_VClock vclock;      /**< Node's vector clock for causality */
    Seraph_Aether_Directory_Entry* directory; /**< Coherence directory */
    size_t directory_capacity;       /**< Directory capacity */
    size_t directory_count;          /**< Number of directory entries */
    bool online;                     /**< Is node reachable? */
    Seraph_Aether_Void_Reason injected_failure; /**< Injected failure for testing */
} Seraph_Aether_Sim_Node;

/**
 * @brief Page cache structure
 */
typedef struct Seraph_Aether_Cache {
    Seraph_Aether_Cache_Entry* entries; /**< Cache entry array */
    size_t capacity;                    /**< Maximum entries */
    size_t count;                       /**< Current entry count */
    Seraph_Aether_Cache_Entry* lru_head; /**< LRU list head (most recent) */
    Seraph_Aether_Cache_Entry* lru_tail; /**< LRU list tail (least recent) */
} Seraph_Aether_Cache;

/**
 * @brief Main Aether state structure
 */
typedef struct Seraph_Aether {
    uint16_t local_node_id;          /**< This node's ID */
    uint16_t node_count;             /**< Total nodes in cluster */
    bool initialized;                /**< Is Aether initialized? */

    /* Page cache */
    Seraph_Aether_Cache cache;       /**< Local page cache */

    /* Simulated nodes (for userspace testing) */
    Seraph_Aether_Sim_Node* sim_nodes; /**< Array of simulated nodes */
    size_t sim_node_count;            /**< Number of simulated nodes */

    /* Statistics */
    uint64_t cache_hits;             /**< Cache hit count */
    uint64_t cache_misses;           /**< Cache miss count */
    uint64_t remote_fetches;         /**< Remote fetch count */
    uint64_t invalidations_sent;     /**< Invalidations sent */
    uint64_t invalidations_received; /**< Invalidations received */
} Seraph_Aether;

/*============================================================================
 * Address Manipulation Functions (Inline for performance)
 *============================================================================*/

/**
 * @brief Extract node ID from Aether address
 */
static inline uint16_t seraph_aether_get_node(uint64_t addr) {
    return (uint16_t)((addr & SERAPH_AETHER_NODE_MASK) >> SERAPH_AETHER_NODE_SHIFT);
}

/**
 * @brief Extract local offset from Aether address
 */
static inline uint64_t seraph_aether_get_offset(uint64_t addr) {
    return addr & SERAPH_AETHER_OFFSET_MASK;
}

/**
 * @brief Construct Aether address from node ID and offset
 */
static inline uint64_t seraph_aether_make_addr(uint16_t node_id, uint64_t offset) {
    return SERAPH_AETHER_BASE
         | ((uint64_t)node_id << SERAPH_AETHER_NODE_SHIFT)
         | (offset & SERAPH_AETHER_OFFSET_MASK);
}

/**
 * @brief Check if address is in Aether range
 */
static inline bool seraph_aether_is_aether_addr(uint64_t addr) {
    return addr >= SERAPH_AETHER_BASE && addr <= SERAPH_AETHER_END;
}

/**
 * @brief Align address down to page boundary
 */
static inline uint64_t seraph_aether_page_align(uint64_t addr) {
    return addr & ~((uint64_t)(SERAPH_AETHER_PAGE_SIZE - 1));
}

/**
 * @brief Get page offset within address
 */
static inline uint64_t seraph_aether_page_offset(uint64_t addr) {
    return addr & ((uint64_t)(SERAPH_AETHER_PAGE_SIZE - 1));
}

/*============================================================================
 * Global Generation Functions (Inline for performance)
 *============================================================================*/

/**
 * @brief Pack global generation into 64 bits
 *
 * Layout: [63:48] node_id, [47:0] local_gen
 */
static inline uint64_t seraph_aether_pack_global_gen(uint16_t node_id, uint64_t local_gen) {
    return ((uint64_t)node_id << 48) | (local_gen & 0x0000FFFFFFFFFFFFULL);
}

/**
 * @brief Unpack global generation from 64 bits
 */
static inline Seraph_Aether_Global_Gen seraph_aether_unpack_global_gen(uint64_t packed) {
    Seraph_Aether_Global_Gen gen;
    gen.node_id = (uint16_t)(packed >> 48);
    gen.local_gen = packed & 0x0000FFFFFFFFFFFFULL;
    return gen;
}

/*============================================================================
 * Initialization API
 *============================================================================*/

/**
 * @brief Initialize Aether with specified node configuration
 *
 * @param aether Aether state to initialize
 * @param node_id This node's ID (0 to node_count-1)
 * @param node_count Total nodes in cluster
 * @return SERAPH_TRUE on success, SERAPH_FALSE on failure
 */
Seraph_Vbit seraph_aether_init(
    Seraph_Aether* aether,
    uint16_t node_id,
    uint16_t node_count
);

/**
 * @brief Initialize Aether with defaults (single node)
 */
Seraph_Vbit seraph_aether_init_default(Seraph_Aether* aether);

/**
 * @brief Destroy Aether and free resources
 */
void seraph_aether_destroy(Seraph_Aether* aether);

/**
 * @brief Get local node ID
 */
uint16_t seraph_aether_get_local_node_id(const Seraph_Aether* aether);

/**
 * @brief Check if address is on local node
 */
bool seraph_aether_is_local(const Seraph_Aether* aether, uint64_t addr);

/*============================================================================
 * Simulated Node Management (for userspace testing)
 *============================================================================*/

/**
 * @brief Add a simulated node to the Aether network
 *
 * @param aether Aether state
 * @param node_id Node ID to add
 * @param memory_size Size of memory to allocate for this node
 * @return SERAPH_TRUE on success
 */
Seraph_Vbit seraph_aether_add_sim_node(
    Seraph_Aether* aether,
    uint16_t node_id,
    size_t memory_size
);

/**
 * @brief Set node online/offline status
 */
void seraph_aether_set_node_online(
    Seraph_Aether* aether,
    uint16_t node_id,
    bool online
);

/**
 * @brief Inject failure for testing VOID scenarios
 */
void seraph_aether_inject_failure(
    Seraph_Aether* aether,
    uint16_t node_id,
    Seraph_Aether_Void_Reason reason
);

/**
 * @brief Clear injected failure
 */
void seraph_aether_clear_failure(
    Seraph_Aether* aether,
    uint16_t node_id
);

/*============================================================================
 * Memory Operations
 *============================================================================*/

/**
 * @brief Allocate memory on local node, returning Aether address
 *
 * @param aether Aether state
 * @param size Size to allocate
 * @return Aether address, or SERAPH_VOID_U64 on failure
 */
uint64_t seraph_aether_alloc(Seraph_Aether* aether, size_t size);

/**
 * @brief Allocate memory on specific node
 */
uint64_t seraph_aether_alloc_on_node(
    Seraph_Aether* aether,
    uint16_t node_id,
    size_t size
);

/**
 * @brief Free previously allocated memory
 */
void seraph_aether_free(Seraph_Aether* aether, uint64_t addr, size_t size);

/**
 * @brief Read from Aether address (may trigger remote fetch)
 *
 * @param aether Aether state
 * @param addr Aether address to read from
 * @param dest Destination buffer
 * @param size Bytes to read
 * @return Fetch result with status
 */
Seraph_Aether_Fetch_Result seraph_aether_read(
    Seraph_Aether* aether,
    uint64_t addr,
    void* dest,
    size_t size
);

/**
 * @brief Write to Aether address (may invalidate remote caches)
 *
 * @param aether Aether state
 * @param addr Aether address to write to
 * @param src Source buffer
 * @param size Bytes to write
 * @return Fetch result with status
 */
Seraph_Aether_Fetch_Result seraph_aether_write(
    Seraph_Aether* aether,
    uint64_t addr,
    const void* src,
    size_t size
);

/**
 * @brief Read with VOID semantics - returns VOID on any failure
 */
Seraph_Vbit seraph_aether_read_vbit(
    Seraph_Aether* aether,
    uint64_t addr,
    void* dest,
    size_t size
);

/**
 * @brief Write with VOID semantics
 */
Seraph_Vbit seraph_aether_write_vbit(
    Seraph_Aether* aether,
    uint64_t addr,
    const void* src,
    size_t size
);

/*============================================================================
 * Cache Operations
 *============================================================================*/

/**
 * @brief Look up address in cache
 *
 * @param aether Aether state
 * @param addr Aether address (will be page-aligned)
 * @return Cache entry or NULL if not found
 */
Seraph_Aether_Cache_Entry* seraph_aether_cache_lookup(
    Seraph_Aether* aether,
    uint64_t addr
);

/**
 * @brief Insert page into cache
 */
Seraph_Aether_Cache_Entry* seraph_aether_cache_insert(
    Seraph_Aether* aether,
    uint64_t addr,
    void* page,
    uint16_t owner_node,
    uint64_t generation
);

/**
 * @brief Invalidate cache entry
 */
void seraph_aether_cache_invalidate(Seraph_Aether* aether, uint64_t addr);

/**
 * @brief Flush all dirty pages back to owners
 */
uint32_t seraph_aether_cache_flush(Seraph_Aether* aether);

/**
 * @brief Clear entire cache
 */
void seraph_aether_cache_clear(Seraph_Aether* aether);

/**
 * @brief Get cache statistics
 */
void seraph_aether_cache_stats(
    const Seraph_Aether* aether,
    uint64_t* hits,
    uint64_t* misses
);

/*============================================================================
 * Generation and Revocation
 *============================================================================*/

/**
 * @brief Get current generation for an address
 */
uint64_t seraph_aether_get_generation(Seraph_Aether* aether, uint64_t addr);

/**
 * @brief Check if generation is current
 */
Seraph_Vbit seraph_aether_check_generation(
    Seraph_Aether* aether,
    uint64_t addr,
    uint64_t expected_gen
);

/**
 * @brief Revoke capability (increment generation)
 *
 * Works cluster-wide: if the address is on a remote node,
 * sends revocation request to owner.
 */
Seraph_Vbit seraph_aether_revoke(Seraph_Aether* aether, uint64_t addr);

/**
 * @brief Get global generation for an address
 */
uint64_t seraph_aether_get_global_gen(Seraph_Aether* aether, uint64_t addr);

/*============================================================================
 * Coherence Protocol
 *============================================================================*/

/**
 * @brief Handle read request from another node
 */
Seraph_Aether_Response seraph_aether_handle_read_request(
    Seraph_Aether* aether,
    uint16_t requester_node,
    uint64_t offset
);

/**
 * @brief Handle write request from another node
 */
Seraph_Aether_Response seraph_aether_handle_write_request(
    Seraph_Aether* aether,
    uint16_t requester_node,
    uint64_t offset,
    const void* data,
    size_t size
);

/**
 * @brief Handle invalidation message from owner
 */
void seraph_aether_handle_invalidate(
    Seraph_Aether* aether,
    uint64_t addr,
    uint64_t new_generation
);

/**
 * @brief Broadcast invalidation to all sharers
 */
void seraph_aether_broadcast_invalidation(
    Seraph_Aether* aether,
    uint64_t offset,
    uint64_t new_generation
);

/*============================================================================
 * Directory Operations
 *============================================================================*/

/**
 * @brief Get or create directory entry for offset
 */
Seraph_Aether_Directory_Entry* seraph_aether_get_directory_entry(
    Seraph_Aether* aether,
    uint16_t node_id,
    uint64_t offset
);

/**
 * @brief Add sharer to directory entry
 */
void seraph_aether_directory_add_sharer(
    Seraph_Aether_Directory_Entry* entry,
    uint16_t node_id
);

/**
 * @brief Remove sharer from directory entry
 */
void seraph_aether_directory_remove_sharer(
    Seraph_Aether_Directory_Entry* entry,
    uint16_t node_id
);

/*============================================================================
 * VOID Context
 *============================================================================*/

/**
 * @brief Get last VOID reason for this thread
 */
Seraph_Aether_Void_Reason seraph_aether_get_void_reason(void);

/**
 * @brief Get last VOID address for this thread
 */
uint64_t seraph_aether_get_void_addr(void);

/**
 * @brief Clear VOID context
 */
void seraph_aether_clear_void_context(void);

/*============================================================================
 * Statistics
 *============================================================================*/

/**
 * @brief Get Aether statistics
 */
void seraph_aether_get_stats(
    const Seraph_Aether* aether,
    uint64_t* cache_hits,
    uint64_t* cache_misses,
    uint64_t* remote_fetches,
    uint64_t* invalidations_sent,
    uint64_t* invalidations_received
);

/**
 * @brief Reset statistics
 */
void seraph_aether_reset_stats(Seraph_Aether* aether);

/*============================================================================
 * Vector Clock Operations for Coherence
 *============================================================================*/

/**
 * @brief Get vector clock for a cached page
 *
 * Returns the vector clock associated with a cached page entry.
 *
 * @param aether Aether state
 * @param addr Aether address (will be page-aligned)
 * @return Pointer to page's vector clock, or NULL if not cached
 */
Seraph_Sparse_VClock* seraph_aether_get_page_vclock(
    Seraph_Aether* aether,
    uint64_t addr
);

/**
 * @brief Compare causality of two page operations
 *
 * Compares the vector clocks of two cached pages to determine
 * their causal relationship.
 *
 * @param aether Aether state
 * @param addr_a First page address
 * @param addr_b Second page address
 * @return Causal ordering between the pages
 */
Seraph_Sparse_VClock_Order seraph_aether_compare_page_causality(
    Seraph_Aether* aether,
    uint64_t addr_a,
    uint64_t addr_b
);

/**
 * @brief Check if page operation A happened before B
 *
 * @param aether Aether state
 * @param addr_a First page address
 * @param addr_b Second page address
 * @return TRUE if A -> B, FALSE otherwise, VOID if invalid
 */
Seraph_Vbit seraph_aether_page_happened_before(
    Seraph_Aether* aether,
    uint64_t addr_a,
    uint64_t addr_b
);

/**
 * @brief Detect concurrent writes to a page
 *
 * Checks if the local page's clock is concurrent with the given clock,
 * indicating a potential write conflict that needs resolution.
 *
 * @param aether Aether state
 * @param addr Page address
 * @param other_vclock Vector clock from another node
 * @return TRUE if concurrent (conflict), FALSE if ordered, VOID if invalid
 */
Seraph_Vbit seraph_aether_detect_conflict(
    Seraph_Aether* aether,
    uint64_t addr,
    const Seraph_Sparse_VClock* other_vclock
);

/**
 * @brief Increment node's vector clock
 *
 * Increments the local node's vector clock component. Call this
 * when performing a local operation that needs causality tracking.
 *
 * @param aether Aether state
 * @return New timestamp for local node, or VOID on error
 */
uint64_t seraph_aether_vclock_tick(Seraph_Aether* aether);

/**
 * @brief Merge received vector clock into node's clock
 *
 * When receiving a message or page from another node, merge their
 * vector clock into ours: local[i] = max(local[i], received[i])
 *
 * @param aether Aether state
 * @param received Vector clock from remote node
 * @return TRUE on success, FALSE on failure, VOID if invalid
 */
Seraph_Vbit seraph_aether_vclock_merge(
    Seraph_Aether* aether,
    const Seraph_Sparse_VClock* received
);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_AETHER_H */
