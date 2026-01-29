/**
 * @file vector_clock.c
 * @brief Sparse Vector Clock Implementation for Aether DSM
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This implements a sparse vector clock optimized for distributed shared memory.
 * Entries are kept sorted by node_id for efficient binary search and merge operations.
 *
 * COMPLEXITY:
 *   - seraph_sparse_vclock_get():       O(log n) via binary search
 *   - seraph_sparse_vclock_set():       O(n) worst case (insertion in sorted array)
 *   - seraph_sparse_vclock_increment(): O(log n) lookup + O(1) update
 *   - seraph_sparse_vclock_merge():     O(n + m) merge-sort style
 *   - seraph_sparse_vclock_compare():   O(n + m) simultaneous traversal
 *
 * where n and m are the number of entries in the clocks being operated on.
 * Since most pages are touched by few nodes (n << max_nodes), this is efficient.
 */

#include "seraph/vector_clock.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Internal Helper Functions
 *============================================================================*/

/**
 * @brief Binary search for node_id in sorted entries array
 *
 * @param entries  Sorted array of entries
 * @param count    Number of entries
 * @param node_id  Node ID to find
 * @return Index of entry if found, or insertion point (negative-1) if not found
 *         e.g., returns -1 for insertion at index 0, -2 for index 1, etc.
 */
static int32_t vclock_binary_search(
    const Seraph_VClock_Entry* entries,
    uint16_t count,
    uint16_t node_id
) {
    if (entries == NULL || count == 0) {
        return -1;  /* Insert at position 0 */
    }

    int32_t low = 0;
    int32_t high = (int32_t)count - 1;

    while (low <= high) {
        int32_t mid = low + (high - low) / 2;
        uint16_t mid_node = entries[mid].node_id;

        if (mid_node == node_id) {
            return mid;  /* Found */
        } else if (mid_node < node_id) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    /* Not found, return insertion point encoded as -(index + 1) */
    return -(low + 1);
}

/**
 * @brief Ensure capacity for at least one more entry
 *
 * @param vclock  Vector clock to potentially grow
 * @return true if capacity is sufficient or growth succeeded
 */
static bool vclock_ensure_capacity(Seraph_Sparse_VClock* vclock) {
    if (vclock == NULL) {
        return false;
    }

    /* Check if we have room */
    if (vclock->count < vclock->capacity) {
        return true;
    }

    /* Can't grow borrowed buffers */
    if (vclock->flags & SERAPH_SPARSE_VCLOCK_FLAG_BORROWED) {
        return false;
    }

    /* Check saturation limit */
    if (vclock->capacity >= SERAPH_SPARSE_VCLOCK_MAX_ENTRIES) {
        vclock->flags |= SERAPH_SPARSE_VCLOCK_FLAG_SATURATED;
        return false;
    }

    /* Calculate new capacity with growth factor */
    uint32_t new_cap = ((uint32_t)vclock->capacity * SERAPH_SPARSE_VCLOCK_GROWTH_NUM) /
                       SERAPH_SPARSE_VCLOCK_GROWTH_DEN;
    if (new_cap < (uint32_t)(vclock->capacity + 1)) {
        new_cap = (uint32_t)(vclock->capacity + 1);
    }
    if (new_cap > SERAPH_SPARSE_VCLOCK_MAX_ENTRIES) {
        new_cap = SERAPH_SPARSE_VCLOCK_MAX_ENTRIES;
    }

    /* Reallocate */
    Seraph_VClock_Entry* new_entries = (Seraph_VClock_Entry*)realloc(
        vclock->entries,
        new_cap * sizeof(Seraph_VClock_Entry)
    );

    if (new_entries == NULL) {
        return false;
    }

    vclock->entries = new_entries;
    vclock->capacity = (uint16_t)new_cap;
    return true;
}

/**
 * @brief Insert entry at specified index, shifting existing entries
 *
 * @param vclock    Vector clock
 * @param index     Insertion index
 * @param node_id   Node ID for new entry
 * @param timestamp Timestamp for new entry
 * @return true on success, false on failure
 */
static bool vclock_insert_at(
    Seraph_Sparse_VClock* vclock,
    uint16_t index,
    uint16_t node_id,
    uint64_t timestamp
) {
    if (!vclock_ensure_capacity(vclock)) {
        return false;
    }

    /* Shift entries to make room */
    if (index < vclock->count) {
        memmove(
            &vclock->entries[index + 1],
            &vclock->entries[index],
            (vclock->count - index) * sizeof(Seraph_VClock_Entry)
        );
    }

    /* Insert new entry */
    vclock->entries[index].node_id = node_id;
    vclock->entries[index].reserved = 0;
    vclock->entries[index].timestamp = timestamp;
    vclock->count++;

    return true;
}

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

Seraph_Vbit seraph_sparse_vclock_init(Seraph_Sparse_VClock* vclock, uint16_t owner_node) {
    if (vclock == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Allocate initial capacity */
    vclock->entries = (Seraph_VClock_Entry*)calloc(
        SERAPH_SPARSE_VCLOCK_INITIAL_CAPACITY,
        sizeof(Seraph_VClock_Entry)
    );

    if (vclock->entries == NULL) {
        memset(vclock, 0, sizeof(Seraph_Sparse_VClock));
        return SERAPH_VBIT_FALSE;
    }

    vclock->count = 0;
    vclock->capacity = SERAPH_SPARSE_VCLOCK_INITIAL_CAPACITY;
    vclock->owner_node = owner_node;
    vclock->flags = SERAPH_SPARSE_VCLOCK_FLAG_NONE;

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_sparse_vclock_init_with_buffer(
    Seraph_Sparse_VClock* vclock,
    uint16_t owner_node,
    Seraph_VClock_Entry* buffer,
    uint16_t capacity
) {
    if (vclock == NULL || buffer == NULL) {
        return SERAPH_VBIT_VOID;
    }

    vclock->entries = buffer;
    vclock->count = 0;
    vclock->capacity = capacity;
    vclock->owner_node = owner_node;
    vclock->flags = SERAPH_SPARSE_VCLOCK_FLAG_BORROWED;

    return SERAPH_VBIT_TRUE;
}

void seraph_sparse_vclock_destroy(Seraph_Sparse_VClock* vclock) {
    if (vclock == NULL) {
        return;
    }

    /* Only free if not borrowed */
    if (vclock->entries != NULL && !(vclock->flags & SERAPH_SPARSE_VCLOCK_FLAG_BORROWED)) {
        free(vclock->entries);
    }

    memset(vclock, 0, sizeof(Seraph_Sparse_VClock));
}

void seraph_sparse_vclock_reset(Seraph_Sparse_VClock* vclock) {
    if (vclock == NULL) {
        return;
    }

    vclock->count = 0;
    vclock->flags &= ~SERAPH_SPARSE_VCLOCK_FLAG_SATURATED;  /* Clear saturation */
}

Seraph_Vbit seraph_sparse_vclock_copy(Seraph_Sparse_VClock* dst, const Seraph_Sparse_VClock* src) {
    if (dst == NULL || src == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Initialize destination with same owner */
    Seraph_Vbit result = seraph_sparse_vclock_init(dst, src->owner_node);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Copy entries */
    if (src->count > 0) {
        /* Ensure capacity */
        if (src->count > dst->capacity) {
            Seraph_VClock_Entry* new_entries = (Seraph_VClock_Entry*)realloc(
                dst->entries,
                src->count * sizeof(Seraph_VClock_Entry)
            );
            if (new_entries == NULL) {
                seraph_sparse_vclock_destroy(dst);
                return SERAPH_VBIT_FALSE;
            }
            dst->entries = new_entries;
            dst->capacity = src->count;
        }

        memcpy(dst->entries, src->entries, src->count * sizeof(Seraph_VClock_Entry));
        dst->count = src->count;
    }

    dst->flags = src->flags & ~SERAPH_SPARSE_VCLOCK_FLAG_BORROWED;  /* New copy owns its memory */

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Query Functions
 *============================================================================*/

uint64_t seraph_sparse_vclock_get(const Seraph_Sparse_VClock* vclock, uint16_t node_id) {
    if (vclock == NULL) {
        return SERAPH_VOID_U64;
    }

    if (vclock->count == 0) {
        return 0;  /* Not present means timestamp 0 */
    }

    int32_t index = vclock_binary_search(vclock->entries, vclock->count, node_id);
    if (index >= 0) {
        return vclock->entries[index].timestamp;
    }

    return 0;  /* Not present */
}

/*============================================================================
 * Modification Functions
 *============================================================================*/

uint64_t seraph_sparse_vclock_increment(Seraph_Sparse_VClock* vclock) {
    if (vclock == NULL) {
        return SERAPH_VOID_U64;
    }

    /* Find or create entry for owner node */
    int32_t index = vclock_binary_search(vclock->entries, vclock->count, vclock->owner_node);

    if (index >= 0) {
        /* Entry exists, increment */
        uint64_t new_ts = vclock->entries[index].timestamp + 1;

        /* Check for overflow */
        if (new_ts == SERAPH_VOID_U64 || new_ts < vclock->entries[index].timestamp) {
            return SERAPH_VOID_U64;  /* Overflow */
        }

        vclock->entries[index].timestamp = new_ts;
        return new_ts;
    }

    /* Entry doesn't exist, create with timestamp 1 */
    uint16_t insert_idx = (uint16_t)(-(index + 1));
    if (!vclock_insert_at(vclock, insert_idx, vclock->owner_node, 1)) {
        return SERAPH_VOID_U64;  /* Allocation failed */
    }

    return 1;
}

Seraph_Vbit seraph_sparse_vclock_set(
    Seraph_Sparse_VClock* vclock,
    uint16_t node_id,
    uint64_t timestamp
) {
    if (vclock == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Don't store VOID timestamps */
    if (timestamp == SERAPH_VOID_U64) {
        return SERAPH_VBIT_FALSE;
    }

    int32_t index = vclock_binary_search(vclock->entries, vclock->count, node_id);

    if (index >= 0) {
        /* Entry exists, update */
        vclock->entries[index].timestamp = timestamp;
        return SERAPH_VBIT_TRUE;
    }

    /* Entry doesn't exist, insert */
    uint16_t insert_idx = (uint16_t)(-(index + 1));
    if (!vclock_insert_at(vclock, insert_idx, node_id, timestamp)) {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_sparse_vclock_merge(Seraph_Sparse_VClock* vclock, const Seraph_Sparse_VClock* other) {
    if (vclock == NULL || other == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (other->count == 0) {
        return SERAPH_VBIT_TRUE;  /* Nothing to merge */
    }

    /* Merge-sort style: iterate through both in parallel */
    /* Build a new merged array, then swap */

    uint16_t max_count = vclock->count + other->count;
    if (max_count > SERAPH_SPARSE_VCLOCK_MAX_ENTRIES) {
        max_count = SERAPH_SPARSE_VCLOCK_MAX_ENTRIES;
    }

    Seraph_VClock_Entry* merged = (Seraph_VClock_Entry*)calloc(
        max_count, sizeof(Seraph_VClock_Entry)
    );
    if (merged == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    uint16_t i = 0;  /* Index into vclock */
    uint16_t j = 0;  /* Index into other */
    uint16_t k = 0;  /* Index into merged */

    while (i < vclock->count && j < other->count && k < max_count) {
        uint16_t node_a = vclock->entries[i].node_id;
        uint16_t node_b = other->entries[j].node_id;

        if (node_a < node_b) {
            merged[k++] = vclock->entries[i++];
        } else if (node_b < node_a) {
            merged[k++] = other->entries[j++];
        } else {
            /* Same node: take maximum timestamp */
            merged[k].node_id = node_a;
            merged[k].reserved = 0;
            merged[k].timestamp = (vclock->entries[i].timestamp > other->entries[j].timestamp)
                                  ? vclock->entries[i].timestamp
                                  : other->entries[j].timestamp;
            k++;
            i++;
            j++;
        }
    }

    /* Copy remaining from vclock */
    while (i < vclock->count && k < max_count) {
        merged[k++] = vclock->entries[i++];
    }

    /* Copy remaining from other */
    while (j < other->count && k < max_count) {
        merged[k++] = other->entries[j++];
    }

    /* Check if we hit saturation */
    bool saturated = (i < vclock->count || j < other->count);

    /* Replace vclock's entries with merged */
    if (!(vclock->flags & SERAPH_SPARSE_VCLOCK_FLAG_BORROWED)) {
        free(vclock->entries);
    }

    vclock->entries = merged;
    vclock->count = k;
    vclock->capacity = max_count;
    vclock->flags &= ~SERAPH_SPARSE_VCLOCK_FLAG_BORROWED;  /* We now own this memory */

    if (saturated) {
        vclock->flags |= SERAPH_SPARSE_VCLOCK_FLAG_SATURATED;
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Comparison Functions
 *============================================================================*/

Seraph_Sparse_VClock_Order seraph_sparse_vclock_compare(
    const Seraph_Sparse_VClock* a,
    const Seraph_Sparse_VClock* b
) {
    if (a == NULL || b == NULL) {
        return SERAPH_SPARSE_VCLOCK_VOID;
    }

    /* Empty clocks are equal to each other (both all zeros) */
    if (a->count == 0 && b->count == 0) {
        return SERAPH_SPARSE_VCLOCK_EQUAL;
    }

    /* Track comparison results */
    bool a_has_greater = false;  /* a[i] > b[i] for some i */
    bool b_has_greater = false;  /* b[i] > a[i] for some i */

    /* Merge-style traversal */
    uint16_t i = 0;
    uint16_t j = 0;

    while (i < a->count || j < b->count) {
        uint16_t node_a = (i < a->count) ? a->entries[i].node_id : UINT16_MAX;
        uint16_t node_b = (j < b->count) ? b->entries[j].node_id : UINT16_MAX;

        uint64_t ts_a, ts_b;

        if (node_a < node_b) {
            /* Node in a but not in b: compare a[node_a] vs 0 */
            ts_a = a->entries[i].timestamp;
            ts_b = 0;
            i++;
        } else if (node_b < node_a) {
            /* Node in b but not in a: compare 0 vs b[node_b] */
            ts_a = 0;
            ts_b = b->entries[j].timestamp;
            j++;
        } else {
            /* Same node in both */
            ts_a = a->entries[i].timestamp;
            ts_b = b->entries[j].timestamp;
            i++;
            j++;
        }

        if (ts_a > ts_b) {
            a_has_greater = true;
        }
        if (ts_b > ts_a) {
            b_has_greater = true;
        }

        /* Early exit for concurrent */
        if (a_has_greater && b_has_greater) {
            return SERAPH_SPARSE_VCLOCK_CONCURRENT;
        }
    }

    /* Determine ordering */
    if (a_has_greater && !b_has_greater) {
        return SERAPH_SPARSE_VCLOCK_AFTER;  /* b happened before a */
    }
    if (b_has_greater && !a_has_greater) {
        return SERAPH_SPARSE_VCLOCK_BEFORE;  /* a happened before b */
    }
    if (!a_has_greater && !b_has_greater) {
        return SERAPH_SPARSE_VCLOCK_EQUAL;
    }

    /* Both have greater - concurrent (should have been caught above) */
    return SERAPH_SPARSE_VCLOCK_CONCURRENT;
}

Seraph_Vbit seraph_sparse_vclock_happened_before(
    const Seraph_Sparse_VClock* a,
    const Seraph_Sparse_VClock* b
) {
    if (a == NULL || b == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Sparse_VClock_Order order = seraph_sparse_vclock_compare(a, b);

    if (order == SERAPH_SPARSE_VCLOCK_VOID) {
        return SERAPH_VBIT_VOID;
    }

    return (order == SERAPH_SPARSE_VCLOCK_BEFORE) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

Seraph_Vbit seraph_sparse_vclock_is_concurrent(
    const Seraph_Sparse_VClock* a,
    const Seraph_Sparse_VClock* b
) {
    if (a == NULL || b == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Sparse_VClock_Order order = seraph_sparse_vclock_compare(a, b);

    if (order == SERAPH_SPARSE_VCLOCK_VOID) {
        return SERAPH_VBIT_VOID;
    }

    return (order == SERAPH_SPARSE_VCLOCK_CONCURRENT) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Serialization Functions
 *============================================================================*/

size_t seraph_sparse_vclock_serialized_size(const Seraph_Sparse_VClock* vclock) {
    if (vclock == NULL) {
        return 0;
    }

    /* Format: count(2) + entries(count * 12) */
    return 2 + (size_t)vclock->count * sizeof(Seraph_VClock_Entry);
}

size_t seraph_sparse_vclock_serialize(
    const Seraph_Sparse_VClock* vclock,
    uint8_t* buffer,
    size_t size
) {
    if (vclock == NULL || buffer == NULL) {
        return 0;
    }

    size_t needed = seraph_sparse_vclock_serialized_size(vclock);
    if (size < needed) {
        return 0;
    }

    /* Write count (little-endian) */
    buffer[0] = (uint8_t)(vclock->count & 0xFF);
    buffer[1] = (uint8_t)((vclock->count >> 8) & 0xFF);

    /* Write entries */
    uint8_t* ptr = buffer + 2;
    for (uint16_t i = 0; i < vclock->count; i++) {
        const Seraph_VClock_Entry* entry = &vclock->entries[i];

        /* node_id (2 bytes, little-endian) */
        ptr[0] = (uint8_t)(entry->node_id & 0xFF);
        ptr[1] = (uint8_t)((entry->node_id >> 8) & 0xFF);

        /* reserved (2 bytes) */
        ptr[2] = 0;
        ptr[3] = 0;

        /* timestamp (8 bytes, little-endian) */
        for (int b = 0; b < 8; b++) {
            ptr[4 + b] = (uint8_t)((entry->timestamp >> (b * 8)) & 0xFF);
        }

        ptr += sizeof(Seraph_VClock_Entry);
    }

    return needed;
}

size_t seraph_sparse_vclock_deserialize(
    Seraph_Sparse_VClock* vclock,
    uint16_t owner_node,
    const uint8_t* buffer,
    size_t size
) {
    if (vclock == NULL || buffer == NULL || size < 2) {
        return 0;
    }

    /* Read count */
    uint16_t count = (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);

    /* Validate size */
    size_t needed = 2 + (size_t)count * sizeof(Seraph_VClock_Entry);
    if (size < needed) {
        return 0;
    }

    /* Check count limit */
    if (count > SERAPH_SPARSE_VCLOCK_MAX_ENTRIES) {
        return 0;
    }

    /* Initialize with sufficient capacity */
    vclock->entries = (Seraph_VClock_Entry*)calloc(
        count > 0 ? count : SERAPH_SPARSE_VCLOCK_INITIAL_CAPACITY,
        sizeof(Seraph_VClock_Entry)
    );

    if (vclock->entries == NULL) {
        memset(vclock, 0, sizeof(Seraph_Sparse_VClock));
        return 0;
    }

    vclock->count = count;
    vclock->capacity = count > 0 ? count : SERAPH_SPARSE_VCLOCK_INITIAL_CAPACITY;
    vclock->owner_node = owner_node;
    vclock->flags = SERAPH_SPARSE_VCLOCK_FLAG_NONE;

    /* Read entries */
    const uint8_t* ptr = buffer + 2;
    for (uint16_t i = 0; i < count; i++) {
        Seraph_VClock_Entry* entry = &vclock->entries[i];

        /* node_id */
        entry->node_id = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
        entry->reserved = 0;

        /* timestamp */
        entry->timestamp = 0;
        for (int b = 0; b < 8; b++) {
            entry->timestamp |= ((uint64_t)ptr[4 + b]) << (b * 8);
        }

        ptr += sizeof(Seraph_VClock_Entry);
    }

    return needed;
}

/*============================================================================
 * Debugging Functions
 *============================================================================*/

size_t seraph_sparse_vclock_to_string(
    const Seraph_Sparse_VClock* vclock,
    char* buffer,
    size_t size
) {
    if (buffer == NULL || size == 0) {
        return 0;
    }

    if (vclock == NULL) {
        int n = snprintf(buffer, size, "{NULL}");
        return (n > 0) ? (size_t)n : 0;
    }

    if (vclock->count == 0) {
        int n = snprintf(buffer, size, "{}");
        return (n > 0) ? (size_t)n : 0;
    }

    size_t written = 0;
    int n;

    n = snprintf(buffer, size, "{");
    if (n < 0) return 0;
    written += (size_t)n;
    if (written >= size) return written;

    for (uint16_t i = 0; i < vclock->count; i++) {
        if (i > 0) {
            n = snprintf(buffer + written, size - written, ", ");
            if (n < 0) return written;
            written += (size_t)n;
            if (written >= size) return written;
        }

        n = snprintf(
            buffer + written, size - written,
            "%u:%llu",
            (unsigned)vclock->entries[i].node_id,
            (unsigned long long)vclock->entries[i].timestamp
        );
        if (n < 0) return written;
        written += (size_t)n;
        if (written >= size) return written;
    }

    n = snprintf(buffer + written, size - written, "}");
    if (n < 0) return written;
    written += (size_t)n;

    return written;
}
