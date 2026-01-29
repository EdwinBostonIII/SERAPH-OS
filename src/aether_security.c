/**
 * @file aether_security.c
 * @brief Aether Network Security Implementation
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * Implements security hardening for the Aether DSM protocol:
 *   - SHA-256 hash (NIST FIPS 180-4)
 *   - HMAC-SHA256 authentication (RFC 2104)
 *   - Constant-time comparison
 *   - Token bucket rate limiting
 *   - Sliding window replay detection
 *   - Security event logging
 *
 * KERNEL SAFETY:
 *   - No floating point
 *   - No dynamic allocation
 *   - Limited stack usage
 *   - Constant-time crypto operations
 */

#include "seraph/aether_security.h"
#include "seraph/aether.h"
#include "seraph/drivers/nic.h"
#include <string.h>

/*============================================================================
 * SHA-256 Constants (NIST FIPS 180-4)
 *============================================================================*/

/** SHA-256 initial hash values */
static const uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/** SHA-256 round constants */
static const uint32_t SHA256_K[64] = {
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
 * SHA-256 Helper Macros
 *============================================================================*/

#define ROTR32(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)        (ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define EP1(x)        (ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define SIG0(x)       (ROTR32(x, 7) ^ ROTR32(x, 18) ^ ((x) >> 3))
#define SIG1(x)       (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

/*============================================================================
 * SHA-256 Implementation
 *============================================================================*/

/**
 * @brief Process a 64-byte block
 */
static void sha256_transform(Aether_SHA256_Context* ctx, const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }

    for (i = 16; i < 64; i++) {
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];
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
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + SHA256_K[i] + W[i];
        t2 = EP0(a) + MAJ(a, b, c);
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

void aether_sha256_init(Aether_SHA256_Context* ctx) {
    if (ctx == NULL) return;

    memcpy(ctx->state, SHA256_H0, sizeof(SHA256_H0));
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void aether_sha256_update(Aether_SHA256_Context* ctx,
                          const void* data, size_t len) {
    if (ctx == NULL || data == NULL || len == 0) return;

    const uint8_t* bytes = (const uint8_t*)data;
    size_t buf_idx = (size_t)((ctx->count / 8) % 64);

    ctx->count += (uint64_t)len * 8;

    /* Fill buffer and process */
    while (len > 0) {
        size_t to_copy = 64 - buf_idx;
        if (to_copy > len) to_copy = len;

        memcpy(ctx->buffer + buf_idx, bytes, to_copy);
        buf_idx += to_copy;
        bytes += to_copy;
        len -= to_copy;

        if (buf_idx == 64) {
            sha256_transform(ctx, ctx->buffer);
            buf_idx = 0;
        }
    }
}

void aether_sha256_final(Aether_SHA256_Context* ctx, uint8_t digest[32]) {
    if (ctx == NULL || digest == NULL) return;

    size_t buf_idx = (size_t)((ctx->count / 8) % 64);

    /* Padding */
    ctx->buffer[buf_idx++] = 0x80;

    if (buf_idx > 56) {
        /* Need two blocks */
        memset(ctx->buffer + buf_idx, 0, 64 - buf_idx);
        sha256_transform(ctx, ctx->buffer);
        buf_idx = 0;
    }

    memset(ctx->buffer + buf_idx, 0, 56 - buf_idx);

    /* Append length (big-endian) */
    ctx->buffer[56] = (uint8_t)(ctx->count >> 56);
    ctx->buffer[57] = (uint8_t)(ctx->count >> 48);
    ctx->buffer[58] = (uint8_t)(ctx->count >> 40);
    ctx->buffer[59] = (uint8_t)(ctx->count >> 32);
    ctx->buffer[60] = (uint8_t)(ctx->count >> 24);
    ctx->buffer[61] = (uint8_t)(ctx->count >> 16);
    ctx->buffer[62] = (uint8_t)(ctx->count >> 8);
    ctx->buffer[63] = (uint8_t)(ctx->count);

    sha256_transform(ctx, ctx->buffer);

    /* Output digest (big-endian) */
    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+3] = (uint8_t)(ctx->state[i]);
    }

    /* Clear sensitive data */
    memset(ctx, 0, sizeof(*ctx));
}

void aether_sha256(const void* data, size_t len, uint8_t digest[32]) {
    Aether_SHA256_Context ctx;
    aether_sha256_init(&ctx);
    aether_sha256_update(&ctx, data, len);
    aether_sha256_final(&ctx, digest);
}

/*============================================================================
 * HMAC-SHA256 Implementation (RFC 2104)
 *============================================================================*/

#define HMAC_BLOCK_SIZE 64
#define HMAC_IPAD 0x36
#define HMAC_OPAD 0x5c

void aether_hmac_sha256_init(Aether_HMAC_Context* ctx,
                              const uint8_t* key, size_t key_len) {
    if (ctx == NULL || key == NULL) return;

    uint8_t key_block[HMAC_BLOCK_SIZE];
    memset(key_block, 0, sizeof(key_block));

    /* Hash key if too long */
    if (key_len > HMAC_BLOCK_SIZE) {
        aether_sha256(key, key_len, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    /* Prepare opad key (store for final) */
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) {
        ctx->key_pad[i] = key_block[i] ^ HMAC_OPAD;
    }

    /* Initialize inner hash with ipad */
    aether_sha256_init(&ctx->sha_ctx);
    uint8_t ipad_key[HMAC_BLOCK_SIZE];
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) {
        ipad_key[i] = key_block[i] ^ HMAC_IPAD;
    }
    aether_sha256_update(&ctx->sha_ctx, ipad_key, HMAC_BLOCK_SIZE);

    /* Clear sensitive data */
    memset(key_block, 0, sizeof(key_block));
    memset(ipad_key, 0, sizeof(ipad_key));
}

void aether_hmac_sha256_update(Aether_HMAC_Context* ctx,
                                const void* data, size_t len) {
    if (ctx == NULL) return;
    aether_sha256_update(&ctx->sha_ctx, data, len);
}

void aether_hmac_sha256_final(Aether_HMAC_Context* ctx, uint8_t mac[32]) {
    if (ctx == NULL || mac == NULL) return;

    /* Finalize inner hash */
    uint8_t inner_hash[32];
    aether_sha256_final(&ctx->sha_ctx, inner_hash);

    /* Outer hash: H(opad || inner_hash) */
    aether_sha256_init(&ctx->sha_ctx);
    aether_sha256_update(&ctx->sha_ctx, ctx->key_pad, HMAC_BLOCK_SIZE);
    aether_sha256_update(&ctx->sha_ctx, inner_hash, 32);
    aether_sha256_final(&ctx->sha_ctx, mac);

    /* Clear sensitive data */
    memset(inner_hash, 0, sizeof(inner_hash));
    memset(ctx->key_pad, 0, sizeof(ctx->key_pad));
}

void aether_hmac_sha256(const uint8_t* key, size_t key_len,
                         const void* data, size_t data_len,
                         uint8_t mac[32]) {
    Aether_HMAC_Context ctx;
    aether_hmac_sha256_init(&ctx, key, key_len);
    aether_hmac_sha256_update(&ctx, data, data_len);
    aether_hmac_sha256_final(&ctx, mac);
}

/**
 * @brief Constant-time byte comparison
 *
 * Prevents timing attacks by always comparing all bytes
 * regardless of where differences are found.
 */
bool aether_hmac_verify(const uint8_t a[32], const uint8_t b[32]) {
    if (a == NULL || b == NULL) return false;

    volatile uint8_t diff = 0;

    /* XOR all bytes - difference will set bits */
    for (int i = 0; i < 32; i++) {
        diff |= a[i] ^ b[i];
    }

    /* Convert to bool without branching on diff value */
    return diff == 0;
}

/*============================================================================
 * Replay Attack Prevention
 *============================================================================*/

void aether_replay_reset(Aether_Replay_State* state) {
    if (state == NULL) return;
    state->last_seq = 0;
    state->window_bitmap = 0;
    state->initialized = false;
}

Aether_Replay_Result aether_replay_check(Aether_Replay_State* state,
                                          uint32_t seq_num) {
    if (state == NULL) return AETHER_REPLAY_DUPLICATE;

    /* First packet initializes state */
    if (!state->initialized) {
        return AETHER_REPLAY_OK;
    }

    /* Packet newer than window? Always OK (will advance window) */
    if (seq_num > state->last_seq) {
        return AETHER_REPLAY_OK;
    }

    /* How far back in window? */
    uint32_t diff = state->last_seq - seq_num;

    /* Too old for window? */
    if (diff >= AETHER_REPLAY_WINDOW_SIZE) {
        return AETHER_REPLAY_TOO_OLD;
    }

    /* Check bitmap for this position */
    uint64_t bit = (uint64_t)1 << diff;
    if (state->window_bitmap & bit) {
        return AETHER_REPLAY_DUPLICATE;
    }

    return AETHER_REPLAY_OK;
}

void aether_replay_accept(Aether_Replay_State* state, uint32_t seq_num) {
    if (state == NULL) return;

    if (!state->initialized) {
        /* First packet */
        state->last_seq = seq_num;
        state->window_bitmap = 1;  /* Current packet is bit 0 */
        state->initialized = true;
        return;
    }

    if (seq_num > state->last_seq) {
        /* Advance window */
        uint32_t shift = seq_num - state->last_seq;
        if (shift >= AETHER_REPLAY_WINDOW_SIZE) {
            /* Complete window reset */
            state->window_bitmap = 1;
        } else {
            /* Shift bitmap and set new bit */
            state->window_bitmap <<= shift;
            state->window_bitmap |= 1;
        }
        state->last_seq = seq_num;
    } else {
        /* Mark packet in window */
        uint32_t diff = state->last_seq - seq_num;
        if (diff < AETHER_REPLAY_WINDOW_SIZE) {
            state->window_bitmap |= ((uint64_t)1 << diff);
        }
    }
}

/*============================================================================
 * Rate Limiting (Token Bucket)
 *============================================================================*/

/* Fixed-point shift (16.16 format) */
#define RATE_FP_SHIFT 16
#define RATE_FP_ONE   (1 << RATE_FP_SHIFT)

void aether_rate_config_init(Aether_Rate_Config* config,
                              uint32_t pps, uint32_t burst,
                              uint32_t ticks_per_sec) {
    if (config == NULL) return;

    config->tokens_per_second = pps;
    config->bucket_size = burst;
    config->ticks_per_second = ticks_per_sec > 0 ? ticks_per_sec : 1000;
}

void aether_rate_reset(Aether_Rate_State* state) {
    if (state == NULL) return;
    state->tokens = 0;
    state->last_refill_tick = 0;
    state->dropped_packets = 0;
    state->throttled = false;
}

Aether_Rate_Result aether_rate_check(Aether_Rate_State* state,
                                      const Aether_Rate_Config* config,
                                      uint64_t current_tick) {
    if (state == NULL || config == NULL) return AETHER_RATE_LIMITED;

    /* Initialize on first packet */
    if (state->last_refill_tick == 0) {
        state->tokens = config->bucket_size << RATE_FP_SHIFT;
        state->last_refill_tick = current_tick;
    }

    /* Calculate elapsed time and refill tokens */
    uint64_t elapsed = current_tick - state->last_refill_tick;
    if (elapsed > 0) {
        /* Tokens to add = elapsed_ticks * tokens_per_second / ticks_per_second */
        /* Use 64-bit to avoid overflow */
        uint64_t tokens_to_add = (elapsed * config->tokens_per_second *
                                   RATE_FP_ONE) / config->ticks_per_second;

        uint32_t max_tokens = config->bucket_size << RATE_FP_SHIFT;
        uint64_t new_tokens = state->tokens + tokens_to_add;

        if (new_tokens > max_tokens) {
            new_tokens = max_tokens;
        }

        state->tokens = (uint32_t)new_tokens;
        state->last_refill_tick = current_tick;
    }

    /* Check if we have a token */
    if (state->tokens >= RATE_FP_ONE) {
        state->throttled = false;
        return AETHER_RATE_OK;
    }

    /* Rate limited */
    state->dropped_packets++;
    state->throttled = true;

    /* Suggest backoff if partially empty */
    if (state->tokens > 0) {
        return AETHER_RATE_BACKOFF;
    }

    return AETHER_RATE_LIMITED;
}

void aether_rate_consume(Aether_Rate_State* state) {
    if (state == NULL) return;

    if (state->tokens >= RATE_FP_ONE) {
        state->tokens -= RATE_FP_ONE;
    }
}

uint32_t aether_rate_get_dropped(const Aether_Rate_State* state) {
    if (state == NULL) return 0;
    return state->dropped_packets;
}

/*============================================================================
 * Security Event Logging
 *============================================================================*/

void aether_security_log_init(Aether_Security_Log* log) {
    if (log == NULL) return;
    memset(log, 0, sizeof(*log));
}

void aether_security_log_event(Aether_Security_Log* log,
                                uint64_t timestamp,
                                uint16_t src_node,
                                Aether_Security_Event_Type type,
                                uint32_t seq_num,
                                uint64_t offset,
                                uint32_t details) {
    if (log == NULL) return;

    Aether_Security_Event* event = &log->events[log->head];
    event->timestamp = timestamp;
    event->src_node = src_node;
    event->event_type = (uint16_t)type;
    event->seq_num = seq_num;
    event->offset = offset;
    event->details = details;

    log->head = (log->head + 1) % AETHER_SECURITY_LOG_SIZE;
    log->count++;
}

uint32_t aether_security_log_get(const Aether_Security_Log* log,
                                  Aether_Security_Event* events,
                                  uint32_t max_events) {
    if (log == NULL || events == NULL || max_events == 0) return 0;

    uint32_t available = log->count < AETHER_SECURITY_LOG_SIZE ?
                         log->count : AETHER_SECURITY_LOG_SIZE;
    uint32_t to_copy = available < max_events ? available : max_events;

    /* Copy most recent events */
    for (uint32_t i = 0; i < to_copy; i++) {
        uint32_t idx = (log->head + AETHER_SECURITY_LOG_SIZE - 1 - i) %
                       AETHER_SECURITY_LOG_SIZE;
        events[i] = log->events[idx];
    }

    return to_copy;
}

uint32_t aether_security_log_count_type(const Aether_Security_Log* log,
                                         Aether_Security_Event_Type type) {
    if (log == NULL) return 0;

    uint32_t count = 0;
    uint32_t available = log->count < AETHER_SECURITY_LOG_SIZE ?
                         log->count : AETHER_SECURITY_LOG_SIZE;

    for (uint32_t i = 0; i < available; i++) {
        if (log->events[i].event_type == (uint16_t)type) {
            count++;
        }
    }

    return count;
}

void aether_security_log_clear(Aether_Security_Log* log) {
    if (log == NULL) return;
    log->head = 0;
    log->count = 0;
    log->dropped = 0;
}

/*============================================================================
 * Combined Security State
 *============================================================================*/

Seraph_Vbit aether_security_init(Aether_Security_State* state) {
    return aether_security_init_flags(state, AETHER_SEC_FLAG_STRICT);
}

Seraph_Vbit aether_security_init_flags(Aether_Security_State* state,
                                        uint32_t flags) {
    if (state == NULL) return SERAPH_VBIT_VOID;

    memset(state, 0, sizeof(*state));
    state->flags = flags;

    /* Initialize rate config with defaults */
    aether_rate_config_init(&state->rate_config,
                             AETHER_DEFAULT_RATE_LIMIT_PPS,
                             AETHER_DEFAULT_RATE_BUCKET_SIZE,
                             1000);  /* Assume 1000 ticks/sec */

    /* Initialize per-node state */
    for (uint32_t i = 0; i < AETHER_SECURITY_MAX_NODES; i++) {
        aether_replay_reset(&state->replay[i]);
        aether_rate_reset(&state->rate[i]);
        state->permissions[i].node_id = (uint16_t)i;
        state->permissions[i].permissions = AETHER_NODE_PERM_NONE;
        state->permissions[i].authenticated = false;
    }

    aether_security_log_init(&state->log);
    state->initialized = true;

    return SERAPH_VBIT_TRUE;
}

void aether_security_destroy(Aether_Security_State* state) {
    if (state == NULL) return;

    /* Clear sensitive key material */
    for (uint32_t i = 0; i < AETHER_SECURITY_MAX_NODES; i++) {
        memset(state->permissions[i].key, 0, AETHER_HMAC_KEY_SIZE);
    }

    memset(state, 0, sizeof(*state));
}

Seraph_Vbit aether_security_set_node_key(Aether_Security_State* state,
                                          uint16_t node_id,
                                          const uint8_t* key,
                                          uint8_t permissions) {
    if (state == NULL || key == NULL ||
        node_id >= AETHER_SECURITY_MAX_NODES) {
        return SERAPH_VBIT_VOID;
    }

    Aether_Node_Permission* perm = &state->permissions[node_id];
    perm->node_id = node_id;
    memcpy(perm->key, key, AETHER_HMAC_KEY_SIZE);
    perm->permissions = permissions;
    perm->authenticated = true;

    return SERAPH_VBIT_TRUE;
}

const Aether_Node_Permission* aether_security_get_node_perm(
    const Aether_Security_State* state,
    uint16_t node_id) {
    if (state == NULL || node_id >= AETHER_SECURITY_MAX_NODES) {
        return NULL;
    }
    return &state->permissions[node_id];
}

void aether_security_get_stats(const Aether_Security_State* state,
                                uint64_t* validated,
                                uint64_t* rejected,
                                uint64_t* hmac_fail,
                                uint64_t* replay,
                                uint64_t* rate_limit,
                                uint64_t* perm_denied) {
    if (state == NULL) return;

    if (validated) *validated = state->packets_validated;
    if (rejected) *rejected = state->packets_rejected;
    if (hmac_fail) *hmac_fail = state->hmac_failures;
    if (replay) *replay = state->replay_attacks;
    if (rate_limit) *rate_limit = state->rate_limited;
    if (perm_denied) *perm_denied = state->permission_denied;
}

/*============================================================================
 * Packet Validation
 *============================================================================*/

/* Forward declaration - Aether header structure from aether_nic.c */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t seq_num;
    uint16_t src_node;
    uint16_t dst_node;
    uint64_t offset;
    uint16_t flags;
    uint16_t data_len;
    uint64_t generation;
} Aether_Header_Internal;

/* Aether frame with Ethernet header */
typedef struct __attribute__((packed)) {
    Seraph_Ethernet_Header eth;
    Aether_Header_Internal aether;
    uint8_t payload[];
} Aether_Frame_Internal;

/** Aether magic number */
#define AETHER_MAGIC_VALUE 0x48544541

/** Aether protocol version */
#define AETHER_VERSION_VALUE 1

/** Maximum valid message type */
#define AETHER_MSG_TYPE_MAX 0x06

Aether_Validate_Result aether_security_validate_frame(
    Aether_Security_State* state,
    const void* frame_data,
    size_t frame_len,
    uint64_t current_tick,
    uint16_t* src_node_out) {

    if (state == NULL || frame_data == NULL || !state->initialized) {
        return AETHER_VALIDATE_MALFORMED;
    }

    /* ========================================
     * STEP 1: Structural Validation (BEFORE any other check)
     * ======================================== */

    /* Check minimum frame size */
    if (frame_len < sizeof(Aether_Frame_Internal)) {
        aether_security_log_event(&state->log, current_tick, 0xFFFF,
                                   AETHER_SEC_EVENT_BOUNDS_VIOLATION,
                                   0, 0, (uint32_t)frame_len);
        state->packets_rejected++;
        return AETHER_VALIDATE_MALFORMED;
    }

    const Aether_Frame_Internal* frame =
        (const Aether_Frame_Internal*)frame_data;

    /* Validate magic number */
    if (frame->aether.magic != AETHER_MAGIC_VALUE) {
        aether_security_log_event(&state->log, current_tick, 0xFFFF,
                                   AETHER_SEC_EVENT_INVALID_MAGIC,
                                   0, 0, frame->aether.magic);
        state->packets_rejected++;
        return AETHER_VALIDATE_MALFORMED;
    }

    /* Validate protocol version */
    if (frame->aether.version != AETHER_VERSION_VALUE) {
        aether_security_log_event(&state->log, current_tick, 0xFFFF,
                                   AETHER_SEC_EVENT_INVALID_VERSION,
                                   0, 0, frame->aether.version);
        state->packets_rejected++;
        return AETHER_VALIDATE_MALFORMED;
    }

    /* Validate message type range */
    if (frame->aether.type == 0 || frame->aether.type > AETHER_MSG_TYPE_MAX) {
        aether_security_log_event(&state->log, current_tick, 0xFFFF,
                                   AETHER_SEC_EVENT_INVALID_TYPE,
                                   frame->aether.seq_num, 0, frame->aether.type);
        state->packets_rejected++;
        return AETHER_VALIDATE_MALFORMED;
    }

    /* Validate source node ID */
    uint16_t src_node = frame->aether.src_node;
    if (src_node >= AETHER_SECURITY_MAX_NODES) {
        aether_security_log_event(&state->log, current_tick, src_node,
                                   AETHER_SEC_EVENT_NODE_UNKNOWN,
                                   frame->aether.seq_num, 0, src_node);
        state->packets_rejected++;
        return AETHER_VALIDATE_MALFORMED;
    }

    /* Validate claimed data length vs actual frame length */
    size_t header_size = sizeof(Aether_Frame_Internal);
    size_t claimed_total = header_size + frame->aether.data_len;

    /* Check for overflow and frame length mismatch */
    if (claimed_total < header_size ||  /* Overflow check */
        frame_len < claimed_total) {     /* Not enough data */
        aether_security_log_event(&state->log, current_tick, src_node,
                                   AETHER_SEC_EVENT_BOUNDS_VIOLATION,
                                   frame->aether.seq_num, 0,
                                   (uint32_t)frame->aether.data_len);
        state->packets_rejected++;
        return AETHER_VALIDATE_MALFORMED;
    }

    /* Validate offset is within reasonable bounds (48-bit max) */
    if (frame->aether.offset > SERAPH_AETHER_MAX_OFFSET) {
        aether_security_log_event(&state->log, current_tick, src_node,
                                   AETHER_SEC_EVENT_OFFSET_INVALID,
                                   frame->aether.seq_num, frame->aether.offset, 0);
        state->packets_rejected++;
        return AETHER_VALIDATE_MALFORMED;
    }

    /* ========================================
     * STEP 2: Rate Limiting (BEFORE crypto to prevent DoS)
     * ======================================== */

    if (state->flags & AETHER_SEC_FLAG_RATE_LIMIT) {
        Aether_Rate_Result rate_result = aether_rate_check(
            &state->rate[src_node],
            &state->rate_config,
            current_tick);

        if (rate_result == AETHER_RATE_LIMITED) {
            aether_security_log_event(&state->log, current_tick, src_node,
                                       AETHER_SEC_EVENT_RATE_LIMITED,
                                       frame->aether.seq_num, 0,
                                       state->rate[src_node].dropped_packets);
            state->rate_limited++;
            state->packets_rejected++;
            return AETHER_VALIDATE_RATE_LIMITED;
        }
    }

    /* ========================================
     * STEP 3: HMAC Verification (after rate limit)
     * ======================================== */

    if (state->flags & AETHER_SEC_FLAG_REQUIRE_HMAC) {
        const Aether_Node_Permission* perm = &state->permissions[src_node];

        if (!perm->authenticated) {
            aether_security_log_event(&state->log, current_tick, src_node,
                                       AETHER_SEC_EVENT_HMAC_FAILURE,
                                       frame->aether.seq_num, 0, 0);
            state->hmac_failures++;
            state->packets_rejected++;
            return AETHER_VALIDATE_HMAC_FAIL;
        }

        /* HMAC is appended after payload */
        size_t hmac_offset = claimed_total;
        if (frame_len < hmac_offset + AETHER_HMAC_DIGEST_SIZE) {
            aether_security_log_event(&state->log, current_tick, src_node,
                                       AETHER_SEC_EVENT_HMAC_FAILURE,
                                       frame->aether.seq_num, 0, 1);
            state->hmac_failures++;
            state->packets_rejected++;
            return AETHER_VALIDATE_HMAC_FAIL;
        }

        /* Compute expected HMAC */
        uint8_t expected_mac[32];
        aether_hmac_sha256(perm->key, AETHER_HMAC_KEY_SIZE,
                            frame_data, claimed_total,
                            expected_mac);

        /* Constant-time comparison */
        const uint8_t* received_mac = (const uint8_t*)frame_data + hmac_offset;
        if (!aether_hmac_verify(expected_mac, received_mac)) {
            aether_security_log_event(&state->log, current_tick, src_node,
                                       AETHER_SEC_EVENT_HMAC_FAILURE,
                                       frame->aether.seq_num, 0, 2);
            state->hmac_failures++;
            state->packets_rejected++;
            return AETHER_VALIDATE_HMAC_FAIL;
        }
    }

    /* ========================================
     * STEP 4: Replay Detection (after HMAC to ensure authenticity)
     * ======================================== */

    if (state->flags & AETHER_SEC_FLAG_ENFORCE_REPLAY) {
        Aether_Replay_Result replay_result = aether_replay_check(
            &state->replay[src_node],
            frame->aether.seq_num);

        if (replay_result != AETHER_REPLAY_OK) {
            aether_security_log_event(&state->log, current_tick, src_node,
                                       AETHER_SEC_EVENT_REPLAY_ATTACK,
                                       frame->aether.seq_num, 0,
                                       (uint32_t)replay_result);
            state->replay_attacks++;
            state->packets_rejected++;
            return AETHER_VALIDATE_REPLAY;
        }
    }

    /* ========================================
     * STEP 5: Permission Check
     * ======================================== */

    if (state->flags & AETHER_SEC_FLAG_CHECK_PERMISSIONS) {
        const Aether_Node_Permission* perm = &state->permissions[src_node];

        uint8_t required_perm = AETHER_NODE_PERM_NONE;
        switch (frame->aether.type) {
            case 0x01:  /* PAGE_REQUEST */
                required_perm = (frame->aether.flags & 0x01) ?
                                AETHER_NODE_PERM_WRITE : AETHER_NODE_PERM_READ;
                break;
            case 0x02:  /* PAGE_RESPONSE */
                required_perm = AETHER_NODE_PERM_READ;
                break;
            case 0x03:  /* INVALIDATE */
                required_perm = AETHER_NODE_PERM_INVALIDATE;
                break;
            case 0x04:  /* GENERATION */
                required_perm = AETHER_NODE_PERM_GENERATION;
                break;
            case 0x05:  /* REVOKE */
                required_perm = AETHER_NODE_PERM_REVOKE;
                break;
            case 0x06:  /* ACK */
                /* ACKs allowed from any authenticated node */
                required_perm = AETHER_NODE_PERM_NONE;
                break;
        }

        if (required_perm != AETHER_NODE_PERM_NONE &&
            !aether_node_has_perm(perm, required_perm)) {
            aether_security_log_event(&state->log, current_tick, src_node,
                                       AETHER_SEC_EVENT_PERMISSION_DENIED,
                                       frame->aether.seq_num, frame->aether.offset,
                                       required_perm);
            state->permission_denied++;
            state->packets_rejected++;
            return AETHER_VALIDATE_PERMISSION;
        }
    }

    /* ========================================
     * VALIDATION PASSED
     * ======================================== */

    state->packets_validated++;
    if (src_node_out) *src_node_out = src_node;

    /* Consume rate limit token */
    if (state->flags & AETHER_SEC_FLAG_RATE_LIMIT) {
        aether_rate_consume(&state->rate[src_node]);
    }

    return AETHER_VALIDATE_OK;
}

void aether_security_accept_packet(Aether_Security_State* state,
                                    uint16_t src_node,
                                    uint32_t seq_num) {
    if (state == NULL || src_node >= AETHER_SECURITY_MAX_NODES) return;

    if (state->flags & AETHER_SEC_FLAG_ENFORCE_REPLAY) {
        aether_replay_accept(&state->replay[src_node], seq_num);
    }
}

Seraph_Vbit aether_security_compute_hmac(const Aether_Security_State* state,
                                          uint16_t dst_node,
                                          const void* frame_data,
                                          size_t frame_len,
                                          uint8_t hmac_out[32]) {
    if (state == NULL || frame_data == NULL || hmac_out == NULL ||
        dst_node >= AETHER_SECURITY_MAX_NODES) {
        return SERAPH_VBIT_VOID;
    }

    const Aether_Node_Permission* perm = &state->permissions[dst_node];
    if (!perm->authenticated) {
        return SERAPH_VBIT_FALSE;
    }

    aether_hmac_sha256(perm->key, AETHER_HMAC_KEY_SIZE,
                        frame_data, frame_len,
                        hmac_out);

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Generation Validation
 *============================================================================*/

Seraph_Vbit aether_security_check_generation(Seraph_Aether* aether,
                                              uint64_t offset,
                                              uint64_t claimed_gen,
                                              uint16_t node_id) {
    if (aether == NULL) return SERAPH_VBIT_VOID;

    /* Use existing Aether generation check */
    uint64_t aether_addr = seraph_aether_make_addr(
        seraph_aether_get_local_node_id(aether),
        offset);

    return seraph_aether_check_generation(aether, aether_addr, claimed_gen);

    /* Note: node_id could be used for additional per-node access control */
    (void)node_id;
}
