/**
 * @file nvme.h
 * @brief MC24: The Infinite Drive - NVMe Driver Interface
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module implements the NVMe (Non-Volatile Memory Express) driver
 * that powers Atlas - SERAPH's single-level store. NVMe provides direct
 * communication with solid-state storage through PCIe.
 *
 * NVMe ARCHITECTURE:
 *
 *   NVMe uses a submission/completion queue model:
 *
 *   1. Host writes commands to Submission Queue (SQ)
 *   2. Host writes SQ tail doorbell to notify controller
 *   3. Controller fetches and processes commands
 *   4. Controller writes completions to Completion Queue (CQ)
 *   5. Controller generates MSI/MSI-X interrupt
 *   6. Host processes completions and updates CQ head doorbell
 *
 * QUEUE STRUCTURE:
 *
 *   Admin Queue: For controller management (identify, create I/O queues)
 *   I/O Queues: For actual read/write operations
 *
 * PRP (Physical Region Page) ADDRESSING:
 *
 *   NVMe uses PRPs to describe data buffers:
 *   - PRP1: First page of data
 *   - PRP2: Second page or pointer to PRP list
 *
 * INTEGRATION WITH ATLAS:
 *
 *   Atlas uses this driver for demand paging:
 *   - Page faults in Atlas region trigger NVMe reads
 *   - Dirty pages are written back via NVMe writes
 *   - Copy-on-write creates new versions on NVMe
 */

#ifndef SERAPH_DRIVERS_NVME_H
#define SERAPH_DRIVERS_NVME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "seraph/void.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * NVMe Constants
 *============================================================================*/

/** NVMe specification version this driver supports */
#define SERAPH_NVME_VERSION 0x00010400  /* 1.4.0 */

/** Queue depth (entries per queue) */
#define SERAPH_NVME_QUEUE_DEPTH 256

/** Maximum PRPs in a list (for large transfers) */
#define SERAPH_NVME_MAX_PRPS 32

/** NVMe sector size (512 bytes typically, but may vary) */
#define SERAPH_NVME_SECTOR_SIZE 512

/** Maximum transfer size (512 KB) */
#define SERAPH_NVME_MAX_TRANSFER (512 * 1024)

/** Timeout in milliseconds for admin commands */
#define SERAPH_NVME_ADMIN_TIMEOUT_MS 5000

/** Timeout in milliseconds for I/O commands */
#define SERAPH_NVME_IO_TIMEOUT_MS 30000

/*============================================================================
 * NVMe Command Structure (64 bytes)
 *============================================================================*/

/**
 * @brief NVMe command structure
 *
 * All NVMe commands are 64 bytes. The first 40 bytes are common
 * to all commands, the remaining 24 bytes (CDW10-15) are command-specific.
 */
typedef struct __attribute__((packed)) {
    /* Dword 0 */
    uint8_t  opc;           /**< Opcode */
    uint8_t  flags;         /**< Fused operation (bits 1:0), Reserved (7:2) */
    uint16_t cid;           /**< Command Identifier */

    /* Dword 1 */
    uint32_t nsid;          /**< Namespace Identifier */

    /* Dword 2-3 */
    uint64_t reserved;      /**< Reserved */

    /* Dword 4-5 */
    uint64_t mptr;          /**< Metadata Pointer */

    /* Dword 6-7 (SGL descriptor, or PRP Entry 1) */
    uint64_t prp1;          /**< PRP Entry 1 */

    /* Dword 8-9 (SGL descriptor, or PRP Entry 2) */
    uint64_t prp2;          /**< PRP Entry 2 (or PRP list pointer) */

    /* Dword 10-15: Command-specific */
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} Seraph_NVMe_Cmd;

_Static_assert(sizeof(Seraph_NVMe_Cmd) == 64, "NVMe command must be 64 bytes");

/*============================================================================
 * NVMe Completion Entry (16 bytes)
 *============================================================================*/

/**
 * @brief NVMe completion queue entry
 */
typedef struct __attribute__((packed)) {
    /* Dword 0: Command-specific */
    uint32_t dw0;

    /* Dword 1: Reserved */
    uint32_t dw1;

    /* Dword 2: SQ head pointer + SQ identifier */
    uint16_t sq_head;       /**< Submission Queue Head Pointer */
    uint16_t sq_id;         /**< Submission Queue Identifier */

    /* Dword 3: Status + CID */
    uint16_t cid;           /**< Command Identifier */
    uint16_t status;        /**< Status Field (Phase bit in bit 0) */
} Seraph_NVMe_Cpl;

_Static_assert(sizeof(Seraph_NVMe_Cpl) == 16, "NVMe completion must be 16 bytes");

/** Extract status code from completion status field */
#define SERAPH_NVME_STATUS_CODE(status)   (((status) >> 1) & 0xFF)
#define SERAPH_NVME_STATUS_TYPE(status)   (((status) >> 9) & 0x7)
#define SERAPH_NVME_STATUS_PHASE(status)  ((status) & 0x1)
#define SERAPH_NVME_STATUS_DNR(status)    (((status) >> 14) & 0x1)  /* Do Not Retry */
#define SERAPH_NVME_STATUS_MORE(status)   (((status) >> 13) & 0x1)

/*============================================================================
 * NVMe Opcodes
 *============================================================================*/

/** @name Admin Command Opcodes */
/**@{*/
#define SERAPH_NVME_ADMIN_DELETE_SQ     0x00
#define SERAPH_NVME_ADMIN_CREATE_SQ     0x01
#define SERAPH_NVME_ADMIN_GET_LOG       0x02
#define SERAPH_NVME_ADMIN_DELETE_CQ     0x04
#define SERAPH_NVME_ADMIN_CREATE_CQ     0x05
#define SERAPH_NVME_ADMIN_IDENTIFY      0x06
#define SERAPH_NVME_ADMIN_ABORT         0x08
#define SERAPH_NVME_ADMIN_SET_FEATURES  0x09
#define SERAPH_NVME_ADMIN_GET_FEATURES  0x0A
#define SERAPH_NVME_ADMIN_ASYNC_EVENT   0x0C
#define SERAPH_NVME_ADMIN_FW_COMMIT     0x10
#define SERAPH_NVME_ADMIN_FW_DOWNLOAD   0x11
/**@}*/

/** @name NVM Command Opcodes (I/O) */
/**@{*/
#define SERAPH_NVME_CMD_FLUSH           0x00
#define SERAPH_NVME_CMD_WRITE           0x01
#define SERAPH_NVME_CMD_READ            0x02
#define SERAPH_NVME_CMD_WRITE_UNCOR     0x04
#define SERAPH_NVME_CMD_COMPARE         0x05
#define SERAPH_NVME_CMD_WRITE_ZEROS     0x08
#define SERAPH_NVME_CMD_DATASET_MGMT    0x09
#define SERAPH_NVME_CMD_VERIFY          0x0C
#define SERAPH_NVME_CMD_RESERVATION_REG 0x0D
#define SERAPH_NVME_CMD_RESERVATION_REP 0x0E
#define SERAPH_NVME_CMD_RESERVATION_ACQ 0x11
#define SERAPH_NVME_CMD_RESERVATION_REL 0x15
/**@}*/

/*============================================================================
 * NVMe Controller Registers (at BAR0)
 *============================================================================*/

/** Controller register offsets */
#define SERAPH_NVME_REG_CAP     0x00    /**< Controller Capabilities (64-bit) */
#define SERAPH_NVME_REG_VS      0x08    /**< Version (32-bit) */
#define SERAPH_NVME_REG_INTMS   0x0C    /**< Interrupt Mask Set (32-bit) */
#define SERAPH_NVME_REG_INTMC   0x10    /**< Interrupt Mask Clear (32-bit) */
#define SERAPH_NVME_REG_CC      0x14    /**< Controller Configuration (32-bit) */
#define SERAPH_NVME_REG_CSTS    0x1C    /**< Controller Status (32-bit) */
#define SERAPH_NVME_REG_NSSR    0x20    /**< NVM Subsystem Reset (32-bit, optional) */
#define SERAPH_NVME_REG_AQA     0x24    /**< Admin Queue Attributes (32-bit) */
#define SERAPH_NVME_REG_ASQ     0x28    /**< Admin SQ Base Address (64-bit) */
#define SERAPH_NVME_REG_ACQ     0x30    /**< Admin CQ Base Address (64-bit) */

/** Doorbell register base (varies based on CAP.DSTRD) */
#define SERAPH_NVME_REG_SQ0TDBL 0x1000  /**< SQ0 Tail Doorbell */

/*============================================================================
 * Controller Capability Bits
 *============================================================================*/

/** CAP register bits */
#define SERAPH_NVME_CAP_MQES(cap)   ((uint16_t)((cap) & 0xFFFF))        /**< Max Queue Entries Supported */
#define SERAPH_NVME_CAP_CQR(cap)    (((cap) >> 16) & 0x1)              /**< Contiguous Queues Required */
#define SERAPH_NVME_CAP_AMS(cap)    (((cap) >> 17) & 0x3)              /**< Arbitration Mechanism Supported */
#define SERAPH_NVME_CAP_TO(cap)     (((cap) >> 24) & 0xFF)             /**< Timeout (500ms units) */
#define SERAPH_NVME_CAP_DSTRD(cap)  (((cap) >> 32) & 0xF)              /**< Doorbell Stride */
#define SERAPH_NVME_CAP_NSSRS(cap)  (((cap) >> 36) & 0x1)              /**< NVM Subsystem Reset Supported */
#define SERAPH_NVME_CAP_CSS(cap)    (((cap) >> 37) & 0xFF)             /**< Command Sets Supported */
#define SERAPH_NVME_CAP_MPSMIN(cap) (((cap) >> 48) & 0xF)              /**< Memory Page Size Minimum */
#define SERAPH_NVME_CAP_MPSMAX(cap) (((cap) >> 52) & 0xF)              /**< Memory Page Size Maximum */

/*============================================================================
 * Controller Configuration (CC) Bits
 *============================================================================*/

#define SERAPH_NVME_CC_EN       (1 << 0)   /**< Enable */
#define SERAPH_NVME_CC_CSS(css) (((css) & 0x7) << 4)  /**< I/O Command Set Selected */
#define SERAPH_NVME_CC_MPS(mps) (((mps) & 0xF) << 7)  /**< Memory Page Size */
#define SERAPH_NVME_CC_AMS(ams) (((ams) & 0x7) << 11) /**< Arbitration Mechanism */
#define SERAPH_NVME_CC_SHN(shn) (((shn) & 0x3) << 14) /**< Shutdown Notification */
#define SERAPH_NVME_CC_IOSQES(x) (((x) & 0xF) << 16)  /**< I/O SQ Entry Size (2^n) */
#define SERAPH_NVME_CC_IOCQES(x) (((x) & 0xF) << 20)  /**< I/O CQ Entry Size (2^n) */

/*============================================================================
 * Controller Status (CSTS) Bits
 *============================================================================*/

#define SERAPH_NVME_CSTS_RDY    (1 << 0)   /**< Ready */
#define SERAPH_NVME_CSTS_CFS    (1 << 1)   /**< Controller Fatal Status */
#define SERAPH_NVME_CSTS_SHST   (3 << 2)   /**< Shutdown Status mask */
#define SERAPH_NVME_CSTS_NSSRO  (1 << 4)   /**< NVM Subsystem Reset Occurred */

/*============================================================================
 * Queue Pair Structure
 *============================================================================*/

/**
 * @brief NVMe queue pair (submission + completion)
 *
 * Each I/O queue consists of a paired SQ and CQ.
 */
typedef struct {
    /** Submission queue entries */
    Seraph_NVMe_Cmd* sq;

    /** Completion queue entries */
    Seraph_NVMe_Cpl* cq;

    /** Physical addresses */
    uint64_t sq_phys;
    uint64_t cq_phys;

    /** Queue head/tail pointers */
    volatile uint32_t sq_tail;   /**< Next slot to write in SQ */
    volatile uint32_t cq_head;   /**< Next slot to read in CQ */

    /** Queue configuration */
    uint32_t depth;              /**< Number of entries */
    uint8_t  phase;              /**< Expected phase bit (toggles on wrap) */
    uint16_t qid;                /**< Queue ID */

    /** Doorbell pointers (memory-mapped registers) */
    volatile uint32_t* sq_doorbell;
    volatile uint32_t* cq_doorbell;

    /** Command ID tracking */
    uint16_t next_cid;           /**< Next command ID to use */
} Seraph_NVMe_Queue;

/*============================================================================
 * Identify Structures
 *============================================================================*/

/**
 * @brief NVMe Identify Controller data (subset of fields)
 */
typedef struct __attribute__((packed)) {
    /* Bytes 0-255: Controller Capabilities and Features */
    uint16_t vid;            /**< PCI Vendor ID */
    uint16_t ssvid;          /**< PCI Subsystem Vendor ID */
    char     sn[20];         /**< Serial Number */
    char     mn[40];         /**< Model Number */
    char     fr[8];          /**< Firmware Revision */
    uint8_t  rab;            /**< Recommended Arbitration Burst */
    uint8_t  ieee[3];        /**< IEEE OUI */
    uint8_t  cmic;           /**< Controller Multi-Path I/O and NS Sharing */
    uint8_t  mdts;           /**< Maximum Data Transfer Size */
    uint16_t cntlid;         /**< Controller ID */
    uint32_t ver;            /**< Version */
    uint32_t rtd3r;          /**< RTD3 Resume Latency */
    uint32_t rtd3e;          /**< RTD3 Entry Latency */
    uint32_t oaes;           /**< Optional Async Events Supported */
    uint32_t ctratt;         /**< Controller Attributes */
    uint8_t  reserved1[12];
    uint8_t  fguid[16];      /**< FRU Globally Unique Identifier */
    uint8_t  reserved2[128];

    /* Bytes 256-511: Admin Command Set Attributes */
    uint16_t oacs;           /**< Optional Admin Command Support */
    uint8_t  acl;            /**< Abort Command Limit */
    uint8_t  aerl;           /**< Async Event Request Limit */
    uint8_t  frmw;           /**< Firmware Updates */
    uint8_t  lpa;            /**< Log Page Attributes */
    uint8_t  elpe;           /**< Error Log Page Entries */
    uint8_t  npss;           /**< Number of Power States Support */
    uint8_t  avscc;          /**< Admin Vendor Specific Command Config */
    uint8_t  apsta;          /**< Autonomous Power State Transition Attrs */
    uint16_t wctemp;         /**< Warning Composite Temperature Threshold */
    uint16_t cctemp;         /**< Critical Composite Temperature Threshold */
    uint16_t mtfa;           /**< Maximum Time for Firmware Activation */
    uint32_t hmpre;          /**< Host Memory Buffer Preferred Size */
    uint32_t hmmin;          /**< Host Memory Buffer Minimum Size */
    uint8_t  tnvmcap[16];    /**< Total NVM Capacity */
    uint8_t  unvmcap[16];    /**< Unallocated NVM Capacity */
    uint32_t rpmbs;          /**< Replay Protected Memory Block Support */
    uint16_t edstt;          /**< Extended Device Self-test Time */
    uint8_t  dsto;           /**< Device Self-test Options */
    uint8_t  fwug;           /**< Firmware Update Granularity */
    uint8_t  reserved3[192];

    /* Bytes 512-767: NVM Command Set Attributes */
    uint8_t  sqes;           /**< Submission Queue Entry Size */
    uint8_t  cqes;           /**< Completion Queue Entry Size */
    uint16_t maxcmd;         /**< Maximum Outstanding Commands */
    uint32_t nn;             /**< Number of Namespaces */
    uint16_t oncs;           /**< Optional NVM Command Support */
    uint16_t fuses;          /**< Fused Operation Support */
    uint8_t  fna;            /**< Format NVM Attributes */
    uint8_t  vwc;            /**< Volatile Write Cache */
    uint16_t awun;           /**< Atomic Write Unit Normal */
    uint16_t awupf;          /**< Atomic Write Unit Power Fail */
    uint8_t  nvscc;          /**< NVM Vendor Specific Command Config */
    uint8_t  nwpc;           /**< Namespace Write Protection Capabilities */
    uint16_t acwu;           /**< Atomic Compare & Write Unit */
    uint8_t  reserved4[2];
    uint32_t sgls;           /**< SGL Support */
    uint32_t mnan;           /**< Maximum Number of Allowed Namespaces */
    uint8_t  reserved5[224];

    /* Bytes 768-4095: Remaining fields */
    uint8_t  subnqn[256];    /**< NVM Subsystem NVMe Qualified Name */
    uint8_t  reserved6[768];
    uint8_t  reserved7[256]; /**< I/O Command Set specific */
    uint8_t  psd[1024];      /**< Power State Descriptors */
    uint8_t  vs[1024];       /**< Vendor Specific */
} Seraph_NVMe_Identify_Controller;

/**
 * @brief NVMe Namespace data (subset of fields)
 */
typedef struct __attribute__((packed)) {
    uint64_t nsze;           /**< Namespace Size (in blocks) */
    uint64_t ncap;           /**< Namespace Capacity */
    uint64_t nuse;           /**< Namespace Utilization */
    uint8_t  nsfeat;         /**< Namespace Features */
    uint8_t  nlbaf;          /**< Number of LBA Formats */
    uint8_t  flbas;          /**< Formatted LBA Size */
    uint8_t  mc;             /**< Metadata Capabilities */
    uint8_t  dpc;            /**< End-to-end Data Protection Caps */
    uint8_t  dps;            /**< End-to-end Data Protection Settings */
    uint8_t  nmic;           /**< Namespace Multi-path I/O and NS Sharing */
    uint8_t  rescap;         /**< Reservation Capabilities */
    uint8_t  fpi;            /**< Format Progress Indicator */
    uint8_t  dlfeat;         /**< Deallocate Logical Block Features */
    uint16_t nawun;          /**< Namespace Atomic Write Unit Normal */
    uint16_t nawupf;         /**< Namespace Atomic Write Unit Power Fail */
    uint16_t nacwu;          /**< Namespace Atomic Compare & Write Unit */
    uint16_t nabsn;          /**< Namespace Atomic Boundary Size Normal */
    uint16_t nabo;           /**< Namespace Atomic Boundary Offset */
    uint16_t nabspf;         /**< Namespace Atomic Boundary Size Power Fail */
    uint16_t noiob;          /**< Namespace Optimal I/O Boundary */
    uint8_t  nvmcap[16];     /**< NVM Capacity */
    uint8_t  reserved1[40];
    uint8_t  nguid[16];      /**< Namespace GUID */
    uint8_t  eui64[8];       /**< IEEE Extended Unique Identifier */

    /** LBA Format support (up to 16 formats) */
    struct __attribute__((packed)) {
        uint16_t ms;         /**< Metadata Size */
        uint8_t  lbads;      /**< LBA Data Size (2^n) */
        uint8_t  rp;         /**< Relative Performance */
    } lbaf[16];

    uint8_t  reserved2[192];
    uint8_t  vs[3712];       /**< Vendor Specific */
} Seraph_NVMe_Identify_Namespace;

/*============================================================================
 * NVMe Controller State
 *============================================================================*/

/**
 * @brief NVMe controller state
 */
typedef struct {
    /** BAR0 mapped base address */
    volatile void* bar0;

    /** Controller capabilities */
    uint64_t cap;
    uint32_t doorbell_stride;    /**< Doorbell stride in bytes */
    uint32_t max_queue_entries;  /**< Maximum queue entries */
    uint32_t timeout_ms;         /**< Controller timeout */

    /** Admin queue */
    Seraph_NVMe_Queue admin_queue;

    /** I/O queue (single queue for simplicity) */
    Seraph_NVMe_Queue io_queue;

    /** Controller identify data */
    Seraph_NVMe_Identify_Controller* ctrl_data;

    /** Namespace identify data */
    Seraph_NVMe_Identify_Namespace* ns_data;

    /** Namespace configuration */
    uint32_t ns_id;              /**< Active namespace ID */
    uint64_t ns_size;            /**< Namespace size in blocks */
    uint32_t block_size;         /**< Block size in bytes */

    /** Command ID counter */
    uint16_t cid_counter;

    /** Status */
    bool initialized;
    bool io_queue_created;
} Seraph_NVMe;

/*============================================================================
 * NVMe API
 *============================================================================*/

/**
 * @brief Initialize NVMe controller
 *
 * @param nvme NVMe state structure
 * @param bar0_phys Physical address of BAR0
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_nvme_init(Seraph_NVMe* nvme, uint64_t bar0_phys);

/**
 * @brief Shutdown NVMe controller
 *
 * @param nvme NVMe state structure
 */
void seraph_nvme_shutdown(Seraph_NVMe* nvme);

/**
 * @brief Read blocks from NVMe
 *
 * @param nvme NVMe state
 * @param lba Starting Logical Block Address
 * @param block_count Number of blocks to read
 * @param buffer Destination buffer (must be properly aligned)
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_nvme_read(Seraph_NVMe* nvme, uint64_t lba,
                              uint32_t block_count, void* buffer);

/**
 * @brief Write blocks to NVMe
 *
 * @param nvme NVMe state
 * @param lba Starting Logical Block Address
 * @param block_count Number of blocks to write
 * @param buffer Source buffer (must be properly aligned)
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_nvme_write(Seraph_NVMe* nvme, uint64_t lba,
                               uint32_t block_count, const void* buffer);

/**
 * @brief Flush data to NVMe
 *
 * Ensures all previously written data is persisted.
 *
 * @param nvme NVMe state
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_nvme_flush(Seraph_NVMe* nvme);

/**
 * @brief Poll for command completion
 *
 * @param nvme NVMe state
 * @param queue Queue to poll
 * @param cid Command ID to wait for
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_FALSE on timeout, SERAPH_VBIT_VOID on error
 */
Seraph_Vbit seraph_nvme_poll_completion(Seraph_NVMe* nvme,
                                         Seraph_NVMe_Queue* queue,
                                         uint16_t cid);

/*============================================================================
 * NVMe Queue Management
 *============================================================================*/

/**
 * @brief Allocate and initialize a queue pair
 *
 * @param queue Queue structure to initialize
 * @param qid Queue ID
 * @param depth Number of entries
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_nvme_queue_init(Seraph_NVMe_Queue* queue,
                                    uint16_t qid, uint32_t depth);

/**
 * @brief Free queue resources
 *
 * @param queue Queue to destroy
 */
void seraph_nvme_queue_destroy(Seraph_NVMe_Queue* queue);

/**
 * @brief Submit a command to a queue
 *
 * @param nvme NVMe state
 * @param queue Target queue
 * @param cmd Command to submit
 * @return Command ID, or SERAPH_VOID_U16 on failure
 */
uint16_t seraph_nvme_submit(Seraph_NVMe* nvme, Seraph_NVMe_Queue* queue,
                             const Seraph_NVMe_Cmd* cmd);

/**
 * @brief Check for completion and retrieve result
 *
 * @param queue Queue to check
 * @param cpl Output completion entry
 * @return SERAPH_VBIT_TRUE if completion available, SERAPH_VBIT_FALSE if not
 */
Seraph_Vbit seraph_nvme_check_completion(Seraph_NVMe_Queue* queue,
                                          Seraph_NVMe_Cpl* cpl);

/*============================================================================
 * NVMe Command Construction
 *============================================================================*/

/**
 * @brief Build Identify Controller command
 */
void seraph_nvme_cmd_identify_ctrl(Seraph_NVMe_Cmd* cmd, uint64_t prp);

/**
 * @brief Build Identify Namespace command
 */
void seraph_nvme_cmd_identify_ns(Seraph_NVMe_Cmd* cmd, uint32_t nsid, uint64_t prp);

/**
 * @brief Build Create I/O CQ command
 */
void seraph_nvme_cmd_create_cq(Seraph_NVMe_Cmd* cmd, uint16_t qid,
                                uint64_t prp, uint16_t size, uint16_t vector);

/**
 * @brief Build Create I/O SQ command
 */
void seraph_nvme_cmd_create_sq(Seraph_NVMe_Cmd* cmd, uint16_t qid,
                                uint64_t prp, uint16_t size, uint16_t cqid);

/**
 * @brief Build Read command
 */
void seraph_nvme_cmd_read(Seraph_NVMe_Cmd* cmd, uint32_t nsid,
                           uint64_t lba, uint16_t blocks,
                           uint64_t prp1, uint64_t prp2);

/**
 * @brief Build Write command
 */
void seraph_nvme_cmd_write(Seraph_NVMe_Cmd* cmd, uint32_t nsid,
                            uint64_t lba, uint16_t blocks,
                            uint64_t prp1, uint64_t prp2);

/**
 * @brief Build Flush command
 */
void seraph_nvme_cmd_flush(Seraph_NVMe_Cmd* cmd, uint32_t nsid);

/*============================================================================
 * NVMe Status Codes
 *============================================================================*/

/**
 * @brief Get human-readable status string
 *
 * @param status Status code from completion
 * @return String description
 */
const char* seraph_nvme_status_str(uint16_t status);

/**
 * @brief Check if status indicates success
 */
static inline bool seraph_nvme_status_ok(uint16_t status) {
    return SERAPH_NVME_STATUS_CODE(status) == 0;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_DRIVERS_NVME_H */
