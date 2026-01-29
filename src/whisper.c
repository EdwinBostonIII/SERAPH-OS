/**
 * @file whisper.c
 * @brief MC12: Whisper - Capability-Based Zero-Copy IPC Implementation
 *
 * "A message is not 'data in transit.' A message is the DELEGATION OF AUTHORITY."
 *
 * VOID-SAFE IMPLEMENTATION:
 * This implementation provides full VOID causality tracking for all Whisper
 * operations. When a VOID occurs (endpoint dead, queue full, invalid message,
 * etc.), the context is recorded for debugging via Void Archaeology.
 *
 * Key VOID scenarios handled:
 * - Endpoint disconnection: Records SERAPH_VOID_REASON_ENDPOINT_DEAD
 * - Queue full: Records SERAPH_VOID_REASON_CHANNEL_FULL
 * - Queue empty: Records SERAPH_VOID_REASON_CHANNEL_EMPTY
 * - VOID capability in message: Records SERAPH_VOID_REASON_VOID_CAP_IN_MSG
 * - Lend expiration: Records SERAPH_VOID_REASON_LEND_EXPIRED
 * - Lend revocation: Records SERAPH_VOID_REASON_LEND_REVOKED
 */

#include "seraph/whisper.h"
#ifdef SERAPH_KERNEL
    extern void* memset(void* dest, int val, size_t count);
    extern void* memcpy(void* dest, const void* src, size_t count);
    extern void* malloc(size_t size);
    extern void* calloc(size_t nmemb, size_t size);
    extern void free(void* ptr);
#else
    #include <string.h>
    #include <stdlib.h>
    #include <stdio.h>
#endif

/*============================================================================
 * Internal State
 *============================================================================*/

/** Counter for generating unique message IDs */
static _Atomic uint64_t message_id_counter = 1;

/** Counter for generating unique channel IDs */
static _Atomic uint64_t channel_id_counter = 1;

/** Counter for generating unique endpoint IDs */
static _Atomic uint64_t endpoint_id_counter = 1;

/*============================================================================
 * VOID Tracking State
 *
 * Thread-local state for tracking the most recent Whisper VOID context.
 *============================================================================*/

#if defined(__GNUC__) || defined(__clang__)
    #define WHISPER_TLS __thread
#elif defined(_WIN32) && defined(_MSC_VER)
    #define WHISPER_TLS __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define WHISPER_TLS _Thread_local
#else
    #define WHISPER_TLS
#endif

/** Most recent Whisper VOID ID for quick access */
static WHISPER_TLS uint64_t g_last_whisper_void_id = 0;

/** Most recent Whisper endpoint that generated a VOID */
static WHISPER_TLS uint64_t g_last_void_endpoint_id = 0;

/** Most recent Whisper message ID associated with a VOID */
static WHISPER_TLS uint64_t g_last_void_message_id = 0;

/* Forward declaration of whisper_record_void (defined below after helpers) */
static uint64_t whisper_record_void(
    Seraph_VoidReason reason,
    uint64_t predecessor,
    uint64_t endpoint_id,
    uint64_t message_id,
    const char* msg
);

/**
 * @brief Get the last Whisper VOID ID
 */
uint64_t seraph_whisper_get_last_void_id(void) {
    return g_last_whisper_void_id;
}

/**
 * @brief Get the last VOID endpoint ID
 */
uint64_t seraph_whisper_get_last_void_endpoint(void) {
    return g_last_void_endpoint_id;
}

/**
 * @brief Get the last VOID message ID
 */
uint64_t seraph_whisper_get_last_void_message(void) {
    return g_last_void_message_id;
}

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Generate a unique message ID
 */
static uint64_t generate_message_id(void) {
    uint64_t id = atomic_fetch_add(&message_id_counter, 1);
    /* Skip VOID values */
    if (id == SERAPH_VOID_U64) {
        id = atomic_fetch_add(&message_id_counter, 1);
    }
    return id;
}

/**
 * @brief Generate a unique channel ID
 */
static uint64_t generate_channel_id(void) {
    uint64_t id = atomic_fetch_add(&channel_id_counter, 1);
    if (id == SERAPH_VOID_U64) {
        id = atomic_fetch_add(&channel_id_counter, 1);
    }
    return id;
}

/**
 * @brief Generate a unique endpoint ID
 */
static uint64_t generate_endpoint_id(void) {
    uint64_t id = atomic_fetch_add(&endpoint_id_counter, 1);
    if (id == SERAPH_VOID_U64) {
        id = atomic_fetch_add(&endpoint_id_counter, 1);
    }
    return id;
}

/**
 * @brief Initialize an endpoint
 */
static void endpoint_init(Seraph_Whisper_Endpoint* ep) {
    memset(ep->send_queue, 0, sizeof(ep->send_queue));
    memset(ep->recv_queue, 0, sizeof(ep->recv_queue));
    atomic_store(&ep->send_head, 0);
    atomic_store(&ep->send_tail, 0);
    atomic_store(&ep->recv_head, 0);
    atomic_store(&ep->recv_tail, 0);
    atomic_store(&ep->connected, SERAPH_VBIT_TRUE);
    atomic_store(&ep->last_activity, 0);
    atomic_store(&ep->total_sent, 0);
    atomic_store(&ep->total_received, 0);
    atomic_store(&ep->total_dropped, 0);
    ep->endpoint_id = generate_endpoint_id();

    /* Initialize lend registry */
    memset(ep->lend_registry, 0, sizeof(ep->lend_registry));
    atomic_store(&ep->active_lend_count, 0);
    atomic_store(&ep->total_lends, 0);
    atomic_store(&ep->total_returns, 0);
    atomic_store(&ep->total_expirations, 0);
    atomic_store(&ep->total_revocations, 0);
}

/**
 * @brief Find an empty slot in the lend registry
 * @return Index of empty slot, or -1 if full
 */
static int32_t lend_registry_find_empty(Seraph_Whisper_Endpoint* ep) {
    for (uint32_t i = 0; i < SERAPH_WHISPER_MAX_LENDS; i++) {
        if (ep->lend_registry[i].status == SERAPH_LEND_STATUS_VOID) {
            return (int32_t)i;
        }
    }
    return -1;
}

/**
 * @brief Find a lend record by message ID
 * @return Index of the record, or -1 if not found
 */
static int32_t lend_registry_find_by_id(Seraph_Whisper_Endpoint* ep, uint64_t message_id) {
    for (uint32_t i = 0; i < SERAPH_WHISPER_MAX_LENDS; i++) {
        if (ep->lend_registry[i].lend_message_id == message_id &&
            ep->lend_registry[i].status != SERAPH_LEND_STATUS_VOID) {
            return (int32_t)i;
        }
    }
    return -1;
}

/**
 * @brief Create a lend record for a newly sent LEND message
 */
static Seraph_Vbit lend_registry_create(
    Seraph_Whisper_Endpoint* ep,
    Seraph_Capability original_cap,
    Seraph_Capability borrowed_cap,
    uint64_t message_id,
    Seraph_Chronon current_chronon,
    Seraph_Chronon timeout,
    uint64_t borrower_endpoint_id
) {
    int32_t slot = lend_registry_find_empty(ep);
    if (slot < 0) {
        return SERAPH_VBIT_FALSE;  /* Registry full */
    }

    Seraph_Whisper_Lend_Record* record = &ep->lend_registry[slot];
    record->original_cap = original_cap;
    record->borrowed_cap = borrowed_cap;
    record->lend_message_id = message_id;
    record->lend_chronon = current_chronon;
    record->expiry_chronon = (timeout > 0) ? (current_chronon + timeout) : 0;
    record->borrower_endpoint_id = borrower_endpoint_id;
    record->status = SERAPH_LEND_STATUS_ACTIVE;

    atomic_fetch_add(&ep->active_lend_count, 1);
    atomic_fetch_add(&ep->total_lends, 1);

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Check if endpoint is valid
 */
static bool endpoint_is_valid(Seraph_Whisper_Endpoint* ep) {
    if (ep == NULL) return false;
    return seraph_vbit_is_true(atomic_load(&ep->connected));
}

/*============================================================================
 * VOID Tracking Helpers
 *============================================================================*/

/**
 * @brief Record a Whisper VOID with full context tracking
 *
 * @param reason Why this VOID occurred
 * @param predecessor Predecessor VOID ID (0 if root cause)
 * @param endpoint_id Endpoint that generated this VOID
 * @param message_id Message ID context (0 if not applicable)
 * @param msg Human-readable message
 * @return VOID ID for this occurrence
 */
static uint64_t whisper_record_void(
    Seraph_VoidReason reason,
    uint64_t predecessor,
    uint64_t endpoint_id,
    uint64_t message_id,
    const char* msg
) {
    uint64_t void_id = seraph_void_record(
        reason,
        predecessor,
        endpoint_id,
        message_id,
        __FILE__,
        "whisper_ipc",
        0,
        msg
    );

    /* Update thread-local tracking state */
    g_last_whisper_void_id = void_id;
    g_last_void_endpoint_id = endpoint_id;
    g_last_void_message_id = message_id;

    return void_id;
}

/**
 * @brief Record a Whisper VOID with source location
 */
static uint64_t whisper_record_void_loc(
    Seraph_VoidReason reason,
    uint64_t predecessor,
    uint64_t endpoint_id,
    uint64_t message_id,
    const char* file,
    const char* function,
    uint32_t line,
    const char* msg
) {
    uint64_t void_id = seraph_void_record(
        reason,
        predecessor,
        endpoint_id,
        message_id,
        file,
        function,
        line,
        msg
    );

    g_last_whisper_void_id = void_id;
    g_last_void_endpoint_id = endpoint_id;
    g_last_void_message_id = message_id;

    return void_id;
}

/**
 * @brief Check if a capability is VOID
 */
static bool cap_is_void(Seraph_Capability cap) {
    return seraph_cap_is_void(cap);
}

/**
 * @brief Compute VOID capability mask for a message
 *
 * Scans all capabilities and returns a bitmask indicating which are VOID.
 */
static uint8_t compute_void_cap_mask(const Seraph_Whisper_Message* msg) {
    if (msg == NULL) return 0xFF;
    if (msg->cap_count == 0) return 0;

    uint8_t mask = 0;
    for (uint8_t i = 0; i < msg->cap_count && i < SERAPH_WHISPER_MAX_CAPS; i++) {
        if (cap_is_void(msg->caps[i])) {
            mask |= (1u << i);
        }
    }
    return mask;
}

/**
 * @brief Count VOID capabilities in a message
 */
static uint8_t count_void_caps(uint8_t mask) {
    uint8_t count = 0;
    while (mask) {
        count += (mask & 1);
        mask >>= 1;
    }
    return count;
}

/**
 * @brief Get send queue depth
 */
static uint32_t send_queue_depth(Seraph_Whisper_Endpoint* ep) {
    uint32_t head = atomic_load(&ep->send_head);
    uint32_t tail = atomic_load(&ep->send_tail);
    return (head - tail) & SERAPH_WHISPER_QUEUE_MASK;
}

/**
 * @brief Get receive queue depth
 */
static uint32_t recv_queue_depth(Seraph_Whisper_Endpoint* ep) {
    uint32_t head = atomic_load(&ep->recv_head);
    uint32_t tail = atomic_load(&ep->recv_tail);
    return (head - tail) & SERAPH_WHISPER_QUEUE_MASK;
}

/**
 * @brief Check if send queue is full
 */
static bool send_queue_full(Seraph_Whisper_Endpoint* ep) {
    return send_queue_depth(ep) >= (SERAPH_WHISPER_QUEUE_SIZE - 1);
}

/**
 * @brief Check if receive queue is empty
 */
static bool recv_queue_empty(Seraph_Whisper_Endpoint* ep) {
    uint32_t head = atomic_load(&ep->recv_head);
    uint32_t tail = atomic_load(&ep->recv_tail);
    return head == tail;
}

/*============================================================================
 * Channel Operations
 *============================================================================*/

Seraph_Whisper_Channel seraph_whisper_channel_create(void) {
    Seraph_Whisper_Channel channel;

    /* Initialize endpoints */
    endpoint_init(&channel.parent_end);
    endpoint_init(&channel.child_end);

    /* Set channel metadata */
    channel.channel_id = generate_channel_id();
    channel.active = SERAPH_VBIT_TRUE;
    channel.generation = 1;

    return channel;
}

Seraph_Vbit seraph_whisper_channel_init(Seraph_Whisper_Channel* channel) {
    if (channel == NULL) {
        return SERAPH_VBIT_VOID;
    }

    endpoint_init(&channel->parent_end);
    endpoint_init(&channel->child_end);
    channel->channel_id = generate_channel_id();
    channel->active = SERAPH_VBIT_TRUE;
    channel->generation = 1;

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_whisper_channel_close(Seraph_Whisper_Channel* channel) {
    if (channel == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (seraph_vbit_is_false(channel->active)) {
        return SERAPH_VBIT_VOID;  /* Already closed */
    }

    /* Mark channel as inactive */
    channel->active = SERAPH_VBIT_FALSE;

    /* Mark both endpoints as disconnected */
    atomic_store(&channel->parent_end.connected, SERAPH_VBIT_FALSE);
    atomic_store(&channel->child_end.connected, SERAPH_VBIT_FALSE);

    return SERAPH_VBIT_TRUE;
}

void seraph_whisper_channel_destroy(Seraph_Whisper_Channel* channel) {
    if (channel == NULL) {
        return;
    }

    /* Close first if not already closed */
    seraph_whisper_channel_close(channel);

    /* Increment generation to invalidate all capabilities */
    channel->generation++;

    /* Zero out channel ID to mark as destroyed */
    channel->channel_id = SERAPH_VOID_U64;
}

Seraph_Capability seraph_whisper_channel_get_cap(
    Seraph_Whisper_Channel* channel,
    bool is_child_end
) {
    if (channel == NULL || seraph_whisper_channel_is_void(channel)) {
        return SERAPH_CAP_VOID;
    }

    Seraph_Whisper_Endpoint* ep = is_child_end ?
        &channel->child_end : &channel->parent_end;

    /* Create capability to the endpoint */
    return seraph_cap_create(
        ep,
        sizeof(Seraph_Whisper_Endpoint),
        channel->generation,
        SERAPH_CAP_RW
    );
}

/*============================================================================
 * Message Construction
 *============================================================================*/

Seraph_Whisper_Message seraph_whisper_message_new(Seraph_Whisper_Type type) {
    Seraph_Whisper_Message msg;
    memset(&msg, 0, sizeof(msg));

    msg.message_id = generate_message_id();
    msg.sender_id = 0;  /* Filled in by send */
    msg.send_chronon = 0;  /* Filled in by send */
    msg.type = type;
    msg.cap_count = 0;
    msg.flags = SERAPH_WHISPER_FLAG_NONE;
    msg.lend_timeout = 0;

    return msg;
}

Seraph_Vbit seraph_whisper_message_add_cap(
    Seraph_Whisper_Message* msg,
    Seraph_Capability cap
) {
    if (msg == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (msg->cap_count >= SERAPH_WHISPER_MAX_CAPS) {
        return SERAPH_VBIT_FALSE;  /* Message full */
    }

    msg->caps[msg->cap_count++] = cap;
    return SERAPH_VBIT_TRUE;
}

Seraph_Capability seraph_whisper_message_get_cap(
    Seraph_Whisper_Message* msg,
    uint8_t index
) {
    if (msg == NULL || index >= msg->cap_count) {
        return SERAPH_CAP_VOID;
    }

    return msg->caps[index];
}

/*============================================================================
 * Send Operations
 *============================================================================*/

Seraph_Vbit seraph_whisper_send(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Whisper_Message message
) {
    /* Check endpoint validity with VOID tracking */
    if (endpoint == NULL) {
        whisper_record_void(
            SERAPH_VOID_REASON_NULL_PTR,
            0, 0, message.message_id,
            "null endpoint in send"
        );
        return SERAPH_VBIT_VOID;
    }

    if (!endpoint_is_valid(endpoint)) {
        whisper_record_void(
            SERAPH_VOID_REASON_ENDPOINT_DEAD,
            0, endpoint->endpoint_id, message.message_id,
            "endpoint disconnected"
        );
        return SERAPH_VBIT_VOID;
    }

    if (send_queue_full(endpoint)) {
        whisper_record_void(
            SERAPH_VOID_REASON_CHANNEL_FULL,
            0, endpoint->endpoint_id, message.message_id,
            "send queue full"
        );
        atomic_fetch_add(&endpoint->total_dropped, 1);
        return SERAPH_VBIT_FALSE;
    }

    /* Stamp the message */
    message.sender_id = endpoint->endpoint_id;
    /* message.send_chronon would be set from system clock */

    /*
     * VOID PROPAGATION: Compute and store VOID capability tracking
     * before enqueuing. This enables the receiver to know which
     * capabilities are VOID and trace their causality.
     */
    message.void_cap_mask = compute_void_cap_mask(&message);
    message.void_cap_count = count_void_caps(message.void_cap_mask);

    /* If message contains VOID caps, record for archaeology */
    if (message.void_cap_count > 0 && message.void_id == 0) {
        message.void_id = whisper_record_void(
            SERAPH_VOID_REASON_VOID_CAP_IN_MSG,
            0, endpoint->endpoint_id, message.message_id,
            "message contains void capabilities"
        );
    }

    /* Enqueue */
    uint32_t head = atomic_load(&endpoint->send_head);
    endpoint->send_queue[head & SERAPH_WHISPER_QUEUE_MASK] = message;
    atomic_store(&endpoint->send_head, head + 1);

    /* Update statistics */
    atomic_fetch_add(&endpoint->total_sent, 1);
    atomic_store(&endpoint->last_activity, 0);  /* Would be current chronon */

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_whisper_grant(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap
) {
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_GRANT);
    seraph_whisper_message_add_cap(&msg, cap);
    return seraph_whisper_send(endpoint, msg);
}

Seraph_Vbit seraph_whisper_lend(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    Seraph_Chronon timeout
) {
    if (!endpoint_is_valid(endpoint)) {
        return SERAPH_VBIT_VOID;
    }

    /* Create the LEND message */
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_LEND);
    msg.lend_timeout = (uint32_t)timeout;
    msg.flags |= SERAPH_WHISPER_FLAG_BORROWED;
    seraph_whisper_message_add_cap(&msg, cap);

    /* Register this lend in the lend registry BEFORE sending */
    /* The borrowed cap is the same as original (could be restricted in future) */
    Seraph_Chronon current_chronon = 0;  /* Would be seraph_chronon_now() in full impl */
    Seraph_Vbit registered = lend_registry_create(
        endpoint,
        cap,          /* original cap */
        cap,          /* borrowed cap (same for now) */
        msg.message_id,
        current_chronon,
        timeout,
        0             /* borrower_endpoint_id filled when received */
    );

    if (!seraph_vbit_is_true(registered)) {
        /* Lend registry full */
        return SERAPH_VBIT_FALSE;
    }

    /* Now send the message */
    Seraph_Vbit sent = seraph_whisper_send(endpoint, msg);
    if (!seraph_vbit_is_true(sent)) {
        /* Failed to send - remove the lend record */
        int32_t slot = lend_registry_find_by_id(endpoint, msg.message_id);
        if (slot >= 0) {
            endpoint->lend_registry[slot].status = SERAPH_LEND_STATUS_VOID;
            atomic_fetch_sub(&endpoint->active_lend_count, 1);
        }
        return sent;
    }

    return SERAPH_VBIT_TRUE;
}

uint64_t seraph_whisper_request(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability* caps,
    uint8_t cap_count,
    uint16_t flags
) {
    if (!endpoint_is_valid(endpoint) || caps == NULL) {
        return SERAPH_VOID_U64;
    }

    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_REQUEST);
    msg.flags = flags | SERAPH_WHISPER_FLAG_REPLY_REQ;

    for (uint8_t i = 0; i < cap_count && i < SERAPH_WHISPER_MAX_CAPS; i++) {
        seraph_whisper_message_add_cap(&msg, caps[i]);
    }

    Seraph_Vbit sent = seraph_whisper_send(endpoint, msg);
    if (!seraph_vbit_is_true(sent)) {
        return SERAPH_VOID_U64;
    }

    return msg.message_id;
}

Seraph_Vbit seraph_whisper_respond(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t request_id,
    Seraph_Capability* caps,
    uint8_t cap_count
) {
    if (!endpoint_is_valid(endpoint)) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_RESPONSE);

    /* Store the request ID we're responding to in the message's sender_id field
     * (In a full implementation, we'd have a dedicated response_to field) */
    msg.sender_id = request_id;

    for (uint8_t i = 0; i < cap_count && i < SERAPH_WHISPER_MAX_CAPS; i++) {
        seraph_whisper_message_add_cap(&msg, caps[i]);
    }

    return seraph_whisper_send(endpoint, msg);
}

Seraph_Vbit seraph_whisper_notify(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability* caps,
    uint8_t cap_count
) {
    if (!endpoint_is_valid(endpoint)) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION);

    for (uint8_t i = 0; i < cap_count && i < SERAPH_WHISPER_MAX_CAPS; i++) {
        seraph_whisper_message_add_cap(&msg, caps[i]);
    }

    return seraph_whisper_send(endpoint, msg);
}

/*============================================================================
 * Receive Operations
 *============================================================================*/

Seraph_Whisper_Message seraph_whisper_recv(
    Seraph_Whisper_Endpoint* endpoint,
    bool blocking
) {
    /* Check endpoint with VOID tracking */
    if (endpoint == NULL) {
        Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;
        void_msg.void_id = whisper_record_void(
            SERAPH_VOID_REASON_NULL_PTR,
            0, 0, 0,
            "null endpoint in recv"
        );
        return void_msg;
    }

    if (!endpoint_is_valid(endpoint)) {
        Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;
        void_msg.void_id = whisper_record_void(
            SERAPH_VOID_REASON_ENDPOINT_DEAD,
            0, endpoint->endpoint_id, 0,
            "endpoint disconnected in recv"
        );
        return void_msg;
    }

    /* Non-blocking mode: return immediately if empty */
    if (!blocking && recv_queue_empty(endpoint)) {
        Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;
        void_msg.void_id = whisper_record_void(
            SERAPH_VOID_REASON_CHANNEL_EMPTY,
            0, endpoint->endpoint_id, 0,
            "receive queue empty (non-blocking)"
        );
        return void_msg;
    }

    /* Blocking mode: spin-wait for message (simple implementation)
     * A real implementation would use futex/condvar */
    while (recv_queue_empty(endpoint)) {
        if (!endpoint_is_valid(endpoint)) {
            Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;
            void_msg.void_id = whisper_record_void(
                SERAPH_VOID_REASON_ENDPOINT_DEAD,
                0, endpoint->endpoint_id, 0,
                "endpoint died while waiting"
            );
            return void_msg;
        }
        /* Yield CPU - platform specific */
        #if defined(_WIN32)
        /* Windows yield */
        #else
        /* POSIX yield - sched_yield() */
        #endif
    }

    /* Dequeue */
    uint32_t tail = atomic_load(&endpoint->recv_tail);
    Seraph_Whisper_Message msg = endpoint->recv_queue[tail & SERAPH_WHISPER_QUEUE_MASK];
    atomic_store(&endpoint->recv_tail, tail + 1);

    /* Update statistics */
    atomic_fetch_add(&endpoint->total_received, 1);
    atomic_store(&endpoint->last_activity, 0);  /* Would be current chronon */

    /*
     * VOID PROPAGATION: If the received message contains VOID values,
     * update thread-local state for archaeology access.
     */
    if (msg.void_id != 0 && msg.void_id != SERAPH_VOID_U64) {
        g_last_whisper_void_id = msg.void_id;
        g_last_void_endpoint_id = msg.sender_id;
        g_last_void_message_id = msg.message_id;
    }

    return msg;
}

Seraph_Whisper_Message seraph_whisper_peek(Seraph_Whisper_Endpoint* endpoint) {
    /* VOID-safe peek with tracking */
    if (endpoint == NULL) {
        Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;
        void_msg.void_id = whisper_record_void(
            SERAPH_VOID_REASON_NULL_PTR,
            0, 0, 0,
            "null endpoint in peek"
        );
        return void_msg;
    }

    if (!endpoint_is_valid(endpoint)) {
        Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;
        void_msg.void_id = whisper_record_void(
            SERAPH_VOID_REASON_ENDPOINT_DEAD,
            0, endpoint->endpoint_id, 0,
            "endpoint disconnected in peek"
        );
        return void_msg;
    }

    if (recv_queue_empty(endpoint)) {
        Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;
        void_msg.void_id = whisper_record_void(
            SERAPH_VOID_REASON_CHANNEL_EMPTY,
            0, endpoint->endpoint_id, 0,
            "receive queue empty in peek"
        );
        return void_msg;
    }

    uint32_t tail = atomic_load(&endpoint->recv_tail);
    return endpoint->recv_queue[tail & SERAPH_WHISPER_QUEUE_MASK];
}

Seraph_Vbit seraph_whisper_available(Seraph_Whisper_Endpoint* endpoint) {
    if (!endpoint_is_valid(endpoint)) {
        return SERAPH_VBIT_VOID;
    }

    return recv_queue_empty(endpoint) ? SERAPH_VBIT_FALSE : SERAPH_VBIT_TRUE;
}

uint32_t seraph_whisper_pending_count(Seraph_Whisper_Endpoint* endpoint) {
    if (endpoint == NULL) {
        return 0;
    }
    return recv_queue_depth(endpoint);
}

Seraph_Whisper_Message seraph_whisper_await_response(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t request_id,
    uint32_t max_wait
) {
    if (!endpoint_is_valid(endpoint) || request_id == SERAPH_VOID_U64) {
        return SERAPH_WHISPER_MESSAGE_VOID;
    }

    uint32_t count = 0;
    uint32_t tail = atomic_load(&endpoint->recv_tail);
    uint32_t head = atomic_load(&endpoint->recv_head);

    /* Scan the receive queue for the matching response */
    while (tail != head && (max_wait == 0 || count < max_wait)) {
        Seraph_Whisper_Message* msg = &endpoint->recv_queue[tail & SERAPH_WHISPER_QUEUE_MASK];
        if (msg->type == SERAPH_WHISPER_RESPONSE) {
            /* In a full implementation, we'd match on a stored request_id field */
            /* For now, just return the first response */
            Seraph_Whisper_Message result = *msg;

            /* Remove from queue by shifting (simple but inefficient)
             * A real implementation would use a different approach */
            atomic_store(&endpoint->recv_tail, tail + 1);
            atomic_fetch_add(&endpoint->total_received, 1);

            return result;
        }
        tail++;
        count++;
    }

    return SERAPH_WHISPER_MESSAGE_VOID;
}

/*============================================================================
 * Return Operations
 *============================================================================*/

Seraph_Vbit seraph_whisper_return_cap(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap
) {
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_RETURN);
    seraph_whisper_message_add_cap(&msg, cap);
    return seraph_whisper_send(endpoint, msg);
}

Seraph_Vbit seraph_whisper_return_cap_by_id(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    uint64_t lend_message_id
) {
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_RETURN);
    /* Store the original lend message ID in the reserved field for matching */
    /* We use the first 8 bytes of _reserved to store the lend_message_id */
    memcpy(msg._reserved, &lend_message_id, sizeof(uint64_t));
    seraph_whisper_message_add_cap(&msg, cap);
    return seraph_whisper_send(endpoint, msg);
}

/*============================================================================
 * Lend Management Implementation
 *============================================================================*/

uint32_t seraph_whisper_process_lends(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Chronon current_chronon
) {
    if (endpoint == NULL) {
        return 0;
    }

    uint32_t expired_count = 0;

    for (uint32_t i = 0; i < SERAPH_WHISPER_MAX_LENDS; i++) {
        Seraph_Whisper_Lend_Record* record = &endpoint->lend_registry[i];

        /* Only process ACTIVE lends */
        if (record->status != SERAPH_LEND_STATUS_ACTIVE) {
            continue;
        }

        /* Check if expired (expiry_chronon of 0 means never expires) */
        if (record->expiry_chronon > 0 && current_chronon >= record->expiry_chronon) {
            /* Mark as expired */
            record->status = SERAPH_LEND_STATUS_EXPIRED;
            atomic_fetch_sub(&endpoint->active_lend_count, 1);
            atomic_fetch_add(&endpoint->total_expirations, 1);
            expired_count++;

            /*
             * In a full implementation, this would:
             * 1. Invalidate the borrower's capability (increment generation)
             * 2. Restore lender's access to the original cap
             * 3. Potentially send a notification to the borrower
             *
             * For now, the record status change is the primary mechanism.
             * The borrower would check lend validity before using caps.
             */
        }
    }

    return expired_count;
}

Seraph_Vbit seraph_whisper_revoke_lend(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
) {
    if (endpoint == NULL || lend_message_id == SERAPH_VOID_U64) {
        return SERAPH_VBIT_VOID;
    }

    int32_t slot = lend_registry_find_by_id(endpoint, lend_message_id);
    if (slot < 0) {
        return SERAPH_VBIT_FALSE;  /* Not found */
    }

    Seraph_Whisper_Lend_Record* record = &endpoint->lend_registry[slot];

    /* Can only revoke ACTIVE lends */
    if (record->status != SERAPH_LEND_STATUS_ACTIVE) {
        return SERAPH_VBIT_FALSE;
    }

    /* Mark as revoked */
    record->status = SERAPH_LEND_STATUS_REVOKED;
    atomic_fetch_sub(&endpoint->active_lend_count, 1);
    atomic_fetch_add(&endpoint->total_revocations, 1);

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_whisper_lend_is_active(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
) {
    if (endpoint == NULL || lend_message_id == SERAPH_VOID_U64) {
        return SERAPH_VBIT_VOID;
    }

    int32_t slot = lend_registry_find_by_id(endpoint, lend_message_id);
    if (slot < 0) {
        return SERAPH_VBIT_VOID;  /* Not found */
    }

    return (endpoint->lend_registry[slot].status == SERAPH_LEND_STATUS_ACTIVE)
        ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

Seraph_Whisper_Lend_Record* seraph_whisper_get_lend_record(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
) {
    if (endpoint == NULL || lend_message_id == SERAPH_VOID_U64) {
        return NULL;
    }

    int32_t slot = lend_registry_find_by_id(endpoint, lend_message_id);
    if (slot < 0) {
        return NULL;
    }

    return &endpoint->lend_registry[slot];
}

uint32_t seraph_whisper_active_lend_count(Seraph_Whisper_Endpoint* endpoint) {
    if (endpoint == NULL) {
        return 0;
    }
    return atomic_load(&endpoint->active_lend_count);
}

Seraph_Vbit seraph_whisper_handle_return(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Whisper_Message* return_msg
) {
    if (endpoint == NULL || return_msg == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (return_msg->type != SERAPH_WHISPER_RETURN) {
        return SERAPH_VBIT_VOID;
    }

    /*
     * Try to find the lend record to update.
     * First, check if lend_message_id is stored in _reserved field.
     * If not, try to match by capability.
     */
    uint64_t lend_message_id = 0;
    memcpy(&lend_message_id, return_msg->_reserved, sizeof(uint64_t));

    int32_t slot = -1;

    if (lend_message_id != 0 && lend_message_id != SERAPH_VOID_U64) {
        /* Match by message ID */
        slot = lend_registry_find_by_id(endpoint, lend_message_id);
    }

    if (slot < 0 && return_msg->cap_count > 0) {
        /* Try to match by capability base address */
        Seraph_Capability returned_cap = return_msg->caps[0];
        for (uint32_t i = 0; i < SERAPH_WHISPER_MAX_LENDS; i++) {
            Seraph_Whisper_Lend_Record* record = &endpoint->lend_registry[i];
            if (record->status == SERAPH_LEND_STATUS_ACTIVE) {
                /* Match by base address of the borrowed cap */
                if (record->borrowed_cap.base == returned_cap.base) {
                    slot = (int32_t)i;
                    break;
                }
            }
        }
    }

    if (slot < 0) {
        return SERAPH_VBIT_FALSE;  /* Lend not found */
    }

    Seraph_Whisper_Lend_Record* record = &endpoint->lend_registry[slot];

    /* Can only return ACTIVE lends */
    if (record->status != SERAPH_LEND_STATUS_ACTIVE) {
        return SERAPH_VBIT_FALSE;
    }

    /* Mark as returned */
    record->status = SERAPH_LEND_STATUS_RETURNED;
    atomic_fetch_sub(&endpoint->active_lend_count, 1);
    atomic_fetch_add(&endpoint->total_returns, 1);

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Statistics
 *============================================================================*/

Seraph_Whisper_Stats seraph_whisper_get_stats(Seraph_Whisper_Endpoint* endpoint) {
    Seraph_Whisper_Stats stats = {0};

    if (endpoint == NULL) {
        return stats;
    }

    stats.total_sent = atomic_load(&endpoint->total_sent);
    stats.total_received = atomic_load(&endpoint->total_received);
    stats.total_dropped = atomic_load(&endpoint->total_dropped);
    stats.send_queue_depth = send_queue_depth(endpoint);
    stats.recv_queue_depth = recv_queue_depth(endpoint);
    stats.connected = seraph_vbit_is_true(atomic_load(&endpoint->connected));

    return stats;
}

/*============================================================================
 * Message Transfer
 *============================================================================*/

uint32_t seraph_whisper_channel_transfer(Seraph_Whisper_Channel* channel) {
    if (channel == NULL || !seraph_whisper_channel_is_active(channel)) {
        return 0;
    }

    uint32_t transferred = 0;

    /*
     * Transfer parent→child:
     * Move messages from parent's send queue to child's recv queue
     */
    Seraph_Whisper_Endpoint* parent = &channel->parent_end;
    Seraph_Whisper_Endpoint* child = &channel->child_end;

    while (!recv_queue_empty(parent) ||
           (atomic_load(&parent->send_head) != atomic_load(&parent->send_tail))) {
        /* Check if parent has messages to send */
        uint32_t p_head = atomic_load(&parent->send_head);
        uint32_t p_tail = atomic_load(&parent->send_tail);

        if (p_head == p_tail) break;  /* Parent send queue empty */

        /* Check if child recv queue has space */
        uint32_t c_head = atomic_load(&child->recv_head);
        uint32_t c_tail = atomic_load(&child->recv_tail);
        if (((c_head - c_tail) & SERAPH_WHISPER_QUEUE_MASK) >= (SERAPH_WHISPER_QUEUE_SIZE - 1)) {
            break;  /* Child recv queue full */
        }

        /* Get the message being transferred */
        Seraph_Whisper_Message* msg = &parent->send_queue[p_tail & SERAPH_WHISPER_QUEUE_MASK];

        /*
         * LEND SEMANTICS:
         * When transferring a LEND message from parent to child, update the
         * lend record with the borrower's endpoint ID.
         */
        if (msg->type == SERAPH_WHISPER_LEND) {
            int32_t slot = lend_registry_find_by_id(parent, msg->message_id);
            if (slot >= 0) {
                parent->lend_registry[slot].borrower_endpoint_id = child->endpoint_id;
            }
        }

        /*
         * RETURN SEMANTICS (parent returning to child):
         * When a RETURN message arrives at the child, process it immediately
         * to update the child's lend registry.
         */
        if (msg->type == SERAPH_WHISPER_RETURN) {
            seraph_whisper_handle_return(child, msg);
        }

        /* Transfer message */
        child->recv_queue[c_head & SERAPH_WHISPER_QUEUE_MASK] = *msg;
        atomic_store(&child->recv_head, c_head + 1);
        atomic_store(&parent->send_tail, p_tail + 1);
        transferred++;
    }

    /*
     * Transfer child→parent:
     * Move messages from child's send queue to parent's recv queue
     */
    while (atomic_load(&child->send_head) != atomic_load(&child->send_tail)) {
        uint32_t c_head = atomic_load(&child->send_head);
        uint32_t c_tail = atomic_load(&child->send_tail);

        if (c_head == c_tail) break;  /* Child send queue empty */

        /* Check if parent recv queue has space */
        uint32_t p_head = atomic_load(&parent->recv_head);
        uint32_t p_tail = atomic_load(&parent->recv_tail);
        if (((p_head - p_tail) & SERAPH_WHISPER_QUEUE_MASK) >= (SERAPH_WHISPER_QUEUE_SIZE - 1)) {
            break;  /* Parent recv queue full */
        }

        /* Get the message being transferred */
        Seraph_Whisper_Message* msg = &child->send_queue[c_tail & SERAPH_WHISPER_QUEUE_MASK];

        /*
         * RETURN SEMANTICS:
         * When a RETURN message arrives at the parent, process it immediately
         * to update the lend registry and restore the lender's access.
         */
        if (msg->type == SERAPH_WHISPER_RETURN) {
            seraph_whisper_handle_return(parent, msg);
        }

        /*
         * LEND SEMANTICS (child lending to parent):
         * Same handling as parent→child.
         */
        if (msg->type == SERAPH_WHISPER_LEND) {
            int32_t slot = lend_registry_find_by_id(child, msg->message_id);
            if (slot >= 0) {
                child->lend_registry[slot].borrower_endpoint_id = parent->endpoint_id;
            }
        }

        /* Transfer message */
        parent->recv_queue[p_head & SERAPH_WHISPER_QUEUE_MASK] = *msg;
        atomic_store(&parent->recv_head, p_head + 1);
        atomic_store(&child->send_tail, c_tail + 1);
        transferred++;
    }

    /*
     * Also process RETURN messages going from parent to child.
     * This handles the case where parent sent a RETURN (borrowed from child).
     */
    /* Check the messages just transferred to child for RETURN type */
    /* (Already handled in the first loop implicitly via the message copy,
     *  but we need to call handle_return on child for parent→child RETURN) */

    return transferred;
}

/*============================================================================
 * VOID-Safe Whisper Operations Implementation
 *
 * These functions provide full VOID causality tracking for debugging
 * through Void Archaeology.
 *============================================================================*/

Seraph_Whisper_Message seraph_whisper_message_void_with_reason(
    Seraph_VoidReason reason,
    uint64_t predecessor,
    uint64_t endpoint_id,
    const char* message
) {
    Seraph_Whisper_Message void_msg = SERAPH_WHISPER_MESSAGE_VOID;

    /* Record the VOID with causality tracking */
    void_msg.void_id = whisper_record_void(
        reason,
        predecessor,
        endpoint_id,
        0,  /* No message ID yet */
        message ? message : "void message created"
    );

    return void_msg;
}

Seraph_Vbit seraph_whisper_message_has_void_caps(const Seraph_Whisper_Message* msg) {
    if (msg == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* If message itself is VOID, return VOID */
    if (seraph_whisper_message_is_void(*msg)) {
        return SERAPH_VBIT_VOID;
    }

    /* Check each capability */
    for (uint8_t i = 0; i < msg->cap_count && i < SERAPH_WHISPER_MAX_CAPS; i++) {
        if (cap_is_void(msg->caps[i])) {
            return SERAPH_VBIT_TRUE;
        }
    }

    return SERAPH_VBIT_FALSE;
}

uint8_t seraph_whisper_message_get_void_mask(const Seraph_Whisper_Message* msg) {
    if (msg == NULL || seraph_whisper_message_is_void(*msg)) {
        return 0xFF;  /* All bits set indicates message itself is VOID */
    }

    return compute_void_cap_mask(msg);
}

void seraph_whisper_message_update_void_tracking(Seraph_Whisper_Message* msg) {
    if (msg == NULL) return;

    /* Recompute VOID capability mask */
    msg->void_cap_mask = compute_void_cap_mask(msg);
    msg->void_cap_count = count_void_caps(msg->void_cap_mask);

    /* If we now have VOID caps but no void_id, record it */
    if (msg->void_cap_count > 0 && msg->void_id == 0) {
        msg->void_id = whisper_record_void(
            SERAPH_VOID_REASON_VOID_CAP_IN_MSG,
            0,
            msg->sender_id,
            msg->message_id,
            "void tracking updated - void caps detected"
        );
    }
}

Seraph_Vbit seraph_whisper_send_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Whisper_Message message,
    uint64_t predecessor_void_id
) {
    /* Check endpoint validity with VOID tracking */
    if (endpoint == NULL) {
        whisper_record_void_loc(
            SERAPH_VOID_REASON_NULL_PTR,
            predecessor_void_id,
            0, message.message_id,
            __FILE__, __func__, __LINE__,
            "null endpoint in tracked send"
        );
        return SERAPH_VBIT_VOID;
    }

    if (!endpoint_is_valid(endpoint)) {
        whisper_record_void_loc(
            SERAPH_VOID_REASON_ENDPOINT_DEAD,
            predecessor_void_id,
            endpoint->endpoint_id, message.message_id,
            __FILE__, __func__, __LINE__,
            "endpoint disconnected in tracked send"
        );
        return SERAPH_VBIT_VOID;
    }

    if (send_queue_full(endpoint)) {
        whisper_record_void_loc(
            SERAPH_VOID_REASON_CHANNEL_FULL,
            predecessor_void_id,
            endpoint->endpoint_id, message.message_id,
            __FILE__, __func__, __LINE__,
            "send queue full in tracked send"
        );
        atomic_fetch_add(&endpoint->total_dropped, 1);
        return SERAPH_VBIT_FALSE;
    }

    /* Stamp the message */
    message.sender_id = endpoint->endpoint_id;

    /* Update VOID tracking with predecessor chain */
    message.void_cap_mask = compute_void_cap_mask(&message);
    message.void_cap_count = count_void_caps(message.void_cap_mask);

    if (message.void_cap_count > 0 || predecessor_void_id != 0) {
        /* Record with predecessor for causality chain */
        message.void_id = whisper_record_void_loc(
            SERAPH_VOID_REASON_VOID_CAP_IN_MSG,
            predecessor_void_id,
            endpoint->endpoint_id, message.message_id,
            __FILE__, __func__, __LINE__,
            "tracked send with void propagation"
        );
    }

    /* Enqueue */
    uint32_t head = atomic_load(&endpoint->send_head);
    endpoint->send_queue[head & SERAPH_WHISPER_QUEUE_MASK] = message;
    atomic_store(&endpoint->send_head, head + 1);

    atomic_fetch_add(&endpoint->total_sent, 1);
    atomic_store(&endpoint->last_activity, 0);

    return SERAPH_VBIT_TRUE;
}

Seraph_Whisper_Message seraph_whisper_recv_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    bool blocking,
    uint64_t* out_void_id
) {
    /* Initialize output */
    if (out_void_id) *out_void_id = 0;

    /* Use the base recv which now has VOID tracking */
    Seraph_Whisper_Message msg = seraph_whisper_recv(endpoint, blocking);

    /* Extract VOID ID if message carries VOID */
    if (out_void_id && msg.void_id != 0 && msg.void_id != SERAPH_VOID_U64) {
        *out_void_id = msg.void_id;
    }

    return msg;
}

Seraph_Whisper_Message seraph_whisper_peek_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t* out_void_id
) {
    /* Initialize output */
    if (out_void_id) *out_void_id = 0;

    /* Use the base peek which now has VOID tracking */
    Seraph_Whisper_Message msg = seraph_whisper_peek(endpoint);

    /* Extract VOID ID if message carries VOID */
    if (out_void_id && msg.void_id != 0 && msg.void_id != SERAPH_VOID_U64) {
        *out_void_id = msg.void_id;
    }

    return msg;
}

void seraph_whisper_print_void_chain(uint64_t void_id) {
#ifdef SERAPH_KERNEL
    /* Kernel mode: no console output */
    (void)void_id;
#else
    if (void_id == 0 || void_id == SERAPH_VOID_U64) {
        fprintf(stderr, "=== Whisper VOID Archaeology: No valid VOID ID ===\n");
        return;
    }

    fprintf(stderr, "=== Whisper VOID Archaeology for ID %llu ===\n",
            (unsigned long long)void_id);
    fprintf(stderr, "Last Whisper context:\n");
    fprintf(stderr, "  Endpoint ID: %llu\n", (unsigned long long)g_last_void_endpoint_id);
    fprintf(stderr, "  Message ID: %llu\n", (unsigned long long)g_last_void_message_id);
    fprintf(stderr, "\nFull causality chain:\n");

    /* Use the generic void chain printer */
    seraph_void_print_chain(void_id);
#endif
}

Seraph_VoidContext seraph_whisper_last_void(void) {
    if (g_last_whisper_void_id == 0) {
        return SERAPH_VOID_CONTEXT_NONE;
    }
    return seraph_void_lookup(g_last_whisper_void_id);
}

Seraph_Capability seraph_whisper_cap_void_with_reason(
    Seraph_VoidReason reason,
    uint64_t endpoint_id,
    uint64_t message_id,
    uint8_t cap_index
) {
    /* Record the VOID with Whisper context */
    char msg_buf[64];
#ifdef SERAPH_KERNEL
    /* Simple formatting for kernel mode */
    msg_buf[0] = 'c';
    msg_buf[1] = 'a';
    msg_buf[2] = 'p';
    msg_buf[3] = '[';
    msg_buf[4] = '0' + (cap_index % 10);
    msg_buf[5] = ']';
    msg_buf[6] = '\0';
#else
    snprintf(msg_buf, sizeof(msg_buf), "void cap at index %u", cap_index);
#endif

    whisper_record_void(
        reason,
        0,
        endpoint_id,
        message_id,
        msg_buf
    );

    return SERAPH_CAP_VOID;
}

Seraph_Vbit seraph_whisper_grant_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    uint64_t predecessor_void_id
) {
    /* Check if capability is already VOID */
    if (cap_is_void(cap)) {
        /* Propagate the VOID with tracking */
        Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_GRANT);
        seraph_whisper_message_add_cap(&msg, cap);
        msg.void_id = whisper_record_void(
            SERAPH_VOID_REASON_VOID_CAP_IN_MSG,
            predecessor_void_id,
            endpoint ? endpoint->endpoint_id : 0,
            msg.message_id,
            "granting void capability"
        );
        return seraph_whisper_send_tracked(endpoint, msg, msg.void_id);
    }

    /* Normal grant path */
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_GRANT);
    seraph_whisper_message_add_cap(&msg, cap);
    return seraph_whisper_send_tracked(endpoint, msg, predecessor_void_id);
}

Seraph_Vbit seraph_whisper_lend_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    Seraph_Chronon timeout,
    uint64_t predecessor_void_id
) {
    if (endpoint == NULL) {
        whisper_record_void(
            SERAPH_VOID_REASON_NULL_PTR,
            predecessor_void_id,
            0, 0,
            "null endpoint in tracked lend"
        );
        return SERAPH_VBIT_VOID;
    }

    if (!endpoint_is_valid(endpoint)) {
        whisper_record_void(
            SERAPH_VOID_REASON_ENDPOINT_DEAD,
            predecessor_void_id,
            endpoint->endpoint_id, 0,
            "endpoint dead in tracked lend"
        );
        return SERAPH_VBIT_VOID;
    }

    /* Check if capability is VOID */
    if (cap_is_void(cap)) {
        whisper_record_void(
            SERAPH_VOID_REASON_VOID_CAP_IN_MSG,
            predecessor_void_id,
            endpoint->endpoint_id, 0,
            "lending void capability"
        );
        /* Still proceed to send - receiver will see the VOID */
    }

    /* Create LEND message */
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_LEND);
    msg.lend_timeout = (uint32_t)timeout;
    msg.flags |= SERAPH_WHISPER_FLAG_BORROWED;
    seraph_whisper_message_add_cap(&msg, cap);

    /* Register lend with VOID awareness */
    Seraph_Chronon current_chronon = 0;
    Seraph_Vbit registered = lend_registry_create(
        endpoint,
        cap,
        cap,
        msg.message_id,
        current_chronon,
        timeout,
        0
    );

    if (!seraph_vbit_is_true(registered)) {
        whisper_record_void(
            SERAPH_VOID_REASON_LEND_REGISTRY_FULL,
            predecessor_void_id,
            endpoint->endpoint_id, msg.message_id,
            "lend registry full"
        );
        return SERAPH_VBIT_FALSE;
    }

    /* Send with tracking */
    Seraph_Vbit sent = seraph_whisper_send_tracked(endpoint, msg, predecessor_void_id);
    if (!seraph_vbit_is_true(sent)) {
        /* Clean up lend record on send failure */
        int32_t slot = lend_registry_find_by_id(endpoint, msg.message_id);
        if (slot >= 0) {
            endpoint->lend_registry[slot].status = SERAPH_LEND_STATUS_VOID;
            atomic_fetch_sub(&endpoint->active_lend_count, 1);
        }
        return sent;
    }

    return SERAPH_VBIT_TRUE;
}

uint32_t seraph_whisper_process_lends_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Chronon current_chronon,
    uint64_t* out_void_ids,
    uint32_t max_void_ids
) {
    if (endpoint == NULL) {
        return 0;
    }

    uint32_t expired_count = 0;
    uint32_t void_idx = 0;

    for (uint32_t i = 0; i < SERAPH_WHISPER_MAX_LENDS; i++) {
        Seraph_Whisper_Lend_Record* record = &endpoint->lend_registry[i];

        if (record->status != SERAPH_LEND_STATUS_ACTIVE) {
            continue;
        }

        /* Check expiry */
        if (record->expiry_chronon > 0 && current_chronon >= record->expiry_chronon) {
            record->status = SERAPH_LEND_STATUS_EXPIRED;
            atomic_fetch_sub(&endpoint->active_lend_count, 1);
            atomic_fetch_add(&endpoint->total_expirations, 1);
            expired_count++;

            /* Record VOID with causality tracking */
            uint64_t void_id = whisper_record_void(
                SERAPH_VOID_REASON_LEND_EXPIRED,
                0,
                endpoint->endpoint_id,
                record->lend_message_id,
                "lend expired"
            );

            /* Store VOID ID if output array provided */
            if (out_void_ids && void_idx < max_void_ids) {
                out_void_ids[void_idx++] = void_id;
            }
        }
    }

    return expired_count;
}

Seraph_Vbit seraph_whisper_revoke_lend_tracked(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id,
    uint64_t* out_void_id
) {
    if (out_void_id) *out_void_id = 0;

    if (endpoint == NULL || lend_message_id == SERAPH_VOID_U64) {
        uint64_t void_id = whisper_record_void(
            SERAPH_VOID_REASON_INVALID_ARG,
            0, 0, lend_message_id,
            "invalid args to revoke_lend"
        );
        if (out_void_id) *out_void_id = void_id;
        return SERAPH_VBIT_VOID;
    }

    int32_t slot = lend_registry_find_by_id(endpoint, lend_message_id);
    if (slot < 0) {
        uint64_t void_id = whisper_record_void(
            SERAPH_VOID_REASON_LEND_NOT_FOUND,
            0, endpoint->endpoint_id, lend_message_id,
            "lend not found for revocation"
        );
        if (out_void_id) *out_void_id = void_id;
        return SERAPH_VBIT_FALSE;
    }

    Seraph_Whisper_Lend_Record* record = &endpoint->lend_registry[slot];

    if (record->status != SERAPH_LEND_STATUS_ACTIVE) {
        uint64_t void_id = whisper_record_void(
            SERAPH_VOID_REASON_LEND_NOT_FOUND,
            0, endpoint->endpoint_id, lend_message_id,
            "lend not active for revocation"
        );
        if (out_void_id) *out_void_id = void_id;
        return SERAPH_VBIT_FALSE;
    }

    /* Mark as revoked with VOID tracking */
    record->status = SERAPH_LEND_STATUS_REVOKED;
    atomic_fetch_sub(&endpoint->active_lend_count, 1);
    atomic_fetch_add(&endpoint->total_revocations, 1);

    uint64_t void_id = whisper_record_void(
        SERAPH_VOID_REASON_LEND_REVOKED,
        0, endpoint->endpoint_id, lend_message_id,
        "lend manually revoked"
    );
    if (out_void_id) *out_void_id = void_id;

    return SERAPH_VBIT_TRUE;
}
