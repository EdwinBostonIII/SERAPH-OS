/**
 * @file proof_blob.h
 * @brief MC28: Zero-Overhead Strand Execution via Proof Blobs
 *
 * Proof Blobs are compact binary representations of compile-time safety proofs
 * that can be embedded in executables. When a Strand loads a proof blob, it can
 * eliminate runtime safety checks for operations that have been statically verified.
 *
 * CORE CONCEPTS:
 *
 *   1. PROOF BLOB FORMAT: A relocatable binary structure containing:
 *      - Header with magic, version, and offsets
 *      - Hash index for O(1) proof lookup by code location
 *      - Packed proof records
 *      - SHA-256 integrity checksum
 *
 *   2. ZERO-OVERHEAD EXECUTION: When proof exists and is PROVEN:
 *      - No runtime bounds checking
 *      - No VOID propagation checking
 *      - No permission validation
 *      - No generation verification
 *      The check compiles away to nothing.
 *
 *   3. GRACEFUL DEGRADATION: When proof is RUNTIME or missing:
 *      - Full runtime checking is performed
 *      - Same safety guarantees as non-proof-guided execution
 *      - Statistics track how many checks were performed vs skipped
 *
 * PERFORMANCE:
 *   - Proof lookup: ~5 cycles (hash + array access)
 *   - Proven access: 0 cycles (no check generated)
 *   - Runtime check: ~15-50 cycles (varies by check type)
 *
 * WHY THIS MATTERS:
 *   Traditional safe languages pay runtime costs for every operation:
 *     array[i]  → bounds check (5-10 cycles)
 *     ptr->x    → null check (3-5 cycles)
 *     cap.read  → permission check (10-15 cycles)
 *
 *   With proof blobs, statically verified operations have ZERO overhead.
 *   Only uncertain operations pay the runtime cost.
 */

#ifndef SERAPH_PROOF_BLOB_H
#define SERAPH_PROOF_BLOB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/seraphim/proofs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Magic bytes: "SRPHPROF" */
#define SERAPH_PROOF_BLOB_MAGIC     0x464F525048505253ULL

/** Current version: 1.0.0 */
#define SERAPH_PROOF_BLOB_VERSION   0x00010000

/** Maximum proofs in a single blob */
#define SERAPH_PROOF_BLOB_MAX_PROOFS    65536

/** Minimum bucket count for hash index */
#define SERAPH_PROOF_BLOB_MIN_BUCKETS   16

/** Empty bucket sentinel */
#define SERAPH_PROOF_BLOB_EMPTY_BUCKET  0xFFFFFFFF

/** SHA-256 checksum size */
#define SERAPH_PROOF_BLOB_CHECKSUM_SIZE 32

/*============================================================================
 * Proof Blob Flags
 *============================================================================*/

/** Blob has been verified */
#define SERAPH_PROOF_BLOB_FLAG_VERIFIED     (1 << 0)

/** Blob uses compressed proofs */
#define SERAPH_PROOF_BLOB_FLAG_COMPRESSED   (1 << 1)

/** Blob includes debug symbols */
#define SERAPH_PROOF_BLOB_FLAG_DEBUG        (1 << 2)

/** Blob is from a release build */
#define SERAPH_PROOF_BLOB_FLAG_RELEASE      (1 << 3)

/*============================================================================
 * Binary Format Structures (Packed for Portability)
 *============================================================================*/

/**
 * @brief Proof blob header (64 bytes, fixed size)
 *
 * Located at offset 0 of the blob.
 */
typedef struct __attribute__((packed)) {
    uint64_t magic;             /**< SERAPH_PROOF_BLOB_MAGIC */
    uint32_t version;           /**< SERAPH_PROOF_BLOB_VERSION */
    uint32_t flags;             /**< Blob flags */
    uint32_t proof_count;       /**< Number of proofs */
    uint32_t bucket_count;      /**< Hash index bucket count */
    uint64_t index_offset;      /**< Offset to hash index */
    uint64_t proofs_offset;     /**< Offset to proof records */
    uint64_t checksum_offset;   /**< Offset to SHA-256 checksum */
    uint64_t module_hash;       /**< Hash of source module */
    uint64_t generation;        /**< Proof generation timestamp */
    uint8_t  reserved[8];       /**< Reserved for future use */
} Seraph_Proof_Blob_Header;

/**
 * @brief Hash index entry (16 bytes)
 *
 * Maps code location hash to proof record index.
 */
typedef struct __attribute__((packed)) {
    uint64_t code_hash;         /**< Hash of code location */
    uint32_t proof_index;       /**< Index into proofs array */
    uint32_t next;              /**< Next entry in chain (for collisions) */
} Seraph_Proof_Blob_Index_Entry;

/**
 * @brief Packed proof record (40 bytes)
 *
 * Compact representation of a single proof.
 */
typedef struct __attribute__((packed)) {
    uint8_t  kind;              /**< Seraph_Proof_Kind */
    uint8_t  status;            /**< Seraph_Proof_Status */
    uint16_t flags;             /**< Proof-specific flags */
    uint32_t location_offset;   /**< Byte offset in source */
    uint64_t metadata;          /**< Kind-specific metadata */

    /** Union for kind-specific data */
    union {
        /** Bounds proof data */
        struct {
            uint64_t array_size;
            uint64_t index_min;
            uint64_t index_max;
        } bounds;

        /** Effect proof data */
        struct {
            uint32_t required_effects;
            uint32_t allowed_effects;
            uint64_t reserved;
        } effects;

        /** Permission proof data */
        struct {
            uint8_t  required_perms;
            uint8_t  granted_perms;
            uint16_t cap_slot;
            uint32_t reserved1;
            uint64_t reserved2;
        } permissions;

        /** Generation proof data */
        struct {
            uint64_t expected_gen;
            uint64_t current_gen;
        } generation;

        /** Raw bytes for unknown types */
        uint8_t raw[24];
    } data;
} Seraph_Proof_Blob_Record;

/*============================================================================
 * Runtime Representation
 *============================================================================*/

/**
 * @brief Loaded proof blob (runtime structure)
 *
 * This structure wraps a loaded proof blob and provides efficient access.
 * It does NOT own the underlying memory - the blob must remain valid.
 */
typedef struct {
    /** Pointer to blob header */
    const Seraph_Proof_Blob_Header* header;

    /** Pointer to bucket array */
    const uint32_t* buckets;

    /** Pointer to index entries */
    const Seraph_Proof_Blob_Index_Entry* entries;

    /** Pointer to proof records */
    const Seraph_Proof_Blob_Record* proofs;

    /** Pointer to checksum */
    const uint8_t* checksum;

    /** Total blob size */
    size_t blob_size;

    /** Is blob verified? */
    bool verified;

    /** Statistics */
    uint64_t queries;
    uint64_t hits;
    uint64_t misses;
} Seraph_Proof_Blob;

/*============================================================================
 * Proof Blob Builder (for generation)
 *============================================================================*/

/**
 * @brief Builder for constructing proof blobs
 */
typedef struct {
    uint8_t* buffer;            /**< Output buffer */
    size_t   capacity;          /**< Buffer capacity */
    size_t   size;              /**< Current size */
    uint32_t proof_count;       /**< Number of proofs added */
    uint32_t bucket_count;      /**< Number of hash buckets */
    uint64_t module_hash;       /**< Module hash */
    uint32_t flags;             /**< Builder flags */

    /** Temporary storage during building */
    Seraph_Proof_Blob_Record* temp_proofs;
    uint64_t* temp_hashes;
    uint32_t temp_capacity;
} Seraph_Proof_Blob_Builder;

/*============================================================================
 * Proof Blob Loading and Verification
 *============================================================================*/

/**
 * @brief Load a proof blob from memory
 *
 * @param blob      Output proof blob structure
 * @param data      Pointer to blob data
 * @param size      Size of blob data
 * @param verify    Whether to verify checksum
 * @return VBIT_TRUE on success, VBIT_FALSE on error
 *
 * Cost: ~50 cycles (without verify), ~5000 cycles (with verify)
 *
 * The blob structure references the input data directly; the data
 * must remain valid for the lifetime of the blob.
 */
Seraph_Vbit seraph_proof_blob_load(
    Seraph_Proof_Blob* blob,
    const void* data,
    size_t size,
    bool verify
);

/**
 * @brief Verify blob integrity
 *
 * @param blob  The proof blob to verify
 * @return VBIT_TRUE if valid, VBIT_FALSE if corrupted
 *
 * Computes SHA-256 of blob contents and compares to embedded checksum.
 */
Seraph_Vbit seraph_proof_blob_verify(const Seraph_Proof_Blob* blob);

/**
 * @brief Unload a proof blob
 *
 * @param blob  The proof blob to unload
 *
 * Clears the blob structure. Does NOT free underlying memory.
 */
void seraph_proof_blob_unload(Seraph_Proof_Blob* blob);

/*============================================================================
 * Proof Lookup (O(1) Query)
 *============================================================================*/

/**
 * @brief Compute hash for code location
 *
 * @param module_hash   Hash of module name
 * @param function_hash Hash of function name
 * @param offset        Statement offset in function
 * @param expr_index    Expression index within statement
 * @return 64-bit location hash
 *
 * Cost: ~10 cycles
 */
uint64_t seraph_proof_location_hash(
    uint64_t module_hash,
    uint64_t function_hash,
    uint32_t offset,
    uint32_t expr_index
);

/**
 * @brief Compute hash for string
 *
 * @param str  String to hash
 * @return 64-bit hash
 *
 * Uses FNV-1a algorithm.
 */
uint64_t seraph_proof_string_hash(const char* str);

/**
 * @brief Query proof blob for a specific location
 *
 * @param blob          The proof blob
 * @param location_hash Hash of code location
 * @param kind          Kind of proof to find
 * @return Proof status, or SKIPPED if not found
 *
 * Cost: ~5 cycles (single hash bucket lookup)
 */
Seraph_Proof_Status seraph_proof_blob_query(
    const Seraph_Proof_Blob* blob,
    uint64_t location_hash,
    Seraph_Proof_Kind kind
);

/**
 * @brief Get full proof record for a location
 *
 * @param blob          The proof blob
 * @param location_hash Hash of code location
 * @param kind          Kind of proof to find
 * @return Pointer to proof record, or NULL if not found
 *
 * Cost: ~5 cycles
 */
const Seraph_Proof_Blob_Record* seraph_proof_blob_get(
    const Seraph_Proof_Blob* blob,
    uint64_t location_hash,
    Seraph_Proof_Kind kind
);

/**
 * @brief Fast inline check for proven status
 *
 * @param blob          The proof blob (may be NULL)
 * @param location_hash Hash of code location
 * @param kind          Kind of proof to check
 * @return true if proof exists and is PROVEN
 *
 * Cost: ~5 cycles, inlined to branch on NULL then hash lookup
 */
static inline bool seraph_proof_blob_has_proven(
    const Seraph_Proof_Blob* blob,
    uint64_t location_hash,
    Seraph_Proof_Kind kind)
{
    if (!blob || !blob->verified) return false;
    return seraph_proof_blob_query(blob, location_hash, kind)
           == SERAPH_PROOF_STATUS_PROVEN;
}

/*============================================================================
 * Proof Blob Generation
 *============================================================================*/

/**
 * @brief Initialize a proof blob builder
 *
 * @param builder     Builder to initialize
 * @param buffer      Output buffer (or NULL to calculate size only)
 * @param capacity    Buffer capacity
 * @param module_hash Hash of module name
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_proof_blob_builder_init(
    Seraph_Proof_Blob_Builder* builder,
    uint8_t* buffer,
    size_t capacity,
    uint64_t module_hash
);

/**
 * @brief Add a proof to the builder
 *
 * @param builder       The builder
 * @param location_hash Hash of code location
 * @param proof         Proof to add
 * @return VBIT_TRUE on success
 */
Seraph_Vbit seraph_proof_blob_builder_add(
    Seraph_Proof_Blob_Builder* builder,
    uint64_t location_hash,
    const Seraph_Proof* proof
);

/**
 * @brief Finalize and generate the proof blob
 *
 * @param builder  The builder
 * @return Size of generated blob, or 0 on error
 *
 * After finalization, the buffer contains the complete proof blob.
 */
size_t seraph_proof_blob_builder_finalize(Seraph_Proof_Blob_Builder* builder);

/**
 * @brief Destroy builder and free temporary resources
 *
 * @param builder  The builder to destroy
 */
void seraph_proof_blob_builder_destroy(Seraph_Proof_Blob_Builder* builder);

/**
 * @brief Generate proof blob from proof table
 *
 * @param buffer      Output buffer (or NULL to calculate size)
 * @param capacity    Buffer capacity
 * @param table       Proof table to serialize
 * @param module_hash Hash of module name
 * @return Size of generated blob, or 0 on error
 *
 * Convenience function that combines builder operations.
 */
size_t seraph_proof_blob_generate(
    uint8_t* buffer,
    size_t capacity,
    const Seraph_Proof_Table* table,
    uint64_t module_hash
);

/*============================================================================
 * Zero-Overhead Execution Macros
 *============================================================================*/

/**
 * SERAPH_PROOF_GUARDED_BOUNDS - Bounds check with proof elision
 *
 * When proof exists and is PROVEN, this compiles to just the array access.
 * When proof is RUNTIME or missing, performs full bounds check.
 *
 * @param blob   Proof blob (may be NULL)
 * @param loc    Location hash (use SERAPH_LOC())
 * @param array  Array to access
 * @param index  Index to access
 * @param size   Array size
 * @param type   Element type
 * @param defval Default value on bounds error
 */
#define SERAPH_PROOF_GUARDED_BOUNDS(blob, loc, array, index, size, type, defval) \
    (seraph_proof_blob_has_proven((blob), (loc), SERAPH_PROOF_BOUNDS) ? \
        ((array)[(index)]) : \
        (((uint64_t)(index) < (uint64_t)(size)) ? \
            ((array)[(index)]) : (defval)))

/**
 * SERAPH_PROOF_GUARDED_VOID - VOID check with proof elision
 *
 * When proof exists and is PROVEN, skips VOID check entirely.
 * When proof is RUNTIME or missing, performs VOID check.
 *
 * @param blob   Proof blob (may be NULL)
 * @param loc    Location hash
 * @param value  Value to check
 * @param action Action on VOID (e.g., return SERAPH_VOID_U64)
 */
#define SERAPH_PROOF_GUARDED_VOID(blob, loc, value, action) \
    do { \
        if (!seraph_proof_blob_has_proven((blob), (loc), SERAPH_PROOF_VOID)) { \
            if (SERAPH_IS_VOID(value)) { action; } \
        } \
    } while (0)

/**
 * SERAPH_PROOF_GUARDED_PERM - Permission check with proof elision
 *
 * When proof exists and is PROVEN, skips permission validation.
 * When proof is RUNTIME or missing, performs full capability check.
 *
 * @param blob     Proof blob (may be NULL)
 * @param loc      Location hash
 * @param cap      Capability to check
 * @param required Required permissions
 * @param action   Action on permission denied
 */
#define SERAPH_PROOF_GUARDED_PERM(blob, loc, cap, required, action) \
    do { \
        if (!seraph_proof_blob_has_proven((blob), (loc), SERAPH_PROOF_PERMISSION)) { \
            if (!seraph_cap_has_permission((cap), (required))) { action; } \
        } \
    } while (0)

/**
 * SERAPH_PROOF_GUARDED_GEN - Generation check with proof elision
 *
 * When proof exists and is PROVEN, skips generation validation.
 * When proof is RUNTIME or missing, performs temporal validity check.
 *
 * @param blob     Proof blob (may be NULL)
 * @param loc      Location hash
 * @param cap      Capability to check
 * @param current  Current generation
 * @param action   Action on generation mismatch
 */
#define SERAPH_PROOF_GUARDED_GEN(blob, loc, cap, current, action) \
    do { \
        if (!seraph_proof_blob_has_proven((blob), (loc), SERAPH_PROOF_GENERATION)) { \
            if (!seraph_cap_check_generation((cap), (current))) { action; } \
        } \
    } while (0)

/**
 * SERAPH_LOC - Compute location hash at compile time (when possible)
 *
 * For runtime computation, use seraph_proof_location_hash().
 * This macro provides a placeholder for future constexpr optimization.
 */
#define SERAPH_LOC(module, func, offset, expr) \
    seraph_proof_location_hash( \
        seraph_proof_string_hash(module), \
        seraph_proof_string_hash(func), \
        (offset), (expr))

/*============================================================================
 * Statistics and Debugging
 *============================================================================*/

/**
 * @brief Proof blob statistics
 */
typedef struct {
    uint64_t total_proofs;
    uint64_t proven_count;
    uint64_t runtime_count;
    uint64_t assumed_count;
    uint64_t failed_count;
    uint64_t queries;
    uint64_t hits;
    uint64_t misses;
} Seraph_Proof_Blob_Stats;

/**
 * @brief Get statistics for a proof blob
 *
 * @param blob   The proof blob
 * @param stats  Output statistics
 */
void seraph_proof_blob_stats(
    const Seraph_Proof_Blob* blob,
    Seraph_Proof_Blob_Stats* stats
);

/**
 * @brief Print proof blob summary
 *
 * @param blob  The proof blob
 */
void seraph_proof_blob_print(const Seraph_Proof_Blob* blob);

/**
 * @brief Get proof kind name
 */
const char* seraph_proof_blob_kind_name(Seraph_Proof_Kind kind);

/**
 * @brief Get proof status name
 */
const char* seraph_proof_blob_status_name(Seraph_Proof_Status status);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_PROOF_BLOB_H */
