/**
 * @file chronon.h
 * @brief MC7: Chronon - Causal Time and Logical Timestamps
 *
 * Chronon provides deterministic, causality-tracking time for SERAPH.
 * Instead of wall-clock time (which can drift, be NTP-adjusted, or vary
 * between machines), Chronon uses logical timestamps that track what
 * happens before what.
 *
 * CORE CONCEPTS:
 *
 *   1. LAMPORT TIMESTAMPS: Simple monotonic counters that increment on
 *      each local event and merge to max+1 on message receive. Provides
 *      total ordering but can't detect true concurrency.
 *
 *   2. VECTOR CLOCKS: Array of timestamps (one per node) that can
 *      detect whether events are causally related or truly concurrent.
 *      If A[i] <= B[i] for all i, and A[j] < B[j] for some j, then A → B.
 *
 *   3. EVENTS: Immutable records with timestamps and predecessor links,
 *      forming a directed acyclic graph (DAG) of causal history.
 *
 * VOID SEMANTICS:
 *   - VOID timestamps represent invalid/unknown time
 *   - VOID propagates through operations (comparing with VOID yields VOID)
 *   - Vector clocks with any VOID component are entirely VOID
 *
 * CAUSAL ORDERING:
 *   - BEFORE:     A happened before B (A → B)
 *   - AFTER:      B happened before A (B → A)
 *   - CONCURRENT: Neither ordered (A || B) - true parallelism
 *   - EQUAL:      Same event (A == B)
 *   - VOID:       Cannot determine (invalid input)
 */

#ifndef SERAPH_CHRONON_H
#define SERAPH_CHRONON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "seraph/void.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Basic Types
 *============================================================================*/

/**
 * @brief Logical timestamp (Lamport clock value)
 *
 * A 64-bit monotonic counter. Never decreases within a single clock.
 * VOID value (all 1s) represents an invalid/unknown timestamp.
 */
typedef uint64_t Seraph_Chronon;

/** VOID timestamp - represents invalid/unknown time */
#define SERAPH_CHRONON_VOID    SERAPH_VOID_U64

/** Zero timestamp - the beginning of time */
#define SERAPH_CHRONON_ZERO    0ULL

/** Maximum valid timestamp (one less than VOID) */
#define SERAPH_CHRONON_MAX     (SERAPH_VOID_U64 - 1)

/*============================================================================
 * Causal Ordering
 *============================================================================*/

/**
 * @brief Result of comparing two timestamps or events
 *
 * For scalar timestamps, only BEFORE/EQUAL/AFTER/VOID are possible.
 * For vector clocks, CONCURRENT is also possible (true parallelism).
 */
typedef enum {
    SERAPH_CAUSAL_BEFORE     = -1,  /**< A happens-before B (A → B) */
    SERAPH_CAUSAL_EQUAL      =  0,  /**< A and B are the same */
    SERAPH_CAUSAL_AFTER      =  1,  /**< B happens-before A (B → A) */
    SERAPH_CAUSAL_CONCURRENT =  2,  /**< Neither ordered (A || B) */
    SERAPH_CAUSAL_VOID       = 0xFF /**< Cannot determine (VOID input) */
} Seraph_CausalOrder;

/*============================================================================
 * Event Structure
 *============================================================================*/

/**
 * @brief An event in the causal history
 *
 * Events are immutable records that capture a point in logical time.
 * They link to their causal predecessor, forming a DAG.
 */
typedef struct {
    Seraph_Chronon timestamp;      /**< When this event occurred */
    uint64_t       predecessor;    /**< Hash of predecessor event (0 if genesis) */
    uint32_t       source_id;      /**< ID of the node/process that created this */
    uint32_t       sequence;       /**< Sequence number within source */
    uint64_t       payload_hash;   /**< Hash of event payload for integrity */
} Seraph_Event;

/** VOID event - represents an invalid/nonexistent event */
#define SERAPH_EVENT_VOID ((Seraph_Event){ \
    SERAPH_CHRONON_VOID, SERAPH_VOID_U64, SERAPH_VOID_U32, \
    SERAPH_VOID_U32, SERAPH_VOID_U64 })

/** Genesis event - the first event (no predecessor) */
#define SERAPH_EVENT_GENESIS ((Seraph_Event){ \
    SERAPH_CHRONON_ZERO, 0, 0, 0, 0 })

/*============================================================================
 * Vector Clock Structure
 *============================================================================*/

/* Forward declaration for arena (avoids circular include) */
struct Seraph_Arena;

/**
 * @brief Vector clock for distributed causality tracking
 *
 * Contains one timestamp per node in the system. Comparing vector clocks
 * can detect true concurrency (when events happen independently).
 *
 * Can be allocated from heap (arena=NULL) or from an arena (for persistence).
 */
typedef struct {
    Seraph_Chronon* timestamps;    /**< Array of timestamps [node_count] */
    struct Seraph_Arena* arena;           /**< Arena if arena-allocated, NULL if heap */
    uint32_t        node_count;    /**< Number of nodes */
    uint32_t        self_id;       /**< This node's ID (0 to node_count-1) */
    uint32_t        generation;    /**< Allocation generation for temporal safety */
    uint32_t        reserved;      /**< Reserved for future use */
} Seraph_VectorClock;

/*============================================================================
 * Local Clock
 *============================================================================*/

/**
 * @brief Local logical clock (wraps a single Chronon)
 *
 * Provides atomic tick operations for single-threaded use.
 */
typedef struct {
    Seraph_Chronon current;        /**< Current timestamp */
    uint32_t       id;             /**< This clock's unique ID */
    uint32_t       reserved;       /**< Reserved for alignment */
} Seraph_LocalClock;

/*============================================================================
 * Chronon Detection and Masking (Branchless)
 *============================================================================*/

/**
 * @brief Check if chronon is VOID
 */
static inline bool seraph_chronon_is_void(Seraph_Chronon t) {
    return t == SERAPH_CHRONON_VOID;
}

/**
 * @brief Check if chronon exists (is not VOID)
 */
static inline bool seraph_chronon_exists(Seraph_Chronon t) {
    return t != SERAPH_CHRONON_VOID;
}

/**
 * @brief Generate VOID mask for chronon (branchless)
 * @return All 1s if VOID, all 0s if valid
 */
static inline uint64_t seraph_chronon_void_mask(Seraph_Chronon t) {
    return -(uint64_t)(t == SERAPH_CHRONON_VOID);
}

/**
 * @brief Generate VOID mask for two chronons (branchless)
 * @return All 1s if either is VOID, all 0s if both valid
 */
static inline uint64_t seraph_chronon_void_mask2(Seraph_Chronon a, Seraph_Chronon b) {
    return seraph_chronon_void_mask(a) | seraph_chronon_void_mask(b);
}

/**
 * @brief Branchless select for chronon values
 * @param if_void Value to return if mask is all 1s
 * @param if_valid Value to return if mask is all 0s
 * @param mask Result of seraph_chronon_void_mask
 */
static inline Seraph_Chronon seraph_chronon_select(
    Seraph_Chronon if_void, Seraph_Chronon if_valid, uint64_t mask) {
    return (if_void & mask) | (if_valid & ~mask);
}

/*============================================================================
 * Event Detection
 *============================================================================*/

/**
 * @brief Check if event is VOID
 */
static inline bool seraph_event_is_void(Seraph_Event e) {
    return e.timestamp == SERAPH_CHRONON_VOID ||
           e.predecessor == SERAPH_VOID_U64 ||
           e.source_id == SERAPH_VOID_U32;
}

/**
 * @brief Check if event exists (is not VOID)
 */
static inline bool seraph_event_exists(Seraph_Event e) {
    return !seraph_event_is_void(e);
}

/**
 * @brief Check if event is genesis (no predecessor)
 */
static inline bool seraph_event_is_genesis(Seraph_Event e) {
    return e.predecessor == 0 && !seraph_event_is_void(e);
}

/*============================================================================
 * Local Clock Operations
 *============================================================================*/

/**
 * @brief Initialize a local clock
 *
 * @param clock Pointer to clock to initialize
 * @param id Unique identifier for this clock
 * @return TRUE if success, FALSE if clock is NULL, VOID never
 */
Seraph_Vbit seraph_localclock_init(Seraph_LocalClock* clock, uint32_t id);

/**
 * @brief Read the current timestamp
 *
 * @param clock The clock to read
 * @return Current timestamp, or VOID if clock is invalid
 */
static inline Seraph_Chronon seraph_localclock_read(const Seraph_LocalClock* clock) {
    if (!clock) return SERAPH_CHRONON_VOID;
    return clock->current;
}

/**
 * @brief Tick the clock (increment by 1)
 *
 * @param clock The clock to tick
 * @return New timestamp, or VOID if overflow or invalid
 */
Seraph_Chronon seraph_localclock_tick(Seraph_LocalClock* clock);

/**
 * @brief Merge with a received timestamp
 *
 * Sets clock to max(current, received) + 1.
 * This is the Lamport clock receive rule.
 *
 * @param clock The local clock
 * @param received Timestamp received from another node
 * @return New timestamp, or VOID if overflow or invalid
 */
Seraph_Chronon seraph_localclock_merge(Seraph_LocalClock* clock, Seraph_Chronon received);

/*============================================================================
 * Scalar Chronon Operations
 *============================================================================*/

/**
 * @brief Compare two scalar timestamps
 *
 * @param a First timestamp
 * @param b Second timestamp
 * @return BEFORE if a < b, EQUAL if a == b, AFTER if a > b, VOID if either VOID
 */
static inline Seraph_CausalOrder seraph_chronon_compare(Seraph_Chronon a, Seraph_Chronon b) {
    uint64_t void_mask = seraph_chronon_void_mask2(a, b);
    if (void_mask) return SERAPH_CAUSAL_VOID;

    if (a < b) return SERAPH_CAUSAL_BEFORE;
    if (a > b) return SERAPH_CAUSAL_AFTER;
    return SERAPH_CAUSAL_EQUAL;
}

/**
 * @brief Get maximum of two timestamps (branchless)
 *
 * @param a First timestamp
 * @param b Second timestamp
 * @return Maximum, or VOID if either is VOID
 */
static inline Seraph_Chronon seraph_chronon_max(Seraph_Chronon a, Seraph_Chronon b) {
    uint64_t void_mask = seraph_chronon_void_mask2(a, b);
    uint64_t a_ge_b = -(uint64_t)(a >= b);
    Seraph_Chronon result = (a & a_ge_b) | (b & ~a_ge_b);
    return seraph_chronon_select(SERAPH_CHRONON_VOID, result, void_mask);
}

/**
 * @brief Get minimum of two timestamps (branchless)
 *
 * @param a First timestamp
 * @param b Second timestamp
 * @return Minimum, or VOID if either is VOID
 */
static inline Seraph_Chronon seraph_chronon_min(Seraph_Chronon a, Seraph_Chronon b) {
    uint64_t void_mask = seraph_chronon_void_mask2(a, b);
    uint64_t a_le_b = -(uint64_t)(a <= b);
    Seraph_Chronon result = (a & a_le_b) | (b & ~a_le_b);
    return seraph_chronon_select(SERAPH_CHRONON_VOID, result, void_mask);
}

/**
 * @brief Increment timestamp by delta (branchless, VOID on overflow)
 *
 * @param t Timestamp to increment
 * @param delta Amount to add
 * @return t + delta, or VOID if overflow or t was VOID
 */
static inline Seraph_Chronon seraph_chronon_add(Seraph_Chronon t, uint64_t delta) {
    uint64_t void_mask = seraph_chronon_void_mask(t);
    uint64_t result = t + delta;
    /* Overflow if result < t or result >= VOID */
    uint64_t overflow = -(uint64_t)(result < t || result >= SERAPH_CHRONON_VOID);
    return seraph_chronon_select(SERAPH_CHRONON_VOID, result, void_mask | overflow);
}

/*============================================================================
 * Event Operations
 *============================================================================*/

/**
 * @brief Create a new event
 *
 * @param timestamp When the event occurred
 * @param source_id Which node created this event
 * @param sequence Sequence number within source
 * @param payload_hash Hash of event payload
 * @return New event, or VOID if timestamp is VOID
 */
Seraph_Event seraph_event_create(Seraph_Chronon timestamp, uint32_t source_id,
                                  uint32_t sequence, uint64_t payload_hash);

/**
 * @brief Create a new event chained to a predecessor
 *
 * @param predecessor The event this follows
 * @param timestamp When the new event occurred (must be > predecessor)
 * @param source_id Which node created this event
 * @param sequence Sequence number within source
 * @param payload_hash Hash of event payload
 * @return New event linked to predecessor, or VOID if invalid
 */
Seraph_Event seraph_event_chain(Seraph_Event predecessor, Seraph_Chronon timestamp,
                                 uint32_t source_id, uint32_t sequence,
                                 uint64_t payload_hash);

/**
 * @brief Compute hash of an event (for linking)
 *
 * Uses FNV-1a algorithm for speed and good distribution.
 *
 * @param e The event to hash
 * @return 64-bit hash, or VOID if event is VOID
 */
uint64_t seraph_event_hash(Seraph_Event e);

/**
 * @brief Compare two events causally
 *
 * @param a First event
 * @param b Second event
 * @return Causal ordering (based on timestamps)
 */
static inline Seraph_CausalOrder seraph_event_compare(Seraph_Event a, Seraph_Event b) {
    if (seraph_event_is_void(a) || seraph_event_is_void(b)) {
        return SERAPH_CAUSAL_VOID;
    }
    return seraph_chronon_compare(a.timestamp, b.timestamp);
}

/*============================================================================
 * Vector Clock Operations
 *============================================================================*/

/**
 * @brief Initialize a vector clock
 *
 * @param vclock Pointer to vector clock to initialize
 * @param node_count Number of nodes in the system
 * @param self_id This node's ID (0 to node_count-1)
 * @return TRUE if success, FALSE if allocation failed, VOID if params invalid
 */
Seraph_Vbit seraph_vclock_init(Seraph_VectorClock* vclock, uint32_t node_count,
                                uint32_t self_id);

/**
 * @brief Initialize vector clock with arena allocation (Atlas-ready)
 *
 * Allocates the timestamp array from the given arena instead of the heap.
 * The vector clock will persist with the arena if it's mmap-backed.
 *
 * @param vclock Vector clock to initialize
 * @param arena Arena to allocate from
 * @param node_count Number of nodes in the distributed system
 * @param self_id This node's ID (0 to node_count-1)
 * @return TRUE if success, FALSE if failed, VOID if params invalid
 */
Seraph_Vbit seraph_vclock_init_arena(Seraph_VectorClock* vclock, struct Seraph_Arena* arena,
                                      uint32_t node_count, uint32_t self_id);

/**
 * @brief Destroy a vector clock and free resources
 *
 * If vclock was heap-allocated, frees memory. If arena-allocated, does nothing
 * (memory is freed when arena is reset/destroyed).
 *
 * @param vclock The vector clock to destroy
 */
void seraph_vclock_destroy(Seraph_VectorClock* vclock);

/**
 * @brief Check if vector clock is valid
 *
 * @param vclock The vector clock to check
 * @return true if valid, false if NULL, uninitialized, or has VOID components
 */
bool seraph_vclock_is_valid(const Seraph_VectorClock* vclock);

/**
 * @brief Tick the vector clock (increment self's component)
 *
 * @param vclock The vector clock to tick
 * @return New value of self's component, or VOID if overflow or invalid
 */
Seraph_Chronon seraph_vclock_tick(Seraph_VectorClock* vclock);

/**
 * @brief Get current timestamp for a specific node
 *
 * @param vclock The vector clock
 * @param node_id Which node's timestamp to get
 * @return Timestamp, or VOID if invalid
 */
static inline Seraph_Chronon seraph_vclock_get(const Seraph_VectorClock* vclock,
                                                uint32_t node_id) {
    if (!vclock || !vclock->timestamps || node_id >= vclock->node_count) {
        return SERAPH_CHRONON_VOID;
    }
    return vclock->timestamps[node_id];
}

/**
 * @brief Create a snapshot of the vector clock for sending
 *
 * @param vclock The source vector clock
 * @param buffer Destination buffer for timestamps
 * @param buffer_size Size of buffer in timestamps
 * @return Number of timestamps copied, or 0 if error
 */
uint32_t seraph_vclock_snapshot(const Seraph_VectorClock* vclock,
                                 Seraph_Chronon* buffer, uint32_t buffer_size);

/**
 * @brief Receive and merge a vector clock from another node
 *
 * For each component i: self[i] = max(self[i], received[i])
 * Then increment self's component.
 *
 * @param vclock The local vector clock
 * @param received Array of received timestamps
 * @param count Number of timestamps in received array
 * @return TRUE if success, FALSE if size mismatch, VOID if invalid
 */
Seraph_Vbit seraph_vclock_receive(Seraph_VectorClock* vclock,
                                   const Seraph_Chronon* received, uint32_t count);

/**
 * @brief Compare two vector clocks for causal ordering
 *
 * @param a First vector clock
 * @param b Second vector clock
 * @return BEFORE if a→b, AFTER if b→a, CONCURRENT if a||b, EQUAL if a==b, VOID if invalid
 */
Seraph_CausalOrder seraph_vclock_compare(const Seraph_VectorClock* a,
                                          const Seraph_VectorClock* b);

/**
 * @brief Compare vector clock against a snapshot
 *
 * @param vclock The vector clock
 * @param snapshot Array of timestamps to compare against
 * @param count Number of timestamps in snapshot
 * @return Causal ordering
 */
Seraph_CausalOrder seraph_vclock_compare_snapshot(const Seraph_VectorClock* vclock,
                                                   const Seraph_Chronon* snapshot,
                                                   uint32_t count);

/**
 * @brief Check if a happens-before b
 *
 * @param a First vector clock
 * @param b Second vector clock
 * @return TRUE if a→b, FALSE if not, VOID if cannot determine
 */
Seraph_Vbit seraph_vclock_happens_before(const Seraph_VectorClock* a,
                                          const Seraph_VectorClock* b);

/**
 * @brief Check if two vector clocks are concurrent
 *
 * @param a First vector clock
 * @param b Second vector clock
 * @return TRUE if a||b (neither ordered), FALSE otherwise, VOID if invalid
 */
Seraph_Vbit seraph_vclock_is_concurrent(const Seraph_VectorClock* a,
                                         const Seraph_VectorClock* b);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Copy a vector clock
 *
 * @param dst Destination (must be initialized with same node_count)
 * @param src Source vector clock
 * @return TRUE if success, FALSE if size mismatch, VOID if invalid
 */
Seraph_Vbit seraph_vclock_copy(Seraph_VectorClock* dst,
                                const Seraph_VectorClock* src);

/**
 * @brief Merge two vector clocks (component-wise max)
 *
 * @param dst Destination (receives max of self and other)
 * @param other The other vector clock
 * @return TRUE if success, FALSE if size mismatch, VOID if invalid
 */
Seraph_Vbit seraph_vclock_merge(Seraph_VectorClock* dst,
                                 const Seraph_VectorClock* other);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_CHRONON_H */
