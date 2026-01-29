/**
 * @file void.c
 * @brief MC0: VOID Semantics Implementation
 *
 * Includes Void Archaeology (causality tracking) - the sidecar metadata
 * table that remembers WHY values became VOID.
 */

#include "seraph/void.h"
#ifdef SERAPH_KERNEL
    /* Kernel mode - use SERAPH runtime */
    extern void* memset(void* dest, int val, size_t count);
    extern void* memcpy(void* dest, const void* src, size_t count);
    extern size_t strlen(const char* str);
#else
    #include <string.h>
    #include <stdio.h>
    #include <stdlib.h>
#endif

/*============================================================================
 * Thread-Local VOID Context Table (Void Archaeology)
 *
 * Each thread has its own circular buffer of VOID contexts for lock-free
 * operation. The table acts as a sidecar metadata store.
 *============================================================================*/

/* Thread-local storage for VOID context table */
#if defined(__GNUC__) || defined(__clang__)
    /* GCC/Clang on any platform (including MinGW on Windows) */
    #define SERAPH_TLS __thread
#elif defined(_WIN32) && defined(_MSC_VER)
    /* MSVC on Windows */
    #define SERAPH_TLS __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    /* C11 _Thread_local */
    #define SERAPH_TLS _Thread_local
#else
    /* Fallback: no TLS (single-threaded only) */
    #define SERAPH_TLS
#endif

/** Per-thread context table */
typedef struct {
    Seraph_VoidContext contexts[SERAPH_VOID_CONTEXT_TABLE_SIZE];
    uint64_t next_id;           /**< Next VOID ID to assign */
    uint32_t write_index;       /**< Next slot to write (circular) */
    uint32_t count;             /**< Number of valid entries */
    bool     initialized;       /**< Has init been called? */
    bool     enabled;           /**< Is tracking enabled? */
} Seraph_VoidTable;

static SERAPH_TLS Seraph_VoidTable g_void_table = {
    .next_id = 1,
    .write_index = 0,
    .count = 0,
    .initialized = false,
    .enabled = true
};

/** Monotonic timestamp counter (thread-local) */
static SERAPH_TLS uint64_t g_void_timestamp = 0;

/*============================================================================
 * VOID Tracking Implementation
 *============================================================================*/

void seraph_void_tracking_init(void) {
    if (g_void_table.initialized) return;

    memset(g_void_table.contexts, 0, sizeof(g_void_table.contexts));
    g_void_table.next_id = 1;
    g_void_table.write_index = 0;
    g_void_table.count = 0;
    g_void_table.enabled = true;
    g_void_table.initialized = true;
    g_void_timestamp = 0;
}

void seraph_void_tracking_shutdown(void) {
    g_void_table.initialized = false;
    g_void_table.count = 0;
}

bool seraph_void_tracking_enabled(void) {
    return g_void_table.enabled;
}

void seraph_void_tracking_set_enabled(bool enabled) {
    g_void_table.enabled = enabled;
}

uint64_t seraph_void_record(Seraph_VoidReason reason, uint64_t predecessor,
                            uint64_t input_a, uint64_t input_b,
                            const char* file, const char* function,
                            uint32_t line, const char* message) {
    /* Auto-init on first use */
    if (!g_void_table.initialized) {
        seraph_void_tracking_init();
    }

    /* Skip if tracking disabled */
    if (!g_void_table.enabled) {
        return 0;
    }

    /* Get the slot to write */
    uint32_t slot = g_void_table.write_index;
    Seraph_VoidContext* ctx = &g_void_table.contexts[slot];

    /* Fill in the context */
    ctx->void_id = g_void_table.next_id++;
    ctx->reason = reason;
    ctx->timestamp = ++g_void_timestamp;
    ctx->predecessor = predecessor;
    ctx->input_a = input_a;
    ctx->input_b = input_b;
    ctx->file = file;
    ctx->function = function;
    ctx->line = line;

    /* Copy message if provided */
    if (message) {
        size_t len = strlen(message);
        if (len >= sizeof(ctx->message)) {
            len = sizeof(ctx->message) - 1;
        }
        memcpy(ctx->message, message, len);
        ctx->message[len] = '\0';
    } else {
        ctx->message[0] = '\0';
    }

    /* Advance circular buffer */
    g_void_table.write_index = (slot + 1) % SERAPH_VOID_CONTEXT_TABLE_SIZE;
    if (g_void_table.count < SERAPH_VOID_CONTEXT_TABLE_SIZE) {
        g_void_table.count++;
    }

    return ctx->void_id;
}

Seraph_VoidContext seraph_void_lookup(uint64_t void_id) {
    if (!g_void_table.initialized || void_id == 0) {
        return SERAPH_VOID_CONTEXT_NONE;
    }

    /* Search the circular buffer */
    for (uint32_t i = 0; i < g_void_table.count; i++) {
        if (g_void_table.contexts[i].void_id == void_id) {
            return g_void_table.contexts[i];
        }
    }

    return SERAPH_VOID_CONTEXT_NONE;
}

Seraph_VoidContext seraph_void_last(void) {
    if (!g_void_table.initialized || g_void_table.count == 0) {
        return SERAPH_VOID_CONTEXT_NONE;
    }

    /* Last written is at (write_index - 1 + SIZE) % SIZE */
    uint32_t last = (g_void_table.write_index + SERAPH_VOID_CONTEXT_TABLE_SIZE - 1)
                    % SERAPH_VOID_CONTEXT_TABLE_SIZE;
    return g_void_table.contexts[last];
}

size_t seraph_void_walk_chain(uint64_t void_id,
                               void (*callback)(const Seraph_VoidContext*, void*),
                               void* user_data) {
    if (!callback || void_id == 0) return 0;

    /* Build the chain by following predecessors */
    uint64_t chain[64];  /* Max depth of 64 */
    size_t depth = 0;

    uint64_t current = void_id;
    while (current != 0 && depth < 64) {
        Seraph_VoidContext ctx = seraph_void_lookup(current);
        if (ctx.void_id == 0) break;  /* Not found */

        chain[depth++] = current;
        current = ctx.predecessor;
    }

    /* Call callback in root-first order */
    for (size_t i = depth; i > 0; i--) {
        Seraph_VoidContext ctx = seraph_void_lookup(chain[i - 1]);
        callback(&ctx, user_data);
    }

    return depth;
}

#ifndef SERAPH_KERNEL
static void print_context_callback(const Seraph_VoidContext* ctx, void* user_data) {
    (void)user_data;
    fprintf(stderr, "  [%llu] %s at %s:%u in %s()\n",
            (unsigned long long)ctx->void_id,
            seraph_void_reason_str(ctx->reason),
            ctx->file ? ctx->file : "unknown",
            ctx->line,
            ctx->function ? ctx->function : "unknown");
    if (ctx->message[0]) {
        fprintf(stderr, "        Message: %s\n", ctx->message);
    }
    if (ctx->input_a != 0 || ctx->input_b != 0) {
        fprintf(stderr, "        Inputs: a=%llu, b=%llu\n",
                (unsigned long long)ctx->input_a,
                (unsigned long long)ctx->input_b);
    }
}
#endif

void seraph_void_print_chain(uint64_t void_id) {
#ifdef SERAPH_KERNEL
    /* Kernel mode: no console output available */
    (void)void_id;
#else
    fprintf(stderr, "=== VOID Archaeology for ID %llu ===\n",
            (unsigned long long)void_id);
    size_t depth = seraph_void_walk_chain(void_id, print_context_callback, NULL);
    if (depth == 0) {
        fprintf(stderr, "  (no context found)\n");
    }
    fprintf(stderr, "=== End chain (depth=%zu) ===\n", depth);
#endif
}

void seraph_void_clear(void) {
    if (!g_void_table.initialized) return;

    g_void_table.write_index = 0;
    g_void_table.count = 0;
    /* Keep next_id incrementing to avoid ID reuse */
}

const char* seraph_void_reason_str(Seraph_VoidReason reason) {
    switch (reason) {
        case SERAPH_VOID_REASON_UNKNOWN:       return "unknown";
        case SERAPH_VOID_REASON_EXPLICIT:      return "explicit";
        case SERAPH_VOID_REASON_PROPAGATED:    return "propagated";
        case SERAPH_VOID_REASON_DIV_ZERO:      return "divide-by-zero";
        case SERAPH_VOID_REASON_OVERFLOW:      return "overflow";
        case SERAPH_VOID_REASON_UNDERFLOW:     return "underflow";
        case SERAPH_VOID_REASON_OUT_OF_BOUNDS: return "out-of-bounds";
        case SERAPH_VOID_REASON_NULL_PTR:      return "null-pointer";
        case SERAPH_VOID_REASON_INVALID_ARG:   return "invalid-argument";
        case SERAPH_VOID_REASON_ALLOC_FAIL:    return "allocation-failed";
        case SERAPH_VOID_REASON_TIMEOUT:       return "timeout";
        case SERAPH_VOID_REASON_PERMISSION:    return "permission-denied";
        case SERAPH_VOID_REASON_NOT_FOUND:     return "not-found";
        case SERAPH_VOID_REASON_GENERATION:    return "generation-mismatch";
        case SERAPH_VOID_REASON_NETWORK:       return "network-error";
        case SERAPH_VOID_REASON_IO:            return "io-error";
        case SERAPH_VOID_REASON_HW_CRC:        return "hw-crc-error";
        case SERAPH_VOID_REASON_HW_SYMBOL:     return "hw-symbol-error";
        case SERAPH_VOID_REASON_HW_SEQUENCE:   return "hw-sequence-error";
        case SERAPH_VOID_REASON_HW_RX_DATA:    return "hw-rx-data-error";
        case SERAPH_VOID_REASON_HW_TX_UNDERRUN:return "hw-tx-underrun";
        case SERAPH_VOID_REASON_HW_COLLISION:  return "hw-late-collision";
        case SERAPH_VOID_REASON_HW_DMA:        return "hw-dma-error";
        case SERAPH_VOID_REASON_HW_NVME:       return "hw-nvme-error";

        /* Whisper IPC-specific reasons */
        case SERAPH_VOID_REASON_CHANNEL_CLOSED:    return "channel-closed";
        case SERAPH_VOID_REASON_CHANNEL_FULL:      return "channel-full";
        case SERAPH_VOID_REASON_CHANNEL_EMPTY:     return "channel-empty";
        case SERAPH_VOID_REASON_ENDPOINT_DEAD:     return "endpoint-dead";
        case SERAPH_VOID_REASON_MESSAGE_INVALID:   return "message-invalid";
        case SERAPH_VOID_REASON_LEND_EXPIRED:      return "lend-expired";
        case SERAPH_VOID_REASON_LEND_REVOKED:      return "lend-revoked";
        case SERAPH_VOID_REASON_CAP_TRANSFER_FAIL: return "cap-transfer-failed";
        case SERAPH_VOID_REASON_VOID_CAP_IN_MSG:   return "void-cap-in-message";
        case SERAPH_VOID_REASON_LEND_REGISTRY_FULL:return "lend-registry-full";
        case SERAPH_VOID_REASON_LEND_NOT_FOUND:    return "lend-not-found";
        case SERAPH_VOID_REASON_CHANNEL_DESTROYED: return "channel-destroyed";

        case SERAPH_VOID_REASON_CUSTOM:        return "custom";
        default:                               return "unknown";
    }
}

/*============================================================================
 * Tracked Arithmetic Operations
 *============================================================================*/

uint64_t seraph_tracked_div_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_PROPAGATED, 0, a, b, "propagated void");
        return SERAPH_VOID_U64;
    }
    if (b == 0) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_DIV_ZERO, 0, a, b, "division by zero");
        return SERAPH_VOID_U64;
    }
    return a / b;
}

uint64_t seraph_tracked_mod_u64(uint64_t a, uint64_t b) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_PROPAGATED, 0, a, b, "propagated void");
        return SERAPH_VOID_U64;
    }
    if (b == 0) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_DIV_ZERO, 0, a, b, "modulo by zero");
        return SERAPH_VOID_U64;
    }
    return a % b;
}

/*============================================================================
 * VOID Array Operations Implementation
 *============================================================================*/

size_t seraph_void_count_u64(const uint64_t* values, size_t count) {
    if (!values || count == 0) return 0;

    size_t void_count = 0;
    size_t i = 0;

#ifdef __AVX2__
    /* Process 4 values at a time with AVX2 */
    for (; i + 4 <= count; i += 4) {
        uint32_t mask = seraph_void_check_4x64(&values[i]);
        /* Count bits set in mask */
        void_count += __builtin_popcount(mask);
    }
#endif

    /* Handle remaining values */
    for (; i < count; i++) {
        if (SERAPH_IS_VOID_U64(values[i])) {
            void_count++;
        }
    }

    return void_count;
}

size_t seraph_void_find_first_u64(const uint64_t* values, size_t count) {
    if (!values || count == 0) return SIZE_MAX;

    size_t i = 0;

#ifdef __AVX2__
    /* Process 4 values at a time with AVX2 */
    for (; i + 4 <= count; i += 4) {
        uint32_t mask = seraph_void_check_4x64(&values[i]);
        if (mask) {
            /* Found at least one VOID - find the first */
            return i + __builtin_ctz(mask);
        }
    }
#endif

    /* Handle remaining values */
    for (; i < count; i++) {
        if (SERAPH_IS_VOID_U64(values[i])) {
            return i;
        }
    }

    return SIZE_MAX;
}

bool seraph_void_any_u64(const uint64_t* values, size_t count) {
    return seraph_void_find_first_u64(values, count) != SIZE_MAX;
}

bool seraph_void_all_u64(const uint64_t* values, size_t count) {
    if (!values || count == 0) return false;

    size_t i = 0;

#ifdef __AVX2__
    /* Process 4 values at a time with AVX2 */
    for (; i + 4 <= count; i += 4) {
        uint32_t mask = seraph_void_check_4x64(&values[i]);
        if (mask != 0xF) {  /* Not all 4 bits set = not all VOID */
            return false;
        }
    }
#endif

    /* Handle remaining values */
    for (; i < count; i++) {
        if (!SERAPH_IS_VOID_U64(values[i])) {
            return false;
        }
    }

    return true;
}

size_t seraph_void_replace_u64(uint64_t* values, size_t count, uint64_t default_val) {
    if (!values || count == 0) return 0;

    size_t replaced = 0;

    for (size_t i = 0; i < count; i++) {
        if (SERAPH_IS_VOID_U64(values[i])) {
            values[i] = default_val;
            replaced++;
        }
    }

    return replaced;
}

/*============================================================================
 * Additional VOID Utility Functions
 *============================================================================*/

/**
 * @brief Copy array, replacing VOIDs with default
 */
void seraph_void_copy_replace_u64(const uint64_t* src, uint64_t* dst,
                                   size_t count, uint64_t default_val) {
    if (!src || !dst || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        dst[i] = SERAPH_IS_VOID_U64(src[i]) ? default_val : src[i];
    }
}

/**
 * @brief Compact array by removing VOIDs
 * @return New count (number of non-VOID values)
 */
size_t seraph_void_compact_u64(uint64_t* values, size_t count) {
    if (!values || count == 0) return 0;

    size_t write_idx = 0;

    for (size_t read_idx = 0; read_idx < count; read_idx++) {
        if (!SERAPH_IS_VOID_U64(values[read_idx])) {
            if (write_idx != read_idx) {
                values[write_idx] = values[read_idx];
            }
            write_idx++;
        }
    }

    return write_idx;
}

/**
 * @brief Create a validity mask for an array
 * @param values Source array
 * @param mask Output mask (must be at least (count+7)/8 bytes)
 * @param count Number of elements
 *
 * Each bit in mask indicates if corresponding value EXISTS (is not VOID).
 * Bit 0 of byte 0 = values[0], Bit 1 of byte 0 = values[1], etc.
 */
void seraph_void_make_mask_u64(const uint64_t* values, uint8_t* mask, size_t count) {
    if (!values || !mask || count == 0) return;

    size_t mask_bytes = (count + 7) / 8;
    memset(mask, 0, mask_bytes);

    for (size_t i = 0; i < count; i++) {
        if (!SERAPH_IS_VOID_U64(values[i])) {
            mask[i / 8] |= (1u << (i % 8));
        }
    }
}

/**
 * @brief Apply mask to array, setting masked positions to VOID
 * @param values Array to modify
 * @param mask Validity mask
 * @param count Number of elements
 */
void seraph_void_apply_mask_u64(uint64_t* values, const uint8_t* mask, size_t count) {
    if (!values || !mask || count == 0) return;

    for (size_t i = 0; i < count; i++) {
        if (!(mask[i / 8] & (1u << (i % 8)))) {
            values[i] = SERAPH_VOID_U64;
        }
    }
}
