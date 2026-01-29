/**
 * @file whisper.h
 * @brief MC12: Whisper - Capability-Based Zero-Copy IPC
 *
 * "A message is not 'data in transit.' A message is the DELEGATION OF AUTHORITY."
 *
 * Whisper is SERAPH's inter-process communication system. Unlike traditional
 * IPC that copies data between processes, Whisper transfers CAPABILITIES.
 * The data itself never moves - only the AUTHORITY to access it moves.
 *
 * TRANSFER MODES:
 * - GRANT: Permanent transfer ("I give you this forever")
 * - LEND: Temporary transfer with timeout ("Borrow this, give it back")
 * - COPY: Shared read-only access ("We both can read this")
 * - DERIVE: Narrowed capability ("You get a restricted version")
 *
 * ZERO-COPY GUARANTEE:
 * Traditional IPC copies data: User→Kernel→User (2 copies minimum).
 * Whisper transfers capabilities: Only 256 bytes (the message) ever move.
 * A 1GB buffer? Send a 256-byte message with capability to the buffer.
 * The buffer doesn't move. Only AUTHORITY moves.
 */

#ifndef SERAPH_WHISPER_H
#define SERAPH_WHISPER_H

#include "void.h"
#include "vbit.h"
#include "capability.h"
#include "chronon.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations and Constants (needed by VOID tracking)
 *============================================================================*/

/** Maximum capabilities per message (forward declaration for VOID tracking) */
#define SERAPH_WHISPER_MAX_CAPS 7

/** Forward-declared message type (actual enum defined below) */
typedef uint8_t Seraph_Whisper_Type_t;

/*============================================================================
 * VOID Causality Tracking for Whisper IPC
 *
 * When a VOID value propagates through a Whisper channel, we need to track:
 * 1. WHERE the VOID originated (sender endpoint, message ID)
 * 2. WHY it became VOID (channel closed, queue full, cap invalid, etc.)
 * 3. WHEN it happened (chronon timestamp)
 * 4. The CAUSAL CHAIN (predecessor VOID IDs for archaeology)
 *
 * This enables "Whisper Archaeology" - debugging IPC failures by excavating
 * the complete history of how VOIDs propagated through the system.
 *============================================================================*/

/**
 * @brief VOID causality context specific to Whisper operations
 *
 * This structure captures Whisper-specific metadata when a VOID occurs,
 * supplementing the generic Seraph_VoidContext with IPC details.
 */
typedef struct {
    /** Endpoint ID where the VOID originated */
    uint64_t origin_endpoint_id;

    /** Message ID that carried or caused the VOID */
    uint64_t origin_message_id;

    /** Channel ID (if applicable) */
    uint64_t channel_id;

    /** Chronon when the VOID was recorded */
    Seraph_Chronon void_chronon;

    /** Index of the VOID capability within the message (0-6), or 0xFF if N/A */
    uint8_t cap_index;

    /** Message type that was being processed (see Seraph_Whisper_Type enum) */
    Seraph_Whisper_Type_t msg_type;

    /** Reserved for alignment */
    uint8_t _reserved[6];
} Seraph_Whisper_VoidContext;

/**
 * @brief VOID propagation record for a Whisper message
 *
 * When a message carries VOID values (either in its metadata or capabilities),
 * this record tracks the causality chain for debugging.
 */
typedef struct {
    /** Is this propagation record active? */
    bool active;

    /** Number of VOID capabilities in the message */
    uint8_t void_cap_count;

    /** Bitmask of which capabilities are VOID (bits 0-6) */
    uint8_t void_cap_mask;

    /** Reserved for alignment */
    uint8_t _reserved;

    /** VOID IDs for each capability slot (0 if not VOID) */
    uint64_t cap_void_ids[SERAPH_WHISPER_MAX_CAPS];

    /** The predecessor VOID ID that caused this message to become VOID */
    uint64_t predecessor_void_id;

    /** When the VOID was first detected */
    Seraph_Chronon detection_chronon;
} Seraph_Whisper_VoidPropagation;

/*============================================================================
 * VOID Recording Macros for Whisper Operations
 *
 * These macros provide convenient VOID recording with automatic source
 * location capture and Whisper-specific context.
 *============================================================================*/

/**
 * @brief Record a Whisper-specific VOID with full context
 */
#define SERAPH_WHISPER_VOID_RECORD(reason, pred, ep_id, msg_id, msg_text) \
    seraph_void_record((reason), (pred), (ep_id), (msg_id), \
                       __FILE__, __func__, __LINE__, (msg_text))

/**
 * @brief Record and return VOID_VBIT for Whisper operations
 */
#define SERAPH_WHISPER_VOID_VBIT(reason, pred, ep_id, msg_id, msg_text) \
    (seraph_void_record((reason), (pred), (ep_id), (msg_id), \
                        __FILE__, __func__, __LINE__, (msg_text)), \
     SERAPH_VBIT_VOID)

/**
 * @brief Record channel closed VOID
 */
#define SERAPH_WHISPER_VOID_CLOSED(ep_id) \
    SERAPH_WHISPER_VOID_VBIT(SERAPH_VOID_REASON_CHANNEL_CLOSED, 0, \
                              (ep_id), 0, "channel closed")

/**
 * @brief Record channel full VOID
 */
#define SERAPH_WHISPER_VOID_FULL(ep_id) \
    SERAPH_WHISPER_VOID_VBIT(SERAPH_VOID_REASON_CHANNEL_FULL, 0, \
                              (ep_id), 0, "send queue full")

/**
 * @brief Record endpoint dead VOID
 */
#define SERAPH_WHISPER_VOID_DEAD(ep_id) \
    SERAPH_WHISPER_VOID_VBIT(SERAPH_VOID_REASON_ENDPOINT_DEAD, 0, \
                              (ep_id), 0, "endpoint disconnected")

/**
 * @brief Record channel empty VOID
 */
#define SERAPH_WHISPER_VOID_EMPTY(ep_id) \
    SERAPH_WHISPER_VOID_VBIT(SERAPH_VOID_REASON_CHANNEL_EMPTY, 0, \
                              (ep_id), 0, "receive queue empty")

/*============================================================================
 * Configuration Constants
 *============================================================================*/

/* Note: SERAPH_WHISPER_MAX_CAPS defined above for forward compatibility */

/** Channel queue depth (ring buffer size) */
#define SERAPH_WHISPER_QUEUE_SIZE 64

/** Queue size mask for efficient modulo */
#define SERAPH_WHISPER_QUEUE_MASK (SERAPH_WHISPER_QUEUE_SIZE - 1)

/** Maximum concurrent lends per endpoint */
#define SERAPH_WHISPER_MAX_LENDS 64

/*============================================================================
 * Lend Tracking (for LEND/RETURN semantics)
 *============================================================================*/

/**
 * @brief Status of a lend record
 *
 * Tracks the lifecycle of a lent capability:
 * VOID → ACTIVE → (RETURNED | EXPIRED | REVOKED)
 */
typedef enum {
    /** Empty/unused slot */
    SERAPH_LEND_STATUS_VOID = 0,

    /** Capability is currently lent out */
    SERAPH_LEND_STATUS_ACTIVE = 1,

    /** Borrower returned the capability early */
    SERAPH_LEND_STATUS_RETURNED = 2,

    /** Timeout expired, capability automatically revoked */
    SERAPH_LEND_STATUS_EXPIRED = 3,

    /** Lender manually revoked before timeout */
    SERAPH_LEND_STATUS_REVOKED = 4
} Seraph_Whisper_Lend_Status;

/**
 * @brief Record tracking a single lent capability
 *
 * When Process A lends a capability to Process B:
 * 1. A creates a Lend_Record in its lend_registry
 * 2. A sends LEND message to B with the capability
 * 3. A's original cap is marked as "lent" (cannot be used while lent)
 * 4. When timeout expires or B sends RETURN: cap returns to A
 *
 * This prevents memory leaks by ensuring borrowed caps are always
 * eventually returned or revoked.
 */
typedef struct {
    /** The original capability that was lent */
    Seraph_Capability original_cap;

    /** The capability given to borrower (may have reduced permissions) */
    Seraph_Capability borrowed_cap;

    /** Message ID of the LEND message (used to match RETURN) */
    uint64_t lend_message_id;

    /** Chronon when the lend started */
    Seraph_Chronon lend_chronon;

    /** Chronon when the lend expires (0 = never) */
    Seraph_Chronon expiry_chronon;

    /** Endpoint ID of the borrower */
    uint64_t borrower_endpoint_id;

    /** Current status of this lend */
    Seraph_Whisper_Lend_Status status;

    /** Padding for alignment */
    uint8_t _pad[3];
} Seraph_Whisper_Lend_Record;

/*============================================================================
 * Message Types
 *============================================================================*/

/**
 * @brief Whisper message type enumeration
 *
 * Each message type has specific semantics for capability transfer.
 */
typedef enum {
    /** Request expecting a response. Should include REPLY capability. */
    SERAPH_WHISPER_REQUEST = 0,

    /** Response to a previous request. References original message_id. */
    SERAPH_WHISPER_RESPONSE = 1,

    /** One-way notification, no response expected. */
    SERAPH_WHISPER_NOTIFICATION = 2,

    /** Permanent capability transfer - sender loses the cap. */
    SERAPH_WHISPER_GRANT = 3,

    /** Temporary capability loan with timeout. */
    SERAPH_WHISPER_LEND = 4,

    /** Explicit return of a borrowed capability. */
    SERAPH_WHISPER_RETURN = 5,

    /** Derive: send a restricted version of a capability */
    SERAPH_WHISPER_DERIVE = 6,

    /** Copy: share a read-only capability */
    SERAPH_WHISPER_COPY = 7,

    /** VOID message - channel closed or message invalid */
    SERAPH_WHISPER_VOID = 0xFF
} Seraph_Whisper_Type;

/*============================================================================
 * Message Flags
 *============================================================================*/

/**
 * @brief Whisper message flags
 */
typedef enum {
    SERAPH_WHISPER_FLAG_NONE      = 0,
    SERAPH_WHISPER_FLAG_URGENT    = (1 << 0),  /**< High-priority message */
    SERAPH_WHISPER_FLAG_REPLY_REQ = (1 << 1),  /**< Response required */
    SERAPH_WHISPER_FLAG_ORDERED   = (1 << 2),  /**< Must be processed in order */
    SERAPH_WHISPER_FLAG_IDEMPOTENT= (1 << 3),  /**< Safe to retry if lost */
    SERAPH_WHISPER_FLAG_BORROWED  = (1 << 4),  /**< Caps are borrowed, not granted */
    SERAPH_WHISPER_FLAG_BROADCAST = (1 << 5),  /**< Sent to multiple receivers */
} Seraph_Whisper_Flags;

/*============================================================================
 * Message Structure (256 bytes, 32-byte aligned)
 *============================================================================*/

/**
 * @brief Whisper message - the fundamental unit of SERAPH IPC
 *
 * A Whisper message is exactly 256 bytes (4 cache lines):
 * - 32 bytes: Header (ID, sender, timestamp, type, count, flags)
 * - 224 bytes: Up to 7 capabilities (7 x 32 bytes)
 *
 * The message itself is TINY. The actual DATA is accessed via the capabilities.
 * Want to send 1GB of video? Send a 256-byte message with a capability to it.
 */
typedef struct __attribute__((aligned(32))) {
    /*=== HEADER (32 bytes) ===*/

    /** Unique identifier for this message */
    uint64_t message_id;

    /** Sovereign ID of sender (truncated to 64 bits) */
    uint64_t sender_id;

    /** When the message was sent */
    Seraph_Chronon send_chronon;

    /** Message type */
    Seraph_Whisper_Type type;

    /** Number of capabilities (0-7) */
    uint8_t cap_count;

    /** Message flags */
    uint16_t flags;

    /** Timeout for LEND messages (Chronons from send time) */
    uint32_t lend_timeout;

    /*=== CAPABILITIES (168 bytes = 7 x 24 bytes) ===*/

    /** The capabilities being transferred */
    Seraph_Capability caps[SERAPH_WHISPER_MAX_CAPS];

    /*=== VOID CAUSALITY TRACKING (16 bytes) ===*/

    /**
     * @brief VOID propagation ID for causality tracking
     *
     * If this message carries a VOID value (either the message itself
     * is VOID, or it contains VOID capabilities), this field stores
     * the VOID ID for archaeology. Set to 0 if message is valid.
     */
    uint64_t void_id;

    /**
     * @brief Bitmask of VOID capabilities (bits 0-6)
     *
     * Each bit indicates whether the corresponding capability slot
     * contains a VOID capability. Enables quick VOID detection.
     */
    uint8_t void_cap_mask;

    /** Number of VOID capabilities in this message */
    uint8_t void_cap_count;

    /** Reserved for alignment */
    uint8_t _void_reserved[6];

    /*=== PADDING (24 bytes to reach 256 total) ===*/
    /* Layout: 40 (header) + 168 (caps) + 24 (void) + 24 (reserved) = 256 bytes */

    /** Reserved for future use / message extension */
    uint8_t _reserved[24];

} Seraph_Whisper_Message;

/* Static assertions for layout */
_Static_assert(sizeof(Seraph_Whisper_Message) == 256,
    "Whisper message must be exactly 256 bytes (4 cache lines)");

/** VOID message constant */
#define SERAPH_WHISPER_MESSAGE_VOID ((Seraph_Whisper_Message){ \
    .message_id = SERAPH_VOID_U64,                            \
    .sender_id = SERAPH_VOID_U64,                             \
    .send_chronon = 0,                                        \
    .type = SERAPH_WHISPER_VOID,                              \
    .cap_count = 0,                                           \
    .flags = 0,                                               \
    .lend_timeout = 0,                                        \
    .caps = {{0}},                                            \
    .void_id = SERAPH_VOID_U64,                               \
    .void_cap_mask = 0xFF,                                    \
    .void_cap_count = 0                                       \
})

/**
 * @brief Create a VOID message with causality tracking
 *
 * Creates a VOID message that records WHY it became VOID, enabling
 * debugging through Void Archaeology.
 *
 * @param reason The reason for VOID
 * @param predecessor Predecessor VOID ID (0 if root cause)
 * @param endpoint_id Endpoint that generated this VOID
 * @param message Optional message string
 * @return VOID message with recorded causality
 */
Seraph_Whisper_Message seraph_whisper_message_void_with_reason(
    Seraph_VoidReason reason,
    uint64_t predecessor,
    uint64_t endpoint_id,
    const char* message
);

/*============================================================================
 * Message Detection
 *============================================================================*/

/**
 * @brief Check if a message is VOID
 */
static inline bool seraph_whisper_message_is_void(Seraph_Whisper_Message msg) {
    return msg.message_id == SERAPH_VOID_U64 || msg.type == SERAPH_WHISPER_VOID;
}

/**
 * @brief Check if a message exists (is not VOID)
 */
static inline bool seraph_whisper_message_exists(Seraph_Whisper_Message msg) {
    return !seraph_whisper_message_is_void(msg);
}

/*============================================================================
 * Channel Endpoint
 *============================================================================*/

/**
 * @brief One end of a Whisper Channel
 *
 * Each endpoint has:
 * - Send queue (outgoing messages)
 * - Recv queue (incoming messages)
 * - Atomic indices for lock-free operation
 * - Statistics
 */
typedef struct {
    /** Send queue (ring buffer of outgoing messages) */
    Seraph_Whisper_Message send_queue[SERAPH_WHISPER_QUEUE_SIZE];

    /** Receive queue (ring buffer of incoming messages) */
    Seraph_Whisper_Message recv_queue[SERAPH_WHISPER_QUEUE_SIZE];

    /** Send queue indices (atomic for lock-free) */
    _Atomic uint32_t send_head;  /**< Where to write next */
    _Atomic uint32_t send_tail;  /**< Where reader is at */

    /** Receive queue indices */
    _Atomic uint32_t recv_head;  /**< Where to write next */
    _Atomic uint32_t recv_tail;  /**< Where reader is at */

    /** Channel state */
    _Atomic Seraph_Vbit connected;     /**< Is the other end alive? */
    _Atomic Seraph_Chronon last_activity; /**< Last send or receive */

    /** Statistics */
    _Atomic uint64_t total_sent;
    _Atomic uint64_t total_received;
    _Atomic uint64_t total_dropped;    /**< Messages lost due to full queue */

    /** Endpoint identifier */
    uint64_t endpoint_id;

    /*=========================================================================
     * Lend Registry - tracks capabilities this endpoint has LENT OUT
     *
     * When this endpoint lends a capability, a record is stored here.
     * The lend remains active until:
     * - Timeout expires (automatic expiration)
     * - Borrower sends RETURN message (early return)
     * - Lender calls seraph_whisper_revoke_lend() (manual revocation)
     *=========================================================================*/

    /** Registry of active lends from this endpoint */
    Seraph_Whisper_Lend_Record lend_registry[SERAPH_WHISPER_MAX_LENDS];

    /** Number of active lend records (includes ACTIVE status only) */
    _Atomic uint32_t active_lend_count;

    /** Total lends ever made (for statistics) */
    _Atomic uint64_t total_lends;

    /** Total returns received */
    _Atomic uint64_t total_returns;

    /** Total timeouts (expirations) */
    _Atomic uint64_t total_expirations;

    /** Total manual revocations */
    _Atomic uint64_t total_revocations;

} Seraph_Whisper_Endpoint;

/*============================================================================
 * Whisper Channel
 *============================================================================*/

/**
 * @brief A complete Whisper Channel (two endpoints connected)
 *
 * A channel is created by a parent Sovereign. The parent keeps parent_end,
 * the child gets a capability to child_end. They can then communicate by
 * sending capabilities back and forth.
 */
typedef struct {
    /** Parent's endpoint */
    Seraph_Whisper_Endpoint parent_end;

    /** Child's endpoint */
    Seraph_Whisper_Endpoint child_end;

    /** Unique channel identifier */
    uint64_t channel_id;

    /** Is channel active? */
    Seraph_Vbit active;

    /** Generation for capability validation */
    uint64_t generation;

} Seraph_Whisper_Channel;

/** VOID channel constant */
#define SERAPH_WHISPER_CHANNEL_VOID ((Seraph_Whisper_Channel){ \
    .channel_id = SERAPH_VOID_U64,                           \
    .active = SERAPH_VBIT_VOID                               \
})

/*============================================================================
 * Channel Detection
 *============================================================================*/

/**
 * @brief Check if a channel is VOID
 */
static inline bool seraph_whisper_channel_is_void(Seraph_Whisper_Channel* channel) {
    if (channel == NULL) return true;
    return channel->channel_id == SERAPH_VOID_U64;
}

/**
 * @brief Check if a channel is active
 */
static inline bool seraph_whisper_channel_is_active(Seraph_Whisper_Channel* channel) {
    if (channel == NULL) return false;
    return seraph_vbit_is_true(channel->active);
}

/*============================================================================
 * Channel Operations
 *============================================================================*/

/**
 * @brief Create a new Whisper Channel
 *
 * Creates a bidirectional channel with two endpoints.
 * The parent keeps parent_end, grants child_end to a child Sovereign.
 *
 * @return New channel, or VOID channel on failure
 */
Seraph_Whisper_Channel seraph_whisper_channel_create(void);

/**
 * @brief Initialize a channel in-place
 *
 * @param channel Pointer to channel to initialize
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_whisper_channel_init(Seraph_Whisper_Channel* channel);

/**
 * @brief Close a Whisper Channel
 *
 * Marks channel as inactive. Pending messages remain accessible
 * until both endpoints are destroyed.
 *
 * @param channel Channel to close
 * @return TRUE on success, VOID if channel was already closed
 */
Seraph_Vbit seraph_whisper_channel_close(Seraph_Whisper_Channel* channel);

/**
 * @brief Destroy a Whisper Channel
 *
 * Fully destroys the channel and invalidates all capabilities to it.
 *
 * @param channel Channel to destroy
 */
void seraph_whisper_channel_destroy(Seraph_Whisper_Channel* channel);

/**
 * @brief Get a capability to one end of a channel
 *
 * @param channel The channel
 * @param is_child_end TRUE for child end, FALSE for parent end
 * @return Capability to the endpoint, or VOID capability on failure
 */
Seraph_Capability seraph_whisper_channel_get_cap(
    Seraph_Whisper_Channel* channel,
    bool is_child_end
);

/*============================================================================
 * Message Construction
 *============================================================================*/

/**
 * @brief Create a new message with specified type
 *
 * @param type Message type
 * @return New message with unique ID, or VOID message on failure
 */
Seraph_Whisper_Message seraph_whisper_message_new(Seraph_Whisper_Type type);

/**
 * @brief Add a capability to a message
 *
 * @param msg Message to modify
 * @param cap Capability to add
 * @return TRUE on success, FALSE if message full, VOID on error
 */
Seraph_Vbit seraph_whisper_message_add_cap(
    Seraph_Whisper_Message* msg,
    Seraph_Capability cap
);

/**
 * @brief Get a capability from a message
 *
 * @param msg Message to read
 * @param index Capability index (0-6)
 * @return The capability, or VOID capability if index out of range
 */
Seraph_Capability seraph_whisper_message_get_cap(
    Seraph_Whisper_Message* msg,
    uint8_t index
);

/**
 * @brief Set message flags
 */
static inline void seraph_whisper_message_set_flags(
    Seraph_Whisper_Message* msg,
    uint16_t flags
) {
    if (msg) msg->flags = flags;
}

/*============================================================================
 * Send Operations
 *============================================================================*/

/**
 * @brief Send a message through an endpoint
 *
 * The message is moved into the send queue. Capabilities in the message
 * follow the transfer semantics of the message type.
 *
 * @param endpoint Endpoint to send through
 * @param message Message to send (consumed)
 * @return TRUE if sent, FALSE if queue full, VOID if endpoint dead
 */
Seraph_Vbit seraph_whisper_send(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Whisper_Message message
);

/**
 * @brief Send a single capability as a GRANT message
 *
 * Convenience function for permanent capability transfer.
 * The sender loses access to the capability.
 *
 * @param endpoint Endpoint to send through
 * @param cap Capability to grant (consumed)
 * @return TRUE if sent, FALSE if queue full, VOID if endpoint dead
 */
Seraph_Vbit seraph_whisper_grant(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap
);

/**
 * @brief Send a capability as a LEND message with timeout
 *
 * The capability is temporarily transferred. It automatically
 * returns when the timeout expires or the receiver returns it early.
 *
 * @param endpoint Endpoint to send through
 * @param cap Capability to lend
 * @param timeout Chronons until automatic return
 * @return TRUE if sent, FALSE if queue full, VOID if endpoint dead
 */
Seraph_Vbit seraph_whisper_lend(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    Seraph_Chronon timeout
);

/**
 * @brief Send a request expecting a response
 *
 * @param endpoint Endpoint to send through
 * @param caps Array of capabilities for the request
 * @param cap_count Number of capabilities
 * @param flags Message flags
 * @return Message ID of the sent request, or VOID on failure
 */
uint64_t seraph_whisper_request(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability* caps,
    uint8_t cap_count,
    uint16_t flags
);

/**
 * @brief Send a response to a request
 *
 * @param endpoint Endpoint to send through
 * @param request_id Message ID of the original request
 * @param caps Array of capabilities for the response
 * @param cap_count Number of capabilities
 * @return TRUE if sent, FALSE if queue full, VOID if endpoint dead
 */
Seraph_Vbit seraph_whisper_respond(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t request_id,
    Seraph_Capability* caps,
    uint8_t cap_count
);

/**
 * @brief Send a notification (no response expected)
 *
 * @param endpoint Endpoint to send through
 * @param caps Array of capabilities
 * @param cap_count Number of capabilities
 * @return TRUE if sent, FALSE if queue full, VOID if endpoint dead
 */
Seraph_Vbit seraph_whisper_notify(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability* caps,
    uint8_t cap_count
);

/*============================================================================
 * Receive Operations
 *============================================================================*/

/**
 * @brief Receive a message from an endpoint
 *
 * @param endpoint Endpoint to receive from
 * @param blocking If true, wait for message; if false, return immediately
 * @return Received message, or VOID message if none available/timeout
 */
Seraph_Whisper_Message seraph_whisper_recv(
    Seraph_Whisper_Endpoint* endpoint,
    bool blocking
);

/**
 * @brief Peek at the next message without removing it
 *
 * @param endpoint Endpoint to peek
 * @return The next message, or VOID message if queue empty
 */
Seraph_Whisper_Message seraph_whisper_peek(Seraph_Whisper_Endpoint* endpoint);

/**
 * @brief Check if messages are available
 *
 * @param endpoint Endpoint to check
 * @return TRUE if messages available, FALSE if empty, VOID if endpoint dead
 */
Seraph_Vbit seraph_whisper_available(Seraph_Whisper_Endpoint* endpoint);

/**
 * @brief Get number of pending messages
 *
 * @param endpoint Endpoint to check
 * @return Number of messages in receive queue
 */
uint32_t seraph_whisper_pending_count(Seraph_Whisper_Endpoint* endpoint);

/**
 * @brief Wait for a specific response by message ID
 *
 * Scans the receive queue for a RESPONSE message matching the request_id.
 *
 * @param endpoint Endpoint to receive from
 * @param request_id Message ID of the original request
 * @param max_wait Maximum messages to scan (0 = unlimited)
 * @return The response message, or VOID message if not found
 */
Seraph_Whisper_Message seraph_whisper_await_response(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t request_id,
    uint32_t max_wait
);

/*============================================================================
 * Return Operations (for LEND semantics)
 *============================================================================*/

/**
 * @brief Return a borrowed capability early
 *
 * Sends a RETURN message through the channel.
 *
 * @param endpoint Endpoint to send through
 * @param cap The borrowed capability to return
 * @return TRUE if returned, VOID on failure
 */
Seraph_Vbit seraph_whisper_return_cap(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap
);

/**
 * @brief Return a borrowed capability with the original lend message ID
 *
 * More precise return that matches by message ID.
 *
 * @param endpoint Endpoint to send through
 * @param cap The borrowed capability to return
 * @param lend_message_id The message ID from the original LEND message
 * @return TRUE if returned, VOID on failure
 */
Seraph_Vbit seraph_whisper_return_cap_by_id(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    uint64_t lend_message_id
);

/*============================================================================
 * Lend Management Operations
 *============================================================================*/

/**
 * @brief Process lend timeouts and handle expired lends
 *
 * This function should be called periodically (or before accessing lent caps)
 * to process expired lends. For each expired lend:
 * - The lend record is marked EXPIRED
 * - The lender's capability access is restored
 * - The borrower's capability is invalidated
 *
 * @param endpoint The endpoint whose lends to process
 * @param current_chronon Current time for timeout checking
 * @return Number of lends that expired
 */
uint32_t seraph_whisper_process_lends(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Chronon current_chronon
);

/**
 * @brief Manually revoke a lent capability before timeout
 *
 * Allows the lender to forcibly reclaim a lent capability.
 * The borrower's copy becomes invalid immediately.
 *
 * @param endpoint The lender's endpoint
 * @param lend_message_id Message ID of the original LEND
 * @return TRUE if revoked, FALSE if not found, VOID on error
 */
Seraph_Vbit seraph_whisper_revoke_lend(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
);

/**
 * @brief Check if a lend is still active
 *
 * @param endpoint The lender's endpoint
 * @param lend_message_id Message ID of the LEND
 * @return TRUE if active, FALSE if not, VOID if not found
 */
Seraph_Vbit seraph_whisper_lend_is_active(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
);

/**
 * @brief Get a lend record by message ID
 *
 * @param endpoint The endpoint to search
 * @param lend_message_id Message ID of the LEND
 * @return Pointer to the record, or NULL if not found
 */
Seraph_Whisper_Lend_Record* seraph_whisper_get_lend_record(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
);

/**
 * @brief Get count of active lends from this endpoint
 *
 * @param endpoint The endpoint to query
 * @return Number of currently active lends
 */
uint32_t seraph_whisper_active_lend_count(Seraph_Whisper_Endpoint* endpoint);

/**
 * @brief Handle a RETURN message (internal, called by channel_transfer)
 *
 * When a RETURN message is received, this marks the corresponding
 * lend record as RETURNED and restores the lender's access.
 *
 * @param endpoint The lender's endpoint
 * @param return_msg The RETURN message
 * @return TRUE if handled, FALSE if lend not found, VOID on error
 */
Seraph_Vbit seraph_whisper_handle_return(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Whisper_Message* return_msg
);

/*============================================================================
 * Statistics
 *============================================================================*/

/**
 * @brief Get endpoint statistics
 */
typedef struct {
    uint64_t total_sent;
    uint64_t total_received;
    uint64_t total_dropped;
    uint32_t send_queue_depth;
    uint32_t recv_queue_depth;
    bool connected;
} Seraph_Whisper_Stats;

/**
 * @brief Get statistics for an endpoint
 */
Seraph_Whisper_Stats seraph_whisper_get_stats(Seraph_Whisper_Endpoint* endpoint);

/*============================================================================
 * Transfer from internal queues
 *============================================================================*/

/**
 * @brief Transfer messages between connected endpoints
 *
 * This should be called periodically (or by the kernel) to move messages
 * from one endpoint's send queue to the other endpoint's receive queue.
 *
 * @param channel The channel to process
 * @return Number of messages transferred
 */
uint32_t seraph_whisper_channel_transfer(Seraph_Whisper_Channel* channel);

/*============================================================================
 * VOID-Safe Whisper Operations
 *
 * These operations properly handle and propagate VOID values with full
 * causality tracking for debugging through Void Archaeology.
 *============================================================================*/

/**
 * @brief Check if a message contains any VOID capabilities
 *
 * Scans all capabilities in the message and returns TRUE if any are VOID.
 *
 * @param msg Message to check
 * @return TRUE if any cap is VOID, FALSE if all valid, VOID if msg is VOID
 */
Seraph_Vbit seraph_whisper_message_has_void_caps(const Seraph_Whisper_Message* msg);

/**
 * @brief Get the VOID mask for a message's capabilities
 *
 * Returns a bitmask where bit i is set if caps[i] is VOID.
 *
 * @param msg Message to analyze
 * @return Bitmask (0x00-0x7F), or 0xFF if message itself is VOID
 */
uint8_t seraph_whisper_message_get_void_mask(const Seraph_Whisper_Message* msg);

/**
 * @brief Update message's VOID tracking fields
 *
 * Scans capabilities and updates void_cap_mask and void_cap_count.
 * Should be called after modifying message capabilities.
 *
 * @param msg Message to update
 */
void seraph_whisper_message_update_void_tracking(Seraph_Whisper_Message* msg);

/**
 * @brief VOID-safe send with causality tracking
 *
 * Like seraph_whisper_send, but records causality when:
 * - Endpoint is dead (VOID result with ENDPOINT_DEAD reason)
 * - Queue is full (VOID result with CHANNEL_FULL reason)
 * - Message contains VOID capabilities (propagates VOID IDs)
 *
 * @param endpoint Endpoint to send through
 * @param message Message to send
 * @param predecessor_void_id Predecessor VOID ID if propagating (0 if new)
 * @return TRUE if sent, FALSE if queue full, VOID if endpoint dead
 */
Seraph_Vbit seraph_whisper_send_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Whisper_Message message,
    uint64_t predecessor_void_id
);

/**
 * @brief VOID-safe receive with causality tracking
 *
 * Like seraph_whisper_recv, but:
 * - Records causality when receive fails (ENDPOINT_DEAD, CHANNEL_EMPTY)
 * - Extracts VOID IDs from received messages for archaeology
 *
 * @param endpoint Endpoint to receive from
 * @param blocking Wait for message if queue empty
 * @param out_void_id Output: VOID ID if message carries VOID (can be NULL)
 * @return Received message, or VOID message with tracking
 */
Seraph_Whisper_Message seraph_whisper_recv_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    bool blocking,
    uint64_t* out_void_id
);

/**
 * @brief VOID-safe peek with causality tracking
 *
 * Like seraph_whisper_peek, but records causality on failure.
 *
 * @param endpoint Endpoint to peek
 * @param out_void_id Output: VOID ID if next message carries VOID (can be NULL)
 * @return Next message, or VOID message with tracking
 */
Seraph_Whisper_Message seraph_whisper_peek_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t* out_void_id
);

/**
 * @brief Print Whisper VOID archaeology for debugging
 *
 * Walks the VOID causality chain and prints Whisper-specific context.
 *
 * @param void_id The VOID ID to investigate
 */
void seraph_whisper_print_void_chain(uint64_t void_id);

/**
 * @brief Get the last Whisper VOID context
 *
 * Returns the most recent VOID context that occurred in Whisper operations.
 *
 * @return VOID context, or NONE if no VOIDs recorded
 */
Seraph_VoidContext seraph_whisper_last_void(void);

/**
 * @brief Check if a message is a VOID propagation
 *
 * A message is a VOID propagation if:
 * - It was explicitly created as VOID, OR
 * - It carries VOID capabilities from the sender
 *
 * @param msg Message to check
 * @return TRUE if propagating VOID, FALSE otherwise
 */
static inline bool seraph_whisper_message_is_void_propagation(
    const Seraph_Whisper_Message* msg
) {
    if (msg == NULL) return true;
    return msg->void_id != 0 && msg->void_id != SERAPH_VOID_U64;
}

/**
 * @brief Create a VOID capability with Whisper causality
 *
 * Creates a VOID capability that records its origin for archaeology.
 *
 * @param reason Why the capability became VOID
 * @param endpoint_id Endpoint that created this VOID
 * @param message_id Message ID context
 * @param cap_index Index in message (0-6)
 * @return VOID capability with recorded causality
 */
Seraph_Capability seraph_whisper_cap_void_with_reason(
    Seraph_VoidReason reason,
    uint64_t endpoint_id,
    uint64_t message_id,
    uint8_t cap_index
);

/**
 * @brief VOID-safe grant with tracking
 *
 * @param endpoint Endpoint to send through
 * @param cap Capability to grant
 * @param predecessor_void_id Predecessor VOID ID if propagating
 * @return TRUE if sent, FALSE if queue full, VOID with tracking if failed
 */
Seraph_Vbit seraph_whisper_grant_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    uint64_t predecessor_void_id
);

/**
 * @brief VOID-safe lend with tracking
 *
 * @param endpoint Endpoint to send through
 * @param cap Capability to lend
 * @param timeout Chronons until automatic return
 * @param predecessor_void_id Predecessor VOID ID if propagating
 * @return TRUE if sent, FALSE if queue full, VOID with tracking if failed
 */
Seraph_Vbit seraph_whisper_lend_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    Seraph_Chronon timeout,
    uint64_t predecessor_void_id
);

/**
 * @brief Process lend timeouts with VOID tracking
 *
 * Like seraph_whisper_process_lends, but records VOID contexts for
 * expired lends to enable archaeology.
 *
 * @param endpoint Endpoint whose lends to process
 * @param current_chronon Current time for timeout checking
 * @param out_void_ids Array to receive VOID IDs for expired lends (can be NULL)
 * @param max_void_ids Maximum IDs to store in out_void_ids
 * @return Number of lends that expired
 */
uint32_t seraph_whisper_process_lends_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Chronon current_chronon,
    uint64_t* out_void_ids,
    uint32_t max_void_ids
);

/**
 * @brief Revoke lend with VOID tracking
 *
 * Like seraph_whisper_revoke_lend, but records a VOID context for
 * the revocation to enable archaeology.
 *
 * @param endpoint The lender's endpoint
 * @param lend_message_id Message ID of the original LEND
 * @param out_void_id Output: VOID ID for the revocation (can be NULL)
 * @return TRUE if revoked, FALSE if not found, VOID on error
 */
Seraph_Vbit seraph_whisper_revoke_lend_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id,
    uint64_t* out_void_id
);

/*============================================================================
 * VOID Archaeology Query Functions
 *============================================================================*/

/**
 * @brief Get the last Whisper VOID ID recorded in this thread
 * @return Last VOID ID, or 0 if no VOID recorded
 */
uint64_t seraph_whisper_get_last_void_id(void);

/**
 * @brief Get the endpoint ID that generated the last VOID
 * @return Endpoint ID, or 0 if no VOID recorded
 */
uint64_t seraph_whisper_get_last_void_endpoint(void);

/**
 * @brief Get the message ID associated with the last VOID
 * @return Message ID, or 0 if no VOID recorded
 */
uint64_t seraph_whisper_get_last_void_message(void);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_WHISPER_H */
