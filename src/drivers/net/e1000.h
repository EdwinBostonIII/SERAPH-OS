/**
 * @file e1000.h
 * @brief MC25: Intel e1000 (Gigabit Ethernet) Driver - Internal Header
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This is the internal header for the e1000 driver. It defines
 * hardware-specific constants, register offsets, and data structures.
 *
 * SUPPORTED DEVICES:
 *   - Intel 82540EM (common in QEMU/KVM)
 *   - Intel 82545EM (common in VMware)
 *   - Intel 82574L
 *
 * HARDWARE ARCHITECTURE:
 *
 *   The e1000 uses descriptor rings for TX and RX:
 *
 *   TX Ring:
 *     - Software writes packets to TX descriptors
 *     - Software advances TDT (Tail)
 *     - Hardware reads from TDH (Head) and transmits
 *     - Hardware sets DD bit when complete
 *
 *   RX Ring:
 *     - Hardware writes received packets to RX descriptors
 *     - Hardware advances RDH (Head)
 *     - Software reads from RDT (Tail) and processes
 *     - Software advances RDT after processing
 */

#ifndef SERAPH_DRIVERS_E1000_H
#define SERAPH_DRIVERS_E1000_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/drivers/nic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * PCI Identifiers
 *============================================================================*/

#define SERAPH_E1000_VENDOR_ID    0x8086    /* Intel */
#define SERAPH_E1000_DEVICE_82540 0x100E    /* 82540EM */
#define SERAPH_E1000_DEVICE_82545 0x100F    /* 82545EM */
#define SERAPH_E1000_DEVICE_82574 0x10D3    /* 82574L */

/*============================================================================
 * Register Offsets
 *============================================================================*/

/** Device Control */
#define SERAPH_E1000_REG_CTRL     0x0000
/** Device Status */
#define SERAPH_E1000_REG_STATUS   0x0008
/** EEPROM Control */
#define SERAPH_E1000_REG_EECD     0x0010
/** EEPROM Read */
#define SERAPH_E1000_REG_EERD     0x0014
/** Flow Control Address Low */
#define SERAPH_E1000_REG_FCAL     0x0028
/** Flow Control Address High */
#define SERAPH_E1000_REG_FCAH     0x002C
/** Flow Control Type */
#define SERAPH_E1000_REG_FCT      0x0030
/** Flow Control Transmit Timer Value */
#define SERAPH_E1000_REG_FCTTV    0x0170

/** Interrupt Cause Read */
#define SERAPH_E1000_REG_ICR      0x00C0
/** Interrupt Throttle Rate */
#define SERAPH_E1000_REG_ITR      0x00C4
/** Interrupt Cause Set */
#define SERAPH_E1000_REG_ICS      0x00C8
/** Interrupt Mask Set */
#define SERAPH_E1000_REG_IMS      0x00D0
/** Interrupt Mask Clear */
#define SERAPH_E1000_REG_IMC      0x00D8

/** Receive Control */
#define SERAPH_E1000_REG_RCTL     0x0100
/** Receive Descriptor Base Low */
#define SERAPH_E1000_REG_RDBAL    0x2800
/** Receive Descriptor Base High */
#define SERAPH_E1000_REG_RDBAH    0x2804
/** Receive Descriptor Length */
#define SERAPH_E1000_REG_RDLEN    0x2808
/** Receive Descriptor Head */
#define SERAPH_E1000_REG_RDH      0x2810
/** Receive Descriptor Tail */
#define SERAPH_E1000_REG_RDT      0x2818
/** Receive Delay Timer */
#define SERAPH_E1000_REG_RDTR     0x2820
/** Receive Checksum Control */
#define SERAPH_E1000_REG_RXCSUM   0x5000

/** Transmit Control */
#define SERAPH_E1000_REG_TCTL     0x0400
/** Transmit IPG */
#define SERAPH_E1000_REG_TIPG     0x0410
/** Transmit Descriptor Base Low */
#define SERAPH_E1000_REG_TDBAL    0x3800
/** Transmit Descriptor Base High */
#define SERAPH_E1000_REG_TDBAH    0x3804
/** Transmit Descriptor Length */
#define SERAPH_E1000_REG_TDLEN    0x3808
/** Transmit Descriptor Head */
#define SERAPH_E1000_REG_TDH      0x3810
/** Transmit Descriptor Tail */
#define SERAPH_E1000_REG_TDT      0x3818

/** Receive Address Low (RAL0) */
#define SERAPH_E1000_REG_RAL0     0x5400
/** Receive Address High (RAH0) */
#define SERAPH_E1000_REG_RAH0     0x5404

/** Multicast Table Array */
#define SERAPH_E1000_REG_MTA      0x5200

/** Statistics Registers */
#define SERAPH_E1000_REG_CRCERRS  0x4000
#define SERAPH_E1000_REG_GPRC     0x4074
#define SERAPH_E1000_REG_GPTC     0x4080
#define SERAPH_E1000_REG_GORCL    0x4088
#define SERAPH_E1000_REG_GORCH    0x408C
#define SERAPH_E1000_REG_GOTCL    0x4090
#define SERAPH_E1000_REG_GOTCH    0x4094

/*============================================================================
 * Control Register Bits (CTRL)
 *============================================================================*/

#define SERAPH_E1000_CTRL_FD      (1 << 0)   /**< Full Duplex */
#define SERAPH_E1000_CTRL_LRST    (1 << 3)   /**< Link Reset */
#define SERAPH_E1000_CTRL_ASDE    (1 << 5)   /**< Auto-Speed Detection Enable */
#define SERAPH_E1000_CTRL_SLU     (1 << 6)   /**< Set Link Up */
#define SERAPH_E1000_CTRL_ILOS    (1 << 7)   /**< Invert Loss-of-Signal */
#define SERAPH_E1000_CTRL_RST     (1 << 26)  /**< Device Reset */
#define SERAPH_E1000_CTRL_VME     (1 << 30)  /**< VLAN Mode Enable */
#define SERAPH_E1000_CTRL_PHY_RST (1 << 31)  /**< PHY Reset */

/*============================================================================
 * Status Register Bits (STATUS)
 *============================================================================*/

#define SERAPH_E1000_STATUS_FD    (1 << 0)   /**< Full Duplex */
#define SERAPH_E1000_STATUS_LU    (1 << 1)   /**< Link Up */
#define SERAPH_E1000_STATUS_SPEED_MASK (3 << 6)
#define SERAPH_E1000_STATUS_SPEED_10   (0 << 6)
#define SERAPH_E1000_STATUS_SPEED_100  (1 << 6)
#define SERAPH_E1000_STATUS_SPEED_1000 (2 << 6)

/*============================================================================
 * Receive Control Register Bits (RCTL)
 *============================================================================*/

#define SERAPH_E1000_RCTL_EN      (1 << 1)   /**< Receiver Enable */
#define SERAPH_E1000_RCTL_SBP     (1 << 2)   /**< Store Bad Packets */
#define SERAPH_E1000_RCTL_UPE     (1 << 3)   /**< Unicast Promiscuous */
#define SERAPH_E1000_RCTL_MPE     (1 << 4)   /**< Multicast Promiscuous */
#define SERAPH_E1000_RCTL_LPE     (1 << 5)   /**< Long Packet Enable */
#define SERAPH_E1000_RCTL_LBM_MASK (3 << 6)  /**< Loopback Mode */
#define SERAPH_E1000_RCTL_RDMTS   (3 << 8)   /**< Receive Descriptor Min Threshold */
#define SERAPH_E1000_RCTL_MO      (3 << 12)  /**< Multicast Offset */
#define SERAPH_E1000_RCTL_BAM     (1 << 15)  /**< Broadcast Accept Mode */
#define SERAPH_E1000_RCTL_BSIZE_MASK (3 << 16)
#define SERAPH_E1000_RCTL_BSIZE_2048 (0 << 16)
#define SERAPH_E1000_RCTL_BSIZE_1024 (1 << 16)
#define SERAPH_E1000_RCTL_BSIZE_512  (2 << 16)
#define SERAPH_E1000_RCTL_BSIZE_256  (3 << 16)
#define SERAPH_E1000_RCTL_VFE     (1 << 18)  /**< VLAN Filter Enable */
#define SERAPH_E1000_RCTL_CFIEN   (1 << 19)  /**< CFI Enable */
#define SERAPH_E1000_RCTL_CFI     (1 << 20)  /**< CFI Indication */
#define SERAPH_E1000_RCTL_DPF     (1 << 22)  /**< Discard Pause Frames */
#define SERAPH_E1000_RCTL_PMCF    (1 << 23)  /**< Pass MAC Control Frames */
#define SERAPH_E1000_RCTL_BSEX    (1 << 25)  /**< Buffer Size Extension */
#define SERAPH_E1000_RCTL_SECRC   (1 << 26)  /**< Strip Ethernet CRC */

/*============================================================================
 * Transmit Control Register Bits (TCTL)
 *============================================================================*/

#define SERAPH_E1000_TCTL_EN      (1 << 1)   /**< Transmit Enable */
#define SERAPH_E1000_TCTL_PSP     (1 << 3)   /**< Pad Short Packets */
#define SERAPH_E1000_TCTL_CT_SHIFT 4         /**< Collision Threshold */
#define SERAPH_E1000_TCTL_COLD_SHIFT 12      /**< Collision Distance */
#define SERAPH_E1000_TCTL_SWXOFF  (1 << 22)  /**< Software XOFF Transmission */
#define SERAPH_E1000_TCTL_RTLC    (1 << 24)  /**< Re-transmit on Late Collision */

/*============================================================================
 * Interrupt Bits
 *============================================================================*/

#define SERAPH_E1000_INT_TXDW     (1 << 0)   /**< TX Descriptor Written Back */
#define SERAPH_E1000_INT_TXQE     (1 << 1)   /**< TX Queue Empty */
#define SERAPH_E1000_INT_LSC      (1 << 2)   /**< Link Status Change */
#define SERAPH_E1000_INT_RXSEQ    (1 << 3)   /**< RX Sequence Error */
#define SERAPH_E1000_INT_RXDMT0   (1 << 4)   /**< RX Desc Min Threshold */
#define SERAPH_E1000_INT_RXO      (1 << 6)   /**< RX Overrun */
#define SERAPH_E1000_INT_RXT0     (1 << 7)   /**< RX Timer Interrupt */

/*============================================================================
 * EEPROM
 *============================================================================*/

#define SERAPH_E1000_EERD_START   (1 << 0)
#define SERAPH_E1000_EERD_DONE    (1 << 4)
#define SERAPH_E1000_EERD_ADDR_SHIFT 8
#define SERAPH_E1000_EERD_DATA_SHIFT 16

#define SERAPH_E1000_EEPROM_MAC   0x00       /**< MAC address offset in EEPROM */

/*============================================================================
 * Descriptor Structures
 *============================================================================*/

/** Descriptor ring size (must be multiple of 8, max 65536) */
#define SERAPH_E1000_NUM_RX_DESC  128
#define SERAPH_E1000_NUM_TX_DESC  128

/** RX buffer size */
#define SERAPH_E1000_RX_BUFFER_SIZE 2048

/**
 * @brief Legacy Receive Descriptor
 */
typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;    /**< Address of receive buffer */
    uint16_t length;         /**< Length of received data */
    uint16_t checksum;       /**< Packet checksum */
    uint8_t  status;         /**< Descriptor status */
    uint8_t  errors;         /**< Errors */
    uint16_t special;        /**< Special (VLAN tag) */
} Seraph_E1000_RX_Desc;

/** RX Descriptor Status bits */
#define SERAPH_E1000_RXD_STATUS_DD    (1 << 0)  /**< Descriptor Done */
#define SERAPH_E1000_RXD_STATUS_EOP   (1 << 1)  /**< End of Packet */
#define SERAPH_E1000_RXD_STATUS_VP    (1 << 3)  /**< VLAN Packet */
#define SERAPH_E1000_RXD_STATUS_TCPCS (1 << 5)  /**< TCP Checksum Calculated */
#define SERAPH_E1000_RXD_STATUS_IPCS  (1 << 6)  /**< IP Checksum Calculated */
#define SERAPH_E1000_RXD_STATUS_PIF   (1 << 7)  /**< Passed In-exact Filter */

/** RX Descriptor Errors bits */
#define SERAPH_E1000_RXD_ERROR_CE     (1 << 0)  /**< CRC Error */
#define SERAPH_E1000_RXD_ERROR_SE     (1 << 1)  /**< Symbol Error */
#define SERAPH_E1000_RXD_ERROR_SEQ    (1 << 2)  /**< Sequence Error */
#define SERAPH_E1000_RXD_ERROR_RXE    (1 << 7)  /**< RX Data Error */

/**
 * @brief Legacy Transmit Descriptor
 */
typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;    /**< Address of transmit buffer */
    uint16_t length;         /**< Length of data to transmit */
    uint8_t  cso;            /**< Checksum Offset */
    uint8_t  cmd;            /**< Command */
    uint8_t  status;         /**< Descriptor status */
    uint8_t  css;            /**< Checksum Start */
    uint16_t special;        /**< Special (VLAN tag) */
} Seraph_E1000_TX_Desc;

/** TX Descriptor Command bits */
#define SERAPH_E1000_TXD_CMD_EOP   (1 << 0)  /**< End of Packet */
#define SERAPH_E1000_TXD_CMD_IFCS  (1 << 1)  /**< Insert FCS/CRC */
#define SERAPH_E1000_TXD_CMD_IC    (1 << 2)  /**< Insert Checksum */
#define SERAPH_E1000_TXD_CMD_RS    (1 << 3)  /**< Report Status */
#define SERAPH_E1000_TXD_CMD_RPS   (1 << 4)  /**< Report Packet Sent */
#define SERAPH_E1000_TXD_CMD_DEXT  (1 << 5)  /**< Descriptor Extension */
#define SERAPH_E1000_TXD_CMD_VLE   (1 << 6)  /**< VLAN Packet Enable */
#define SERAPH_E1000_TXD_CMD_IDE   (1 << 7)  /**< Interrupt Delay Enable */

/** TX Descriptor Status bits */
#define SERAPH_E1000_TXD_STATUS_DD   (1 << 0)  /**< Descriptor Done */
#define SERAPH_E1000_TXD_STATUS_EC   (1 << 1)  /**< Excess Collisions */
#define SERAPH_E1000_TXD_STATUS_LC   (1 << 2)  /**< Late Collision */
#define SERAPH_E1000_TXD_STATUS_TU   (1 << 3)  /**< Transmit Underrun */

/*============================================================================
 * Hardware Archaeology - Semantic Interrupts
 *
 * When a hardware error occurs, we capture a complete snapshot of the NIC
 * register state at the moment of failure. This enables "Hardware Archaeology"
 * - excavating the exact physical state of the device when a VOID occurred.
 *============================================================================*/

/** Maximum number of hardware archaeology entries to retain */
#define SERAPH_E1000_HW_ARCHAEOLOGY_SIZE 64

/**
 * @brief Hardware register snapshot at moment of failure
 *
 * This is the "fossil record" of the NIC state when an error occurred.
 * Captures all relevant registers to enable post-mortem analysis.
 */
typedef struct {
    /* Device registers */
    uint32_t ctrl;          /**< CTRL register (device control) */
    uint32_t status;        /**< STATUS register (device status) */
    uint32_t icr;           /**< ICR register (interrupt cause) */
    uint32_t ims;           /**< IMS register (interrupt mask) */
    uint32_t rctl;          /**< RCTL register (receive control) */
    uint32_t tctl;          /**< TCTL register (transmit control) */

    /* Descriptor ring state */
    uint32_t rdh;           /**< RX descriptor head */
    uint32_t rdt;           /**< RX descriptor tail */
    uint32_t tdh;           /**< TX descriptor head */
    uint32_t tdt;           /**< TX descriptor tail */

    /* Error statistics registers */
    uint32_t crcerrs;       /**< CRC error count */

    /* Descriptor state at failure */
    uint8_t  desc_status;   /**< Descriptor status byte */
    uint8_t  desc_errors;   /**< Descriptor errors byte */
    uint16_t desc_length;   /**< Descriptor length */
    uint16_t desc_checksum; /**< Descriptor checksum */
    uint16_t desc_special;  /**< Descriptor special/VLAN */

    /* Ring position */
    uint32_t desc_index;    /**< Index of failing descriptor */
} Seraph_E1000_HW_Snapshot;

/**
 * @brief Hardware Archaeology entry - links VOID to hardware state
 *
 * This is the core of Semantic Interrupts: each hardware error gets
 * recorded with its full causality context AND the physical register
 * state at the moment of failure.
 */
typedef struct {
    uint64_t                   void_id;      /**< VOID ID from seraph_void_record */
    uint64_t                   timestamp;    /**< Monotonic timestamp (nanoseconds) */
    Seraph_E1000_HW_Snapshot   snapshot;     /**< Register snapshot at failure */
    Seraph_VoidReason          reason;       /**< Mapped VOID reason */
    uint32_t                   raw_error;    /**< Raw hardware error bits */
} Seraph_E1000_HW_Archaeology;

/*============================================================================
 * Driver State
 *============================================================================*/

/**
 * @brief E1000 driver state
 */
typedef struct {
    /** BAR0 mapped address */
    volatile uint8_t* bar0;

    /** MAC address */
    Seraph_MAC_Address mac;

    /** Receive descriptors (aligned) */
    Seraph_E1000_RX_Desc* rx_descs;
    uint64_t rx_descs_phys;

    /** Receive buffers */
    uint8_t* rx_buffers[SERAPH_E1000_NUM_RX_DESC];

    /** Current RX descriptor index */
    uint32_t rx_cur;

    /** Transmit descriptors (aligned) */
    Seraph_E1000_TX_Desc* tx_descs;
    uint64_t tx_descs_phys;

    /** Transmit buffers */
    uint8_t* tx_buffers[SERAPH_E1000_NUM_TX_DESC];

    /** Current TX descriptor index */
    uint32_t tx_cur;

    /** IRQ number */
    uint8_t irq;

    /** Statistics */
    Seraph_NIC_Stats stats;

    /** Promiscuous mode */
    bool promisc;

    /** Initialized flag */
    bool initialized;

    /*------------------------------------------------------------------------
     * Hardware Archaeology (Semantic Interrupts)
     *------------------------------------------------------------------------*/

    /** Hardware archaeology ring buffer */
    Seraph_E1000_HW_Archaeology hw_archaeology[SERAPH_E1000_HW_ARCHAEOLOGY_SIZE];

    /** Next write index in archaeology ring */
    uint32_t hw_arch_write_idx;

    /** Number of valid archaeology entries */
    uint32_t hw_arch_count;

    /** Monotonic timestamp counter for archaeology */
    uint64_t hw_arch_timestamp;

} Seraph_E1000;

/*============================================================================
 * Driver API
 *============================================================================*/

/**
 * @brief Create E1000 driver instance
 *
 * @param bar0_phys Physical address of BAR0
 * @param irq IRQ number
 * @return Driver instance, or NULL on failure
 */
Seraph_E1000* seraph_e1000_create(uint64_t bar0_phys, uint8_t irq);

/**
 * @brief Destroy E1000 driver instance
 */
void seraph_e1000_destroy_driver(Seraph_E1000* e1000);

/**
 * @brief Get the NIC operations vtable for E1000
 */
const Seraph_NIC_Ops* seraph_e1000_get_ops(void);

/**
 * @brief Create a generic NIC handle wrapping an E1000
 *
 * @param bar0_phys Physical address of BAR0
 * @param irq IRQ number
 * @return NIC handle (caller must free)
 */
Seraph_NIC* seraph_e1000_create_nic(uint64_t bar0_phys, uint8_t irq);

/*============================================================================
 * Hardware Archaeology API (Semantic Interrupts)
 *============================================================================*/

/**
 * @brief Capture current hardware state into a snapshot
 *
 * @param e1000 Driver instance
 * @param snapshot Output snapshot structure
 * @param desc_idx Index of descriptor to capture (or -1 for none)
 */
void seraph_e1000_capture_hw_state(Seraph_E1000* e1000,
                                    Seraph_E1000_HW_Snapshot* snapshot,
                                    int32_t desc_idx);

/**
 * @brief Map hardware error bits to VOID reason
 *
 * @param errors Raw error bits from RX descriptor
 * @return Appropriate Seraph_VoidReason
 */
Seraph_VoidReason seraph_e1000_map_error_to_reason(uint8_t errors);

/**
 * @brief Record hardware archaeology for an error
 *
 * @param e1000 Driver instance
 * @param reason VOID reason code
 * @param raw_error Raw hardware error bits
 * @param desc_idx Descriptor index where error occurred
 * @return VOID ID of the recorded error
 */
uint64_t seraph_e1000_record_hw_archaeology(Seraph_E1000* e1000,
                                             Seraph_VoidReason reason,
                                             uint32_t raw_error,
                                             uint32_t desc_idx);

/**
 * @brief Look up hardware archaeology by VOID ID
 *
 * @param e1000 Driver instance
 * @param void_id VOID ID to look up
 * @return Pointer to archaeology entry, or NULL if not found
 */
const Seraph_E1000_HW_Archaeology* seraph_e1000_lookup_archaeology(
    const Seraph_E1000* e1000, uint64_t void_id);

/**
 * @brief Print hardware archaeology chain (for debugging)
 *
 * @param e1000 Driver instance
 * @param void_id Starting VOID ID
 */
void seraph_e1000_print_archaeology(const Seraph_E1000* e1000, uint64_t void_id);

/**
 * @brief Get the most recent hardware archaeology entry
 *
 * @param e1000 Driver instance
 * @return Pointer to most recent entry, or NULL if none
 */
const Seraph_E1000_HW_Archaeology* seraph_e1000_last_archaeology(
    const Seraph_E1000* e1000);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_DRIVERS_E1000_H */
