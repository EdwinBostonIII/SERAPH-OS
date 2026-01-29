/**
 * @file e1000.c
 * @brief MC25: Intel e1000 (Gigabit Ethernet) Driver Implementation
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This driver supports Intel e1000-family NICs, commonly used in
 * virtual machines (QEMU, VMware, VirtualBox) and older hardware.
 *
 * INITIALIZATION SEQUENCE:
 *
 *   1. Reset the device
 *   2. Read MAC address from EEPROM
 *   3. Allocate TX/RX descriptor rings
 *   4. Configure receive (enable, set buffer size)
 *   5. Configure transmit (enable, set IPG)
 *   6. Set up interrupts
 *   7. Enable RX/TX
 *
 * TRANSMIT PATH:
 *
 *   1. Copy packet to TX buffer
 *   2. Write descriptor (buffer address, length, flags)
 *   3. Advance TDT (Tail) to notify hardware
 *   4. Hardware transmits and sets DD bit
 *
 * RECEIVE PATH:
 *
 *   1. Hardware receives packet into RX buffer
 *   2. Hardware writes descriptor (length, status)
 *   3. Hardware advances RDH (Head)
 *   4. Software polls for DD bit in descriptor
 *   5. Software copies packet and advances RDT (Tail)
 */

#include "e1000.h"
#include "seraph/void.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*============================================================================
 * Platform Abstraction
 *============================================================================*/

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <time.h>
#endif

static void e1000_delay_us(uint32_t us) {
#ifdef _WIN32
    Sleep((us + 999) / 1000);
#else
    usleep(us);
#endif
}

static void e1000_delay_ms(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static void* e1000_alloc_aligned(size_t size, size_t align) {
#ifdef _WIN32
    return _aligned_malloc(size, align);
#else
    void* ptr;
    if (posix_memalign(&ptr, align, size) != 0) {
        return NULL;
    }
    return ptr;
#endif
}

static void e1000_free_aligned(void* ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/*============================================================================
 * Hardware Archaeology (Semantic Interrupts) Implementation
 *
 * These functions implement "Semantic Interrupts" - the mapping of hardware
 * error conditions to VOID causality chains with full register state capture.
 *============================================================================*/

void seraph_e1000_capture_hw_state(Seraph_E1000* e1000,
                                    Seraph_E1000_HW_Snapshot* snapshot,
                                    int32_t desc_idx) {
    if (e1000 == NULL || snapshot == NULL) return;

    /* Capture device registers */
    snapshot->ctrl   = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_CTRL);
    snapshot->status = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_STATUS);
    snapshot->icr    = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_ICR);
    snapshot->ims    = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_IMS);
    snapshot->rctl   = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_RCTL);
    snapshot->tctl   = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_TCTL);

    /* Capture descriptor ring state */
    snapshot->rdh = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_RDH);
    snapshot->rdt = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_RDT);
    snapshot->tdh = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_TDH);
    snapshot->tdt = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_TDT);

    /* Capture error statistics */
    snapshot->crcerrs = *(volatile uint32_t*)(e1000->bar0 + SERAPH_E1000_REG_CRCERRS);

    /* Capture descriptor state if valid index */
    if (desc_idx >= 0 && desc_idx < SERAPH_E1000_NUM_RX_DESC && e1000->rx_descs) {
        Seraph_E1000_RX_Desc* desc = &e1000->rx_descs[desc_idx];
        snapshot->desc_status   = desc->status;
        snapshot->desc_errors   = desc->errors;
        snapshot->desc_length   = desc->length;
        snapshot->desc_checksum = desc->checksum;
        snapshot->desc_special  = desc->special;
        snapshot->desc_index    = (uint32_t)desc_idx;
    } else {
        snapshot->desc_status   = 0;
        snapshot->desc_errors   = 0;
        snapshot->desc_length   = 0;
        snapshot->desc_checksum = 0;
        snapshot->desc_special  = 0;
        snapshot->desc_index    = 0xFFFFFFFF;
    }
}

Seraph_VoidReason seraph_e1000_map_error_to_reason(uint8_t errors) {
    /* Map hardware error bits to semantic VOID reasons
     * Priority order: most specific first */

    if (errors & SERAPH_E1000_RXD_ERROR_CE) {
        return SERAPH_VOID_REASON_HW_CRC;
    }
    if (errors & SERAPH_E1000_RXD_ERROR_SE) {
        return SERAPH_VOID_REASON_HW_SYMBOL;
    }
    if (errors & SERAPH_E1000_RXD_ERROR_SEQ) {
        return SERAPH_VOID_REASON_HW_SEQUENCE;
    }
    if (errors & SERAPH_E1000_RXD_ERROR_RXE) {
        return SERAPH_VOID_REASON_HW_RX_DATA;
    }

    /* Unknown hardware error - generic network error */
    return SERAPH_VOID_REASON_NETWORK;
}

uint64_t seraph_e1000_record_hw_archaeology(Seraph_E1000* e1000,
                                             Seraph_VoidReason reason,
                                             uint32_t raw_error,
                                             uint32_t desc_idx) {
    if (e1000 == NULL) return 0;

    /* Generate message describing the hardware error */
    char msg[64];
    snprintf(msg, sizeof(msg), "e1000 err=0x%02X desc=%u", raw_error, desc_idx);

    /* Record in VOID causality system */
    uint64_t void_id = seraph_void_record(
        reason,
        0,  /* No predecessor - this is a root cause from hardware */
        (uint64_t)raw_error,
        (uint64_t)desc_idx,
        __FILE__,
        __func__,
        __LINE__,
        msg
    );

    /* Store in hardware archaeology table */
    uint32_t slot = e1000->hw_arch_write_idx;
    Seraph_E1000_HW_Archaeology* entry = &e1000->hw_archaeology[slot];

    entry->void_id   = void_id;
    entry->timestamp = ++e1000->hw_arch_timestamp;
    entry->reason    = reason;
    entry->raw_error = raw_error;

    /* Capture full hardware state at moment of failure */
    seraph_e1000_capture_hw_state(e1000, &entry->snapshot, (int32_t)desc_idx);

    /* Advance circular buffer */
    e1000->hw_arch_write_idx = (slot + 1) % SERAPH_E1000_HW_ARCHAEOLOGY_SIZE;
    if (e1000->hw_arch_count < SERAPH_E1000_HW_ARCHAEOLOGY_SIZE) {
        e1000->hw_arch_count++;
    }

    return void_id;
}

const Seraph_E1000_HW_Archaeology* seraph_e1000_lookup_archaeology(
    const Seraph_E1000* e1000, uint64_t void_id) {
    if (e1000 == NULL || void_id == 0) return NULL;

    /* Search the circular buffer */
    for (uint32_t i = 0; i < e1000->hw_arch_count; i++) {
        if (e1000->hw_archaeology[i].void_id == void_id) {
            return &e1000->hw_archaeology[i];
        }
    }

    return NULL;
}

const Seraph_E1000_HW_Archaeology* seraph_e1000_last_archaeology(
    const Seraph_E1000* e1000) {
    if (e1000 == NULL || e1000->hw_arch_count == 0) return NULL;

    uint32_t last_idx = (e1000->hw_arch_write_idx + SERAPH_E1000_HW_ARCHAEOLOGY_SIZE - 1)
                        % SERAPH_E1000_HW_ARCHAEOLOGY_SIZE;
    return &e1000->hw_archaeology[last_idx];
}

void seraph_e1000_print_archaeology(const Seraph_E1000* e1000, uint64_t void_id) {
    if (e1000 == NULL) return;

    const Seraph_E1000_HW_Archaeology* entry = seraph_e1000_lookup_archaeology(e1000, void_id);
    if (entry == NULL) {
        fprintf(stderr, "=== E1000 Hardware Archaeology: VOID %llu not found ===\n",
                (unsigned long long)void_id);
        return;
    }

    fprintf(stderr, "=== E1000 Hardware Archaeology for VOID %llu ===\n",
            (unsigned long long)void_id);
    fprintf(stderr, "  Reason: %s (raw_error=0x%02X)\n",
            seraph_void_reason_str(entry->reason), entry->raw_error);
    fprintf(stderr, "  Timestamp: %llu\n", (unsigned long long)entry->timestamp);
    fprintf(stderr, "\n  Device Registers:\n");
    fprintf(stderr, "    CTRL:   0x%08X  STATUS: 0x%08X\n",
            entry->snapshot.ctrl, entry->snapshot.status);
    fprintf(stderr, "    ICR:    0x%08X  IMS:    0x%08X\n",
            entry->snapshot.icr, entry->snapshot.ims);
    fprintf(stderr, "    RCTL:   0x%08X  TCTL:   0x%08X\n",
            entry->snapshot.rctl, entry->snapshot.tctl);
    fprintf(stderr, "\n  Descriptor Ring State:\n");
    fprintf(stderr, "    RDH: %u  RDT: %u  TDH: %u  TDT: %u\n",
            entry->snapshot.rdh, entry->snapshot.rdt,
            entry->snapshot.tdh, entry->snapshot.tdt);
    fprintf(stderr, "\n  Failing Descriptor [%u]:\n", entry->snapshot.desc_index);
    fprintf(stderr, "    status=0x%02X  errors=0x%02X  length=%u\n",
            entry->snapshot.desc_status, entry->snapshot.desc_errors,
            entry->snapshot.desc_length);
    fprintf(stderr, "    checksum=0x%04X  special=0x%04X\n",
            entry->snapshot.desc_checksum, entry->snapshot.desc_special);
    fprintf(stderr, "\n  CRC Errors Total: %u\n", entry->snapshot.crcerrs);
    fprintf(stderr, "=== End Hardware Archaeology ===\n");

    /* Also print the VOID causality chain */
    seraph_void_print_chain(void_id);
}

/*============================================================================
 * Register Access
 *============================================================================*/

static inline uint32_t e1000_read(Seraph_E1000* e1000, uint32_t reg) {
    volatile uint32_t* addr = (volatile uint32_t*)(e1000->bar0 + reg);
    return *addr;
}

static inline void e1000_write(Seraph_E1000* e1000, uint32_t reg, uint32_t value) {
    volatile uint32_t* addr = (volatile uint32_t*)(e1000->bar0 + reg);
    *addr = value;
}

/*============================================================================
 * EEPROM Access
 *============================================================================*/

/**
 * @brief Read word from EEPROM
 */
static uint16_t e1000_eeprom_read(Seraph_E1000* e1000, uint8_t addr) {
    uint32_t eerd;

    /* Start read */
    e1000_write(e1000, SERAPH_E1000_REG_EERD,
                SERAPH_E1000_EERD_START |
                ((uint32_t)addr << SERAPH_E1000_EERD_ADDR_SHIFT));

    /* Wait for completion */
    while (1) {
        eerd = e1000_read(e1000, SERAPH_E1000_REG_EERD);
        if (eerd & SERAPH_E1000_EERD_DONE) {
            break;
        }
        e1000_delay_us(1);
    }

    return (uint16_t)(eerd >> SERAPH_E1000_EERD_DATA_SHIFT);
}

/**
 * @brief Read MAC address from EEPROM
 */
static void e1000_read_mac(Seraph_E1000* e1000) {
    /* Try reading from RAL0/RAH0 first (might be set by BIOS) */
    uint32_t ral = e1000_read(e1000, SERAPH_E1000_REG_RAL0);
    uint32_t rah = e1000_read(e1000, SERAPH_E1000_REG_RAH0);

    if (ral != 0 && ral != 0xFFFFFFFF) {
        e1000->mac.bytes[0] = (uint8_t)(ral);
        e1000->mac.bytes[1] = (uint8_t)(ral >> 8);
        e1000->mac.bytes[2] = (uint8_t)(ral >> 16);
        e1000->mac.bytes[3] = (uint8_t)(ral >> 24);
        e1000->mac.bytes[4] = (uint8_t)(rah);
        e1000->mac.bytes[5] = (uint8_t)(rah >> 8);
        return;
    }

    /* Read from EEPROM */
    uint16_t word0 = e1000_eeprom_read(e1000, SERAPH_E1000_EEPROM_MAC + 0);
    uint16_t word1 = e1000_eeprom_read(e1000, SERAPH_E1000_EEPROM_MAC + 1);
    uint16_t word2 = e1000_eeprom_read(e1000, SERAPH_E1000_EEPROM_MAC + 2);

    e1000->mac.bytes[0] = (uint8_t)(word0);
    e1000->mac.bytes[1] = (uint8_t)(word0 >> 8);
    e1000->mac.bytes[2] = (uint8_t)(word1);
    e1000->mac.bytes[3] = (uint8_t)(word1 >> 8);
    e1000->mac.bytes[4] = (uint8_t)(word2);
    e1000->mac.bytes[5] = (uint8_t)(word2 >> 8);
}

/*============================================================================
 * Device Initialization
 *============================================================================*/

/**
 * @brief Reset the device
 */
static void e1000_reset(Seraph_E1000* e1000) {
    /* Set reset bit */
    uint32_t ctrl = e1000_read(e1000, SERAPH_E1000_REG_CTRL);
    e1000_write(e1000, SERAPH_E1000_REG_CTRL, ctrl | SERAPH_E1000_CTRL_RST);

    /* Wait for reset to complete */
    e1000_delay_ms(10);

    /* Disable interrupts */
    e1000_write(e1000, SERAPH_E1000_REG_IMC, 0xFFFFFFFF);
}

/**
 * @brief Initialize RX descriptors
 */
static Seraph_Vbit e1000_init_rx(Seraph_E1000* e1000) {
    /* Allocate descriptor ring (16-byte aligned, though 128 is better) */
    size_t desc_size = SERAPH_E1000_NUM_RX_DESC * sizeof(Seraph_E1000_RX_Desc);
    e1000->rx_descs = e1000_alloc_aligned(desc_size, 128);
    if (e1000->rx_descs == NULL) {
        return SERAPH_VBIT_VOID;
    }
    memset(e1000->rx_descs, 0, desc_size);
    e1000->rx_descs_phys = (uint64_t)(uintptr_t)e1000->rx_descs;

    /* Allocate RX buffers */
    for (int i = 0; i < SERAPH_E1000_NUM_RX_DESC; i++) {
        e1000->rx_buffers[i] = e1000_alloc_aligned(SERAPH_E1000_RX_BUFFER_SIZE, 16);
        if (e1000->rx_buffers[i] == NULL) {
            return SERAPH_VBIT_VOID;
        }
        memset(e1000->rx_buffers[i], 0, SERAPH_E1000_RX_BUFFER_SIZE);

        e1000->rx_descs[i].buffer_addr = (uint64_t)(uintptr_t)e1000->rx_buffers[i];
        e1000->rx_descs[i].status = 0;
    }

    /* Configure descriptor ring */
    e1000_write(e1000, SERAPH_E1000_REG_RDBAL, (uint32_t)(e1000->rx_descs_phys & 0xFFFFFFFF));
    e1000_write(e1000, SERAPH_E1000_REG_RDBAH, (uint32_t)(e1000->rx_descs_phys >> 32));
    e1000_write(e1000, SERAPH_E1000_REG_RDLEN, (uint32_t)desc_size);

    /* Set head and tail */
    e1000_write(e1000, SERAPH_E1000_REG_RDH, 0);
    e1000_write(e1000, SERAPH_E1000_REG_RDT, SERAPH_E1000_NUM_RX_DESC - 1);

    e1000->rx_cur = 0;

    /* Configure receive control:
     * - Enable receiver
     * - Accept broadcast
     * - 2KB buffer size
     * - Strip CRC */
    uint32_t rctl = SERAPH_E1000_RCTL_EN |
                    SERAPH_E1000_RCTL_BAM |
                    SERAPH_E1000_RCTL_BSIZE_2048 |
                    SERAPH_E1000_RCTL_SECRC;
    e1000_write(e1000, SERAPH_E1000_REG_RCTL, rctl);

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Initialize TX descriptors
 */
static Seraph_Vbit e1000_init_tx(Seraph_E1000* e1000) {
    /* Allocate descriptor ring */
    size_t desc_size = SERAPH_E1000_NUM_TX_DESC * sizeof(Seraph_E1000_TX_Desc);
    e1000->tx_descs = e1000_alloc_aligned(desc_size, 128);
    if (e1000->tx_descs == NULL) {
        return SERAPH_VBIT_VOID;
    }
    memset(e1000->tx_descs, 0, desc_size);
    e1000->tx_descs_phys = (uint64_t)(uintptr_t)e1000->tx_descs;

    /* Allocate TX buffers */
    for (int i = 0; i < SERAPH_E1000_NUM_TX_DESC; i++) {
        e1000->tx_buffers[i] = e1000_alloc_aligned(SERAPH_NIC_MAX_FRAME_SIZE, 16);
        if (e1000->tx_buffers[i] == NULL) {
            return SERAPH_VBIT_VOID;
        }
        memset(e1000->tx_buffers[i], 0, SERAPH_NIC_MAX_FRAME_SIZE);

        e1000->tx_descs[i].buffer_addr = (uint64_t)(uintptr_t)e1000->tx_buffers[i];
        e1000->tx_descs[i].status = SERAPH_E1000_TXD_STATUS_DD;  /* Initially "done" */
    }

    /* Configure descriptor ring */
    e1000_write(e1000, SERAPH_E1000_REG_TDBAL, (uint32_t)(e1000->tx_descs_phys & 0xFFFFFFFF));
    e1000_write(e1000, SERAPH_E1000_REG_TDBAH, (uint32_t)(e1000->tx_descs_phys >> 32));
    e1000_write(e1000, SERAPH_E1000_REG_TDLEN, (uint32_t)desc_size);

    /* Set head and tail */
    e1000_write(e1000, SERAPH_E1000_REG_TDH, 0);
    e1000_write(e1000, SERAPH_E1000_REG_TDT, 0);

    e1000->tx_cur = 0;

    /* Configure transmit control:
     * - Enable transmitter
     * - Pad short packets
     * - Full duplex collision settings */
    uint32_t tctl = SERAPH_E1000_TCTL_EN |
                    SERAPH_E1000_TCTL_PSP |
                    (15 << SERAPH_E1000_TCTL_CT_SHIFT) |    /* Collision Threshold */
                    (64 << SERAPH_E1000_TCTL_COLD_SHIFT);   /* Collision Distance */
    e1000_write(e1000, SERAPH_E1000_REG_TCTL, tctl);

    /* Set inter-packet gap */
    e1000_write(e1000, SERAPH_E1000_REG_TIPG, 10 | (8 << 10) | (6 << 20));

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * NIC Operations Implementation
 *============================================================================*/

static Seraph_Vbit e1000_op_init(void* driver) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Reset device */
    e1000_reset(e1000);

    /* Read MAC address */
    e1000_read_mac(e1000);

    /* Initialize RX */
    Seraph_Vbit result = e1000_init_rx(e1000);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Initialize TX */
    result = e1000_init_tx(e1000);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Set MAC address in RAL0/RAH0 */
    uint32_t ral = ((uint32_t)e1000->mac.bytes[0]) |
                   ((uint32_t)e1000->mac.bytes[1] << 8) |
                   ((uint32_t)e1000->mac.bytes[2] << 16) |
                   ((uint32_t)e1000->mac.bytes[3] << 24);
    uint32_t rah = ((uint32_t)e1000->mac.bytes[4]) |
                   ((uint32_t)e1000->mac.bytes[5] << 8) |
                   (1u << 31);  /* Address Valid bit */
    e1000_write(e1000, SERAPH_E1000_REG_RAL0, ral);
    e1000_write(e1000, SERAPH_E1000_REG_RAH0, rah);

    /* Clear multicast table */
    for (int i = 0; i < 128; i++) {
        e1000_write(e1000, SERAPH_E1000_REG_MTA + (i * 4), 0);
    }

    /* Set link up */
    uint32_t ctrl = e1000_read(e1000, SERAPH_E1000_REG_CTRL);
    ctrl |= SERAPH_E1000_CTRL_SLU;
    e1000_write(e1000, SERAPH_E1000_REG_CTRL, ctrl);

    e1000->initialized = true;
    return SERAPH_VBIT_TRUE;
}

static void e1000_op_destroy(void* driver) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL) {
        return;
    }

    /* Disable RX/TX */
    e1000_write(e1000, SERAPH_E1000_REG_RCTL, 0);
    e1000_write(e1000, SERAPH_E1000_REG_TCTL, 0);

    /* Disable interrupts */
    e1000_write(e1000, SERAPH_E1000_REG_IMC, 0xFFFFFFFF);

    /* Free buffers */
    for (int i = 0; i < SERAPH_E1000_NUM_RX_DESC; i++) {
        if (e1000->rx_buffers[i]) {
            e1000_free_aligned(e1000->rx_buffers[i]);
        }
    }
    for (int i = 0; i < SERAPH_E1000_NUM_TX_DESC; i++) {
        if (e1000->tx_buffers[i]) {
            e1000_free_aligned(e1000->tx_buffers[i]);
        }
    }

    /* Free descriptor rings */
    if (e1000->rx_descs) {
        e1000_free_aligned(e1000->rx_descs);
    }
    if (e1000->tx_descs) {
        e1000_free_aligned(e1000->tx_descs);
    }

    e1000->initialized = false;
}

static Seraph_Vbit e1000_op_send(void* driver, const void* data, size_t len) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || !e1000->initialized || data == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (len > SERAPH_NIC_MAX_FRAME_SIZE || len < SERAPH_NIC_MIN_FRAME_SIZE) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_INVALID_ARG, 0,
                          (uint64_t)len, (uint64_t)SERAPH_NIC_MAX_FRAME_SIZE,
                          "frame size out of range");
        return SERAPH_VBIT_VOID;
    }

    uint32_t cur = e1000->tx_cur;
    Seraph_E1000_TX_Desc* desc = &e1000->tx_descs[cur];

    /* Wait for descriptor to be available (DD set) */
    int timeout = 1000;
    while (!(desc->status & SERAPH_E1000_TXD_STATUS_DD)) {
        if (--timeout <= 0) {
            e1000->stats.tx_dropped++;
            SERAPH_VOID_RECORD(SERAPH_VOID_REASON_TIMEOUT, 0,
                              (uint64_t)cur, 0, "tx descriptor timeout");
            return SERAPH_VBIT_VOID;  /* Timeout with causality tracking */
        }
        e1000_delay_us(1);
    }

    /* Check for TX errors in previous transmission on this descriptor */
    if (desc->status & (SERAPH_E1000_TXD_STATUS_EC | SERAPH_E1000_TXD_STATUS_LC)) {
        Seraph_VoidReason reason;
        if (desc->status & SERAPH_E1000_TXD_STATUS_LC) {
            reason = SERAPH_VOID_REASON_HW_COLLISION;
        } else {
            reason = SERAPH_VOID_REASON_HW_COLLISION;  /* Excess collisions */
        }

        /* Record hardware archaeology for TX error (retroactive) */
        seraph_e1000_record_hw_archaeology(e1000, reason, desc->status, cur);
    }

    /* Copy data to TX buffer */
    memcpy(e1000->tx_buffers[cur], data, len);

    /* Set up descriptor */
    desc->length = (uint16_t)len;
    desc->cmd = SERAPH_E1000_TXD_CMD_EOP |
                SERAPH_E1000_TXD_CMD_IFCS |
                SERAPH_E1000_TXD_CMD_RS;
    desc->status = 0;

    /* Advance tail to submit */
    e1000->tx_cur = (cur + 1) % SERAPH_E1000_NUM_TX_DESC;
    e1000_write(e1000, SERAPH_E1000_REG_TDT, e1000->tx_cur);

    e1000->stats.tx_packets++;
    e1000->stats.tx_bytes += len;

    return SERAPH_VBIT_TRUE;
}

static Seraph_Vbit e1000_op_recv(void* driver, void* buffer, size_t* len) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || !e1000->initialized || buffer == NULL || len == NULL) {
        return SERAPH_VBIT_VOID;
    }

    uint32_t cur = e1000->rx_cur;
    Seraph_E1000_RX_Desc* desc = &e1000->rx_descs[cur];

    /* Check if descriptor has a packet (DD set) */
    if (!(desc->status & SERAPH_E1000_RXD_STATUS_DD)) {
        return SERAPH_VBIT_FALSE;  /* No packet */
    }

    /* Check for errors - SEMANTIC INTERRUPTS with Hardware Archaeology */
    if (desc->errors) {
        e1000->stats.rx_errors++;
        if (desc->errors & SERAPH_E1000_RXD_ERROR_CE) {
            e1000->stats.rx_crc_errors++;
        }

        /*
         * SEMANTIC INTERRUPT: Map hardware error to VOID reason and record
         * with full Hardware Archaeology - capturing the NIC register state
         * at the exact moment of failure.
         */
        Seraph_VoidReason reason = seraph_e1000_map_error_to_reason(desc->errors);
        uint64_t void_id = seraph_e1000_record_hw_archaeology(
            e1000,
            reason,
            (uint32_t)desc->errors,
            cur
        );

        /* The VOID ID is now linked to full hardware state snapshot.
         * Callers can use seraph_e1000_lookup_archaeology(e1000, void_id)
         * to excavate the exact register state when this error occurred. */
        (void)void_id;  /* Available for caller via seraph_void_last() */

        /* Reset descriptor and continue */
        desc->status = 0;
        e1000->rx_cur = (cur + 1) % SERAPH_E1000_NUM_RX_DESC;
        e1000_write(e1000, SERAPH_E1000_REG_RDT, cur);
        return SERAPH_VBIT_VOID;  /* Now returns VOID with causality tracking */
    }

    /* Get packet length */
    uint16_t pkt_len = desc->length;
    if (pkt_len > *len) {
        /* Buffer too small */
        e1000->stats.rx_dropped++;
        desc->status = 0;
        e1000->rx_cur = (cur + 1) % SERAPH_E1000_NUM_RX_DESC;
        e1000_write(e1000, SERAPH_E1000_REG_RDT, cur);
        return SERAPH_VBIT_VOID;
    }

    /* Copy packet */
    memcpy(buffer, e1000->rx_buffers[cur], pkt_len);
    *len = pkt_len;

    e1000->stats.rx_packets++;
    e1000->stats.rx_bytes += pkt_len;

    /* Reset descriptor */
    desc->status = 0;

    /* Advance to next descriptor */
    e1000->rx_cur = (cur + 1) % SERAPH_E1000_NUM_RX_DESC;

    /* Update tail to give buffer back to hardware */
    e1000_write(e1000, SERAPH_E1000_REG_RDT, cur);

    return SERAPH_VBIT_TRUE;
}

static Seraph_MAC_Address e1000_op_get_mac(void* driver) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL) {
        return SERAPH_MAC_NULL;
    }
    return e1000->mac;
}

static Seraph_Vbit e1000_op_set_mac(void* driver, const Seraph_MAC_Address* mac) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || mac == NULL) {
        return SERAPH_VBIT_VOID;
    }

    e1000->mac = *mac;

    /* Update RAL0/RAH0 */
    uint32_t ral = ((uint32_t)mac->bytes[0]) |
                   ((uint32_t)mac->bytes[1] << 8) |
                   ((uint32_t)mac->bytes[2] << 16) |
                   ((uint32_t)mac->bytes[3] << 24);
    uint32_t rah = ((uint32_t)mac->bytes[4]) |
                   ((uint32_t)mac->bytes[5] << 8) |
                   (1u << 31);
    e1000_write(e1000, SERAPH_E1000_REG_RAL0, ral);
    e1000_write(e1000, SERAPH_E1000_REG_RAH0, rah);

    return SERAPH_VBIT_TRUE;
}

static Seraph_NIC_Link_Info e1000_op_get_link(void* driver) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    Seraph_NIC_Link_Info info = { SERAPH_NIC_LINK_DOWN, SERAPH_NIC_SPEED_UNKNOWN, false };

    if (e1000 == NULL || !e1000->initialized) {
        return info;
    }

    uint32_t status = e1000_read(e1000, SERAPH_E1000_REG_STATUS);

    if (status & SERAPH_E1000_STATUS_LU) {
        info.state = SERAPH_NIC_LINK_UP;
    }

    info.full_duplex = (status & SERAPH_E1000_STATUS_FD) != 0;

    switch (status & SERAPH_E1000_STATUS_SPEED_MASK) {
        case SERAPH_E1000_STATUS_SPEED_10:
            info.speed = SERAPH_NIC_SPEED_10MBPS;
            break;
        case SERAPH_E1000_STATUS_SPEED_100:
            info.speed = SERAPH_NIC_SPEED_100MBPS;
            break;
        case SERAPH_E1000_STATUS_SPEED_1000:
            info.speed = SERAPH_NIC_SPEED_1GBPS;
            break;
    }

    return info;
}

static void e1000_op_get_stats(void* driver, Seraph_NIC_Stats* stats) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || stats == NULL) {
        return;
    }
    *stats = e1000->stats;
}

static Seraph_Vbit e1000_op_set_promisc(void* driver, bool enable) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || !e1000->initialized) {
        return SERAPH_VBIT_VOID;
    }

    uint32_t rctl = e1000_read(e1000, SERAPH_E1000_REG_RCTL);

    if (enable) {
        rctl |= SERAPH_E1000_RCTL_UPE | SERAPH_E1000_RCTL_MPE;
    } else {
        rctl &= ~(SERAPH_E1000_RCTL_UPE | SERAPH_E1000_RCTL_MPE);
    }

    e1000_write(e1000, SERAPH_E1000_REG_RCTL, rctl);
    e1000->promisc = enable;

    return SERAPH_VBIT_TRUE;
}

static Seraph_Vbit e1000_op_add_multicast(void* driver, const Seraph_MAC_Address* mac) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || mac == NULL || !e1000->initialized) {
        return SERAPH_VBIT_VOID;
    }

    /* Hash the MAC address to get MTA index */
    uint32_t hash = ((uint32_t)mac->bytes[4] >> 2) | ((uint32_t)mac->bytes[5] << 6);
    hash &= 0xFFF;

    uint32_t reg = SERAPH_E1000_REG_MTA + ((hash >> 5) * 4);
    uint32_t bit = 1u << (hash & 0x1F);

    uint32_t mta = e1000_read(e1000, reg);
    e1000_write(e1000, reg, mta | bit);

    return SERAPH_VBIT_TRUE;
}

static Seraph_Vbit e1000_op_del_multicast(void* driver, const Seraph_MAC_Address* mac) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || mac == NULL || !e1000->initialized) {
        return SERAPH_VBIT_VOID;
    }

    uint32_t hash = ((uint32_t)mac->bytes[4] >> 2) | ((uint32_t)mac->bytes[5] << 6);
    hash &= 0xFFF;

    uint32_t reg = SERAPH_E1000_REG_MTA + ((hash >> 5) * 4);
    uint32_t bit = 1u << (hash & 0x1F);

    uint32_t mta = e1000_read(e1000, reg);
    e1000_write(e1000, reg, mta & ~bit);

    return SERAPH_VBIT_TRUE;
}

static uint32_t e1000_op_poll(void* driver) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || !e1000->initialized) {
        return 0;
    }

    /* Read and clear ICR */
    uint32_t icr = e1000_read(e1000, SERAPH_E1000_REG_ICR);
    (void)icr;

    /* Count RX packets available */
    uint32_t events = 0;
    uint32_t cur = e1000->rx_cur;

    for (int i = 0; i < SERAPH_E1000_NUM_RX_DESC; i++) {
        if (e1000->rx_descs[cur].status & SERAPH_E1000_RXD_STATUS_DD) {
            events++;
        }
        cur = (cur + 1) % SERAPH_E1000_NUM_RX_DESC;
    }

    return events;
}

static void e1000_op_enable_irq(void* driver) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || !e1000->initialized) {
        return;
    }

    e1000_write(e1000, SERAPH_E1000_REG_IMS,
                SERAPH_E1000_INT_RXT0 |
                SERAPH_E1000_INT_LSC |
                SERAPH_E1000_INT_TXDW);
}

static void e1000_op_disable_irq(void* driver) {
    Seraph_E1000* e1000 = (Seraph_E1000*)driver;
    if (e1000 == NULL || !e1000->initialized) {
        return;
    }

    e1000_write(e1000, SERAPH_E1000_REG_IMC, 0xFFFFFFFF);
}

/*============================================================================
 * VTable
 *============================================================================*/

static const Seraph_NIC_Ops g_e1000_ops = {
    .init          = e1000_op_init,
    .destroy       = e1000_op_destroy,
    .send          = e1000_op_send,
    .recv          = e1000_op_recv,
    .get_mac       = e1000_op_get_mac,
    .set_mac       = e1000_op_set_mac,
    .get_link      = e1000_op_get_link,
    .get_stats     = e1000_op_get_stats,
    .set_promisc   = e1000_op_set_promisc,
    .add_multicast = e1000_op_add_multicast,
    .del_multicast = e1000_op_del_multicast,
    .poll          = e1000_op_poll,
    .enable_irq    = e1000_op_enable_irq,
    .disable_irq   = e1000_op_disable_irq,
};

/*============================================================================
 * Public API
 *============================================================================*/

const Seraph_NIC_Ops* seraph_e1000_get_ops(void) {
    return &g_e1000_ops;
}

Seraph_E1000* seraph_e1000_create(uint64_t bar0_phys, uint8_t irq) {
    Seraph_E1000* e1000 = calloc(1, sizeof(Seraph_E1000));
    if (e1000 == NULL) {
        return NULL;
    }

    e1000->bar0 = (volatile uint8_t*)(uintptr_t)bar0_phys;
    e1000->irq = irq;

    return e1000;
}

void seraph_e1000_destroy_driver(Seraph_E1000* e1000) {
    if (e1000 != NULL) {
        free(e1000);
    }
}

Seraph_NIC* seraph_e1000_create_nic(uint64_t bar0_phys, uint8_t irq) {
    Seraph_NIC* nic = calloc(1, sizeof(Seraph_NIC));
    if (nic == NULL) {
        return NULL;
    }

    nic->driver_data = seraph_e1000_create(bar0_phys, irq);
    if (nic->driver_data == NULL) {
        free(nic);
        return NULL;
    }

    nic->ops = seraph_e1000_get_ops();
    nic->initialized = false;

    return nic;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

void seraph_mac_to_string(const Seraph_MAC_Address* mac, char* buf) {
    if (mac == NULL || buf == NULL) {
        return;
    }
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac->bytes[0], mac->bytes[1], mac->bytes[2],
            mac->bytes[3], mac->bytes[4], mac->bytes[5]);
}

Seraph_Vbit seraph_mac_from_string(const char* str, Seraph_MAC_Address* mac) {
    if (str == NULL || mac == NULL) {
        return SERAPH_VBIT_VOID;
    }

    unsigned int b[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return SERAPH_VBIT_FALSE;
    }

    for (int i = 0; i < 6; i++) {
        if (b[i] > 255) {
            return SERAPH_VBIT_FALSE;
        }
        mac->bytes[i] = (uint8_t)b[i];
    }

    return SERAPH_VBIT_TRUE;
}
