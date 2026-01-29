/**
 * @file chronon.c
 * @brief MC7: Chronon - Causal Time Implementation
 *
 * This file implements logical timestamps and vector clocks for
 * causality tracking in SERAPH.
 *
 * Supports both heap allocation (calloc/free) and arena allocation
 * for Atlas-ready persistent memory.
 */

#include "seraph/chronon.h"
#include "seraph/arena.h"
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * FNV-1a Hash Constants
 *============================================================================*/

#define FNV_OFFSET_BASIS 14695981039346656037ULL
#define FNV_PRIME        1099511628211ULL

/*============================================================================
 * Local Clock Operations
 *============================================================================*/

Seraph_Vbit seraph_localclock_init(Seraph_LocalClock* clock, uint32_t id) {
    if (!clock) return SERAPH_VBIT_FALSE;
    if (id == SERAPH_VOID_U32) return SERAPH_VBIT_FALSE;

    clock->current = SERAPH_CHRONON_ZERO;
    clock->id = id;
    clock->reserved = 0;

    return SERAPH_VBIT_TRUE;
}

Seraph_Chronon seraph_localclock_tick(Seraph_LocalClock* clock) {
    if (!clock) return SERAPH_CHRONON_VOID;

    /* Check for overflow (can't increment past MAX) */
    if (clock->current >= SERAPH_CHRONON_MAX) {
        return SERAPH_CHRONON_VOID;
    }

    clock->current++;
    return clock->current;
}

Seraph_Chronon seraph_localclock_merge(Seraph_LocalClock* clock, Seraph_Chronon received) {
    if (!clock) return SERAPH_CHRONON_VOID;
    if (seraph_chronon_is_void(received)) return SERAPH_CHRONON_VOID;

    /* Lamport receive rule: max(local, received) + 1 */
    Seraph_Chronon max_ts = seraph_chronon_max(clock->current, received);
    if (seraph_chronon_is_void(max_ts)) return SERAPH_CHRONON_VOID;

    /* Add 1 with overflow check */
    Seraph_Chronon new_ts = seraph_chronon_add(max_ts, 1);
    if (seraph_chronon_is_void(new_ts)) return SERAPH_CHRONON_VOID;

    clock->current = new_ts;
    return new_ts;
}

/*============================================================================
 * Event Operations
 *============================================================================*/

Seraph_Event seraph_event_create(Seraph_Chronon timestamp, uint32_t source_id,
                                  uint32_t sequence, uint64_t payload_hash) {
    /* Check for VOID inputs */
    if (seraph_chronon_is_void(timestamp)) return SERAPH_EVENT_VOID;
    if (source_id == SERAPH_VOID_U32) return SERAPH_EVENT_VOID;
    if (sequence == SERAPH_VOID_U32) return SERAPH_EVENT_VOID;

    return (Seraph_Event){
        .timestamp = timestamp,
        .predecessor = 0,  /* No predecessor - genesis event */
        .source_id = source_id,
        .sequence = sequence,
        .payload_hash = payload_hash
    };
}

Seraph_Event seraph_event_chain(Seraph_Event predecessor, Seraph_Chronon timestamp,
                                 uint32_t source_id, uint32_t sequence,
                                 uint64_t payload_hash) {
    /* Check for VOID inputs */
    if (seraph_event_is_void(predecessor)) return SERAPH_EVENT_VOID;
    if (seraph_chronon_is_void(timestamp)) return SERAPH_EVENT_VOID;
    if (source_id == SERAPH_VOID_U32) return SERAPH_EVENT_VOID;
    if (sequence == SERAPH_VOID_U32) return SERAPH_EVENT_VOID;

    /* New event must have timestamp > predecessor */
    if (timestamp <= predecessor.timestamp) return SERAPH_EVENT_VOID;

    /* Compute predecessor hash for linking */
    uint64_t pred_hash = seraph_event_hash(predecessor);
    if (pred_hash == SERAPH_VOID_U64) return SERAPH_EVENT_VOID;

    return (Seraph_Event){
        .timestamp = timestamp,
        .predecessor = pred_hash,
        .source_id = source_id,
        .sequence = sequence,
        .payload_hash = payload_hash
    };
}

uint64_t seraph_event_hash(Seraph_Event e) {
    if (seraph_event_is_void(e)) return SERAPH_VOID_U64;

    /* FNV-1a hash algorithm */
    uint64_t hash = FNV_OFFSET_BASIS;

    /* Hash each field */
    hash ^= e.timestamp;
    hash *= FNV_PRIME;

    hash ^= e.predecessor;
    hash *= FNV_PRIME;

    hash ^= (uint64_t)e.source_id;
    hash *= FNV_PRIME;

    hash ^= (uint64_t)e.sequence;
    hash *= FNV_PRIME;

    hash ^= e.payload_hash;
    hash *= FNV_PRIME;

    /* Avoid returning VOID pattern */
    if (hash == SERAPH_VOID_U64) {
        hash = FNV_OFFSET_BASIS;  /* Fallback to offset basis */
    }

    return hash;
}

/*============================================================================
 * Vector Clock Operations
 *============================================================================*/

/**
 * @brief Internal helper to initialize vclock timestamps after allocation
 */
static void vclock_init_timestamps(Seraph_VectorClock* vclock, uint32_t node_count,
                                    uint32_t self_id) {
    vclock->node_count = node_count;
    vclock->self_id = self_id;
    vclock->generation = 1;
    vclock->reserved = 0;

    /* All timestamps start at 0 */
    for (uint32_t i = 0; i < node_count; i++) {
        vclock->timestamps[i] = SERAPH_CHRONON_ZERO;
    }
}

Seraph_Vbit seraph_vclock_init(Seraph_VectorClock* vclock, uint32_t node_count,
                                uint32_t self_id) {
    if (!vclock) return SERAPH_VBIT_VOID;
    if (node_count == 0 || node_count > 65536) return SERAPH_VBIT_FALSE;
    if (self_id >= node_count) return SERAPH_VBIT_FALSE;

    vclock->timestamps = (Seraph_Chronon*)calloc(node_count, sizeof(Seraph_Chronon));
    if (!vclock->timestamps) return SERAPH_VBIT_FALSE;

    vclock->arena = NULL;  /* Heap allocated */
    vclock_init_timestamps(vclock, node_count, self_id);

    return SERAPH_VBIT_TRUE;
}

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
                                      uint32_t node_count, uint32_t self_id) {
    if (!vclock) return SERAPH_VBIT_VOID;
    if (!seraph_arena_is_valid(arena)) return SERAPH_VBIT_VOID;
    if (node_count == 0 || node_count > 65536) return SERAPH_VBIT_FALSE;
    if (self_id >= node_count) return SERAPH_VBIT_FALSE;

    vclock->timestamps = (Seraph_Chronon*)seraph_arena_calloc(
        arena, node_count * sizeof(Seraph_Chronon), sizeof(Seraph_Chronon));
    if (vclock->timestamps == SERAPH_VOID_PTR) {
        vclock->timestamps = NULL;
        return SERAPH_VBIT_FALSE;
    }

    vclock->arena = arena;  /* Arena allocated */
    vclock_init_timestamps(vclock, node_count, self_id);

    return SERAPH_VBIT_TRUE;
}

void seraph_vclock_destroy(Seraph_VectorClock* vclock) {
    if (!vclock) return;

    /* Only free if heap allocated (arena = NULL) */
    if (vclock->arena == NULL && vclock->timestamps != NULL) {
        free(vclock->timestamps);
    }
    /* Arena-allocated timestamps are freed when arena is reset/destroyed */

    vclock->timestamps = NULL;
    vclock->arena = NULL;
    vclock->node_count = 0;
    vclock->self_id = 0;
    vclock->generation = 0;
}

bool seraph_vclock_is_valid(const Seraph_VectorClock* vclock) {
    if (!vclock) return false;
    if (!vclock->timestamps) return false;
    if (vclock->node_count == 0) return false;
    if (vclock->self_id >= vclock->node_count) return false;

    /* Check for any VOID components */
    for (uint32_t i = 0; i < vclock->node_count; i++) {
        if (seraph_chronon_is_void(vclock->timestamps[i])) {
            return false;
        }
    }

    return true;
}

Seraph_Chronon seraph_vclock_tick(Seraph_VectorClock* vclock) {
    if (!vclock || !vclock->timestamps) return SERAPH_CHRONON_VOID;
    if (vclock->self_id >= vclock->node_count) return SERAPH_CHRONON_VOID;

    Seraph_Chronon* self_ts = &vclock->timestamps[vclock->self_id];

    /* Check for overflow */
    if (*self_ts >= SERAPH_CHRONON_MAX) {
        return SERAPH_CHRONON_VOID;
    }

    (*self_ts)++;
    return *self_ts;
}

uint32_t seraph_vclock_snapshot(const Seraph_VectorClock* vclock,
                                 Seraph_Chronon* buffer, uint32_t buffer_size) {
    if (!vclock || !vclock->timestamps || !buffer) return 0;
    if (buffer_size < vclock->node_count) return 0;

    memcpy(buffer, vclock->timestamps, vclock->node_count * sizeof(Seraph_Chronon));
    return vclock->node_count;
}

Seraph_Vbit seraph_vclock_receive(Seraph_VectorClock* vclock,
                                   const Seraph_Chronon* received, uint32_t count) {
    if (!vclock || !vclock->timestamps || !received) return SERAPH_VBIT_VOID;
    if (count != vclock->node_count) return SERAPH_VBIT_FALSE;

    /* Check for VOID in received */
    for (uint32_t i = 0; i < count; i++) {
        if (seraph_chronon_is_void(received[i])) {
            return SERAPH_VBIT_VOID;
        }
    }

    /* Merge: component-wise max */
    for (uint32_t i = 0; i < count; i++) {
        vclock->timestamps[i] = seraph_chronon_max(vclock->timestamps[i], received[i]);
    }

    /* Tick self after merge */
    Seraph_Chronon new_ts = seraph_vclock_tick(vclock);
    if (seraph_chronon_is_void(new_ts)) {
        /* Overflow - mark vector clock as invalid */
        vclock->timestamps[vclock->self_id] = SERAPH_CHRONON_VOID;
        return SERAPH_VBIT_VOID;
    }

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Internal: Compare two timestamp arrays (branchless accumulation)
 *
 * Computes relationship by accumulating flags:
 * - any_lt: true if any A[i] < B[i]
 * - any_gt: true if any A[i] > B[i]
 *
 * From these:
 * - EQUAL:      !any_lt && !any_gt
 * - BEFORE:     any_lt && !any_gt
 * - AFTER:      !any_lt && any_gt
 * - CONCURRENT: any_lt && any_gt
 */
static Seraph_CausalOrder vclock_compare_arrays(const Seraph_Chronon* a,
                                                 const Seraph_Chronon* b,
                                                 uint32_t count) {
    uint64_t any_lt = 0;  /* Any A[i] < B[i] */
    uint64_t any_gt = 0;  /* Any A[i] > B[i] */
    uint64_t any_void = 0; /* Any VOID component */

    for (uint32_t i = 0; i < count; i++) {
        /* Branchless VOID check */
        any_void |= seraph_chronon_void_mask(a[i]);
        any_void |= seraph_chronon_void_mask(b[i]);

        /* Branchless comparison flags */
        uint64_t lt = -(uint64_t)(a[i] < b[i]);
        uint64_t gt = -(uint64_t)(a[i] > b[i]);

        any_lt |= lt;
        any_gt |= gt;
    }

    /* If any VOID, result is VOID */
    if (any_void) return SERAPH_CAUSAL_VOID;

    /* Determine relationship */
    int has_lt = (any_lt != 0);
    int has_gt = (any_gt != 0);

    if (!has_lt && !has_gt) return SERAPH_CAUSAL_EQUAL;
    if (has_lt && !has_gt)  return SERAPH_CAUSAL_BEFORE;
    if (!has_lt && has_gt)  return SERAPH_CAUSAL_AFTER;
    return SERAPH_CAUSAL_CONCURRENT;
}

Seraph_CausalOrder seraph_vclock_compare(const Seraph_VectorClock* a,
                                          const Seraph_VectorClock* b) {
    if (!a || !b) return SERAPH_CAUSAL_VOID;
    if (!a->timestamps || !b->timestamps) return SERAPH_CAUSAL_VOID;
    if (a->node_count != b->node_count) return SERAPH_CAUSAL_VOID;

    return vclock_compare_arrays(a->timestamps, b->timestamps, a->node_count);
}

Seraph_CausalOrder seraph_vclock_compare_snapshot(const Seraph_VectorClock* vclock,
                                                   const Seraph_Chronon* snapshot,
                                                   uint32_t count) {
    if (!vclock || !vclock->timestamps || !snapshot) return SERAPH_CAUSAL_VOID;
    if (vclock->node_count != count) return SERAPH_CAUSAL_VOID;

    return vclock_compare_arrays(vclock->timestamps, snapshot, count);
}

Seraph_Vbit seraph_vclock_happens_before(const Seraph_VectorClock* a,
                                          const Seraph_VectorClock* b) {
    Seraph_CausalOrder order = seraph_vclock_compare(a, b);

    switch (order) {
        case SERAPH_CAUSAL_BEFORE: return SERAPH_VBIT_TRUE;
        case SERAPH_CAUSAL_AFTER:
        case SERAPH_CAUSAL_EQUAL:
        case SERAPH_CAUSAL_CONCURRENT: return SERAPH_VBIT_FALSE;
        default: return SERAPH_VBIT_VOID;
    }
}

Seraph_Vbit seraph_vclock_is_concurrent(const Seraph_VectorClock* a,
                                         const Seraph_VectorClock* b) {
    Seraph_CausalOrder order = seraph_vclock_compare(a, b);

    switch (order) {
        case SERAPH_CAUSAL_CONCURRENT: return SERAPH_VBIT_TRUE;
        case SERAPH_CAUSAL_BEFORE:
        case SERAPH_CAUSAL_AFTER:
        case SERAPH_CAUSAL_EQUAL: return SERAPH_VBIT_FALSE;
        default: return SERAPH_VBIT_VOID;
    }
}

Seraph_Vbit seraph_vclock_copy(Seraph_VectorClock* dst,
                                const Seraph_VectorClock* src) {
    if (!dst || !src) return SERAPH_VBIT_VOID;
    if (!dst->timestamps || !src->timestamps) return SERAPH_VBIT_VOID;
    if (dst->node_count != src->node_count) return SERAPH_VBIT_FALSE;

    memcpy(dst->timestamps, src->timestamps, src->node_count * sizeof(Seraph_Chronon));
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_vclock_merge(Seraph_VectorClock* dst,
                                 const Seraph_VectorClock* other) {
    if (!dst || !other) return SERAPH_VBIT_VOID;
    if (!dst->timestamps || !other->timestamps) return SERAPH_VBIT_VOID;
    if (dst->node_count != other->node_count) return SERAPH_VBIT_FALSE;

    /* Component-wise max (branchless via seraph_chronon_max) */
    for (uint32_t i = 0; i < dst->node_count; i++) {
        Seraph_Chronon merged = seraph_chronon_max(dst->timestamps[i],
                                                    other->timestamps[i]);
        if (seraph_chronon_is_void(merged)) {
            return SERAPH_VBIT_VOID;
        }
        dst->timestamps[i] = merged;
    }

    return SERAPH_VBIT_TRUE;
}
