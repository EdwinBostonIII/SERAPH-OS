/**
 * @file proof_blob.c
 * @brief MC28: Zero-Overhead Strand Execution via Proof Blobs - Implementation
 *
 * Implementation of proof blob loading, verification, query, and generation.
 * Provides the runtime support for proof-guided strand execution.
 */

#include "seraph/proof_blob.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*============================================================================
 * Internal: FNV-1a Hash Implementation (NIH: No external dependencies)
 *============================================================================*/

/** FNV-1a 64-bit offset basis */
#define SERAPH_FNV64_OFFSET 0xcbf29ce484222325ULL

/** FNV-1a 64-bit prime */
#define SERAPH_FNV64_PRIME  0x100000001b3ULL

/**
 * @brief FNV-1a hash for bytes
 */
static uint64_t fnv1a_hash(const uint8_t* data, size_t len) {
    uint64_t hash = SERAPH_FNV64_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= SERAPH_FNV64_PRIME;
    }
    return hash;
}

/**
 * @brief Generation counter for proof blob timestamps
 *
 * Since the kernel may not have access to wall-clock time, we use a
 * monotonically increasing counter as a logical timestamp. This ensures
 * each proof blob has a unique, ordered generation number.
 */
static uint64_t g_proof_blob_generation = 1;

/*============================================================================
 * Internal: Simple SHA-256 Implementation (NIH: No OpenSSL dependency)
 *============================================================================*/

/**
 * SHA-256 context
 */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} SHA256_CTX;

/** SHA-256 constants */
static const uint32_t sha256_k[64] = {
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

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX* ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t w[64];

    /* Prepare message schedule */
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = SHA256_SIG1(w[i - 2]) + w[i - 7] +
               SHA256_SIG0(w[i - 15]) + w[i - 16];
    }

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* Main loop */
    for (int i = 0; i < 64; i++) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + w[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add to state */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX* ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(SHA256_CTX* ctx, const uint8_t* data, size_t len) {
    size_t i = 0;
    size_t idx = (size_t)(ctx->count & 0x3F);

    ctx->count += len;

    /* Fill buffer and transform */
    if (idx) {
        size_t left = 64 - idx;
        if (len < left) {
            memcpy(ctx->buffer + idx, data, len);
            return;
        }
        memcpy(ctx->buffer + idx, data, left);
        sha256_transform(ctx, ctx->buffer);
        i = left;
    }

    /* Transform full blocks */
    while (i + 64 <= len) {
        sha256_transform(ctx, data + i);
        i += 64;
    }

    /* Copy remainder */
    if (i < len) {
        memcpy(ctx->buffer, data + i, len - i);
    }
}

static void sha256_final(SHA256_CTX* ctx, uint8_t hash[32]) {
    size_t idx = (size_t)(ctx->count & 0x3F);
    uint64_t bits = ctx->count * 8;

    /* Pad */
    ctx->buffer[idx++] = 0x80;
    if (idx > 56) {
        memset(ctx->buffer + idx, 0, 64 - idx);
        sha256_transform(ctx, ctx->buffer);
        idx = 0;
    }
    memset(ctx->buffer + idx, 0, 56 - idx);

    /* Append length */
    ctx->buffer[56] = (uint8_t)(bits >> 56);
    ctx->buffer[57] = (uint8_t)(bits >> 48);
    ctx->buffer[58] = (uint8_t)(bits >> 40);
    ctx->buffer[59] = (uint8_t)(bits >> 32);
    ctx->buffer[60] = (uint8_t)(bits >> 24);
    ctx->buffer[61] = (uint8_t)(bits >> 16);
    ctx->buffer[62] = (uint8_t)(bits >> 8);
    ctx->buffer[63] = (uint8_t)(bits);
    sha256_transform(ctx, ctx->buffer);

    /* Output hash */
    for (int i = 0; i < 8; i++) {
        hash[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

/*============================================================================
 * String Hashing
 *============================================================================*/

uint64_t seraph_proof_string_hash(const char* str) {
    if (!str) return 0;
    return fnv1a_hash((const uint8_t*)str, strlen(str));
}

/*============================================================================
 * Location Hash Computation
 *============================================================================*/

uint64_t seraph_proof_location_hash(
    uint64_t module_hash,
    uint64_t function_hash,
    uint32_t offset,
    uint32_t expr_index)
{
    /* Combine all components into a single hash */
    uint64_t hash = SERAPH_FNV64_OFFSET;

    /* Mix in module hash */
    hash ^= module_hash;
    hash *= SERAPH_FNV64_PRIME;

    /* Mix in function hash */
    hash ^= function_hash;
    hash *= SERAPH_FNV64_PRIME;

    /* Mix in offset */
    hash ^= (uint64_t)offset;
    hash *= SERAPH_FNV64_PRIME;

    /* Mix in expression index */
    hash ^= (uint64_t)expr_index;
    hash *= SERAPH_FNV64_PRIME;

    return hash;
}

/*============================================================================
 * Proof Blob Loading
 *============================================================================*/

Seraph_Vbit seraph_proof_blob_load(
    Seraph_Proof_Blob* blob,
    const void* data,
    size_t size,
    bool verify)
{
    if (!blob || !data) return SERAPH_VBIT_VOID;

    /* Clear blob structure */
    memset(blob, 0, sizeof(Seraph_Proof_Blob));

    /* Minimum size check */
    if (size < sizeof(Seraph_Proof_Blob_Header)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Map header */
    const Seraph_Proof_Blob_Header* header =
        (const Seraph_Proof_Blob_Header*)data;

    /* Validate magic */
    if (header->magic != SERAPH_PROOF_BLOB_MAGIC) {
        return SERAPH_VBIT_FALSE;
    }

    /* Validate version */
    if ((header->version >> 16) != (SERAPH_PROOF_BLOB_VERSION >> 16)) {
        /* Major version mismatch */
        return SERAPH_VBIT_FALSE;
    }

    /* Validate offsets */
    if (header->index_offset >= size ||
        header->proofs_offset >= size ||
        header->checksum_offset >= size) {
        return SERAPH_VBIT_FALSE;
    }

    /* Calculate expected size */
    size_t expected_size = header->checksum_offset + SERAPH_PROOF_BLOB_CHECKSUM_SIZE;
    if (size < expected_size) {
        return SERAPH_VBIT_FALSE;
    }

    /* Map structures */
    blob->header = header;
    blob->blob_size = size;

    /* Map hash index */
    const uint8_t* base = (const uint8_t*)data;
    blob->buckets = (const uint32_t*)(base + header->index_offset);
    blob->entries = (const Seraph_Proof_Blob_Index_Entry*)(
        base + header->index_offset + header->bucket_count * sizeof(uint32_t));

    /* Map proofs */
    blob->proofs = (const Seraph_Proof_Blob_Record*)(base + header->proofs_offset);

    /* Map checksum */
    blob->checksum = base + header->checksum_offset;

    /* Verify if requested */
    if (verify) {
        if (seraph_proof_blob_verify(blob) != SERAPH_VBIT_TRUE) {
            memset(blob, 0, sizeof(Seraph_Proof_Blob));
            return SERAPH_VBIT_FALSE;
        }
        blob->verified = true;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_proof_blob_verify(const Seraph_Proof_Blob* blob) {
    if (!blob || !blob->header) return SERAPH_VBIT_VOID;

    /* Compute SHA-256 of everything except the checksum */
    SHA256_CTX ctx;
    sha256_init(&ctx);

    const uint8_t* data = (const uint8_t*)blob->header;
    size_t data_size = blob->header->checksum_offset;

    sha256_update(&ctx, data, data_size);

    uint8_t computed[32];
    sha256_final(&ctx, computed);

    /* Compare with embedded checksum */
    if (memcmp(computed, blob->checksum, 32) != 0) {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

void seraph_proof_blob_unload(Seraph_Proof_Blob* blob) {
    if (!blob) return;
    memset(blob, 0, sizeof(Seraph_Proof_Blob));
}

/*============================================================================
 * Proof Lookup
 *============================================================================*/

Seraph_Proof_Status seraph_proof_blob_query(
    const Seraph_Proof_Blob* blob,
    uint64_t location_hash,
    Seraph_Proof_Kind kind)
{
    if (!blob || !blob->header) return SERAPH_PROOF_STATUS_SKIPPED;

    /* Update statistics (cast away const for stats) */
    ((Seraph_Proof_Blob*)blob)->queries++;

    /* Calculate bucket */
    uint32_t bucket = (uint32_t)(location_hash % blob->header->bucket_count);
    uint32_t entry_idx = blob->buckets[bucket];

    /* Walk chain */
    while (entry_idx != SERAPH_PROOF_BLOB_EMPTY_BUCKET) {
        const Seraph_Proof_Blob_Index_Entry* entry = &blob->entries[entry_idx];

        if (entry->code_hash == location_hash) {
            const Seraph_Proof_Blob_Record* proof = &blob->proofs[entry->proof_index];

            if (proof->kind == kind) {
                ((Seraph_Proof_Blob*)blob)->hits++;
                return (Seraph_Proof_Status)proof->status;
            }
        }

        entry_idx = entry->next;
    }

    ((Seraph_Proof_Blob*)blob)->misses++;
    return SERAPH_PROOF_STATUS_SKIPPED;
}

const Seraph_Proof_Blob_Record* seraph_proof_blob_get(
    const Seraph_Proof_Blob* blob,
    uint64_t location_hash,
    Seraph_Proof_Kind kind)
{
    if (!blob || !blob->header) return NULL;

    /* Calculate bucket */
    uint32_t bucket = (uint32_t)(location_hash % blob->header->bucket_count);
    uint32_t entry_idx = blob->buckets[bucket];

    /* Walk chain */
    while (entry_idx != SERAPH_PROOF_BLOB_EMPTY_BUCKET) {
        const Seraph_Proof_Blob_Index_Entry* entry = &blob->entries[entry_idx];

        if (entry->code_hash == location_hash) {
            const Seraph_Proof_Blob_Record* proof = &blob->proofs[entry->proof_index];

            if (proof->kind == kind) {
                return proof;
            }
        }

        entry_idx = entry->next;
    }

    return NULL;
}

/*============================================================================
 * Proof Blob Generation
 *============================================================================*/

Seraph_Vbit seraph_proof_blob_builder_init(
    Seraph_Proof_Blob_Builder* builder,
    uint8_t* buffer,
    size_t capacity,
    uint64_t module_hash)
{
    if (!builder) return SERAPH_VBIT_VOID;

    memset(builder, 0, sizeof(Seraph_Proof_Blob_Builder));

    builder->buffer = buffer;
    builder->capacity = capacity;
    builder->module_hash = module_hash;

    /* Allocate temporary storage */
    builder->temp_capacity = 256;
    builder->temp_proofs = (Seraph_Proof_Blob_Record*)malloc(
        builder->temp_capacity * sizeof(Seraph_Proof_Blob_Record));
    builder->temp_hashes = (uint64_t*)malloc(
        builder->temp_capacity * sizeof(uint64_t));

    if (!builder->temp_proofs || !builder->temp_hashes) {
        free(builder->temp_proofs);
        free(builder->temp_hashes);
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_proof_blob_builder_add(
    Seraph_Proof_Blob_Builder* builder,
    uint64_t location_hash,
    const Seraph_Proof* proof)
{
    if (!builder || !proof) return SERAPH_VBIT_VOID;

    /* Grow if needed */
    if (builder->proof_count >= builder->temp_capacity) {
        uint32_t new_capacity = builder->temp_capacity * 2;

        Seraph_Proof_Blob_Record* new_proofs = (Seraph_Proof_Blob_Record*)realloc(
            builder->temp_proofs,
            new_capacity * sizeof(Seraph_Proof_Blob_Record));
        uint64_t* new_hashes = (uint64_t*)realloc(
            builder->temp_hashes,
            new_capacity * sizeof(uint64_t));

        if (!new_proofs || !new_hashes) {
            free(new_proofs);
            free(new_hashes);
            return SERAPH_VBIT_FALSE;
        }

        builder->temp_proofs = new_proofs;
        builder->temp_hashes = new_hashes;
        builder->temp_capacity = new_capacity;
    }

    /* Convert Seraph_Proof to packed record */
    Seraph_Proof_Blob_Record* record = &builder->temp_proofs[builder->proof_count];
    memset(record, 0, sizeof(Seraph_Proof_Blob_Record));

    record->kind = (uint8_t)proof->kind;
    record->status = (uint8_t)proof->status;
    record->flags = 0;
    record->location_offset = proof->loc.offset;
    record->metadata = proof->metadata;

    /* Copy kind-specific data */
    switch (proof->kind) {
        case SERAPH_PROOF_BOUNDS:
            record->data.bounds.array_size = proof->bounds.array_size;
            record->data.bounds.index_min = proof->bounds.index_min;
            record->data.bounds.index_max = proof->bounds.index_max;
            break;

        case SERAPH_PROOF_EFFECT:
            record->data.effects.required_effects = proof->effects.required_effects;
            record->data.effects.allowed_effects = proof->effects.allowed_effects;
            break;

        case SERAPH_PROOF_PERMISSION:
            record->data.permissions.required_perms = proof->permissions.required_perms;
            record->data.permissions.granted_perms = proof->permissions.granted_perms;
            break;

        default:
            /* Other kinds just use metadata */
            break;
    }

    builder->temp_hashes[builder->proof_count] = location_hash;
    builder->proof_count++;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Calculate optimal bucket count for hash table
 */
static uint32_t calculate_bucket_count(uint32_t proof_count) {
    /* Use load factor of ~0.75 */
    uint32_t buckets = (proof_count * 4) / 3;

    /* Round up to power of 2 for fast modulo */
    if (buckets < SERAPH_PROOF_BLOB_MIN_BUCKETS) {
        buckets = SERAPH_PROOF_BLOB_MIN_BUCKETS;
    }

    /* Find next power of 2 */
    buckets--;
    buckets |= buckets >> 1;
    buckets |= buckets >> 2;
    buckets |= buckets >> 4;
    buckets |= buckets >> 8;
    buckets |= buckets >> 16;
    buckets++;

    return buckets;
}

size_t seraph_proof_blob_builder_finalize(Seraph_Proof_Blob_Builder* builder) {
    if (!builder) return 0;

    uint32_t proof_count = builder->proof_count;
    uint32_t bucket_count = calculate_bucket_count(proof_count);

    /* Calculate sizes */
    size_t header_size = sizeof(Seraph_Proof_Blob_Header);
    size_t bucket_size = bucket_count * sizeof(uint32_t);
    size_t entries_size = proof_count * sizeof(Seraph_Proof_Blob_Index_Entry);
    size_t index_size = bucket_size + entries_size;
    size_t proofs_size = proof_count * sizeof(Seraph_Proof_Blob_Record);
    size_t checksum_size = SERAPH_PROOF_BLOB_CHECKSUM_SIZE;

    size_t total_size = header_size + index_size + proofs_size + checksum_size;

    /* If no buffer, just return size */
    if (!builder->buffer) {
        return total_size;
    }

    /* Check capacity */
    if (total_size > builder->capacity) {
        return 0;
    }

    /* Calculate offsets */
    uint64_t index_offset = header_size;
    uint64_t proofs_offset = index_offset + index_size;
    uint64_t checksum_offset = proofs_offset + proofs_size;

    /* Write header */
    Seraph_Proof_Blob_Header* header = (Seraph_Proof_Blob_Header*)builder->buffer;
    header->magic = SERAPH_PROOF_BLOB_MAGIC;
    header->version = SERAPH_PROOF_BLOB_VERSION;
    header->flags = builder->flags | SERAPH_PROOF_BLOB_FLAG_RELEASE;
    header->proof_count = proof_count;
    header->bucket_count = bucket_count;
    header->index_offset = index_offset;
    header->proofs_offset = proofs_offset;
    header->checksum_offset = checksum_offset;
    header->module_hash = builder->module_hash;
    header->generation = g_proof_blob_generation++;  /* Monotonic logical timestamp */
    memset(header->reserved, 0, sizeof(header->reserved));

    /* Write buckets (all empty initially) */
    uint32_t* buckets = (uint32_t*)(builder->buffer + index_offset);
    for (uint32_t i = 0; i < bucket_count; i++) {
        buckets[i] = SERAPH_PROOF_BLOB_EMPTY_BUCKET;
    }

    /* Write entries and build hash chains */
    Seraph_Proof_Blob_Index_Entry* entries =
        (Seraph_Proof_Blob_Index_Entry*)(builder->buffer + index_offset + bucket_size);

    for (uint32_t i = 0; i < proof_count; i++) {
        uint64_t hash = builder->temp_hashes[i];
        uint32_t bucket = (uint32_t)(hash % bucket_count);

        entries[i].code_hash = hash;
        entries[i].proof_index = i;
        entries[i].next = buckets[bucket];
        buckets[bucket] = i;
    }

    /* Write proofs */
    memcpy(builder->buffer + proofs_offset,
           builder->temp_proofs,
           proofs_size);

    /* Compute and write checksum */
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, builder->buffer, checksum_offset);
    sha256_final(&ctx, builder->buffer + checksum_offset);

    builder->size = total_size;
    return total_size;
}

void seraph_proof_blob_builder_destroy(Seraph_Proof_Blob_Builder* builder) {
    if (!builder) return;

    free(builder->temp_proofs);
    free(builder->temp_hashes);
    memset(builder, 0, sizeof(Seraph_Proof_Blob_Builder));
}

size_t seraph_proof_blob_generate(
    uint8_t* buffer,
    size_t capacity,
    const Seraph_Proof_Table* table,
    uint64_t module_hash)
{
    if (!table) return 0;

    Seraph_Proof_Blob_Builder builder;
    if (seraph_proof_blob_builder_init(&builder, buffer, capacity, module_hash)
        != SERAPH_VBIT_TRUE) {
        return 0;
    }

    /* Add all proofs from table */
    for (Seraph_Proof* proof = table->proofs; proof; proof = proof->next) {
        /* Compute location hash from source location */
        uint64_t loc_hash = seraph_proof_location_hash(
            module_hash,
            0,  /* No function hash available in proof */
            proof->loc.offset,
            0   /* No expression index */
        );

        if (seraph_proof_blob_builder_add(&builder, loc_hash, proof)
            != SERAPH_VBIT_TRUE) {
            seraph_proof_blob_builder_destroy(&builder);
            return 0;
        }
    }

    size_t size = seraph_proof_blob_builder_finalize(&builder);
    seraph_proof_blob_builder_destroy(&builder);

    return size;
}

/*============================================================================
 * Statistics and Debugging
 *============================================================================*/

void seraph_proof_blob_stats(
    const Seraph_Proof_Blob* blob,
    Seraph_Proof_Blob_Stats* stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(Seraph_Proof_Blob_Stats));

    if (!blob || !blob->header) return;

    stats->total_proofs = blob->header->proof_count;
    stats->queries = blob->queries;
    stats->hits = blob->hits;
    stats->misses = blob->misses;

    /* Count by status */
    for (uint32_t i = 0; i < blob->header->proof_count; i++) {
        const Seraph_Proof_Blob_Record* proof = &blob->proofs[i];
        switch ((Seraph_Proof_Status)proof->status) {
            case SERAPH_PROOF_STATUS_PROVEN:
                stats->proven_count++;
                break;
            case SERAPH_PROOF_STATUS_RUNTIME:
                stats->runtime_count++;
                break;
            case SERAPH_PROOF_STATUS_ASSUMED:
                stats->assumed_count++;
                break;
            case SERAPH_PROOF_STATUS_FAILED:
                stats->failed_count++;
                break;
            default:
                break;
        }
    }
}

const char* seraph_proof_blob_kind_name(Seraph_Proof_Kind kind) {
    switch (kind) {
        case SERAPH_PROOF_BOUNDS:      return "BOUNDS";
        case SERAPH_PROOF_VOID:        return "VOID";
        case SERAPH_PROOF_EFFECT:      return "EFFECT";
        case SERAPH_PROOF_PERMISSION:  return "PERMISSION";
        case SERAPH_PROOF_GENERATION:  return "GENERATION";
        case SERAPH_PROOF_SUBSTRATE:   return "SUBSTRATE";
        case SERAPH_PROOF_TYPE:        return "TYPE";
        case SERAPH_PROOF_INIT:        return "INIT";
        case SERAPH_PROOF_OVERFLOW:    return "OVERFLOW";
        case SERAPH_PROOF_NULL:        return "NULL";
        case SERAPH_PROOF_INVARIANT:   return "INVARIANT";
        case SERAPH_PROOF_TERMINATION: return "TERMINATION";
        default:                       return "UNKNOWN";
    }
}

const char* seraph_proof_blob_status_name(Seraph_Proof_Status status) {
    switch (status) {
        case SERAPH_PROOF_STATUS_PROVEN:  return "PROVEN";
        case SERAPH_PROOF_STATUS_ASSUMED: return "ASSUMED";
        case SERAPH_PROOF_STATUS_RUNTIME: return "RUNTIME";
        case SERAPH_PROOF_STATUS_FAILED:  return "FAILED";
        case SERAPH_PROOF_STATUS_SKIPPED: return "SKIPPED";
        default:                          return "UNKNOWN";
    }
}

void seraph_proof_blob_print(const Seraph_Proof_Blob* blob) {
    if (!blob || !blob->header) {
        fprintf(stderr, "Proof Blob: (null or invalid)\n");
        return;
    }

    Seraph_Proof_Blob_Stats stats;
    seraph_proof_blob_stats(blob, &stats);

    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "SERAPH PROOF BLOB\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "Version:        %u.%u.%u\n",
            (blob->header->version >> 16) & 0xFF,
            (blob->header->version >> 8) & 0xFF,
            blob->header->version & 0xFF);
    fprintf(stderr, "Verified:       %s\n", blob->verified ? "YES" : "NO");
    fprintf(stderr, "Module Hash:    0x%016llx\n",
            (unsigned long long)blob->header->module_hash);
    fprintf(stderr, "Size:           %zu bytes\n", blob->blob_size);
    fprintf(stderr, "Buckets:        %u\n", blob->header->bucket_count);
    fprintf(stderr, "--------------------------------------------------------------------------------\n");
    fprintf(stderr, "PROOFS:\n");
    fprintf(stderr, "  Total:        %llu\n", (unsigned long long)stats.total_proofs);
    fprintf(stderr, "  Proven:       %llu (%.1f%% - ZERO OVERHEAD)\n",
            (unsigned long long)stats.proven_count,
            stats.total_proofs ? (100.0 * stats.proven_count / stats.total_proofs) : 0.0);
    fprintf(stderr, "  Runtime:      %llu (%.1f%%)\n",
            (unsigned long long)stats.runtime_count,
            stats.total_proofs ? (100.0 * stats.runtime_count / stats.total_proofs) : 0.0);
    fprintf(stderr, "  Assumed:      %llu\n", (unsigned long long)stats.assumed_count);
    fprintf(stderr, "  Failed:       %llu\n", (unsigned long long)stats.failed_count);
    fprintf(stderr, "--------------------------------------------------------------------------------\n");
    fprintf(stderr, "QUERY STATISTICS:\n");
    fprintf(stderr, "  Queries:      %llu\n", (unsigned long long)stats.queries);
    fprintf(stderr, "  Hits:         %llu (%.1f%%)\n",
            (unsigned long long)stats.hits,
            stats.queries ? (100.0 * stats.hits / stats.queries) : 0.0);
    fprintf(stderr, "  Misses:       %llu\n", (unsigned long long)stats.misses);
    fprintf(stderr, "================================================================================\n");
}
