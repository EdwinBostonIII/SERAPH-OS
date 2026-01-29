/**
 * @file sha256.h
 * @brief SHA-256 Cryptographic Hash Implementation
 *
 * SERAPH's internal SHA-256 implementation for:
 * - SBF content integrity verification
 * - Merkle tree construction for proofs
 * - Binary identity generation
 *
 * This is a standalone implementation with no external dependencies,
 * suitable for use in kernel space.
 *
 * Implementation based on FIPS 180-4 specification.
 */

#ifndef SERAPH_CRYPTO_SHA256_H
#define SERAPH_CRYPTO_SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** SHA-256 produces 256-bit (32-byte) hash */
#define SHA256_DIGEST_SIZE      32

/** SHA-256 processes data in 512-bit (64-byte) blocks */
#define SHA256_BLOCK_SIZE       64

/*============================================================================
 * Types
 *============================================================================*/

/**
 * @brief SHA-256 context for incremental hashing
 */
typedef struct {
    uint32_t state[8];              /**< Hash state (A-H) */
    uint64_t count;                 /**< Number of bits processed */
    uint8_t buffer[SHA256_BLOCK_SIZE]; /**< Partial block buffer */
} SHA256_Context;

/**
 * @brief SHA-256 result type (32 bytes)
 */
typedef struct {
    uint8_t bytes[SHA256_DIGEST_SIZE];
} SHA256_Hash;

/*============================================================================
 * Context API (Incremental Hashing)
 *============================================================================*/

/**
 * @brief Initialize SHA-256 context
 * @param ctx Context to initialize
 */
void sha256_init(SHA256_Context* ctx);

/**
 * @brief Update SHA-256 context with data
 * @param ctx Initialized context
 * @param data Data to hash
 * @param len Length of data in bytes
 */
void sha256_update(SHA256_Context* ctx, const void* data, size_t len);

/**
 * @brief Finalize SHA-256 and get digest
 * @param ctx Context (will be cleared after)
 * @param digest Output buffer (32 bytes)
 */
void sha256_final(SHA256_Context* ctx, uint8_t* digest);

/**
 * @brief Finalize SHA-256 and get digest as SHA256_Hash
 * @param ctx Context (will be cleared after)
 * @param hash Output hash structure
 */
void sha256_final_hash(SHA256_Context* ctx, SHA256_Hash* hash);

/*============================================================================
 * One-Shot API
 *============================================================================*/

/**
 * @brief Compute SHA-256 hash of data in one call
 * @param data Data to hash
 * @param len Length of data in bytes
 * @param digest Output buffer (32 bytes)
 */
void sha256(const void* data, size_t len, uint8_t* digest);

/**
 * @brief Compute SHA-256 hash and return as structure
 * @param data Data to hash
 * @param len Length of data in bytes
 * @return SHA256_Hash structure
 */
SHA256_Hash sha256_compute(const void* data, size_t len);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Compare two SHA-256 hashes for equality
 * @param a First hash
 * @param b Second hash
 * @return 1 if equal, 0 if different
 *
 * Note: Constant-time comparison to prevent timing attacks.
 */
int sha256_equal(const uint8_t* a, const uint8_t* b);

/**
 * @brief Compare two SHA256_Hash structures for equality
 * @param a First hash
 * @param b Second hash
 * @return 1 if equal, 0 if different
 */
int sha256_hash_equal(const SHA256_Hash* a, const SHA256_Hash* b);

/**
 * @brief Check if hash is all zeros
 * @param hash Hash to check
 * @return 1 if all zeros, 0 otherwise
 */
int sha256_is_zero(const uint8_t* hash);

/**
 * @brief Copy SHA-256 hash
 * @param dst Destination (32 bytes)
 * @param src Source (32 bytes)
 */
void sha256_copy(uint8_t* dst, const uint8_t* src);

/**
 * @brief Clear SHA-256 hash to zeros
 * @param hash Hash to clear (32 bytes)
 */
void sha256_clear(uint8_t* hash);

/**
 * @brief Convert SHA-256 hash to hex string
 * @param hash Hash bytes (32 bytes)
 * @param buf Output buffer (at least 65 bytes for null terminator)
 * @param buf_size Size of output buffer
 * @return Number of characters written (excluding null)
 */
size_t sha256_to_hex(const uint8_t* hash, char* buf, size_t buf_size);

/**
 * @brief Parse SHA-256 hash from hex string
 * @param hex Hex string (64 characters)
 * @param hash Output hash (32 bytes)
 * @return 1 on success, 0 on parse error
 */
int sha256_from_hex(const char* hex, uint8_t* hash);

/*============================================================================
 * Merkle Tree Support
 *============================================================================*/

/**
 * @brief Compute hash of two concatenated hashes (for Merkle tree)
 * @param left Left child hash (32 bytes)
 * @param right Right child hash (32 bytes)
 * @param parent Output parent hash (32 bytes)
 */
void sha256_hash_pair(const uint8_t* left, const uint8_t* right, uint8_t* parent);

/**
 * @brief Compute Merkle root from array of leaf hashes
 *
 * For n leaves, computes the binary Merkle tree root.
 * If n is not a power of 2, the tree is padded by duplicating
 * the last hash at each level as needed.
 *
 * @param leaves Array of leaf hashes (each 32 bytes)
 * @param leaf_count Number of leaves
 * @param root Output root hash (32 bytes)
 * @param work_buffer Temporary buffer, must be at least (leaf_count * 32) bytes
 * @return 1 on success, 0 on error
 */
int sha256_merkle_root(
    const uint8_t* leaves,
    size_t leaf_count,
    uint8_t* root,
    uint8_t* work_buffer
);

/**
 * @brief Compute Merkle root with internal allocation
 * @param leaves Array of leaf hashes
 * @param leaf_count Number of leaves
 * @param root Output root hash
 * @return 1 on success, 0 on error or allocation failure
 */
int sha256_merkle_root_alloc(
    const uint8_t* leaves,
    size_t leaf_count,
    uint8_t* root
);

/*============================================================================
 * HMAC-SHA256
 *============================================================================*/

/**
 * @brief Compute HMAC-SHA256
 * @param key HMAC key
 * @param key_len Key length in bytes
 * @param data Data to authenticate
 * @param data_len Data length in bytes
 * @param mac Output MAC (32 bytes)
 */
void hmac_sha256(
    const void* key, size_t key_len,
    const void* data, size_t data_len,
    uint8_t* mac
);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_CRYPTO_SHA256_H */
