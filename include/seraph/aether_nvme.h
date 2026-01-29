/**
 * @file aether_nvme.h
 * @brief MC29: NVMe-Aether Integration - Remote DMA for Persistent Distributed Memory
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * "Storage is just memory that happens to persist. Distance is irrelevant."
 *
 * This module integrates NVMe persistent storage with the Aether distributed
 * shared memory system, enabling transparent access to remote persistent
 * storage as if it were local memory.
 *
 * KEY INNOVATIONS:
 *
 * 1. RDMA-NVME BRIDGING:
 *    Remote nodes can directly read/write to another node's NVMe storage
 *    through Aether protocol, with zero-copy where possible.
 *
 * 2. PERSISTENT DISTRIBUTED ADDRESSES:
 *    Aether addresses can reference NVMe-backed pages that survive restarts.
 *    The Atlas persistent memory system becomes network-transparent.
 *
 * 3. COHERENT PERSISTENCE:
 *    Write-back policies ensure data reaches NVMe before acknowledgment,
 *    providing distributed durability guarantees.
 *
 * 4. VOID-AWARE PERSISTENCE:
 *    NVMe errors propagate as VOID through the Aether network, with full
 *    causality tracking linking network VOID to hardware archaeology.
 *
 * ADDRESS SPACE INTEGRATION:
 *
 *   Aether addresses are divided into:
 *   - Volatile Aether: In-memory only (fast, lost on restart)
 *   - Persistent Aether: NVMe-backed (slower, survives restart)
 *
 *   Layout (within Aether range):
 *     Bit 45 = 0: Volatile distributed memory
 *     Bit 45 = 1: Persistent distributed memory (NVMe-backed)
 *
 * RDMA PROTOCOL EXTENSIONS:
 *
 *   RDMA_READ_PERSIST:  Read from remote NVMe
 *   RDMA_WRITE_PERSIST: Write to remote NVMe with persistence guarantee
 *   RDMA_SYNC_PERSIST:  Force persistence on remote node
 *   RDMA_SNAPSHOT:      Create point-in-time consistent snapshot
 */

#ifndef SERAPH_AETHER_NVME_H
#define SERAPH_AETHER_NVME_H

#include "aether.h"
#include "drivers/nvme.h"
#include "void.h"
#include "vbit.h"
#include "chronon.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Address Space Constants
 *============================================================================*/

/** Bit that distinguishes persistent from volatile in Aether addresses */
#define SERAPH_AETHER_PERSIST_BIT     45

/** Mask for persistent address detection */
#define SERAPH_AETHER_PERSIST_MASK    ((uint64_t)1 << SERAPH_AETHER_PERSIST_BIT)

/** Maximum NVMe-backed pages per node */
#define SERAPH_AETHER_NVME_MAX_PAGES  (1024 * 1024)  /* 4GB with 4K pages */

/** RDMA buffer pool size */
#define SERAPH_AETHER_RDMA_POOL_SIZE  64

/** Maximum in-flight RDMA operations */
#define SERAPH_AETHER_RDMA_MAX_INFLIGHT 32

/*============================================================================
 * RDMA Operation Types
 *============================================================================*/

/**
 * @brief Extended Aether message types for NVMe-RDMA operations
 */
typedef enum {
    /* Standard Aether messages: 0x01-0x06 */

    /* NVMe-RDMA extensions: 0x10-0x1F */
    AETHER_MSG_RDMA_READ_PERSIST   = 0x10,  /**< Read from remote NVMe */
    AETHER_MSG_RDMA_WRITE_PERSIST  = 0x11,  /**< Write to remote NVMe */
    AETHER_MSG_RDMA_SYNC_PERSIST   = 0x12,  /**< Force remote persistence */
    AETHER_MSG_RDMA_SNAPSHOT       = 0x13,  /**< Create snapshot on remote */
    AETHER_MSG_RDMA_READ_RESPONSE  = 0x14,  /**< Response with NVMe data */
    AETHER_MSG_RDMA_WRITE_COMPLETE = 0x15,  /**< Write persistence confirmed */
    AETHER_MSG_RDMA_SNAPSHOT_ACK   = 0x16,  /**< Snapshot created */
    AETHER_MSG_RDMA_ERROR          = 0x1F,  /**< RDMA error response */
} Seraph_Aether_RDMA_Msg;

/**
 * @brief RDMA operation status codes
 */
typedef enum {
    SERAPH_RDMA_OK = 0,             /**< Operation succeeded */
    SERAPH_RDMA_PENDING,            /**< Operation in progress */
    SERAPH_RDMA_TIMEOUT,            /**< Operation timed out */
    SERAPH_RDMA_NVME_ERROR,         /**< NVMe error on remote node */
    SERAPH_RDMA_NETWORK_ERROR,      /**< Network error during transfer */
    SERAPH_RDMA_GENERATION_MISMATCH,/**< Capability revoked */
    SERAPH_RDMA_NOT_FOUND,          /**< Page not found on remote */
    SERAPH_RDMA_PERMISSION_DENIED,  /**< Permission error */
    SERAPH_RDMA_OUT_OF_MEMORY,      /**< Memory allocation failed */
    SERAPH_RDMA_VOID,               /**< Generic VOID propagation */
} Seraph_RDMA_Status;

/*============================================================================
 * RDMA Operation Structures
 *============================================================================*/

/**
 * @brief RDMA operation descriptor
 *
 * Tracks in-flight RDMA operations for completion matching and timeout handling.
 */
typedef struct {
    uint64_t operation_id;          /**< Unique operation ID */
    uint64_t aether_addr;           /**< Target Aether address */
    uint64_t nvme_lba;              /**< NVMe LBA (for local operations) */
    uint32_t block_count;           /**< Number of NVMe blocks */
    uint16_t remote_node;           /**< Remote node ID */
    uint16_t local_node;            /**< Local node ID */
    Seraph_Aether_RDMA_Msg type;    /**< Operation type */
    Seraph_RDMA_Status status;      /**< Current status */
    Seraph_Chronon start_time;      /**< When operation started */
    Seraph_Chronon timeout;         /**< Timeout deadline */
    void* buffer;                   /**< Data buffer */
    size_t buffer_size;             /**< Buffer size */
    uint64_t generation;            /**< Capability generation */
    uint64_t void_id;               /**< VOID ID if error occurred */
    bool completed;                 /**< Is operation complete? */
    bool persisted;                 /**< Has data reached NVMe? */
} Seraph_RDMA_Op;

/**
 * @brief RDMA buffer descriptor
 *
 * Pre-allocated DMA-capable buffers for zero-copy operations.
 */
typedef struct {
    void* buffer;                   /**< DMA buffer address */
    uint64_t phys_addr;             /**< Physical address for DMA */
    size_t size;                    /**< Buffer size */
    bool in_use;                    /**< Is buffer currently in use? */
    uint64_t operation_id;          /**< Associated operation (if in_use) */
} Seraph_RDMA_Buffer;

/**
 * @brief NVMe-backed page mapping
 *
 * Maps Aether offsets to NVMe LBAs for persistent pages.
 */
typedef struct {
    uint64_t aether_offset;         /**< Offset within node's Aether space */
    uint64_t nvme_lba;              /**< Starting LBA on NVMe */
    uint32_t page_count;            /**< Number of contiguous pages */
    uint64_t generation;            /**< Current generation */
    bool allocated;                 /**< Is mapping active? */
    bool dirty;                     /**< Has in-memory copy been modified? */
} Seraph_Aether_NVMe_Mapping;

/**
 * @brief Aether-NVMe integration state
 */
typedef struct {
    /* Core components */
    Seraph_NVMe* nvme;              /**< NVMe controller */
    Seraph_Aether* aether;          /**< Aether DSM state */
    uint16_t local_node_id;         /**< Our node ID */

    /* NVMe mapping table */
    Seraph_Aether_NVMe_Mapping* mappings;
    size_t mapping_count;
    size_t mapping_capacity;
    uint64_t next_lba;              /**< Next free LBA for allocation */
    uint64_t total_lbas;            /**< Total LBAs on NVMe */

    /* RDMA buffer pool */
    Seraph_RDMA_Buffer buffer_pool[SERAPH_AETHER_RDMA_POOL_SIZE];

    /* In-flight operations */
    Seraph_RDMA_Op inflight[SERAPH_AETHER_RDMA_MAX_INFLIGHT];
    uint64_t next_op_id;            /**< Next operation ID */
    uint32_t inflight_count;        /**< Number of in-flight ops */

    /* Statistics */
    uint64_t rdma_reads;            /**< Total RDMA reads */
    uint64_t rdma_writes;           /**< Total RDMA writes */
    uint64_t rdma_syncs;            /**< Total RDMA syncs */
    uint64_t rdma_errors;           /**< Total RDMA errors */
    uint64_t nvme_read_bytes;       /**< Bytes read from NVMe */
    uint64_t nvme_write_bytes;      /**< Bytes written to NVMe */

    /* VOID tracking */
    uint64_t last_void_id;          /**< Last VOID from RDMA error */

    bool initialized;
} Seraph_Aether_NVMe;

/*============================================================================
 * Initialization API
 *============================================================================*/

/**
 * @brief Initialize Aether-NVMe integration
 *
 * @param an Aether-NVMe state to initialize
 * @param nvme Initialized NVMe controller
 * @param aether Initialized Aether DSM state
 * @param node_id Local node ID
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_aether_nvme_init(
    Seraph_Aether_NVMe* an,
    Seraph_NVMe* nvme,
    Seraph_Aether* aether,
    uint16_t node_id
);

/**
 * @brief Shutdown Aether-NVMe integration
 */
void seraph_aether_nvme_shutdown(Seraph_Aether_NVMe* an);

/*============================================================================
 * Address Utilities
 *============================================================================*/

/**
 * @brief Check if Aether address is persistent (NVMe-backed)
 */
static inline bool seraph_aether_is_persistent(uint64_t addr) {
    return (addr & SERAPH_AETHER_PERSIST_MASK) != 0;
}

/**
 * @brief Make Aether address persistent
 */
static inline uint64_t seraph_aether_make_persistent(uint64_t addr) {
    return addr | SERAPH_AETHER_PERSIST_MASK;
}

/**
 * @brief Make Aether address volatile
 */
static inline uint64_t seraph_aether_make_volatile(uint64_t addr) {
    return addr & ~SERAPH_AETHER_PERSIST_MASK;
}

/*============================================================================
 * Page Mapping API
 *============================================================================*/

/**
 * @brief Allocate persistent NVMe-backed pages
 *
 * @param an Aether-NVMe state
 * @param page_count Number of pages to allocate
 * @return Aether address of allocated pages, or SERAPH_VOID_U64 on failure
 */
uint64_t seraph_aether_nvme_alloc(Seraph_Aether_NVMe* an, size_t page_count);

/**
 * @brief Free persistent pages
 */
void seraph_aether_nvme_free(Seraph_Aether_NVMe* an, uint64_t addr, size_t page_count);

/**
 * @brief Get NVMe LBA for an Aether address
 *
 * @param an Aether-NVMe state
 * @param addr Aether address (must be persistent)
 * @return NVMe LBA, or SERAPH_VOID_U64 if not mapped
 */
uint64_t seraph_aether_nvme_get_lba(const Seraph_Aether_NVMe* an, uint64_t addr);

/*============================================================================
 * RDMA Operations API
 *============================================================================*/

/**
 * @brief Read from remote persistent storage
 *
 * Initiates an RDMA read from another node's NVMe storage.
 * The operation completes asynchronously.
 *
 * @param an Aether-NVMe state
 * @param remote_node Target node ID
 * @param remote_addr Aether address on remote node
 * @param buffer Local buffer for received data
 * @param size Number of bytes to read
 * @param generation Expected capability generation
 * @return Operation ID, or SERAPH_VOID_U64 on failure
 */
uint64_t seraph_aether_rdma_read(
    Seraph_Aether_NVMe* an,
    uint16_t remote_node,
    uint64_t remote_addr,
    void* buffer,
    size_t size,
    uint64_t generation
);

/**
 * @brief Write to remote persistent storage
 *
 * Initiates an RDMA write to another node's NVMe storage.
 * Returns after data reaches remote NVMe (persistence guarantee).
 *
 * @param an Aether-NVMe state
 * @param remote_node Target node ID
 * @param remote_addr Aether address on remote node
 * @param data Data to write
 * @param size Number of bytes to write
 * @param generation Expected capability generation
 * @return Operation ID, or SERAPH_VOID_U64 on failure
 */
uint64_t seraph_aether_rdma_write(
    Seraph_Aether_NVMe* an,
    uint16_t remote_node,
    uint64_t remote_addr,
    const void* data,
    size_t size,
    uint64_t generation
);

/**
 * @brief Force persistence on remote node
 *
 * Ensures all previous writes to the remote node have reached NVMe.
 *
 * @param an Aether-NVMe state
 * @param remote_node Target node ID
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_aether_rdma_sync(Seraph_Aether_NVMe* an, uint16_t remote_node);

/**
 * @brief Check RDMA operation status
 *
 * @param an Aether-NVMe state
 * @param op_id Operation ID from read/write call
 * @return Current operation status
 */
Seraph_RDMA_Status seraph_aether_rdma_status(
    const Seraph_Aether_NVMe* an,
    uint64_t op_id
);

/**
 * @brief Wait for RDMA operation completion
 *
 * @param an Aether-NVMe state
 * @param op_id Operation ID
 * @param timeout_ns Timeout in nanoseconds (0 = infinite)
 * @return Final operation status
 */
Seraph_RDMA_Status seraph_aether_rdma_wait(
    Seraph_Aether_NVMe* an,
    uint64_t op_id,
    uint64_t timeout_ns
);

/*============================================================================
 * Local Operations (Handle Remote Requests)
 *============================================================================*/

/**
 * @brief Handle RDMA read request from remote node
 *
 * Reads data from local NVMe and prepares response.
 *
 * @param an Aether-NVMe state
 * @param requester_node Node that sent the request
 * @param local_addr Local Aether address
 * @param size Requested size
 * @param generation Expected generation
 * @return Response status and data
 */
Seraph_Aether_Response seraph_aether_nvme_handle_read(
    Seraph_Aether_NVMe* an,
    uint16_t requester_node,
    uint64_t local_addr,
    size_t size,
    uint64_t generation
);

/**
 * @brief Handle RDMA write request from remote node
 *
 * Writes data to local NVMe with persistence guarantee.
 *
 * @param an Aether-NVMe state
 * @param requester_node Node that sent the request
 * @param local_addr Local Aether address
 * @param data Data to write
 * @param size Data size
 * @param generation Expected generation
 * @return Response status
 */
Seraph_Aether_Response seraph_aether_nvme_handle_write(
    Seraph_Aether_NVMe* an,
    uint16_t requester_node,
    uint64_t local_addr,
    const void* data,
    size_t size,
    uint64_t generation
);

/**
 * @brief Handle sync request from remote node
 *
 * Forces all pending writes to NVMe.
 *
 * @param an Aether-NVMe state
 * @return Response status
 */
Seraph_Aether_Response seraph_aether_nvme_handle_sync(Seraph_Aether_NVMe* an);

/*============================================================================
 * Page Fault Integration
 *============================================================================*/

/**
 * @brief Handle page fault for persistent Aether address
 *
 * Called by VMM when a page fault occurs in persistent Aether space.
 * Loads page from local NVMe or fetches from remote node.
 *
 * @param an Aether-NVMe state
 * @param addr Faulting address
 * @param write Was this a write access?
 * @return Pointer to page data, or NULL on VOID
 */
void* seraph_aether_nvme_page_fault(
    Seraph_Aether_NVMe* an,
    uint64_t addr,
    bool write
);

/**
 * @brief Write back dirty page to NVMe
 *
 * @param an Aether-NVMe state
 * @param addr Page address
 * @param data Page data
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_aether_nvme_writeback(
    Seraph_Aether_NVMe* an,
    uint64_t addr,
    const void* data
);

/*============================================================================
 * Snapshot API
 *============================================================================*/

/**
 * @brief Create point-in-time snapshot of persistent Aether region
 *
 * @param an Aether-NVMe state
 * @param start_addr Start of region
 * @param end_addr End of region
 * @param snapshot_id Output: snapshot identifier
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_aether_nvme_create_snapshot(
    Seraph_Aether_NVMe* an,
    uint64_t start_addr,
    uint64_t end_addr,
    uint64_t* snapshot_id
);

/**
 * @brief Restore from snapshot
 *
 * @param an Aether-NVMe state
 * @param snapshot_id Snapshot to restore
 * @return SERAPH_VBIT_TRUE on success
 */
Seraph_Vbit seraph_aether_nvme_restore_snapshot(
    Seraph_Aether_NVMe* an,
    uint64_t snapshot_id
);

/*============================================================================
 * VOID Integration
 *============================================================================*/

/**
 * @brief Get last VOID ID from RDMA error
 */
uint64_t seraph_aether_nvme_get_last_void(const Seraph_Aether_NVMe* an);

/**
 * @brief Map RDMA status to VOID reason
 */
Seraph_VoidReason seraph_aether_rdma_status_to_void_reason(Seraph_RDMA_Status status);

/*============================================================================
 * Statistics
 *============================================================================*/

/**
 * @brief Get Aether-NVMe statistics
 */
void seraph_aether_nvme_get_stats(
    const Seraph_Aether_NVMe* an,
    uint64_t* rdma_reads,
    uint64_t* rdma_writes,
    uint64_t* rdma_syncs,
    uint64_t* rdma_errors,
    uint64_t* nvme_read_bytes,
    uint64_t* nvme_write_bytes
);

/**
 * @brief Reset statistics
 */
void seraph_aether_nvme_reset_stats(Seraph_Aether_NVMe* an);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_AETHER_NVME_H */
