/**
 * @file aether_nic.c
 * @brief MC25: Aether Network Backend - DSM Protocol over Ethernet
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module implements the Aether Distributed Shared Memory protocol
 * on top of the generic NIC interface. Aether allows transparent access
 * to remote memory across a network.
 *
 * AETHER PROTOCOL:
 *
 *   Aether frames use EtherType 0x88B5 (IEEE 802.1 local experimental).
 *
 *   Frame format:
 *     [Ethernet Header (14 bytes)]
 *     [Aether Header (24 bytes)]
 *     [Payload (variable)]
 *
 *   Aether Header:
 *     magic:      4 bytes  - "AETH" (0x48544541)
 *     version:    2 bytes  - Protocol version
 *     type:       2 bytes  - Message type
 *     seq_num:    4 bytes  - Sequence number
 *     src_node:   2 bytes  - Source node ID
 *     dst_node:   2 bytes  - Destination node ID
 *     offset:     8 bytes  - Memory offset
 *
 * MESSAGE TYPES:
 *
 *   PAGE_REQUEST  (0x01): Request a page from remote node
 *   PAGE_RESPONSE (0x02): Response containing page data
 *   INVALIDATE    (0x03): Cache invalidation notification
 *   GENERATION    (0x04): Generation query/response
 *   REVOKE        (0x05): Capability revocation
 *   ACK           (0x06): Acknowledgment
 *
 * COHERENCE PROTOCOL:
 *
 *   Aether uses a directory-based coherence protocol:
 *
 *   Read:
 *     1. Requester sends PAGE_REQUEST
 *     2. Owner adds requester to sharers list
 *     3. Owner sends PAGE_RESPONSE with data
 *     4. Requester caches page in shared state
 *
 *   Write:
 *     1. Requester sends PAGE_REQUEST with write flag
 *     2. Owner sends INVALIDATE to all sharers
 *     3. Sharers acknowledge invalidation
 *     4. Owner sends PAGE_RESPONSE
 *     5. Requester has exclusive access
 */

#include "seraph/aether.h"
#include "seraph/aether_security.h"
#include "seraph/drivers/nic.h"
#include "seraph/void.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*============================================================================
 * Aether Protocol Constants
 *============================================================================*/

/** Aether magic number ("AETH" in little-endian) */
#define AETHER_MAGIC 0x48544541

/** Aether protocol version */
#define AETHER_VERSION 1

/** Message types */
typedef enum {
    AETHER_MSG_PAGE_REQUEST  = 0x01,
    AETHER_MSG_PAGE_RESPONSE = 0x02,
    AETHER_MSG_INVALIDATE    = 0x03,
    AETHER_MSG_GENERATION    = 0x04,
    AETHER_MSG_REVOKE        = 0x05,
    AETHER_MSG_ACK           = 0x06,
} Aether_Msg_Type;

/** Request flags */
#define AETHER_FLAG_WRITE    (1 << 0)
#define AETHER_FLAG_URGENT   (1 << 1)

/*============================================================================
 * Aether Frame Structures
 *============================================================================*/

/**
 * @brief Aether frame header (follows Ethernet header)
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;          /**< AETHER_MAGIC */
    uint16_t version;        /**< Protocol version */
    uint16_t type;           /**< Message type */
    uint32_t seq_num;        /**< Sequence number for ACK matching */
    uint16_t src_node;       /**< Source node ID */
    uint16_t dst_node;       /**< Destination node ID */
    uint64_t offset;         /**< Memory offset on destination node */
    uint16_t flags;          /**< Request flags */
    uint16_t data_len;       /**< Length of payload data */
    uint64_t generation;     /**< Generation for capability validation */
} Aether_Header;

_Static_assert(sizeof(Aether_Header) == 36, "Aether header must be 36 bytes");

/**
 * @brief Complete Aether frame (with Ethernet header)
 */
typedef struct __attribute__((packed)) {
    Seraph_Ethernet_Header eth;
    Aether_Header          aether;
    uint8_t                payload[];
} Aether_Frame;

/*============================================================================
 * Aether NIC State
 *============================================================================*/

/**
 * @brief Aether NIC backend state
 */
typedef struct {
    Seraph_NIC*      nic;           /**< Underlying NIC handle */
    Seraph_Aether*   aether;        /**< Aether DSM state */
    uint16_t         local_node_id; /**< Our node ID */
    uint32_t         seq_counter;   /**< Sequence number counter */
    bool             initialized;

    /* Node MAC address table (simple array for now) */
    struct {
        uint16_t node_id;
        Seraph_MAC_Address mac;
        bool valid;
    } node_macs[SERAPH_AETHER_MAX_NODES];

    /* Receive buffer */
    uint8_t rx_buffer[SERAPH_NIC_MAX_FRAME_SIZE];

    /* Security state */
#if AETHER_SECURITY_ENABLE
    Aether_Security_State security;
    bool security_enabled;
    uint64_t current_tick;          /**< Current system tick for rate limiting */
#endif

    /* Statistics */
    uint64_t frames_sent;
    uint64_t frames_received;
    uint64_t frames_rejected_security;  /**< Frames rejected by security */
    uint64_t page_requests;
    uint64_t page_responses;
    uint64_t invalidations;
    uint64_t generation_queries;
} Aether_NIC_State;

/* Global state */
static Aether_NIC_State g_aether_nic = {0};

/*============================================================================
 * Security Helper Functions
 *============================================================================*/

#if AETHER_SECURITY_ENABLE
/**
 * @brief Get current system tick for rate limiting
 *
 * In kernel context, this would use hardware timer.
 * For userspace/simulation, uses a simple counter.
 */
static uint64_t aether_get_current_tick(void) {
    /* Increment tick counter each call for simulation */
    /* In real kernel: return rdtsc() / ticks_per_ms or similar */
    return ++g_aether_nic.current_tick;
}

/**
 * @brief Log security rejection
 */
static void aether_log_security_reject(Aether_Validate_Result result,
                                        uint16_t src_node) {
    g_aether_nic.frames_rejected_security++;
    (void)result;  /* Used in debug builds */

    /* Log to debug output if available */
    #ifdef SERAPH_DEBUG
    const char* reason = "unknown";
    switch (result) {
        case AETHER_VALIDATE_MALFORMED:    reason = "malformed"; break;
        case AETHER_VALIDATE_HMAC_FAIL:    reason = "hmac_fail"; break;
        case AETHER_VALIDATE_REPLAY:       reason = "replay"; break;
        case AETHER_VALIDATE_RATE_LIMITED: reason = "rate_limited"; break;
        case AETHER_VALIDATE_PERMISSION:   reason = "permission"; break;
        case AETHER_VALIDATE_GENERATION:   reason = "generation"; break;
        default: break;
    }
    /* printf("Aether: Security reject from node %u: %s\n", src_node, reason); */
    (void)reason;
    #endif
    (void)src_node;
}
#endif /* AETHER_SECURITY_ENABLE */

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Get MAC address for a node ID
 *
 * For simplicity, we generate MACs from node IDs:
 *   02:SE:RA:PH:XX:XX  (locally administered address)
 */
static Seraph_MAC_Address aether_node_to_mac(uint16_t node_id) {
    Seraph_MAC_Address mac = {
        .bytes = {
            0x02,                    /* Locally administered */
            0x53,                    /* 'S' */
            0x45,                    /* 'E' */
            0x52,                    /* 'R' */
            (uint8_t)(node_id >> 8),
            (uint8_t)(node_id & 0xFF)
        }
    };
    return mac;
}

/**
 * @brief Extract node ID from MAC address (reverse of above)
 */
static uint16_t __attribute__((unused)) aether_mac_to_node(const Seraph_MAC_Address* mac) {
    if (mac->bytes[0] != 0x02 || mac->bytes[1] != 0x53 ||
        mac->bytes[2] != 0x45 || mac->bytes[3] != 0x52) {
        return 0xFFFF;  /* Invalid */
    }
    return ((uint16_t)mac->bytes[4] << 8) | mac->bytes[5];
}

/*============================================================================
 * Frame Construction
 *============================================================================*/

/**
 * @brief Build an Aether frame header
 */
static void aether_build_header(Aether_Frame* frame,
                                 uint16_t src_node, uint16_t dst_node,
                                 Aether_Msg_Type type,
                                 uint64_t offset, uint64_t generation,
                                 uint16_t data_len, uint16_t flags) {
    /* Ethernet header */
    frame->eth.dst = aether_node_to_mac(dst_node);
    frame->eth.src = g_aether_nic.nic ? seraph_nic_get_mac(g_aether_nic.nic)
                                       : aether_node_to_mac(src_node);
    frame->eth.ethertype = seraph_htons(SERAPH_ETHERTYPE_AETHER);

    /* Aether header */
    frame->aether.magic = AETHER_MAGIC;
    frame->aether.version = AETHER_VERSION;
    frame->aether.type = (uint16_t)type;
    frame->aether.seq_num = ++g_aether_nic.seq_counter;
    frame->aether.src_node = src_node;
    frame->aether.dst_node = dst_node;
    frame->aether.offset = offset;
    frame->aether.flags = flags;
    frame->aether.data_len = data_len;
    frame->aether.generation = generation;
}

#if AETHER_SECURITY_ENABLE
/**
 * @brief Append HMAC to frame buffer
 *
 * Computes HMAC-SHA256 over the frame and appends it.
 *
 * @param frame_buf Frame buffer (must have AETHER_HMAC_DIGEST_SIZE extra space)
 * @param frame_len Current frame length (excluding HMAC)
 * @param dst_node Destination node (for key lookup)
 * @return New frame length including HMAC, or 0 if no key
 */
static size_t aether_append_hmac(uint8_t* frame_buf, size_t frame_len,
                                  uint16_t dst_node) {
    if (!g_aether_nic.security_enabled) {
        return frame_len;  /* No HMAC when security disabled */
    }

    uint8_t hmac[AETHER_HMAC_DIGEST_SIZE];
    Seraph_Vbit result = aether_security_compute_hmac(
        &g_aether_nic.security,
        dst_node,
        frame_buf,
        frame_len,
        hmac
    );

    if (!seraph_vbit_is_true(result)) {
        /* No key for this node - send without HMAC (or reject) */
        return frame_len;
    }

    /* Append HMAC to frame */
    memcpy(frame_buf + frame_len, hmac, AETHER_HMAC_DIGEST_SIZE);
    return frame_len + AETHER_HMAC_DIGEST_SIZE;
}
#endif /* AETHER_SECURITY_ENABLE */

/*============================================================================
 * Send Operations
 *============================================================================*/

/**
 * @brief Send a page request to a remote node
 *
 * SECURITY: Appends HMAC-SHA256 when security is enabled.
 */
Seraph_Vbit seraph_aether_nic_send_page_request(uint16_t dst_node,
                                                  uint64_t offset,
                                                  uint64_t generation,
                                                  bool for_write) {
    if (!g_aether_nic.initialized) {
        return SERAPH_VBIT_VOID;
    }

    /* Build frame - allocate extra space for HMAC if needed */
#if AETHER_SECURITY_ENABLE
    uint8_t frame_buf[sizeof(Aether_Frame) + AETHER_HMAC_DIGEST_SIZE];
#else
    uint8_t frame_buf[sizeof(Aether_Frame)];
#endif
    Aether_Frame* frame = (Aether_Frame*)frame_buf;

    uint16_t flags = for_write ? AETHER_FLAG_WRITE : 0;

    aether_build_header(frame,
                        g_aether_nic.local_node_id,
                        dst_node,
                        AETHER_MSG_PAGE_REQUEST,
                        offset,
                        generation,
                        0,  /* No payload for request */
                        flags);

    size_t frame_len = sizeof(Aether_Frame);

#if AETHER_SECURITY_ENABLE
    /* Append HMAC if security enabled */
    frame_len = aether_append_hmac(frame_buf, frame_len, dst_node);
#endif

    /* Send frame */
    Seraph_Vbit result = seraph_nic_send(g_aether_nic.nic,
                                          frame_buf,
                                          frame_len);

    if (seraph_vbit_is_true(result)) {
        g_aether_nic.frames_sent++;
        g_aether_nic.page_requests++;
    }

    return result;
}

/**
 * @brief Send a page response with data
 *
 * SECURITY: Appends HMAC-SHA256 when security is enabled.
 */
Seraph_Vbit seraph_aether_nic_send_page_response(uint16_t dst_node,
                                                   uint64_t offset,
                                                   uint64_t generation,
                                                   const void* page_data,
                                                   size_t page_size) {
    if (!g_aether_nic.initialized || page_data == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Calculate frame size */
    size_t base_frame_size = sizeof(Aether_Frame) + page_size;
#if AETHER_SECURITY_ENABLE
    size_t alloc_size = base_frame_size + AETHER_HMAC_DIGEST_SIZE;
#else
    size_t alloc_size = base_frame_size;
#endif

    if (alloc_size > SERAPH_NIC_MAX_FRAME_SIZE) {
        return SERAPH_VBIT_VOID;  /* Page too large for single frame */
    }

    /* Build frame */
    uint8_t* frame_buf = malloc(alloc_size);
    if (frame_buf == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Aether_Frame* frame = (Aether_Frame*)frame_buf;

    aether_build_header(frame,
                        g_aether_nic.local_node_id,
                        dst_node,
                        AETHER_MSG_PAGE_RESPONSE,
                        offset,
                        generation,
                        (uint16_t)page_size,
                        0);

    /* Copy page data */
    memcpy(frame->payload, page_data, page_size);

    size_t frame_size = base_frame_size;

#if AETHER_SECURITY_ENABLE
    /* Append HMAC if security enabled */
    frame_size = aether_append_hmac(frame_buf, frame_size, dst_node);
#endif

    /* Send frame */
    Seraph_Vbit result = seraph_nic_send(g_aether_nic.nic, frame_buf, frame_size);

    free(frame_buf);

    if (seraph_vbit_is_true(result)) {
        g_aether_nic.frames_sent++;
        g_aether_nic.page_responses++;
    }

    return result;
}

/**
 * @brief Send an invalidation message
 *
 * SECURITY: Appends HMAC-SHA256 when security is enabled.
 */
Seraph_Vbit seraph_aether_nic_send_invalidate(uint16_t dst_node,
                                                uint64_t offset,
                                                uint64_t new_generation) {
    if (!g_aether_nic.initialized) {
        return SERAPH_VBIT_VOID;
    }

#if AETHER_SECURITY_ENABLE
    uint8_t frame_buf[sizeof(Aether_Frame) + AETHER_HMAC_DIGEST_SIZE];
#else
    uint8_t frame_buf[sizeof(Aether_Frame)];
#endif
    Aether_Frame* frame = (Aether_Frame*)frame_buf;

    aether_build_header(frame,
                        g_aether_nic.local_node_id,
                        dst_node,
                        AETHER_MSG_INVALIDATE,
                        offset,
                        new_generation,
                        0,
                        0);

    size_t frame_len = sizeof(Aether_Frame);

#if AETHER_SECURITY_ENABLE
    frame_len = aether_append_hmac(frame_buf, frame_len, dst_node);
#endif

    Seraph_Vbit result = seraph_nic_send(g_aether_nic.nic, frame_buf, frame_len);

    if (seraph_vbit_is_true(result)) {
        g_aether_nic.frames_sent++;
        g_aether_nic.invalidations++;
    }

    return result;
}

/**
 * @brief Send generation query
 *
 * SECURITY: Appends HMAC-SHA256 when security is enabled.
 */
Seraph_Vbit seraph_aether_nic_send_generation_query(uint16_t dst_node,
                                                      uint64_t offset) {
    if (!g_aether_nic.initialized) {
        return SERAPH_VBIT_VOID;
    }

#if AETHER_SECURITY_ENABLE
    uint8_t frame_buf[sizeof(Aether_Frame) + AETHER_HMAC_DIGEST_SIZE];
#else
    uint8_t frame_buf[sizeof(Aether_Frame)];
#endif
    Aether_Frame* frame = (Aether_Frame*)frame_buf;

    aether_build_header(frame,
                        g_aether_nic.local_node_id,
                        dst_node,
                        AETHER_MSG_GENERATION,
                        offset,
                        0,  /* Generation unknown */
                        0,
                        0);

    size_t frame_len = sizeof(Aether_Frame);

#if AETHER_SECURITY_ENABLE
    frame_len = aether_append_hmac(frame_buf, frame_len, dst_node);
#endif

    Seraph_Vbit result = seraph_nic_send(g_aether_nic.nic, frame_buf, frame_len);

    if (seraph_vbit_is_true(result)) {
        g_aether_nic.frames_sent++;
        g_aether_nic.generation_queries++;
    }

    return result;
}

/**
 * @brief Send acknowledgment
 *
 * SECURITY: Appends HMAC-SHA256 when security is enabled.
 */
Seraph_Vbit seraph_aether_nic_send_ack(uint16_t dst_node, uint32_t seq_num) {
    if (!g_aether_nic.initialized) {
        return SERAPH_VBIT_VOID;
    }

#if AETHER_SECURITY_ENABLE
    uint8_t frame_buf[sizeof(Aether_Frame) + AETHER_HMAC_DIGEST_SIZE];
#else
    uint8_t frame_buf[sizeof(Aether_Frame)];
#endif
    Aether_Frame* frame = (Aether_Frame*)frame_buf;

    aether_build_header(frame,
                        g_aether_nic.local_node_id,
                        dst_node,
                        AETHER_MSG_ACK,
                        0,
                        0,
                        0,
                        0);

    /* Set seq_num to match the message being acknowledged */
    frame->aether.seq_num = seq_num;

    size_t frame_len = sizeof(Aether_Frame);

#if AETHER_SECURITY_ENABLE
    frame_len = aether_append_hmac(frame_buf, frame_len, dst_node);
#endif

    Seraph_Vbit result = seraph_nic_send(g_aether_nic.nic, frame_buf, frame_len);

    if (seraph_vbit_is_true(result)) {
        g_aether_nic.frames_sent++;
    }

    return result;
}

/*============================================================================
 * Receive Operations
 *============================================================================*/

/**
 * @brief Handle received page request
 *
 * SECURITY: Validates capability generation before serving page data.
 */
static void aether_handle_page_request(const Aether_Header* hdr) {
    if (g_aether_nic.aether == NULL) {
        return;
    }

    uint16_t requester = hdr->src_node;
    uint64_t offset = hdr->offset;
    bool for_write = (hdr->flags & AETHER_FLAG_WRITE) != 0;

#if AETHER_SECURITY_ENABLE
    if (g_aether_nic.security_enabled) {
        /*
         * SECURITY CHECK: Validate generation BEFORE any memory access
         *
         * This prevents serving stale data to nodes with revoked capabilities.
         * The generation in the request must match the current generation
         * for the requested page.
         */
        Seraph_Vbit gen_valid = aether_security_check_generation(
            g_aether_nic.aether,
            offset,
            hdr->generation,
            requester
        );

        if (!seraph_vbit_is_true(gen_valid)) {
            /* Log generation failure */
            aether_security_log_event(
                &g_aether_nic.security.log,
                aether_get_current_tick(),
                requester,
                AETHER_SEC_EVENT_GENERATION_STALE,
                hdr->seq_num,
                offset,
                (uint32_t)(hdr->generation & 0xFFFFFFFF)
            );
            g_aether_nic.security.generation_failures++;

            /* Send error response or just drop - drop is safer */
            return;
        }

        /*
         * SECURITY CHECK: Verify node has permission to access this offset
         *
         * Additional check beyond the packet-level permission check.
         * Could implement per-offset ACLs here if needed.
         */
        const Aether_Node_Permission* perm =
            aether_security_get_node_perm(&g_aether_nic.security, requester);

        if (perm == NULL || !perm->authenticated) {
            aether_security_log_event(
                &g_aether_nic.security.log,
                aether_get_current_tick(),
                requester,
                AETHER_SEC_EVENT_PERMISSION_DENIED,
                hdr->seq_num,
                offset,
                0
            );
            return;
        }

        /* Check read or write permission */
        uint8_t required = for_write ? AETHER_NODE_PERM_WRITE : AETHER_NODE_PERM_READ;
        if (!aether_node_has_perm(perm, required)) {
            aether_security_log_event(
                &g_aether_nic.security.log,
                aether_get_current_tick(),
                requester,
                AETHER_SEC_EVENT_PERMISSION_DENIED,
                hdr->seq_num,
                offset,
                required
            );
            return;
        }
    }
#endif /* AETHER_SECURITY_ENABLE */

    /* Get page data from local memory */
    Seraph_Aether_Response response = seraph_aether_handle_read_request(
        g_aether_nic.aether,
        requester,
        offset
    );

    if (response.status == SERAPH_AETHER_RESP_OK && response.page_data != NULL) {
        seraph_aether_nic_send_page_response(
            requester,
            offset,
            response.generation,
            response.page_data,
            response.data_size
        );
    }

    (void)for_write;  /* Would affect directory state */
}

/**
 * @brief Handle received page response
 */
static void aether_handle_page_response(const Aether_Header* hdr,
                                         const void* payload, size_t payload_len) {
    if (g_aether_nic.aether == NULL) {
        return;
    }

    /* Insert page into cache */
    seraph_aether_cache_insert(
        g_aether_nic.aether,
        seraph_aether_make_addr(hdr->src_node, hdr->offset),
        (void*)payload,  /* Cache will copy */
        hdr->src_node,
        hdr->generation
    );

    (void)payload_len;
}

/**
 * @brief Handle received invalidation
 */
static void aether_handle_invalidate(const Aether_Header* hdr) {
    if (g_aether_nic.aether == NULL) {
        return;
    }

    seraph_aether_handle_invalidate(
        g_aether_nic.aether,
        seraph_aether_make_addr(hdr->src_node, hdr->offset),
        hdr->generation
    );

    /* Send ACK */
    seraph_aether_nic_send_ack(hdr->src_node, hdr->seq_num);
}

/**
 * @brief Process a received Aether frame
 *
 * SECURITY HARDENED: Performs comprehensive validation before processing:
 * 1. Structural validation (bounds, magic, version, type)
 * 2. Rate limiting (before crypto to prevent DoS)
 * 3. HMAC verification (if enabled)
 * 4. Replay detection
 * 5. Permission checking
 *
 * All security checks happen BEFORE any memory access or packet processing.
 */
static void aether_process_frame(const void* frame_data, size_t frame_len) {
    /*
     * ========================================
     * SECURITY: Initial bounds check
     * ========================================
     */
    if (frame_len < sizeof(Aether_Frame)) {
        return;
    }

    const Aether_Frame* frame = (const Aether_Frame*)frame_data;

    /*
     * ========================================
     * SECURITY: Validate EtherType before anything else
     * ========================================
     */
    if (seraph_ntohs(frame->eth.ethertype) != SERAPH_ETHERTYPE_AETHER) {
        return;  /* Not an Aether frame */
    }

#if AETHER_SECURITY_ENABLE
    if (g_aether_nic.security_enabled) {
        /*
         * ========================================
         * COMPREHENSIVE SECURITY VALIDATION
         * ========================================
         *
         * This performs ALL security checks in the correct order:
         * 1. Structural validation (bounds, magic, version, type)
         * 2. Rate limiting (BEFORE crypto to prevent DoS attacks)
         * 3. HMAC verification (constant-time comparison)
         * 4. Replay detection (sliding window)
         * 5. Permission checking
         *
         * Only if ALL checks pass do we proceed to process the packet.
         */
        uint16_t src_node = 0;
        uint64_t current_tick = aether_get_current_tick();

        Aether_Validate_Result result = aether_security_validate_frame(
            &g_aether_nic.security,
            frame_data,
            frame_len,
            current_tick,
            &src_node
        );

        if (result != AETHER_VALIDATE_OK) {
            aether_log_security_reject(result, src_node);
            return;  /* Packet rejected - do not process */
        }

        /* Accept packet into replay window */
        aether_security_accept_packet(
            &g_aether_nic.security,
            frame->aether.src_node,
            frame->aether.seq_num
        );
    } else
#endif /* AETHER_SECURITY_ENABLE */
    {
        /*
         * ========================================
         * LEGACY VALIDATION (when security disabled)
         * ========================================
         *
         * Still perform basic structural checks even without
         * full security enabled.
         */

        /* Validate magic number */
        if (frame->aether.magic != AETHER_MAGIC) {
            return;
        }

        /* Validate message type is in valid range */
        if (frame->aether.type == 0 || frame->aether.type > AETHER_MSG_ACK) {
            return;
        }

        /* Validate source node ID is reasonable */
        if (frame->aether.src_node >= SERAPH_AETHER_MAX_NODES) {
            return;
        }

        /*
         * SECURITY: Validate claimed data_len vs actual frame length
         *
         * This is CRITICAL - prevents reading past end of buffer.
         * header_size + data_len must not exceed frame_len
         */
        size_t header_size = sizeof(Aether_Frame);
        if (frame->aether.data_len > frame_len - header_size) {
            return;  /* Claimed length exceeds actual data */
        }

        /* Validate offset is within addressable range */
        if (frame->aether.offset > SERAPH_AETHER_MAX_OFFSET) {
            return;
        }
    }

    /*
     * ========================================
     * DESTINATION CHECK
     * ========================================
     */
    if (frame->aether.dst_node != g_aether_nic.local_node_id &&
        frame->aether.dst_node != 0xFFFF) {  /* 0xFFFF = broadcast */
        return;  /* Not for us */
    }

    g_aether_nic.frames_received++;

    /*
     * ========================================
     * SAFE PAYLOAD EXTRACTION
     * ========================================
     *
     * At this point, data_len has been validated against frame_len,
     * so it's safe to access the payload.
     */
    size_t payload_len = frame->aether.data_len;
    const void* payload = (payload_len > 0) ? frame->payload : NULL;

    /*
     * ========================================
     * MESSAGE DISPATCH
     * ========================================
     *
     * All handlers receive validated data.
     * Generation checks happen in individual handlers where appropriate.
     */
    switch (frame->aether.type) {
        case AETHER_MSG_PAGE_REQUEST:
            aether_handle_page_request(&frame->aether);
            break;

        case AETHER_MSG_PAGE_RESPONSE:
            aether_handle_page_response(&frame->aether, payload, payload_len);
            break;

        case AETHER_MSG_INVALIDATE:
            aether_handle_invalidate(&frame->aether);
            break;

        case AETHER_MSG_GENERATION:
            /* TODO: Handle generation query/response */
            break;

        case AETHER_MSG_REVOKE:
            /* TODO: Handle revocation */
            break;

        case AETHER_MSG_ACK:
            /* TODO: Handle ACK (for reliable delivery) */
            break;

        default:
            /* Unknown message type - should not reach here after validation */
            break;
    }
}

/**
 * @brief Poll for received Aether frames
 *
 * @return Number of frames processed
 */
uint32_t seraph_aether_nic_poll(void) {
    if (!g_aether_nic.initialized || g_aether_nic.nic == NULL) {
        return 0;
    }

    uint32_t processed = 0;

    while (1) {
        size_t len = sizeof(g_aether_nic.rx_buffer);
        Seraph_Vbit result = seraph_nic_recv(g_aether_nic.nic,
                                              g_aether_nic.rx_buffer,
                                              &len);

        if (!seraph_vbit_is_true(result)) {
            break;  /* No more packets */
        }

        aether_process_frame(g_aether_nic.rx_buffer, len);
        processed++;
    }

    return processed;
}

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * @brief Initialize Aether NIC backend
 *
 * @param nic Initialized NIC handle
 * @param aether Aether DSM state (can be NULL for standalone use)
 * @param node_id Our node ID
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_aether_nic_init(Seraph_NIC* nic,
                                    Seraph_Aether* aether,
                                    uint16_t node_id) {
    if (nic == NULL || !nic->initialized) {
        return SERAPH_VBIT_VOID;
    }

    memset(&g_aether_nic, 0, sizeof(g_aether_nic));
    g_aether_nic.nic = nic;
    g_aether_nic.aether = aether;
    g_aether_nic.local_node_id = node_id;
    g_aether_nic.seq_counter = 0;
    g_aether_nic.initialized = true;

#if AETHER_SECURITY_ENABLE
    /* Initialize security subsystem */
    Seraph_Vbit sec_result = aether_security_init(&g_aether_nic.security);
    if (!seraph_vbit_is_true(sec_result)) {
        memset(&g_aether_nic, 0, sizeof(g_aether_nic));
        return SERAPH_VBIT_FALSE;
    }
    g_aether_nic.security_enabled = true;
    g_aether_nic.current_tick = 0;
#endif

    /* Set our MAC address based on node ID */
    Seraph_MAC_Address our_mac = aether_node_to_mac(node_id);
    seraph_nic_set_mac(nic, &our_mac);

    return SERAPH_VBIT_TRUE;
}

#if AETHER_SECURITY_ENABLE
/**
 * @brief Initialize Aether NIC with custom security flags
 *
 * @param nic Initialized NIC handle
 * @param aether Aether DSM state (can be NULL for standalone use)
 * @param node_id Our node ID
 * @param security_flags Security configuration flags
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_aether_nic_init_secure(Seraph_NIC* nic,
                                           Seraph_Aether* aether,
                                           uint16_t node_id,
                                           uint32_t security_flags) {
    if (nic == NULL || !nic->initialized) {
        return SERAPH_VBIT_VOID;
    }

    memset(&g_aether_nic, 0, sizeof(g_aether_nic));
    g_aether_nic.nic = nic;
    g_aether_nic.aether = aether;
    g_aether_nic.local_node_id = node_id;
    g_aether_nic.seq_counter = 0;
    g_aether_nic.initialized = true;

    /* Initialize security with custom flags */
    Seraph_Vbit sec_result = aether_security_init_flags(
        &g_aether_nic.security, security_flags);
    if (!seraph_vbit_is_true(sec_result)) {
        memset(&g_aether_nic, 0, sizeof(g_aether_nic));
        return SERAPH_VBIT_FALSE;
    }
    g_aether_nic.security_enabled = (security_flags != AETHER_SEC_FLAG_NONE);
    g_aether_nic.current_tick = 0;

    /* Set our MAC address based on node ID */
    Seraph_MAC_Address our_mac = aether_node_to_mac(node_id);
    seraph_nic_set_mac(nic, &our_mac);

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Set pre-shared key for a remote node
 *
 * @param node_id Remote node ID
 * @param key Pre-shared key (AETHER_HMAC_KEY_SIZE bytes)
 * @param permissions Permission mask for this node
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_aether_nic_set_node_key(uint16_t node_id,
                                            const uint8_t* key,
                                            uint8_t permissions) {
    if (!g_aether_nic.initialized || !g_aether_nic.security_enabled) {
        return SERAPH_VBIT_VOID;
    }

    return aether_security_set_node_key(
        &g_aether_nic.security, node_id, key, permissions);
}

/**
 * @brief Enable or disable security
 *
 * @param enable true to enable, false to disable
 */
void seraph_aether_nic_set_security(bool enable) {
    if (!g_aether_nic.initialized) return;
    g_aether_nic.security_enabled = enable;
}

/**
 * @brief Get security statistics
 */
void seraph_aether_nic_get_security_stats(uint64_t* validated,
                                           uint64_t* rejected,
                                           uint64_t* hmac_fail,
                                           uint64_t* replay,
                                           uint64_t* rate_limit) {
    if (!g_aether_nic.initialized) return;

    aether_security_get_stats(&g_aether_nic.security,
                               validated, rejected, hmac_fail,
                               replay, rate_limit, NULL);
}

/**
 * @brief Get recent security events
 *
 * @param events Output buffer
 * @param max_events Buffer size
 * @return Number of events copied
 */
uint32_t seraph_aether_nic_get_security_events(Aether_Security_Event* events,
                                                uint32_t max_events) {
    if (!g_aether_nic.initialized) return 0;

    return aether_security_log_get(&g_aether_nic.security.log,
                                    events, max_events);
}
#endif /* AETHER_SECURITY_ENABLE */

/**
 * @brief Shutdown Aether NIC backend
 */
void seraph_aether_nic_shutdown(void) {
#if AETHER_SECURITY_ENABLE
    if (g_aether_nic.security_enabled) {
        aether_security_destroy(&g_aether_nic.security);
    }
#endif
    memset(&g_aether_nic, 0, sizeof(g_aether_nic));
}

/**
 * @brief Get Aether NIC statistics
 */
void seraph_aether_nic_get_stats(uint64_t* frames_sent,
                                  uint64_t* frames_received,
                                  uint64_t* page_requests,
                                  uint64_t* page_responses,
                                  uint64_t* invalidations) {
    if (frames_sent) *frames_sent = g_aether_nic.frames_sent;
    if (frames_received) *frames_received = g_aether_nic.frames_received;
    if (page_requests) *page_requests = g_aether_nic.page_requests;
    if (page_responses) *page_responses = g_aether_nic.page_responses;
    if (invalidations) *invalidations = g_aether_nic.invalidations;
}

/**
 * @brief Get Aether NIC extended statistics including security
 */
void seraph_aether_nic_get_stats_extended(uint64_t* frames_sent,
                                           uint64_t* frames_received,
                                           uint64_t* frames_rejected,
                                           uint64_t* page_requests,
                                           uint64_t* page_responses,
                                           uint64_t* invalidations,
                                           uint64_t* generation_queries) {
    if (frames_sent) *frames_sent = g_aether_nic.frames_sent;
    if (frames_received) *frames_received = g_aether_nic.frames_received;
    if (frames_rejected) *frames_rejected = g_aether_nic.frames_rejected_security;
    if (page_requests) *page_requests = g_aether_nic.page_requests;
    if (page_responses) *page_responses = g_aether_nic.page_responses;
    if (invalidations) *invalidations = g_aether_nic.invalidations;
    if (generation_queries) *generation_queries = g_aether_nic.generation_queries;
}

/**
 * @brief Check if Aether NIC is initialized
 */
bool seraph_aether_nic_is_initialized(void) {
    return g_aether_nic.initialized;
}
