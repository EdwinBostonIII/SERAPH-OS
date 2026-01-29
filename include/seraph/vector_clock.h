/**
 * @file vector_clock.h
 * @brief Sparse Vector Clock for Aether DSM Causality Tracking
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module provides a sparse vector clock implementation optimized for
 * Aether's Distributed Shared Memory (DSM) system. Unlike dense vector clocks
 * that require O(N) storage for N nodes, this implementation uses a sparse
 * representation that only tracks nodes that have actually touched a page.
 *
 * DESIGN RATIONALE:
 *
 * In a cluster with thousands of nodes, most pages are only accessed by a
 * handful of nodes. A dense vector clock would waste enormous memory tracking
 * zeros for nodes that never touched the page. This sparse implementation
 * stores only (node_id, timestamp) pairs for nodes with non-zero timestamps.
 *
 * KEY FEATURES:
 *
 *   1. SPARSE STORAGE: Only stores entries for nodes with non-zero timestamps.
 *      A page touched by 3 nodes uses ~24 bytes, not 16,384 * 8 = 128KB.
 *
 *   2. SORTED ENTRIES: Entries are sorted by node_id, enabling:
 *      - O(log n) lookup via binary search
 *      - O(n + m) comparison via merge-style traversal
 *      - O(n + m) merge operations
 *
 *   3. COPY-ON-WRITE READY: The structure supports efficient cloning for
 *      page transfers across the network.
 *
 *   4. VOID INTEGRATION: Uses SERAPH's VOID semantics for error handling.
 *      Invalid operations return VOID, which propagates automatically.
 *
 * CAUSAL ORDERING:
 *
 * Vector clocks enable detection of true concurrency:
 *   - BEFORE:     A happened-before B (all A[i] <= B[i], some A[j] < B[j])
 *   - AFTER:      B happened-before A (all B[i] <= A[i], some B[j] < A[j])
 *   - CONCURRENT: Neither ordered (A has some higher, B has some higher)
 *   - EQUAL:      Same logical time (all A[i] == B[i])
 *
 * USAGE IN AETHER:
 *
 * Each Aether page carries a vector clock that tracks causality:
 *   - On write: Increment local node's component
 *   - On page receive: Merge with sender's clock
 *   - On coherence decision: Compare clocks to detect conflicts
 *
 * @see aether.h for the DSM system that uses these clocks
 * @see chronon.h for scalar Lamport timestamps
 */

#ifndef SERAPH_VECTOR_CLOCK_H
#define SERAPH_VECTOR_CLOCK_H

#include "seraph/void.h"
#include "seraph/vbit.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration Constants
 *============================================================================*/

/**
 * @brief Initial capacity for vector clock entries
 *
 * Most pages are touched by very few nodes. Start small and grow as needed.
 */
#define SERAPH_SPARSE_VCLOCK_INITIAL_CAPACITY  8

/**
 * @brief Maximum entries in a vector clock
 *
 * Limits memory usage per page. If a page is touched by more nodes than this,
 * the clock is considered "saturated" and falls back to conservative ordering.
 */
#define SERAPH_SPARSE_VCLOCK_MAX_ENTRIES  256

/**
 * @brief Growth factor when expanding capacity (numerator/denominator = 1.5)
 */
#define SERAPH_SPARSE_VCLOCK_GROWTH_NUM   3
#define SERAPH_SPARSE_VCLOCK_GROWTH_DEN   2

/*============================================================================
 * Type Definitions
 *============================================================================*/

/**
 * @brief Causal ordering result
 *
 * Describes the causal relationship between two vector clocks.
 */
typedef enum Seraph_Sparse_VClock_Order {
    SERAPH_SPARSE_VCLOCK_BEFORE     = -1,  /**< A happened-before B (A -> B) */
    SERAPH_SPARSE_VCLOCK_EQUAL      =  0,  /**< A and B are identical */
    SERAPH_SPARSE_VCLOCK_AFTER      =  1,  /**< B happened-before A (B -> A) */
    SERAPH_SPARSE_VCLOCK_CONCURRENT =  2,  /**< Neither ordered (A || B) */
    SERAPH_SPARSE_VCLOCK_VOID       = 0xFF /**< Cannot determine (invalid input) */
} Seraph_Sparse_VClock_Order;

/**
 * @brief Single entry in a sparse vector clock
 *
 * Represents one node's timestamp. Entries are kept sorted by node_id
 * to enable efficient binary search and merge operations.
 */
typedef struct Seraph_VClock_Entry {
    uint16_t node_id;       /**< Node identifier (0 to MAX_NODES-1) */
    uint16_t reserved;      /**< Reserved for alignment/future use */
    uint64_t timestamp;     /**< Logical timestamp for this node */
} Seraph_VClock_Entry;

/**
 * @brief Sparse vector clock structure
 *
 * A dynamically-sized array of (node_id, timestamp) pairs, sorted by node_id.
 * Tracks causality for a single page or event across multiple nodes.
 *
 * MEMORY LAYOUT:
 *   - Fixed 24-byte header
 *   - Variable-length entry array (12 bytes per entry)
 *   - Typical size: 24 + 8*12 = 120 bytes for 8 nodes
 *
 * INVARIANTS:
 *   - entries is non-NULL if capacity > 0
 *   - count <= capacity
 *   - Entries are sorted by node_id in ascending order
 *   - No duplicate node_ids
 *   - All timestamps are non-VOID
 */
typedef struct Seraph_Sparse_VClock {
    Seraph_VClock_Entry* entries;   /**< Array of (node_id, timestamp) pairs */
    uint16_t count;                 /**< Number of valid entries */
    uint16_t capacity;              /**< Allocated capacity */
    uint16_t owner_node;            /**< Local node ID for increment operations */
    uint16_t flags;                 /**< Status flags (see SERAPH_SPARSE_VCLOCK_FLAG_*) */
} Seraph_Sparse_VClock;

/** @name Vector Clock Flags */
/**@{*/
#define SERAPH_SPARSE_VCLOCK_FLAG_NONE       0x0000  /**< No flags set */
#define SERAPH_SPARSE_VCLOCK_FLAG_SATURATED  0x0001  /**< Too many entries, using conservative ordering */
#define SERAPH_SPARSE_VCLOCK_FLAG_BORROWED   0x0002  /**< Entries array is borrowed (don't free) */
/**@}*/

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

/**
 * @brief Initialize a vector clock
 *
 * Allocates internal storage and prepares the clock for use.
 * The clock starts empty (all timestamps implicitly zero).
 *
 * @param vclock      Pointer to vector clock structure to initialize
 * @param owner_node  Local node ID (used for increment operations)
 * @return SERAPH_VBIT_TRUE on success
 *         SERAPH_VBIT_FALSE if allocation failed
 *         SERAPH_VBIT_VOID if vclock is NULL
 *
 * @code
 * Seraph_Sparse_VClock clock;
 * if (seraph_vbit_is_true(seraph_sparse_vclock_init(&clock, my_node_id))) {
 *     // Use clock...
 *     seraph_sparse_vclock_destroy(&clock);
 * }
 * @endcode
 */
Seraph_Vbit seraph_sparse_vclock_init(Seraph_Sparse_VClock* vclock, uint16_t owner_node);

/**
 * @brief Initialize a vector clock with pre-allocated entries
 *
 * Uses an externally-provided buffer instead of allocating memory.
 * Useful for stack allocation or embedding in other structures.
 *
 * @param vclock      Pointer to vector clock structure
 * @param owner_node  Local node ID
 * @param buffer      Pre-allocated entry buffer
 * @param capacity    Number of entries buffer can hold
 * @return SERAPH_VBIT_TRUE on success
 *         SERAPH_VBIT_VOID if vclock or buffer is NULL
 *
 * @note The buffer must remain valid for the lifetime of the clock.
 *       Call seraph_sparse_vclock_release() (not destroy) to detach without freeing.
 */
Seraph_Vbit seraph_sparse_vclock_init_with_buffer(
    Seraph_Sparse_VClock* vclock,
    uint16_t owner_node,
    Seraph_VClock_Entry* buffer,
    uint16_t capacity
);

/**
 * @brief Destroy a vector clock and free resources
 *
 * Frees the internal entry array if it was allocated by init.
 * Does nothing if the clock uses a borrowed buffer.
 *
 * @param vclock  Pointer to vector clock to destroy (may be NULL)
 */
void seraph_sparse_vclock_destroy(Seraph_Sparse_VClock* vclock);

/**
 * @brief Reset a vector clock to empty state
 *
 * Clears all entries but keeps allocated capacity.
 * Useful for reusing a clock without reallocation.
 *
 * @param vclock  Pointer to vector clock to reset (may be NULL)
 */
void seraph_sparse_vclock_reset(Seraph_Sparse_VClock* vclock);

/**
 * @brief Create a deep copy of a vector clock
 *
 * Allocates new storage and copies all entries.
 *
 * @param dst  Destination clock (will be initialized)
 * @param src  Source clock to copy from
 * @return SERAPH_VBIT_TRUE on success
 *         SERAPH_VBIT_FALSE if allocation failed
 *         SERAPH_VBIT_VOID if dst or src is NULL
 */
Seraph_Vbit seraph_sparse_vclock_copy(Seraph_Sparse_VClock* dst, const Seraph_Sparse_VClock* src);

/*============================================================================
 * Query Functions
 *============================================================================*/

/**
 * @brief Check if vector clock is valid
 *
 * @param vclock  Pointer to vector clock
 * @return true if initialized and valid, false otherwise
 */
static inline bool seraph_sparse_vclock_is_valid(const Seraph_Sparse_VClock* vclock) {
    return vclock != NULL && (vclock->capacity == 0 || vclock->entries != NULL);
}

/**
 * @brief Check if vector clock is empty (all zeros)
 *
 * @param vclock  Pointer to vector clock
 * @return true if count == 0, false otherwise
 */
static inline bool seraph_sparse_vclock_is_empty(const Seraph_Sparse_VClock* vclock) {
    return vclock == NULL || vclock->count == 0;
}

/**
 * @brief Check if vector clock is saturated
 *
 * A saturated clock has reached maximum entries and may use
 * conservative ordering assumptions.
 *
 * @param vclock  Pointer to vector clock
 * @return true if saturated flag is set
 */
static inline bool seraph_sparse_vclock_is_saturated(const Seraph_Sparse_VClock* vclock) {
    return vclock != NULL && (vclock->flags & SERAPH_SPARSE_VCLOCK_FLAG_SATURATED);
}

/**
 * @brief Get timestamp for a specific node
 *
 * @param vclock   Pointer to vector clock
 * @param node_id  Node to query
 * @return Timestamp for node, or 0 if not present
 *         Returns SERAPH_VOID_U64 if vclock is NULL
 */
uint64_t seraph_sparse_vclock_get(const Seraph_Sparse_VClock* vclock, uint16_t node_id);

/**
 * @brief Get number of non-zero entries
 *
 * @param vclock  Pointer to vector clock
 * @return Number of entries, or 0 if NULL
 */
static inline uint16_t seraph_sparse_vclock_count(const Seraph_Sparse_VClock* vclock) {
    return vclock ? vclock->count : 0;
}

/*============================================================================
 * Modification Functions
 *============================================================================*/

/**
 * @brief Increment the local node's timestamp
 *
 * This is the "tick" operation for local events. Increments the timestamp
 * for owner_node (set during init) by 1.
 *
 * @param vclock  Pointer to vector clock
 * @return New timestamp value for local node
 *         Returns SERAPH_VOID_U64 on error (NULL, overflow, or alloc failure)
 *
 * @code
 * // Before sending a message or modifying a page:
 * uint64_t ts = seraph_sparse_vclock_increment(&page->vclock);
 * if (!SERAPH_IS_VOID_U64(ts)) {
 *     send_page_with_clock(page);
 * }
 * @endcode
 */
uint64_t seraph_sparse_vclock_increment(Seraph_Sparse_VClock* vclock);

/**
 * @brief Set timestamp for a specific node
 *
 * Directly sets a node's timestamp. Creates entry if not present.
 * Used when receiving clock data from another node.
 *
 * @param vclock     Pointer to vector clock
 * @param node_id    Node to update
 * @param timestamp  New timestamp value
 * @return SERAPH_VBIT_TRUE on success
 *         SERAPH_VBIT_FALSE if allocation failed or clock saturated
 *         SERAPH_VBIT_VOID if vclock is NULL
 */
Seraph_Vbit seraph_sparse_vclock_set(
    Seraph_Sparse_VClock* vclock,
    uint16_t node_id,
    uint64_t timestamp
);

/**
 * @brief Merge another vector clock into this one
 *
 * For each node, takes the maximum of both timestamps:
 *   result[i] = max(this[i], other[i])
 *
 * Does NOT increment local timestamp - call seraph_sparse_vclock_increment()
 * separately if needed (e.g., after receiving a message).
 *
 * @param vclock  Destination clock (modified in place)
 * @param other   Source clock to merge from
 * @return SERAPH_VBIT_TRUE on success
 *         SERAPH_VBIT_FALSE if merge would exceed capacity
 *         SERAPH_VBIT_VOID if either parameter is NULL
 *
 * @code
 * // When receiving a page from another node:
 * seraph_sparse_vclock_merge(&local_clock, &received_clock);
 * seraph_sparse_vclock_increment(&local_clock);  // Local receive event
 * @endcode
 */
Seraph_Vbit seraph_sparse_vclock_merge(Seraph_Sparse_VClock* vclock, const Seraph_Sparse_VClock* other);

/*============================================================================
 * Comparison Functions
 *============================================================================*/

/**
 * @brief Compare two vector clocks for causal ordering
 *
 * Determines the causal relationship between two vector clocks:
 *   - BEFORE:     a happened before b (a -> b)
 *   - AFTER:      b happened before a (b -> a)
 *   - CONCURRENT: Neither ordered, true concurrency (a || b)
 *   - EQUAL:      Same logical time
 *   - VOID:       Cannot determine (invalid input)
 *
 * @param a  First vector clock
 * @param b  Second vector clock
 * @return Causal ordering relationship
 *
 * @code
 * switch (seraph_sparse_vclock_compare(&clock_a, &clock_b)) {
 *     case SERAPH_SPARSE_VCLOCK_BEFORE:
 *         // a's operation preceded b's
 *         break;
 *     case SERAPH_SPARSE_VCLOCK_CONCURRENT:
 *         // Conflict! Need resolution strategy
 *         break;
 *     // ...
 * }
 * @endcode
 */
Seraph_Sparse_VClock_Order seraph_sparse_vclock_compare(
    const Seraph_Sparse_VClock* a,
    const Seraph_Sparse_VClock* b
);

/**
 * @brief Check if a happened before b
 *
 * Convenience function that returns a boolean instead of full ordering.
 *
 * @param a  First vector clock
 * @param b  Second vector clock
 * @return SERAPH_VBIT_TRUE if a -> b (a causally precedes b)
 *         SERAPH_VBIT_FALSE if not (concurrent, equal, or b -> a)
 *         SERAPH_VBIT_VOID if either parameter is NULL
 */
Seraph_Vbit seraph_sparse_vclock_happened_before(
    const Seraph_Sparse_VClock* a,
    const Seraph_Sparse_VClock* b
);

/**
 * @brief Check if two clocks are concurrent (neither ordered)
 *
 * Concurrent clocks indicate that the events they represent happened
 * independently, with no causal relationship. This often indicates
 * a conflict that needs resolution.
 *
 * @param a  First vector clock
 * @param b  Second vector clock
 * @return SERAPH_VBIT_TRUE if a || b (concurrent)
 *         SERAPH_VBIT_FALSE if causally ordered or equal
 *         SERAPH_VBIT_VOID if either parameter is NULL
 */
Seraph_Vbit seraph_sparse_vclock_is_concurrent(
    const Seraph_Sparse_VClock* a,
    const Seraph_Sparse_VClock* b
);

/*============================================================================
 * Serialization Functions
 *============================================================================*/

/**
 * @brief Calculate serialized size of vector clock
 *
 * Returns the number of bytes needed to serialize this clock.
 * Format: [count:2][entries:count*(2+2+8)]
 *
 * @param vclock  Pointer to vector clock
 * @return Size in bytes, or 0 if NULL
 */
size_t seraph_sparse_vclock_serialized_size(const Seraph_Sparse_VClock* vclock);

/**
 * @brief Serialize vector clock to buffer
 *
 * Writes the clock to a byte buffer for network transmission or storage.
 *
 * @param vclock  Pointer to vector clock
 * @param buffer  Output buffer
 * @param size    Size of output buffer
 * @return Number of bytes written, or 0 on error
 */
size_t seraph_sparse_vclock_serialize(
    const Seraph_Sparse_VClock* vclock,
    uint8_t* buffer,
    size_t size
);

/**
 * @brief Deserialize vector clock from buffer
 *
 * Reads a clock from a byte buffer. Initializes vclock with deserialized data.
 *
 * @param vclock      Pointer to vector clock (will be initialized)
 * @param owner_node  Local node ID for the new clock
 * @param buffer      Input buffer
 * @param size        Size of input buffer
 * @return Number of bytes consumed, or 0 on error
 */
size_t seraph_sparse_vclock_deserialize(
    Seraph_Sparse_VClock* vclock,
    uint16_t owner_node,
    const uint8_t* buffer,
    size_t size
);

/*============================================================================
 * Debugging Functions
 *============================================================================*/

/**
 * @brief Print vector clock to buffer for debugging
 *
 * Formats as: "{node0:ts0, node1:ts1, ...}"
 *
 * @param vclock  Pointer to vector clock
 * @param buffer  Output buffer
 * @param size    Size of output buffer
 * @return Number of characters written (excluding null terminator)
 */
size_t seraph_sparse_vclock_to_string(
    const Seraph_Sparse_VClock* vclock,
    char* buffer,
    size_t size
);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_VECTOR_CLOCK_H */
