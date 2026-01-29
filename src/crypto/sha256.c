/**
 * @file sha256.c
 * @brief SHA-256 Cryptographic Hash Implementation
 *
 * FIPS 180-4 compliant SHA-256 implementation.
 * Optimized for clarity and correctness over speed.
 *
 * This implementation:
 * - Has no external dependencies
 * - Is suitable for kernel space
 * - Handles arbitrary input lengths
 * - Provides Merkle tree support
 */

#include "seraph/crypto/sha256.h"
#include <string.h>

/*============================================================================
 * SHA-256 Constants
 *============================================================================*/

/** SHA-256 initial hash values (first 32 bits of fractional parts of
 *  square roots of first 8 primes 2..19) */
static const uint32_t sha256_init_state[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/** SHA-256 round constants (first 32 bits of fractional parts of
 *  cube roots of first 64 primes 2..311) */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/*============================================================================
 * Helper Macros
 *============================================================================*/

/** Right rotate 32-bit value */
#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/** SHA-256 logical functions */
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x)    (ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define SIGMA1(x)    (ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define sigma0(x)    (ROTR32(x, 7) ^ ROTR32(x, 18) ^ ((x) >> 3))
#define sigma1(x)    (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

/** Load big-endian 32-bit value from bytes */
static inline uint32_t load_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

/** Store big-endian 32-bit value to bytes */
static inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/** Store big-endian 64-bit value to bytes */
static inline void store_be64(uint8_t* p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)v;
}

/*============================================================================
 * SHA-256 Transform (Process One Block)
 *============================================================================*/

/**
 * @brief Process a single 512-bit (64-byte) block
 */
static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;
    int t;

    /* Prepare message schedule */
    for (t = 0; t < 16; t++) {
        W[t] = load_be32(&block[t * 4]);
    }
    for (t = 16; t < 64; t++) {
        W[t] = sigma1(W[t-2]) + W[t-7] + sigma0(W[t-15]) + W[t-16];
    }

    /* Initialize working variables */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    /* 64 rounds */
    for (t = 0; t < 64; t++) {
        T1 = h + SIGMA1(e) + CH(e, f, g) + K[t] + W[t];
        T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    /* Update state */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;

    /* Clear sensitive data */
    memset(W, 0, sizeof(W));
}

/*============================================================================
 * Context API Implementation
 *============================================================================*/

void sha256_init(SHA256_Context* ctx) {
    if (ctx == NULL) return;

    memcpy(ctx->state, sha256_init_state, sizeof(sha256_init_state));
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA256_BLOCK_SIZE);
}

void sha256_update(SHA256_Context* ctx, const void* data, size_t len) {
    if (ctx == NULL || data == NULL || len == 0) return;

    const uint8_t* input = (const uint8_t*)data;
    size_t buffer_fill = (size_t)((ctx->count / 8) % SHA256_BLOCK_SIZE);

    ctx->count += (uint64_t)len * 8;

    /* If we have partial data in buffer, try to complete it */
    if (buffer_fill > 0) {
        size_t needed = SHA256_BLOCK_SIZE - buffer_fill;
        if (len >= needed) {
            memcpy(&ctx->buffer[buffer_fill], input, needed);
            sha256_transform(ctx->state, ctx->buffer);
            input += needed;
            len -= needed;
            buffer_fill = 0;
        } else {
            memcpy(&ctx->buffer[buffer_fill], input, len);
            return;
        }
    }

    /* Process complete blocks */
    while (len >= SHA256_BLOCK_SIZE) {
        sha256_transform(ctx->state, input);
        input += SHA256_BLOCK_SIZE;
        len -= SHA256_BLOCK_SIZE;
    }

    /* Store remaining partial block */
    if (len > 0) {
        memcpy(ctx->buffer, input, len);
    }
}

void sha256_final(SHA256_Context* ctx, uint8_t* digest) {
    if (ctx == NULL || digest == NULL) return;

    uint8_t finalblock[SHA256_BLOCK_SIZE * 2];
    size_t buffer_fill = (size_t)((ctx->count / 8) % SHA256_BLOCK_SIZE);
    size_t pad_len;

    /* Copy partial data */
    memcpy(finalblock, ctx->buffer, buffer_fill);

    /* Append bit '1' to message */
    finalblock[buffer_fill] = 0x80;
    buffer_fill++;

    /* Calculate padding length */
    /* We need space for padding + 8-byte length */
    if (buffer_fill > 56) {
        /* Need two blocks */
        pad_len = 128 - buffer_fill;
    } else {
        /* Fits in one block */
        pad_len = 64 - buffer_fill;
    }

    /* Zero padding (minus 8 bytes for length) */
    memset(&finalblock[buffer_fill], 0, pad_len - 8);

    /* Append length in bits as big-endian 64-bit */
    store_be64(&finalblock[buffer_fill + pad_len - 8], ctx->count);

    /* Process final block(s) */
    sha256_transform(ctx->state, finalblock);
    if (buffer_fill > 56) {
        sha256_transform(ctx->state, &finalblock[64]);
    }

    /* Output hash */
    for (int i = 0; i < 8; i++) {
        store_be32(&digest[i * 4], ctx->state[i]);
    }

    /* Clear sensitive data */
    memset(ctx, 0, sizeof(*ctx));
    memset(finalblock, 0, sizeof(finalblock));
}

void sha256_final_hash(SHA256_Context* ctx, SHA256_Hash* hash) {
    if (hash == NULL) return;
    sha256_final(ctx, hash->bytes);
}

/*============================================================================
 * One-Shot API Implementation
 *============================================================================*/

void sha256(const void* data, size_t len, uint8_t* digest) {
    SHA256_Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

SHA256_Hash sha256_compute(const void* data, size_t len) {
    SHA256_Hash hash;
    sha256(data, len, hash.bytes);
    return hash;
}

/*============================================================================
 * Utility Functions Implementation
 *============================================================================*/

int sha256_equal(const uint8_t* a, const uint8_t* b) {
    if (a == NULL || b == NULL) return 0;

    /* Constant-time comparison to prevent timing attacks */
    uint8_t diff = 0;
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0 ? 1 : 0;
}

int sha256_hash_equal(const SHA256_Hash* a, const SHA256_Hash* b) {
    if (a == NULL || b == NULL) return 0;
    return sha256_equal(a->bytes, b->bytes);
}

int sha256_is_zero(const uint8_t* hash) {
    if (hash == NULL) return 0;

    uint8_t acc = 0;
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        acc |= hash[i];
    }
    return acc == 0 ? 1 : 0;
}

void sha256_copy(uint8_t* dst, const uint8_t* src) {
    if (dst == NULL || src == NULL) return;
    memcpy(dst, src, SHA256_DIGEST_SIZE);
}

void sha256_clear(uint8_t* hash) {
    if (hash == NULL) return;
    memset(hash, 0, SHA256_DIGEST_SIZE);
}

/** Hex character table */
static const char hex_chars[] = "0123456789abcdef";

size_t sha256_to_hex(const uint8_t* hash, char* buf, size_t buf_size) {
    if (hash == NULL || buf == NULL || buf_size < 65) return 0;

    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        buf[i * 2]     = hex_chars[hash[i] >> 4];
        buf[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
    buf[64] = '\0';
    return 64;
}

/** Parse hex digit */
static inline int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int sha256_from_hex(const char* hex, uint8_t* hash) {
    if (hex == NULL || hash == NULL) return 0;

    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        int hi = hex_digit_value(hex[i * 2]);
        int lo = hex_digit_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        hash[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

/*============================================================================
 * Merkle Tree Implementation
 *============================================================================*/

void sha256_hash_pair(const uint8_t* left, const uint8_t* right, uint8_t* parent) {
    if (left == NULL || right == NULL || parent == NULL) return;

    SHA256_Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, left, SHA256_DIGEST_SIZE);
    sha256_update(&ctx, right, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, parent);
}

int sha256_merkle_root(
    const uint8_t* leaves,
    size_t leaf_count,
    uint8_t* root,
    uint8_t* work_buffer
) {
    if (leaves == NULL || root == NULL || work_buffer == NULL || leaf_count == 0) {
        if (root != NULL) sha256_clear(root);
        return leaf_count == 0 ? 1 : 0;
    }

    /* Special case: single leaf is the root */
    if (leaf_count == 1) {
        sha256_copy(root, leaves);
        return 1;
    }

    /* Copy leaves to work buffer */
    memcpy(work_buffer, leaves, leaf_count * SHA256_DIGEST_SIZE);

    /* Build tree level by level */
    size_t level_size = leaf_count;
    uint8_t* level_data = work_buffer;

    while (level_size > 1) {
        size_t next_size = (level_size + 1) / 2;

        for (size_t i = 0; i < next_size; i++) {
            const uint8_t* left = &level_data[i * 2 * SHA256_DIGEST_SIZE];
            const uint8_t* right;

            if (i * 2 + 1 < level_size) {
                right = &level_data[(i * 2 + 1) * SHA256_DIGEST_SIZE];
            } else {
                /* Odd number of nodes: duplicate last one */
                right = left;
            }

            sha256_hash_pair(left, right, &level_data[i * SHA256_DIGEST_SIZE]);
        }

        level_size = next_size;
    }

    /* Root is at the beginning of work buffer */
    sha256_copy(root, level_data);
    return 1;
}

/* Simple memory allocator for standalone use */
#ifdef SERAPH_KERNEL
extern void* seraph_kmalloc(size_t size);
extern void seraph_kfree(void* ptr);
#define MERKLE_ALLOC(size) seraph_kmalloc(size)
#define MERKLE_FREE(ptr) seraph_kfree(ptr)
#else
#include <stdlib.h>
#define MERKLE_ALLOC(size) malloc(size)
#define MERKLE_FREE(ptr) free(ptr)
#endif

int sha256_merkle_root_alloc(
    const uint8_t* leaves,
    size_t leaf_count,
    uint8_t* root
) {
    if (leaves == NULL || root == NULL || leaf_count == 0) {
        if (root != NULL) sha256_clear(root);
        return leaf_count == 0 ? 1 : 0;
    }

    /* Allocate work buffer */
    size_t buffer_size = leaf_count * SHA256_DIGEST_SIZE;
    uint8_t* work = (uint8_t*)MERKLE_ALLOC(buffer_size);
    if (work == NULL) {
        sha256_clear(root);
        return 0;
    }

    int result = sha256_merkle_root(leaves, leaf_count, root, work);

    /* Clear and free work buffer */
    memset(work, 0, buffer_size);
    MERKLE_FREE(work);

    return result;
}

/*============================================================================
 * HMAC-SHA256 Implementation
 *============================================================================*/

void hmac_sha256(
    const void* key, size_t key_len,
    const void* data, size_t data_len,
    uint8_t* mac
) {
    if (key == NULL || data == NULL || mac == NULL) {
        if (mac != NULL) sha256_clear(mac);
        return;
    }

    uint8_t k_ipad[SHA256_BLOCK_SIZE];
    uint8_t k_opad[SHA256_BLOCK_SIZE];
    uint8_t key_hash[SHA256_DIGEST_SIZE];
    const uint8_t* k;
    size_t k_len;

    /* If key is longer than block size, hash it first */
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, key_hash);
        k = key_hash;
        k_len = SHA256_DIGEST_SIZE;
    } else {
        k = (const uint8_t*)key;
        k_len = key_len;
    }

    /* Prepare padded keys */
    memset(k_ipad, 0x36, SHA256_BLOCK_SIZE);
    memset(k_opad, 0x5c, SHA256_BLOCK_SIZE);

    for (size_t i = 0; i < k_len; i++) {
        k_ipad[i] ^= k[i];
        k_opad[i] ^= k[i];
    }

    /* Inner hash: H(k_ipad || data) */
    SHA256_Context ctx;
    uint8_t inner_hash[SHA256_DIGEST_SIZE];

    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    /* Outer hash: H(k_opad || inner_hash) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, mac);

    /* Clear sensitive data */
    memset(k_ipad, 0, SHA256_BLOCK_SIZE);
    memset(k_opad, 0, SHA256_BLOCK_SIZE);
    memset(key_hash, 0, SHA256_DIGEST_SIZE);
    memset(inner_hash, 0, SHA256_DIGEST_SIZE);
}
