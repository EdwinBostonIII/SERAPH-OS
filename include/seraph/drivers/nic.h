/**
 * @file nic.h
 * @brief MC25: The Telepath - Generic Network Interface Controller Interface
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module defines a generic NIC driver interface using a vtable pattern.
 * Different hardware drivers (e1000, virtio-net, etc.) implement this
 * interface, allowing the Aether DSM protocol to work with any network card.
 *
 * DESIGN PHILOSOPHY:
 *
 *   1. ABSTRACTION: Upper layers (Aether) don't know about hardware
 *   2. VOID SEMANTICS: Network errors return VOID, not exceptions
 *   3. ZERO-COPY: Where possible, avoid copying packet data
 *   4. POLLING & INTERRUPTS: Support both modes of operation
 *
 * PACKET FLOW:
 *
 *   TX (Send):
 *     Aether -> seraph_nic_send() -> Driver -> Hardware -> Wire
 *
 *   RX (Receive):
 *     Wire -> Hardware -> IRQ -> Driver -> seraph_nic_recv() -> Aether
 *
 * BUFFER MANAGEMENT:
 *
 *   Drivers manage their own TX/RX descriptor rings.
 *   The generic interface passes simple buffers.
 *   A production implementation would use a more sophisticated
 *   scatter-gather interface.
 */

#ifndef SERAPH_DRIVERS_NIC_H
#define SERAPH_DRIVERS_NIC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum packet size (standard Ethernet MTU + headers) */
#define SERAPH_NIC_MTU 1500

/** Maximum Ethernet frame size (MTU + Ethernet header + CRC) */
#define SERAPH_NIC_MAX_FRAME_SIZE 1522

/** Minimum Ethernet frame size */
#define SERAPH_NIC_MIN_FRAME_SIZE 64

/** Ethernet header size */
#define SERAPH_NIC_ETH_HEADER_SIZE 14

/** MAC address length */
#define SERAPH_NIC_MAC_LEN 6

/*============================================================================
 * MAC Address
 *============================================================================*/

/**
 * @brief MAC address structure
 */
typedef struct {
    uint8_t bytes[SERAPH_NIC_MAC_LEN];
} Seraph_MAC_Address;

/** Broadcast MAC address (FF:FF:FF:FF:FF:FF) */
#define SERAPH_MAC_BROADCAST ((Seraph_MAC_Address){ \
    .bytes = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } \
})

/** Null MAC address (00:00:00:00:00:00) */
#define SERAPH_MAC_NULL ((Seraph_MAC_Address){ \
    .bytes = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } \
})

/**
 * @brief Check if MAC address is broadcast
 */
static inline bool seraph_mac_is_broadcast(const Seraph_MAC_Address* mac) {
    return mac->bytes[0] == 0xFF && mac->bytes[1] == 0xFF &&
           mac->bytes[2] == 0xFF && mac->bytes[3] == 0xFF &&
           mac->bytes[4] == 0xFF && mac->bytes[5] == 0xFF;
}

/**
 * @brief Check if MAC address is multicast
 */
static inline bool seraph_mac_is_multicast(const Seraph_MAC_Address* mac) {
    return (mac->bytes[0] & 0x01) != 0;
}

/**
 * @brief Compare two MAC addresses
 */
static inline bool seraph_mac_equal(const Seraph_MAC_Address* a,
                                     const Seraph_MAC_Address* b) {
    return a->bytes[0] == b->bytes[0] && a->bytes[1] == b->bytes[1] &&
           a->bytes[2] == b->bytes[2] && a->bytes[3] == b->bytes[3] &&
           a->bytes[4] == b->bytes[4] && a->bytes[5] == b->bytes[5];
}

/*============================================================================
 * Ethernet Header
 *============================================================================*/

/**
 * @brief Ethernet header structure
 */
typedef struct __attribute__((packed)) {
    Seraph_MAC_Address dst;      /**< Destination MAC */
    Seraph_MAC_Address src;      /**< Source MAC */
    uint16_t           ethertype; /**< EtherType (big-endian!) */
} Seraph_Ethernet_Header;

/** @name Common EtherTypes */
/**@{*/
#define SERAPH_ETHERTYPE_IPV4    0x0800
#define SERAPH_ETHERTYPE_ARP     0x0806
#define SERAPH_ETHERTYPE_IPV6    0x86DD
#define SERAPH_ETHERTYPE_VLAN    0x8100
#define SERAPH_ETHERTYPE_AETHER  0x88B5  /**< Aether DSM protocol (using IEEE 802.1 local experimental) */
/**@}*/

/*============================================================================
 * Link State
 *============================================================================*/

/**
 * @brief NIC link state
 */
typedef enum {
    SERAPH_NIC_LINK_DOWN = 0,    /**< No link */
    SERAPH_NIC_LINK_UP,          /**< Link established */
    SERAPH_NIC_LINK_UNKNOWN      /**< Unable to determine */
} Seraph_NIC_Link_State;

/**
 * @brief NIC link speed
 */
typedef enum {
    SERAPH_NIC_SPEED_UNKNOWN = 0,
    SERAPH_NIC_SPEED_10MBPS,
    SERAPH_NIC_SPEED_100MBPS,
    SERAPH_NIC_SPEED_1GBPS,
    SERAPH_NIC_SPEED_10GBPS,
    SERAPH_NIC_SPEED_25GBPS,
    SERAPH_NIC_SPEED_40GBPS,
    SERAPH_NIC_SPEED_100GBPS
} Seraph_NIC_Speed;

/**
 * @brief NIC link info
 */
typedef struct {
    Seraph_NIC_Link_State state;
    Seraph_NIC_Speed      speed;
    bool                  full_duplex;
} Seraph_NIC_Link_Info;

/*============================================================================
 * NIC Statistics
 *============================================================================*/

/**
 * @brief NIC statistics
 */
typedef struct {
    /* TX statistics */
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_errors;
    uint64_t tx_dropped;

    /* RX statistics */
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_errors;
    uint64_t rx_dropped;
    uint64_t rx_overrun;
    uint64_t rx_crc_errors;

    /* General */
    uint64_t interrupts;
    uint64_t collisions;
} Seraph_NIC_Stats;

/*============================================================================
 * NIC Driver Operations (vtable)
 *============================================================================*/

/**
 * @brief NIC driver operations vtable
 *
 * Each NIC driver implements these functions. The generic NIC handle
 * uses this vtable to dispatch operations to the appropriate driver.
 */
typedef struct Seraph_NIC_Ops {
    /**
     * @brief Initialize the NIC
     *
     * @param driver Driver-specific state
     * @return SERAPH_VBIT_TRUE on success
     */
    Seraph_Vbit (*init)(void* driver);

    /**
     * @brief Shutdown the NIC
     *
     * @param driver Driver-specific state
     */
    void (*destroy)(void* driver);

    /**
     * @brief Send a packet
     *
     * @param driver Driver-specific state
     * @param data Packet data (including Ethernet header)
     * @param len Packet length
     * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_FALSE if busy
     */
    Seraph_Vbit (*send)(void* driver, const void* data, size_t len);

    /**
     * @brief Receive a packet
     *
     * Non-blocking: returns immediately if no packet available.
     *
     * @param driver Driver-specific state
     * @param buffer Buffer to receive into
     * @param len Input: buffer size; Output: received length
     * @return SERAPH_VBIT_TRUE if packet received, SERAPH_VBIT_FALSE if none
     */
    Seraph_Vbit (*recv)(void* driver, void* buffer, size_t* len);

    /**
     * @brief Get the MAC address
     *
     * @param driver Driver-specific state
     * @return MAC address
     */
    Seraph_MAC_Address (*get_mac)(void* driver);

    /**
     * @brief Set the MAC address
     *
     * @param driver Driver-specific state
     * @param mac New MAC address
     * @return SERAPH_VBIT_TRUE on success
     */
    Seraph_Vbit (*set_mac)(void* driver, const Seraph_MAC_Address* mac);

    /**
     * @brief Get link state
     *
     * @param driver Driver-specific state
     * @return Link information
     */
    Seraph_NIC_Link_Info (*get_link)(void* driver);

    /**
     * @brief Get statistics
     *
     * @param driver Driver-specific state
     * @param stats Output statistics
     */
    void (*get_stats)(void* driver, Seraph_NIC_Stats* stats);

    /**
     * @brief Enable/disable promiscuous mode
     *
     * @param driver Driver-specific state
     * @param enable true to enable, false to disable
     * @return SERAPH_VBIT_TRUE on success
     */
    Seraph_Vbit (*set_promisc)(void* driver, bool enable);

    /**
     * @brief Add multicast address to filter
     *
     * @param driver Driver-specific state
     * @param mac Multicast address to add
     * @return SERAPH_VBIT_TRUE on success
     */
    Seraph_Vbit (*add_multicast)(void* driver, const Seraph_MAC_Address* mac);

    /**
     * @brief Remove multicast address from filter
     *
     * @param driver Driver-specific state
     * @param mac Multicast address to remove
     * @return SERAPH_VBIT_TRUE on success
     */
    Seraph_Vbit (*del_multicast)(void* driver, const Seraph_MAC_Address* mac);

    /**
     * @brief Poll for events (RX/TX completion)
     *
     * Called periodically or in polling mode.
     *
     * @param driver Driver-specific state
     * @return Number of events processed
     */
    uint32_t (*poll)(void* driver);

    /**
     * @brief Enable interrupts
     *
     * @param driver Driver-specific state
     */
    void (*enable_irq)(void* driver);

    /**
     * @brief Disable interrupts
     *
     * @param driver Driver-specific state
     */
    void (*disable_irq)(void* driver);

} Seraph_NIC_Ops;

/*============================================================================
 * Generic NIC Handle
 *============================================================================*/

/**
 * @brief Generic NIC handle
 *
 * This is the public interface to a NIC. It wraps a driver-specific
 * state pointer and a vtable of operations.
 */
typedef struct {
    void*                   driver_data;  /**< Driver-specific state */
    const Seraph_NIC_Ops*   ops;          /**< Operations vtable */
    bool                    initialized;  /**< Is NIC initialized? */
} Seraph_NIC;

/*============================================================================
 * Generic NIC API (dispatches through vtable)
 *============================================================================*/

/**
 * @brief Initialize a NIC
 */
static inline Seraph_Vbit seraph_nic_init(Seraph_NIC* nic) {
    if (nic == NULL || nic->ops == NULL || nic->ops->init == NULL) {
        return SERAPH_VBIT_VOID;
    }
    Seraph_Vbit result = nic->ops->init(nic->driver_data);
    if (seraph_vbit_is_true(result)) {
        nic->initialized = true;
    }
    return result;
}

/**
 * @brief Destroy a NIC
 */
static inline void seraph_nic_destroy(Seraph_NIC* nic) {
    if (nic != NULL && nic->ops != NULL && nic->ops->destroy != NULL) {
        nic->ops->destroy(nic->driver_data);
        nic->initialized = false;
    }
}

/**
 * @brief Send a packet
 */
static inline Seraph_Vbit seraph_nic_send(Seraph_NIC* nic,
                                           const void* data, size_t len) {
    if (nic == NULL || !nic->initialized || nic->ops->send == NULL) {
        return SERAPH_VBIT_VOID;
    }
    return nic->ops->send(nic->driver_data, data, len);
}

/**
 * @brief Receive a packet
 */
static inline Seraph_Vbit seraph_nic_recv(Seraph_NIC* nic,
                                           void* buffer, size_t* len) {
    if (nic == NULL || !nic->initialized || nic->ops->recv == NULL) {
        return SERAPH_VBIT_VOID;
    }
    return nic->ops->recv(nic->driver_data, buffer, len);
}

/**
 * @brief Get MAC address
 */
static inline Seraph_MAC_Address seraph_nic_get_mac(Seraph_NIC* nic) {
    if (nic == NULL || !nic->initialized || nic->ops->get_mac == NULL) {
        return SERAPH_MAC_NULL;
    }
    return nic->ops->get_mac(nic->driver_data);
}

/**
 * @brief Set MAC address
 */
static inline Seraph_Vbit seraph_nic_set_mac(Seraph_NIC* nic,
                                              const Seraph_MAC_Address* mac) {
    if (nic == NULL || !nic->initialized || nic->ops->set_mac == NULL) {
        return SERAPH_VBIT_VOID;
    }
    return nic->ops->set_mac(nic->driver_data, mac);
}

/**
 * @brief Get link state
 */
static inline Seraph_NIC_Link_Info seraph_nic_get_link(Seraph_NIC* nic) {
    if (nic == NULL || !nic->initialized || nic->ops->get_link == NULL) {
        return (Seraph_NIC_Link_Info){ SERAPH_NIC_LINK_UNKNOWN, SERAPH_NIC_SPEED_UNKNOWN, false };
    }
    return nic->ops->get_link(nic->driver_data);
}

/**
 * @brief Get statistics
 */
static inline void seraph_nic_get_stats(Seraph_NIC* nic, Seraph_NIC_Stats* stats) {
    if (nic == NULL || !nic->initialized || nic->ops->get_stats == NULL || stats == NULL) {
        return;
    }
    nic->ops->get_stats(nic->driver_data, stats);
}

/**
 * @brief Poll for events
 */
static inline uint32_t seraph_nic_poll(Seraph_NIC* nic) {
    if (nic == NULL || !nic->initialized || nic->ops->poll == NULL) {
        return 0;
    }
    return nic->ops->poll(nic->driver_data);
}

/**
 * @brief Enable interrupts
 */
static inline void seraph_nic_enable_irq(Seraph_NIC* nic) {
    if (nic != NULL && nic->initialized && nic->ops->enable_irq != NULL) {
        nic->ops->enable_irq(nic->driver_data);
    }
}

/**
 * @brief Disable interrupts
 */
static inline void seraph_nic_disable_irq(Seraph_NIC* nic) {
    if (nic != NULL && nic->initialized && nic->ops->disable_irq != NULL) {
        nic->ops->disable_irq(nic->driver_data);
    }
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Convert big-endian 16-bit value to host byte order
 */
static inline uint16_t seraph_ntohs(uint16_t netshort) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((netshort >> 8) & 0xFF) | ((netshort & 0xFF) << 8);
#else
    return netshort;
#endif
}

/**
 * @brief Convert host 16-bit value to big-endian (network) byte order
 */
static inline uint16_t seraph_htons(uint16_t hostshort) {
    return seraph_ntohs(hostshort);  /* Same operation */
}

/**
 * @brief Format MAC address as string
 *
 * @param mac MAC address
 * @param buf Output buffer (must be at least 18 bytes)
 */
void seraph_mac_to_string(const Seraph_MAC_Address* mac, char* buf);

/**
 * @brief Parse MAC address from string
 *
 * @param str String in format "XX:XX:XX:XX:XX:XX"
 * @param mac Output MAC address
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_mac_from_string(const char* str, Seraph_MAC_Address* mac);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_DRIVERS_NIC_H */
