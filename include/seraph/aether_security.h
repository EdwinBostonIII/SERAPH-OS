/**
 * @file aether_security.h
 * @brief Aether Network Security Hardening
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * Security features for the Aether Distributed Shared Memory protocol:
 *   1. HMAC-SHA256 packet authentication
 *   2. Replay attack prevention (sliding window)
 *   3. Rate limiting (token bucket)
 *   4. Per-node permission masks
 *   5. Security event logging
 *
 * DESIGN CONSTRAINTS (Kernel-Safe):
 *   - No floating point arithmetic
 *   - No dynamic allocation in hot paths
 *   - Limited stack usage (< 256 bytes for crypto)
 *   - Constant-time comparisons for HMAC verification
 *   - All time in integer ticks, not floating point seconds
 */

#ifndef SERAPH_AETHER_SECURITY_H
#define SERAPH_AETHER_SECURITY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "seraph/vbit.h"
#include "seraph/aether.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Security Configuration
 *============================================================================*/

/** Enable/disable security features at compile time */
#ifndef AETHER_SECURITY_ENABLE
#define AETHER_SECURITY_ENABLE 1
#endif

/** HMAC key size in bytes (256-bit key) */
#define AETHER_HMAC_KEY_SIZE     32

/** HMAC digest size (SHA-256 output) */
#define AETHER_HMAC_DIGEST_SIZE  32

/** Maximum nodes to track for security state */
#define AETHER_SECURITY_MAX_NODES SERAPH_AETHER_MAX_NODES

/** Replay window size (packets) - must be power of 2 */
#define AETHER_REPLAY_WINDOW_SIZE 64

/** Default rate limit: packets per second per node */
#define AETHER_DEFAULT_RATE_LIMIT_PPS    1000

/** Default bucket size (max burst) */
#define AETHER_DEFAULT_RATE_BUCKET_SIZE  100

/** Security log buffer size (circular buffer entries) */
#define AETHER_SECURITY_LOG_SIZE  256

/*============================================================================
 * SHA-256 Implementation (Kernel-Safe)
 *============================================================================*/

/**
 * @brief SHA-256 context structure
 *
 * No dynamic allocation, fixed size for kernel safety.
 * Stack usage: 112 bytes
 */
typedef struct {
    uint32_t state[8];     /**< Hash state (H0-H7) */
    uint64_t count;        /**< Total bits processed */
    uint8_t  buffer[64];   /**< Message block buffer */
} Aether_SHA256_Context;

/**
 * @brief Initialize SHA-256 context
 */
void aether_sha256_init(Aether_SHA256_Context* ctx);

/**
 * @brief Update SHA-256 with data
 */
void aether_sha256_update(Aether_SHA256_Context* ctx,
                          const void* data, size_t len);

/**
 * @brief Finalize SHA-256 and output digest
 *
 * @param ctx Context (will be zeroed after)
 * @param digest Output buffer (32 bytes)
 */
void aether_sha256_final(Aether_SHA256_Context* ctx, uint8_t digest[32]);

/**
 * @brief One-shot SHA-256 hash
 */
void aether_sha256(const void* data, size_t len, uint8_t digest[32]);

/*============================================================================
 * HMAC-SHA256 Implementation
 *============================================================================*/

/**
 * @brief HMAC-SHA256 context
 *
 * Uses SHA-256 internally, no additional heap allocation.
 */
typedef struct {
    Aether_SHA256_Context sha_ctx;
    uint8_t key_pad[64];  /**< Padded key for outer hash */
} Aether_HMAC_Context;

/**
 * @brief Initialize HMAC-SHA256 with key
 *
 * @param ctx HMAC context
 * @param key Key data
 * @param key_len Key length (will be hashed if > 64 bytes)
 */
void aether_hmac_sha256_init(Aether_HMAC_Context* ctx,
                              const uint8_t* key, size_t key_len);

/**
 * @brief Update HMAC with data
 */
void aether_hmac_sha256_update(Aether_HMAC_Context* ctx,
                                const void* data, size_t len);

/**
 * @brief Finalize HMAC and output MAC
 *
 * @param ctx Context (will be zeroed after)
 * @param mac Output buffer (32 bytes)
 */
void aether_hmac_sha256_final(Aether_HMAC_Context* ctx, uint8_t mac[32]);

/**
 * @brief One-shot HMAC-SHA256
 */
void aether_hmac_sha256(const uint8_t* key, size_t key_len,
                         const void* data, size_t data_len,
                         uint8_t mac[32]);

/**
 * @brief Constant-time HMAC comparison
 *
 * CRITICAL: Prevents timing attacks by always comparing all bytes.
 *
 * @param a First MAC
 * @param b Second MAC
 * @return true if equal, false otherwise
 */
bool aether_hmac_verify(const uint8_t a[32], const uint8_t b[32]);

/*============================================================================
 * Replay Attack Prevention
 *============================================================================*/

/**
 * @brief Replay detection state for one node
 *
 * Uses sliding window to allow some out-of-order delivery.
 */
typedef struct {
    uint32_t last_seq;           /**< Highest sequence number seen */
    uint64_t window_bitmap;      /**< Bitmap for window (64 packets) */
    bool     initialized;        /**< Has received any packets? */
} Aether_Replay_State;

/**
 * @brief Replay detection result
 */
typedef enum {
    AETHER_REPLAY_OK = 0,        /**< Packet is new, accepted */
    AETHER_REPLAY_DUPLICATE,     /**< Packet is a replay, reject */
    AETHER_REPLAY_TOO_OLD,       /**< Packet seq is too old for window */
} Aether_Replay_Result;

/**
 * @brief Check if packet is a replay
 *
 * @param state Per-node replay state
 * @param seq_num Packet sequence number
 * @return Replay check result
 */
Aether_Replay_Result aether_replay_check(Aether_Replay_State* state,
                                          uint32_t seq_num);

/**
 * @brief Accept packet and update replay state
 *
 * Call after packet passes all other checks.
 *
 * @param state Per-node replay state
 * @param seq_num Packet sequence number
 */
void aether_replay_accept(Aether_Replay_State* state, uint32_t seq_num);

/**
 * @brief Reset replay state for a node
 */
void aether_replay_reset(Aether_Replay_State* state);

/*============================================================================
 * Rate Limiting (Token Bucket)
 *============================================================================*/

/**
 * @brief Rate limiter configuration
 */
typedef struct {
    uint32_t tokens_per_second;  /**< Refill rate */
    uint32_t bucket_size;        /**< Maximum tokens (burst size) */
    uint32_t ticks_per_second;   /**< System ticks per second */
} Aether_Rate_Config;

/**
 * @brief Rate limiter state for one node
 */
typedef struct {
    uint32_t tokens;             /**< Current token count (fixed-point: 16.16) */
    uint64_t last_refill_tick;   /**< Last refill timestamp (ticks) */
    uint32_t dropped_packets;    /**< Packets dropped due to rate limit */
    bool     throttled;          /**< Currently being rate-limited? */
} Aether_Rate_State;

/**
 * @brief Rate limit result
 */
typedef enum {
    AETHER_RATE_OK = 0,          /**< Packet allowed */
    AETHER_RATE_LIMITED,         /**< Packet dropped, rate exceeded */
    AETHER_RATE_BACKOFF,         /**< Soft limit, suggest backoff */
} Aether_Rate_Result;

/**
 * @brief Initialize rate limiter configuration
 */
void aether_rate_config_init(Aether_Rate_Config* config,
                              uint32_t pps, uint32_t burst,
                              uint32_t ticks_per_sec);

/**
 * @brief Check if packet should be rate-limited
 *
 * @param state Per-node rate state
 * @param config Rate configuration
 * @param current_tick Current system tick
 * @return Rate limit result
 */
Aether_Rate_Result aether_rate_check(Aether_Rate_State* state,
                                      const Aether_Rate_Config* config,
                                      uint64_t current_tick);

/**
 * @brief Consume a token (call after rate_check returns OK)
 */
void aether_rate_consume(Aether_Rate_State* state);

/**
 * @brief Reset rate limiter state
 */
void aether_rate_reset(Aether_Rate_State* state);

/**
 * @brief Get rate limit statistics for a node
 */
uint32_t aether_rate_get_dropped(const Aether_Rate_State* state);

/*============================================================================
 * Per-Node Permission Masks
 *============================================================================*/

/**
 * @brief Permission flags for node access
 */
typedef enum {
    AETHER_NODE_PERM_NONE       = 0x00,
    AETHER_NODE_PERM_READ       = 0x01,  /**< Can read pages */
    AETHER_NODE_PERM_WRITE      = 0x02,  /**< Can write pages */
    AETHER_NODE_PERM_INVALIDATE = 0x04,  /**< Can send invalidations */
    AETHER_NODE_PERM_REVOKE     = 0x08,  /**< Can send revocations */
    AETHER_NODE_PERM_GENERATION = 0x10,  /**< Can query generations */
    AETHER_NODE_PERM_ALL        = 0x1F,
} Aether_Node_Perm;

/**
 * @brief Per-node permission entry
 */
typedef struct {
    uint16_t node_id;            /**< Remote node ID */
    uint8_t  permissions;        /**< Allowed operations */
    bool     authenticated;      /**< Has valid shared key? */
    uint8_t  key[AETHER_HMAC_KEY_SIZE];  /**< Pre-shared key */
} Aether_Node_Permission;

/**
 * @brief Check if node has permission for operation
 *
 * @param perm Permission entry for node
 * @param required Required permission flags
 * @return true if permitted
 */
static inline bool aether_node_has_perm(const Aether_Node_Permission* perm,
                                         uint8_t required) {
    if (perm == NULL) return false;
    return (perm->permissions & required) == required;
}

/*============================================================================
 * Security Event Logging
 *============================================================================*/

/**
 * @brief Security event types
 */
typedef enum {
    AETHER_SEC_EVENT_NONE = 0,
    AETHER_SEC_EVENT_INVALID_MAGIC,      /**< Bad magic number */
    AETHER_SEC_EVENT_INVALID_VERSION,    /**< Unsupported version */
    AETHER_SEC_EVENT_INVALID_TYPE,       /**< Unknown message type */
    AETHER_SEC_EVENT_BOUNDS_VIOLATION,   /**< Frame length mismatch */
    AETHER_SEC_EVENT_HMAC_FAILURE,       /**< HMAC verification failed */
    AETHER_SEC_EVENT_REPLAY_ATTACK,      /**< Duplicate/old sequence */
    AETHER_SEC_EVENT_RATE_LIMITED,       /**< Rate limit exceeded */
    AETHER_SEC_EVENT_PERMISSION_DENIED,  /**< Operation not permitted */
    AETHER_SEC_EVENT_GENERATION_STALE,   /**< Capability generation mismatch */
    AETHER_SEC_EVENT_OFFSET_INVALID,     /**< Invalid memory offset */
    AETHER_SEC_EVENT_NODE_UNKNOWN,       /**< Unknown source node */
} Aether_Security_Event_Type;

/**
 * @brief Security event log entry
 */
typedef struct {
    uint64_t timestamp;          /**< Event timestamp (ticks) */
    uint16_t src_node;           /**< Source node of packet */
    uint16_t event_type;         /**< Aether_Security_Event_Type */
    uint32_t seq_num;            /**< Packet sequence number */
    uint64_t offset;             /**< Memory offset (if applicable) */
    uint32_t details;            /**< Event-specific details */
} Aether_Security_Event;

/**
 * @brief Security event log (circular buffer)
 */
typedef struct {
    Aether_Security_Event events[AETHER_SECURITY_LOG_SIZE];
    uint32_t head;               /**< Next write position */
    uint32_t count;              /**< Total events (may wrap) */
    uint64_t dropped;            /**< Events dropped due to full buffer */
} Aether_Security_Log;

/**
 * @brief Initialize security log
 */
void aether_security_log_init(Aether_Security_Log* log);

/**
 * @brief Log a security event
 */
void aether_security_log_event(Aether_Security_Log* log,
                                uint64_t timestamp,
                                uint16_t src_node,
                                Aether_Security_Event_Type type,
                                uint32_t seq_num,
                                uint64_t offset,
                                uint32_t details);

/**
 * @brief Get recent security events
 *
 * @param log Security log
 * @param events Output buffer
 * @param max_events Buffer size
 * @return Number of events copied
 */
uint32_t aether_security_log_get(const Aether_Security_Log* log,
                                  Aether_Security_Event* events,
                                  uint32_t max_events);

/**
 * @brief Get event count by type
 */
uint32_t aether_security_log_count_type(const Aether_Security_Log* log,
                                         Aether_Security_Event_Type type);

/**
 * @brief Clear security log
 */
void aether_security_log_clear(Aether_Security_Log* log);

/*============================================================================
 * Combined Security State
 *============================================================================*/

/**
 * @brief Security configuration flags
 */
typedef enum {
    AETHER_SEC_FLAG_NONE              = 0x00,
    AETHER_SEC_FLAG_REQUIRE_HMAC      = 0x01,  /**< Require HMAC on all packets */
    AETHER_SEC_FLAG_ENFORCE_REPLAY    = 0x02,  /**< Enforce replay detection */
    AETHER_SEC_FLAG_RATE_LIMIT        = 0x04,  /**< Enable rate limiting */
    AETHER_SEC_FLAG_CHECK_PERMISSIONS = 0x08,  /**< Check per-node permissions */
    AETHER_SEC_FLAG_LOG_ALL           = 0x10,  /**< Log all security events */
    AETHER_SEC_FLAG_STRICT            = 0x0F,  /**< All enforcement enabled */
} Aether_Security_Flags;

/**
 * @brief Complete security state for Aether NIC
 */
typedef struct {
    /* Configuration */
    uint32_t flags;              /**< Aether_Security_Flags */
    Aether_Rate_Config rate_config;

    /* Per-node state (indexed by node_id) */
    Aether_Replay_State replay[AETHER_SECURITY_MAX_NODES];
    Aether_Rate_State   rate[AETHER_SECURITY_MAX_NODES];
    Aether_Node_Permission permissions[AETHER_SECURITY_MAX_NODES];

    /* Logging */
    Aether_Security_Log log;

    /* Statistics */
    uint64_t packets_validated;
    uint64_t packets_rejected;
    uint64_t hmac_failures;
    uint64_t replay_attacks;
    uint64_t rate_limited;
    uint64_t permission_denied;
    uint64_t generation_failures;

    bool initialized;
} Aether_Security_State;

/**
 * @brief Initialize security state with default configuration
 */
Seraph_Vbit aether_security_init(Aether_Security_State* state);

/**
 * @brief Initialize security state with custom flags
 */
Seraph_Vbit aether_security_init_flags(Aether_Security_State* state,
                                        uint32_t flags);

/**
 * @brief Destroy security state
 */
void aether_security_destroy(Aether_Security_State* state);

/**
 * @brief Set pre-shared key for a node
 *
 * @param state Security state
 * @param node_id Remote node ID
 * @param key Pre-shared key (AETHER_HMAC_KEY_SIZE bytes)
 * @param permissions Permission mask for this node
 * @return TRUE on success
 */
Seraph_Vbit aether_security_set_node_key(Aether_Security_State* state,
                                          uint16_t node_id,
                                          const uint8_t* key,
                                          uint8_t permissions);

/**
 * @brief Get permission entry for a node
 */
const Aether_Node_Permission* aether_security_get_node_perm(
    const Aether_Security_State* state,
    uint16_t node_id);

/**
 * @brief Get security statistics
 */
void aether_security_get_stats(const Aether_Security_State* state,
                                uint64_t* validated,
                                uint64_t* rejected,
                                uint64_t* hmac_fail,
                                uint64_t* replay,
                                uint64_t* rate_limit,
                                uint64_t* perm_denied);

/*============================================================================
 * Packet Validation API
 *============================================================================*/

/**
 * @brief Packet validation result
 */
typedef enum {
    AETHER_VALIDATE_OK = 0,
    AETHER_VALIDATE_MALFORMED,       /**< Structural problems */
    AETHER_VALIDATE_HMAC_FAIL,       /**< Authentication failed */
    AETHER_VALIDATE_REPLAY,          /**< Replay attack detected */
    AETHER_VALIDATE_RATE_LIMITED,    /**< Rate limit exceeded */
    AETHER_VALIDATE_PERMISSION,      /**< Operation not permitted */
    AETHER_VALIDATE_GENERATION,      /**< Stale capability generation */
} Aether_Validate_Result;

/**
 * @brief Validate an incoming Aether frame
 *
 * Performs all security checks in the correct order:
 * 1. Structural validation (bounds, magic, version, type)
 * 2. Rate limiting (before crypto to prevent DoS)
 * 3. HMAC verification
 * 4. Replay detection
 * 5. Permission check
 *
 * @param state Security state
 * @param frame_data Raw frame data
 * @param frame_len Frame length
 * @param current_tick Current system tick
 * @param src_node_out Output: source node ID
 * @return Validation result
 */
Aether_Validate_Result aether_security_validate_frame(
    Aether_Security_State* state,
    const void* frame_data,
    size_t frame_len,
    uint64_t current_tick,
    uint16_t* src_node_out);

/**
 * @brief Accept a validated packet (update replay state)
 *
 * Call after packet passes validation and before processing.
 */
void aether_security_accept_packet(Aether_Security_State* state,
                                    uint16_t src_node,
                                    uint32_t seq_num);

/**
 * @brief Compute HMAC for outgoing frame
 *
 * @param state Security state
 * @param dst_node Destination node
 * @param frame_data Frame data (excluding HMAC trailer)
 * @param frame_len Frame length
 * @param hmac_out Output HMAC (32 bytes)
 * @return TRUE if HMAC computed, FALSE if no key for node
 */
Seraph_Vbit aether_security_compute_hmac(const Aether_Security_State* state,
                                          uint16_t dst_node,
                                          const void* frame_data,
                                          size_t frame_len,
                                          uint8_t hmac_out[32]);

/*============================================================================
 * Generation Validation
 *============================================================================*/

/**
 * @brief Validate capability generation for page access
 *
 * @param aether Aether DSM state
 * @param offset Page offset being accessed
 * @param claimed_gen Generation claimed in request
 * @param node_id Requesting node
 * @return TRUE if generation is current, FALSE/VOID otherwise
 */
Seraph_Vbit aether_security_check_generation(Seraph_Aether* aether,
                                              uint64_t offset,
                                              uint64_t claimed_gen,
                                              uint16_t node_id);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_AETHER_SECURITY_H */
