/**
 * @file void.h
 * @brief MC0: VOID Semantics - Universal error/nothing representation
 *
 * VOID is represented as "all 1s" bit pattern. It propagates through
 * operations automatically, making error handling implicit.
 *
 * VOID values by type:
 *   u8:  0xFF
 *   u16: 0xFFFF
 *   u32: 0xFFFFFFFF
 *   u64: 0xFFFFFFFFFFFFFFFF
 *   (signed types: -1 in each width)
 *
 * VOID ARCHAEOLOGY (Causality Tracking):
 *   Unlike dumb error codes, SERAPH VOIDs carry "compressed history" via
 *   a sidecar metadata table. When a value becomes VOID, we record WHY:
 *   - What operation caused it (divide by zero, overflow, bounds check)
 *   - Where it happened (file, line, function)
 *   - What the input values were
 *   - The causal chain of predecessor VOIDs
 *
 *   This enables "Void Archaeology" - debugging by excavating the history
 *   of how a VOID propagated through the system.
 */

#ifndef SERAPH_VOID_H
#define SERAPH_VOID_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*============================================================================
 * Compiler Compatibility Macros
 *============================================================================*/

/**
 * @brief Inline function hint
 *
 * Use SERAPH_INLINE for small functions that should be inlined.
 * This is portable across C99/C11 and various compilers.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define SERAPH_INLINE static __inline__ __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define SERAPH_INLINE static __forceinline
#else
    #define SERAPH_INLINE static inline
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * VOID Causality Tracking (Void Archaeology)
 *
 * The sidecar metadata table maps VOID occurrences to their error context.
 * Each thread has its own table (thread-local) for lock-free operation.
 *============================================================================*/

/** Maximum length of source location strings */
#define SERAPH_VOID_MAX_LOCATION 64

/** Maximum number of VOID contexts to track per thread */
#define SERAPH_VOID_CONTEXT_TABLE_SIZE 1024

/** Void reason codes - WHY did this become VOID? */
typedef enum {
    SERAPH_VOID_REASON_UNKNOWN       = 0,
    SERAPH_VOID_REASON_EXPLICIT      = 1,   /**< Explicitly set to VOID */
    SERAPH_VOID_REASON_PROPAGATED    = 2,   /**< Propagated from input VOID */
    SERAPH_VOID_REASON_DIV_ZERO      = 3,   /**< Division by zero */
    SERAPH_VOID_REASON_OVERFLOW      = 4,   /**< Arithmetic overflow */
    SERAPH_VOID_REASON_UNDERFLOW     = 5,   /**< Arithmetic underflow */
    SERAPH_VOID_REASON_OUT_OF_BOUNDS = 6,   /**< Array/buffer bounds exceeded */
    SERAPH_VOID_REASON_NULL_PTR      = 7,   /**< Null pointer dereference */
    SERAPH_VOID_REASON_INVALID_ARG   = 8,   /**< Invalid argument */
    SERAPH_VOID_REASON_ALLOC_FAIL    = 9,   /**< Memory allocation failed */
    SERAPH_VOID_REASON_TIMEOUT       = 10,  /**< Operation timed out */
    SERAPH_VOID_REASON_PERMISSION    = 11,  /**< Permission denied */
    SERAPH_VOID_REASON_NOT_FOUND     = 12,  /**< Resource not found */
    SERAPH_VOID_REASON_GENERATION    = 13,  /**< Generation mismatch (temporal safety) */
    SERAPH_VOID_REASON_NETWORK       = 14,  /**< Network error */
    SERAPH_VOID_REASON_IO            = 15,  /**< I/O error */

    /* Hardware-specific VOID reasons (Semantic Interrupts) */
    SERAPH_VOID_REASON_HW_CRC        = 20,  /**< Hardware CRC error (NIC) */
    SERAPH_VOID_REASON_HW_SYMBOL     = 21,  /**< Hardware symbol error (NIC) */
    SERAPH_VOID_REASON_HW_SEQUENCE   = 22,  /**< Hardware sequence error (NIC) */
    SERAPH_VOID_REASON_HW_RX_DATA    = 23,  /**< Hardware RX data error (NIC) */
    SERAPH_VOID_REASON_HW_TX_UNDERRUN= 24,  /**< Hardware TX underrun (NIC) */
    SERAPH_VOID_REASON_HW_COLLISION  = 25,  /**< Hardware late collision (NIC) */
    SERAPH_VOID_REASON_HW_DMA        = 26,  /**< Hardware DMA error */
    SERAPH_VOID_REASON_HW_NVME       = 27,  /**< NVMe controller error */

    /*=========================================================================
     * Whisper IPC-Specific VOID Reasons
     *
     * These reasons track WHY a Whisper message or operation became VOID,
     * enabling full causality archaeology for IPC debugging.
     *=========================================================================*/

    /** Channel has been closed - no further communication possible */
    SERAPH_VOID_REASON_CHANNEL_CLOSED    = 30,

    /** Channel send queue is full - message could not be enqueued */
    SERAPH_VOID_REASON_CHANNEL_FULL      = 31,

    /** Channel receive queue is empty - no messages available */
    SERAPH_VOID_REASON_CHANNEL_EMPTY     = 32,

    /** Endpoint is dead or disconnected */
    SERAPH_VOID_REASON_ENDPOINT_DEAD     = 33,

    /** Message is invalid or malformed */
    SERAPH_VOID_REASON_MESSAGE_INVALID   = 34,

    /** Lend operation expired before return */
    SERAPH_VOID_REASON_LEND_EXPIRED      = 35,

    /** Lend was manually revoked by lender */
    SERAPH_VOID_REASON_LEND_REVOKED      = 36,

    /** Capability transfer through channel failed */
    SERAPH_VOID_REASON_CAP_TRANSFER_FAIL = 37,

    /** Message contains VOID capability - propagated from sender */
    SERAPH_VOID_REASON_VOID_CAP_IN_MSG   = 38,

    /** Lend registry is full - cannot track new lend */
    SERAPH_VOID_REASON_LEND_REGISTRY_FULL= 39,

    /** Return message for unknown or already-returned lend */
    SERAPH_VOID_REASON_LEND_NOT_FOUND    = 40,

    /** Channel was destroyed during operation */
    SERAPH_VOID_REASON_CHANNEL_DESTROYED = 41,

    SERAPH_VOID_REASON_CUSTOM        = 255  /**< Custom reason (see message) */
} Seraph_VoidReason;

/**
 * @brief Context for a VOID occurrence - the "compressed history"
 *
 * This is what Void Archaeology excavates. Each VOID that matters
 * gets recorded with its full context for debugging.
 */
typedef struct {
    uint64_t          void_id;       /**< Unique ID for this VOID occurrence */
    Seraph_VoidReason reason;        /**< Why this became VOID */
    uint64_t          timestamp;     /**< When (monotonic counter) */
    uint64_t          predecessor;   /**< ID of causal predecessor VOID (0 if root) */
    uint64_t          input_a;       /**< First input value (if applicable) */
    uint64_t          input_b;       /**< Second input value (if applicable) */
    const char*       file;          /**< Source file (static string) */
    const char*       function;      /**< Function name (static string) */
    uint32_t          line;          /**< Source line number */
    char              message[64];   /**< Optional message/details */
} Seraph_VoidContext;

/** VOID context representing "no context" */
#define SERAPH_VOID_CONTEXT_NONE ((Seraph_VoidContext){ \
    0, SERAPH_VOID_REASON_UNKNOWN, 0, 0, 0, 0, NULL, NULL, 0, {0} })

/**
 * @brief Initialize the VOID causality tracking system
 *
 * Must be called once per thread before using VOID tracking.
 * Called automatically on first use if not explicitly initialized.
 */
void seraph_void_tracking_init(void);

/**
 * @brief Shutdown VOID tracking and free resources
 */
void seraph_void_tracking_shutdown(void);

/**
 * @brief Record a new VOID occurrence with context
 *
 * @param reason Why this became VOID
 * @param predecessor ID of predecessor VOID (0 if root cause)
 * @param input_a First input value
 * @param input_b Second input value
 * @param file Source file (__FILE__)
 * @param function Function name (__func__)
 * @param line Line number (__LINE__)
 * @param message Optional message (can be NULL)
 * @return Unique VOID ID for this occurrence
 */
uint64_t seraph_void_record(Seraph_VoidReason reason, uint64_t predecessor,
                            uint64_t input_a, uint64_t input_b,
                            const char* file, const char* function,
                            uint32_t line, const char* message);

/**
 * @brief Look up the context for a VOID occurrence
 *
 * @param void_id The VOID ID to look up
 * @return Context if found, SERAPH_VOID_CONTEXT_NONE if not found
 */
Seraph_VoidContext seraph_void_lookup(uint64_t void_id);

/**
 * @brief Get the most recent VOID context
 *
 * @return Most recent VOID context, or NONE if no VOIDs recorded
 */
Seraph_VoidContext seraph_void_last(void);

/**
 * @brief Walk the causal chain of a VOID
 *
 * @param void_id Starting VOID ID
 * @param callback Called for each context in the chain (root first)
 * @param user_data Passed to callback
 * @return Number of contexts in chain
 */
size_t seraph_void_walk_chain(uint64_t void_id,
                               void (*callback)(const Seraph_VoidContext*, void*),
                               void* user_data);

/**
 * @brief Print a VOID's causal chain to stderr (for debugging)
 *
 * @param void_id The VOID to analyze
 */
void seraph_void_print_chain(uint64_t void_id);

/**
 * @brief Clear all recorded VOID contexts (e.g., after handling errors)
 */
void seraph_void_clear(void);

/**
 * @brief Get reason code as human-readable string
 */
const char* seraph_void_reason_str(Seraph_VoidReason reason);

/*============================================================================
 * Macros for VOID Recording with Source Location
 *============================================================================*/

/**
 * @brief Record a VOID with automatic source location capture
 */
#define SERAPH_VOID_RECORD(reason, pred, in_a, in_b, msg) \
    seraph_void_record((reason), (pred), (in_a), (in_b), \
                       __FILE__, __func__, __LINE__, (msg))

/**
 * @brief Record and return a VOID u64 with tracking
 */
#define SERAPH_VOID_U64_RECORD(reason, pred, in_a, in_b, msg) \
    (seraph_void_record((reason), (pred), (in_a), (in_b), \
                        __FILE__, __func__, __LINE__, (msg)), SERAPH_VOID_U64)

/**
 * @brief Record division by zero and return VOID
 */
#define SERAPH_VOID_DIV_ZERO(a, b) \
    SERAPH_VOID_U64_RECORD(SERAPH_VOID_REASON_DIV_ZERO, 0, (a), (b), "division by zero")

/**
 * @brief Record overflow and return VOID
 */
#define SERAPH_VOID_OVERFLOW(a, b) \
    SERAPH_VOID_U64_RECORD(SERAPH_VOID_REASON_OVERFLOW, 0, (a), (b), "arithmetic overflow")

/**
 * @brief Record bounds violation and return VOID
 */
#define SERAPH_VOID_BOUNDS(index, limit) \
    SERAPH_VOID_U64_RECORD(SERAPH_VOID_REASON_OUT_OF_BOUNDS, 0, (index), (limit), "out of bounds")

/*============================================================================
 * VOID Constants
 *============================================================================*/

/** @name Unsigned VOID Constants */
/**@{*/
#define SERAPH_VOID_U8   ((uint8_t)0xFFU)
#define SERAPH_VOID_U16  ((uint16_t)0xFFFFU)
#define SERAPH_VOID_U32  ((uint32_t)0xFFFFFFFFUL)
#define SERAPH_VOID_U64  ((uint64_t)0xFFFFFFFFFFFFFFFFULL)
/**@}*/

/** @name Signed VOID Constants */
/**@{*/
#define SERAPH_VOID_I8   ((int8_t)-1)
#define SERAPH_VOID_I16  ((int16_t)-1)
#define SERAPH_VOID_I32  ((int32_t)-1)
#define SERAPH_VOID_I64  ((int64_t)-1)
/**@}*/

/** @name Pointer VOID Constant */
/**@{*/
#define SERAPH_VOID_PTR  ((void*)(uintptr_t)SERAPH_VOID_U64)
/**@}*/

/*============================================================================
 * VOID Detection Macros
 *============================================================================*/

/** @name Type-Specific VOID Detection */
/**@{*/
#define SERAPH_IS_VOID_U8(x)   ((uint8_t)(x) == SERAPH_VOID_U8)
#define SERAPH_IS_VOID_U16(x)  ((uint16_t)(x) == SERAPH_VOID_U16)
#define SERAPH_IS_VOID_U32(x)  ((uint32_t)(x) == SERAPH_VOID_U32)
#define SERAPH_IS_VOID_U64(x)  ((uint64_t)(x) == SERAPH_VOID_U64)
#define SERAPH_IS_VOID_I8(x)   ((int8_t)(x) == SERAPH_VOID_I8)
#define SERAPH_IS_VOID_I16(x)  ((int16_t)(x) == SERAPH_VOID_I16)
#define SERAPH_IS_VOID_I32(x)  ((int32_t)(x) == SERAPH_VOID_I32)
#define SERAPH_IS_VOID_I64(x)  ((int64_t)(x) == SERAPH_VOID_I64)
#define SERAPH_IS_VOID_PTR(x)  ((void*)(x) == SERAPH_VOID_PTR)
/**@}*/

/**
 * @brief Type-generic VOID detection
 *
 * Uses C11 _Generic to select appropriate comparison based on type.
 * Works for all integer types and pointers.
 */
#define SERAPH_IS_VOID(x) _Generic((x),                  \
    uint8_t:   SERAPH_IS_VOID_U8(x),                     \
    uint16_t:  SERAPH_IS_VOID_U16(x),                    \
    uint32_t:  SERAPH_IS_VOID_U32(x),                    \
    uint64_t:  SERAPH_IS_VOID_U64(x),                    \
    int8_t:    SERAPH_IS_VOID_I8(x),                     \
    int16_t:   SERAPH_IS_VOID_I16(x),                    \
    int32_t:   SERAPH_IS_VOID_I32(x),                    \
    int64_t:   SERAPH_IS_VOID_I64(x),                    \
    default:   ((uintptr_t)(x) == (uintptr_t)SERAPH_VOID_U64))

/**
 * @brief Check if value EXISTS (is NOT VOID)
 */
#define SERAPH_EXISTS(x) (!SERAPH_IS_VOID(x))

/**
 * @brief Unwrap value or return default if VOID
 */
#define SERAPH_UNWRAP_OR(x, default_val) \
    (SERAPH_IS_VOID(x) ? (default_val) : (x))

/**
 * @brief Unwrap value or execute statement block if VOID
 */
#define SERAPH_UNWRAP_OR_ELSE(x, else_block) \
    do { if (SERAPH_IS_VOID(x)) { else_block; } } while(0)

/*============================================================================
 * VOID Creation Macros
 *============================================================================*/

/**
 * @brief Type-generic VOID creation
 *
 * Returns the VOID constant for the type of the expression.
 */
#define SERAPH_VOID_OF(x) _Generic((x),                  \
    uint8_t:   SERAPH_VOID_U8,                           \
    uint16_t:  SERAPH_VOID_U16,                          \
    uint32_t:  SERAPH_VOID_U32,                          \
    uint64_t:  SERAPH_VOID_U64,                          \
    int8_t:    SERAPH_VOID_I8,                           \
    int16_t:   SERAPH_VOID_I16,                          \
    int32_t:   SERAPH_VOID_I32,                          \
    int64_t:   SERAPH_VOID_I64,                          \
    default:   SERAPH_VOID_PTR)

/*============================================================================
 * VOID Propagation Functions
 *============================================================================*/

/**
 * @brief Propagate VOID through unary operation (uint8_t)
 * @param x Input value
 * @param op Unary operation function pointer
 * @return VOID if input is VOID, otherwise op(x)
 */
static inline uint8_t seraph_void_unary_u8(uint8_t x, uint8_t (*op)(uint8_t)) {
    if (SERAPH_IS_VOID_U8(x)) return SERAPH_VOID_U8;
    return op(x);
}

/**
 * @brief Propagate VOID through unary operation (uint16_t)
 */
static inline uint16_t seraph_void_unary_u16(uint16_t x, uint16_t (*op)(uint16_t)) {
    if (SERAPH_IS_VOID_U16(x)) return SERAPH_VOID_U16;
    return op(x);
}

/**
 * @brief Propagate VOID through unary operation (uint32_t)
 */
static inline uint32_t seraph_void_unary_u32(uint32_t x, uint32_t (*op)(uint32_t)) {
    if (SERAPH_IS_VOID_U32(x)) return SERAPH_VOID_U32;
    return op(x);
}

/**
 * @brief Propagate VOID through unary operation (uint64_t)
 */
static inline uint64_t seraph_void_unary_u64(uint64_t x, uint64_t (*op)(uint64_t)) {
    if (SERAPH_IS_VOID_U64(x)) return SERAPH_VOID_U64;
    return op(x);
}

/**
 * @brief Propagate VOID through binary operation (uint8_t)
 * @param a First input
 * @param b Second input
 * @param op Binary operation function pointer
 * @return VOID if either input is VOID, otherwise op(a, b)
 */
static inline uint8_t seraph_void_binary_u8(uint8_t a, uint8_t b,
                                            uint8_t (*op)(uint8_t, uint8_t)) {
    if (SERAPH_IS_VOID_U8(a) || SERAPH_IS_VOID_U8(b)) return SERAPH_VOID_U8;
    return op(a, b);
}

/**
 * @brief Propagate VOID through binary operation (uint16_t)
 */
static inline uint16_t seraph_void_binary_u16(uint16_t a, uint16_t b,
                                              uint16_t (*op)(uint16_t, uint16_t)) {
    if (SERAPH_IS_VOID_U16(a) || SERAPH_IS_VOID_U16(b)) return SERAPH_VOID_U16;
    return op(a, b);
}

/**
 * @brief Propagate VOID through binary operation (uint32_t)
 */
static inline uint32_t seraph_void_binary_u32(uint32_t a, uint32_t b,
                                              uint32_t (*op)(uint32_t, uint32_t)) {
    if (SERAPH_IS_VOID_U32(a) || SERAPH_IS_VOID_U32(b)) return SERAPH_VOID_U32;
    return op(a, b);
}

/**
 * @brief Propagate VOID through binary operation (uint64_t)
 */
static inline uint64_t seraph_void_binary_u64(uint64_t a, uint64_t b,
                                              uint64_t (*op)(uint64_t, uint64_t)) {
    if (SERAPH_IS_VOID_U64(a) || SERAPH_IS_VOID_U64(b)) return SERAPH_VOID_U64;
    return op(a, b);
}

/**
 * @brief Propagate VOID through binary operation (int32_t)
 */
static inline int32_t seraph_void_binary_i32(int32_t a, int32_t b,
                                             int32_t (*op)(int32_t, int32_t)) {
    if (SERAPH_IS_VOID_I32(a) || SERAPH_IS_VOID_I32(b)) return SERAPH_VOID_I32;
    return op(a, b);
}

/**
 * @brief Propagate VOID through binary operation (int64_t)
 */
static inline int64_t seraph_void_binary_i64(int64_t a, int64_t b,
                                             int64_t (*op)(int64_t, int64_t)) {
    if (SERAPH_IS_VOID_I64(a) || SERAPH_IS_VOID_I64(b)) return SERAPH_VOID_I64;
    return op(a, b);
}

/*============================================================================
 * SIMD Batch VOID Checking
 *============================================================================*/

/*
 * SIMD Detection Notes:
 * - _mm_cmpeq_epi64 requires SSE4.1 (not SSE2!)
 * - _mm256_cmpeq_epi64 requires AVX2
 * - x86-64 always has SSE2, but not SSE4.1
 *
 * We check for the actual instruction sets required, not just SSE2.
 */
#if defined(__AVX2__) || defined(__SSE4_1__)
#include <immintrin.h>
#endif

/**
 * @brief Check 4 x 64-bit values for VOID using AVX2
 * @param values Pointer to 4 uint64_t values (must be 32-byte aligned)
 * @return Bitmask where bit i is set if values[i] is VOID
 */
static inline uint32_t seraph_void_check_4x64(const uint64_t* values) {
#ifdef __AVX2__
    __m256i data = _mm256_loadu_si256((const __m256i*)values);
    __m256i void_pattern = _mm256_set1_epi64x((int64_t)SERAPH_VOID_U64);
    __m256i cmp = _mm256_cmpeq_epi64(data, void_pattern);
    return (uint32_t)_mm256_movemask_pd((__m256d)cmp);
#else
    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        if (SERAPH_IS_VOID_U64(values[i])) mask |= (1u << i);
    }
    return mask;
#endif
}

/**
 * @brief Check 2 x 64-bit values for VOID using SSE4.1
 * @param values Pointer to 2 uint64_t values (must be 16-byte aligned)
 * @return Bitmask where bit i is set if values[i] is VOID
 *
 * Note: _mm_cmpeq_epi64 requires SSE4.1, not SSE2!
 */
static inline uint32_t seraph_void_check_2x64(const uint64_t* values) {
#ifdef __SSE4_1__
    __m128i data = _mm_loadu_si128((const __m128i*)values);
    __m128i void_pattern = _mm_set1_epi64x((int64_t)SERAPH_VOID_U64);
    __m128i cmp = _mm_cmpeq_epi64(data, void_pattern);
    return (uint32_t)_mm_movemask_pd((__m128d)cmp);
#else
    uint32_t mask = 0;
    for (int i = 0; i < 2; i++) {
        if (SERAPH_IS_VOID_U64(values[i])) mask |= (1u << i);
    }
    return mask;
#endif
}

/**
 * @brief Check 8 x 32-bit values for VOID using AVX2
 * @param values Pointer to 8 uint32_t values (must be 32-byte aligned)
 * @return Bitmask where bit i is set if values[i] is VOID
 */
static inline uint32_t seraph_void_check_8x32(const uint32_t* values) {
#ifdef __AVX2__
    __m256i data = _mm256_loadu_si256((const __m256i*)values);
    __m256i void_pattern = _mm256_set1_epi32((int32_t)SERAPH_VOID_U32);
    __m256i cmp = _mm256_cmpeq_epi32(data, void_pattern);
    return (uint32_t)_mm256_movemask_ps((__m256)cmp);
#else
    uint32_t mask = 0;
    for (int i = 0; i < 8; i++) {
        if (SERAPH_IS_VOID_U32(values[i])) mask |= (1u << i);
    }
    return mask;
#endif
}

/*============================================================================
 * Branchless VOID Mask Generation
 *
 * These convert "is VOID" checks into bitmasks for branchless selection:
 *   VOID  -> all 1s (0xFF...FF)
 *   valid -> all 0s (0x00...00)
 *
 * Key trick: -(uint64_t)(condition)
 *   condition=1 -> -1 = 0xFFFFFFFFFFFFFFFF
 *   condition=0 -> -0 = 0x0000000000000000
 *============================================================================*/

/** @brief Generate mask: all 1s if VOID, all 0s otherwise */
static inline uint64_t seraph_void_mask_u64(uint64_t x) {
    return -(uint64_t)(x == SERAPH_VOID_U64);
}

static inline uint32_t seraph_void_mask_u32(uint32_t x) {
    return -(uint32_t)(x == SERAPH_VOID_U32);
}

static inline uint16_t seraph_void_mask_u16(uint16_t x) {
    return -(uint16_t)(x == SERAPH_VOID_U16);
}

static inline uint8_t seraph_void_mask_u8(uint8_t x) {
    return -(uint8_t)(x == SERAPH_VOID_U8);
}

static inline int64_t seraph_void_mask_i64(int64_t x) {
    return -(int64_t)(x == SERAPH_VOID_I64);
}

static inline int32_t seraph_void_mask_i32(int32_t x) {
    return -(int32_t)(x == SERAPH_VOID_I32);
}

/** @brief Generate mask if either value is VOID */
static inline uint64_t seraph_void_mask2_u64(uint64_t a, uint64_t b) {
    return seraph_void_mask_u64(a) | seraph_void_mask_u64(b);
}

static inline int64_t seraph_void_mask2_i64(int64_t a, int64_t b) {
    return seraph_void_mask_i64(a) | seraph_void_mask_i64(b);
}

static inline uint32_t seraph_void_mask2_u32(uint32_t a, uint32_t b) {
    return seraph_void_mask_u32(a) | seraph_void_mask_u32(b);
}

static inline int32_t seraph_void_mask2_i32(int32_t a, int32_t b) {
    return seraph_void_mask_i32(a) | seraph_void_mask_i32(b);
}

/*============================================================================
 * Branchless Selection
 *
 * select(if_void, if_valid, mask):
 *   mask=all 1s -> returns if_void
 *   mask=all 0s -> returns if_valid
 *============================================================================*/

static inline uint64_t seraph_select_u64(uint64_t if_void, uint64_t if_valid, uint64_t mask) {
    return (if_void & mask) | (if_valid & ~mask);
}

static inline int64_t seraph_select_i64(int64_t if_void, int64_t if_valid, int64_t mask) {
    return (if_void & mask) | (if_valid & ~mask);
}

static inline uint32_t seraph_select_u32(uint32_t if_void, uint32_t if_valid, uint32_t mask) {
    return (if_void & mask) | (if_valid & ~mask);
}

static inline int32_t seraph_select_i32(int32_t if_void, int32_t if_valid, int32_t mask) {
    return (if_void & mask) | (if_valid & ~mask);
}

/*============================================================================
 * VOID-Safe Arithmetic Helpers (Branchless)
 *============================================================================*/

/**
 * @brief Safe division that returns VOID on divide-by-zero (branchless)
 */
static inline uint64_t seraph_safe_div_u64(uint64_t a, uint64_t b) {
    uint64_t safe_b = b | (-(uint64_t)(b == 0)); /* b==0 ? ~0 : b */
    uint64_t result = a / safe_b;
    uint64_t void_mask = seraph_void_mask2_u64(a, b) | (-(uint64_t)(b == 0));
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

/**
 * @brief Safe division (signed, branchless)
 *
 * Note: We must avoid INT64_MIN / -1 as it causes undefined behavior.
 * When this condition is detected, we substitute a safe divisor and
 * then return VOID.
 */
static inline int64_t seraph_safe_div_i64(int64_t a, int64_t b) {
    int64_t div_zero = -(int64_t)(b == 0);
    int64_t overflow = -(int64_t)((a == INT64_MIN) & (b == -1));
    int64_t void_mask = seraph_void_mask2_i64(a, b) | div_zero | overflow;

    /* Substitute safe divisor to avoid UB - result will be discarded anyway */
    int64_t is_dangerous = div_zero | overflow;
    int64_t safe_b = (is_dangerous) ? 1 : b;

    int64_t result = a / safe_b;
    return seraph_select_i64(SERAPH_VOID_I64, result, void_mask);
}

/**
 * @brief Safe modulo (branchless)
 */
static inline uint64_t seraph_safe_mod_u64(uint64_t a, uint64_t b) {
    uint64_t safe_b = b | (-(uint64_t)(b == 0));
    uint64_t result = a % safe_b;
    uint64_t void_mask = seraph_void_mask2_u64(a, b) | (-(uint64_t)(b == 0));
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

/**
 * @brief Safe shift left (branchless)
 */
static inline uint64_t seraph_safe_shl_u64(uint64_t x, uint32_t shift) {
    uint64_t result = (shift < 64) ? (x << shift) : 0;
    uint64_t void_mask = seraph_void_mask_u64(x) | (-(uint64_t)(shift >= 64));
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

/**
 * @brief Safe shift right (branchless)
 */
static inline uint64_t seraph_safe_shr_u64(uint64_t x, uint32_t shift) {
    uint64_t result = (shift < 64) ? (x >> shift) : 0;
    uint64_t void_mask = seraph_void_mask_u64(x) | (-(uint64_t)(shift >= 64));
    return seraph_select_u64(SERAPH_VOID_U64, result, void_mask);
}

/*============================================================================
 * VOID Array Operations
 *============================================================================*/

/**
 * @brief Count VOID values in an array
 * @param values Array of values
 * @param count Number of elements
 * @return Number of VOID values found
 */
size_t seraph_void_count_u64(const uint64_t* values, size_t count);

/**
 * @brief Find first VOID in array
 * @param values Array of values
 * @param count Number of elements
 * @return Index of first VOID, or SIZE_MAX if none found
 */
size_t seraph_void_find_first_u64(const uint64_t* values, size_t count);

/**
 * @brief Check if any value in array is VOID
 * @param values Array of values
 * @param count Number of elements
 * @return true if any VOID found
 */
bool seraph_void_any_u64(const uint64_t* values, size_t count);

/**
 * @brief Check if all values in array are VOID
 * @param values Array of values
 * @param count Number of elements
 * @return true if all are VOID
 */
bool seraph_void_all_u64(const uint64_t* values, size_t count);

/**
 * @brief Replace all VOID values with a default
 * @param values Array of values (modified in place)
 * @param count Number of elements
 * @param default_val Value to replace VOIDs with
 * @return Number of values replaced
 */
size_t seraph_void_replace_u64(uint64_t* values, size_t count, uint64_t default_val);

/*============================================================================
 * VOID-Tracking Arithmetic Wrappers
 *
 * These wrappers record causality when VOIDs are created, enabling
 * full archaeology of error chains.
 *============================================================================*/

/**
 * @brief Tracked division - records context on divide-by-zero
 */
uint64_t seraph_tracked_div_u64(uint64_t a, uint64_t b);

/**
 * @brief Tracked modulo - records context on divide-by-zero
 */
uint64_t seraph_tracked_mod_u64(uint64_t a, uint64_t b);

/**
 * @brief Check if tracking is enabled (for conditional recording)
 */
bool seraph_void_tracking_enabled(void);

/**
 * @brief Enable/disable VOID tracking (for performance-critical paths)
 */
void seraph_void_tracking_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_VOID_H */
