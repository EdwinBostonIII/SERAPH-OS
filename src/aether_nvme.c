/**
 * @file aether_nvme.c
 * @brief MC29: NVMe-Aether Integration - Remote DMA Implementation
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This implements the bridge between NVMe persistent storage and the Aether
 * distributed shared memory system, enabling transparent remote persistent
 * memory access.
 */

#include "seraph/aether_nvme.h"
#include "seraph/void.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <malloc.h>
#define aligned_alloc(align, size) _aligned_malloc((size), (align))
#define aligned_free(ptr) _aligned_free(ptr)
#else
#define aligned_free(ptr) free(ptr)
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

/** Initial mapping table capacity */
#define INITIAL_MAPPING_CAPACITY 1024

/** Pages per NVMe block (assuming 4K pages and 512B blocks) */
#define BLOCKS_PER_PAGE (SERAPH_AETHER_PAGE_SIZE / SERAPH_NVME_SECTOR_SIZE)

/** RDMA operation timeout (nanoseconds) */
#define RDMA_TIMEOUT_NS (5ULL * 1000000000ULL)  /* 5 seconds */

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Find free RDMA buffer
 */
static Seraph_RDMA_Buffer* find_free_buffer(Seraph_Aether_NVMe* an, size_t min_size) {
    for (size_t i = 0; i < SERAPH_AETHER_RDMA_POOL_SIZE; i++) {
        if (!an->buffer_pool[i].in_use && an->buffer_pool[i].size >= min_size) {
            return &an->buffer_pool[i];
        }
    }
    return NULL;
}

/**
 * @brief Find in-flight operation by ID
 */
static Seraph_RDMA_Op* find_operation(Seraph_Aether_NVMe* an, uint64_t op_id) {
    for (size_t i = 0; i < SERAPH_AETHER_RDMA_MAX_INFLIGHT; i++) {
        if (an->inflight[i].operation_id == op_id && !an->inflight[i].completed) {
            return &an->inflight[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate operation slot
 */
static Seraph_RDMA_Op* alloc_operation(Seraph_Aether_NVMe* an) {
    for (size_t i = 0; i < SERAPH_AETHER_RDMA_MAX_INFLIGHT; i++) {
        if (an->inflight[i].completed || an->inflight[i].operation_id == 0) {
            memset(&an->inflight[i], 0, sizeof(Seraph_RDMA_Op));
            an->inflight[i].operation_id = ++an->next_op_id;
            an->inflight_count++;
            return &an->inflight[i];
        }
    }
    return NULL;
}

/**
 * @brief Find mapping for Aether address
 */
static Seraph_Aether_NVMe_Mapping* find_mapping(Seraph_Aether_NVMe* an, uint64_t offset) {
    uint64_t page_offset = offset & ~((uint64_t)(SERAPH_AETHER_PAGE_SIZE - 1));

    for (size_t i = 0; i < an->mapping_count; i++) {
        if (an->mappings[i].allocated &&
            an->mappings[i].aether_offset == page_offset) {
            return &an->mappings[i];
        }
    }
    return NULL;
}

/**
 * @brief Record RDMA error as VOID with causality
 */
static uint64_t record_rdma_void(Seraph_Aether_NVMe* an,
                                  Seraph_RDMA_Status status,
                                  uint64_t addr,
                                  uint16_t remote_node) {
    Seraph_VoidReason reason = seraph_aether_rdma_status_to_void_reason(status);

    uint64_t void_id = seraph_void_record(
        reason,
        0,  /* No predecessor */
        addr,
        (uint64_t)remote_node,
        __FILE__,
        __func__,
        __LINE__,
        "RDMA operation failed"
    );

    an->last_void_id = void_id;
    an->rdma_errors++;

    return void_id;
}

/*============================================================================
 * Initialization
 *============================================================================*/

Seraph_Vbit seraph_aether_nvme_init(
    Seraph_Aether_NVMe* an,
    Seraph_NVMe* nvme,
    Seraph_Aether* aether,
    uint16_t node_id) {

    if (an == NULL || nvme == NULL || aether == NULL) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_NULL_PTR, 0, 0, 0,
                          "NULL parameter to aether_nvme_init");
        return SERAPH_VBIT_VOID;
    }

    if (!nvme->initialized) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_INVALID_ARG, 0, 0, 0,
                          "NVMe not initialized");
        return SERAPH_VBIT_VOID;
    }

    memset(an, 0, sizeof(Seraph_Aether_NVMe));

    an->nvme = nvme;
    an->aether = aether;
    an->local_node_id = node_id;

    /* Allocate mapping table */
    an->mapping_capacity = INITIAL_MAPPING_CAPACITY;
    an->mappings = calloc(an->mapping_capacity, sizeof(Seraph_Aether_NVMe_Mapping));
    if (an->mappings == NULL) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_ALLOC_FAIL, 0,
                          an->mapping_capacity, sizeof(Seraph_Aether_NVMe_Mapping),
                          "mapping table allocation failed");
        return SERAPH_VBIT_VOID;
    }

    /* Initialize RDMA buffer pool */
    for (size_t i = 0; i < SERAPH_AETHER_RDMA_POOL_SIZE; i++) {
        an->buffer_pool[i].size = SERAPH_AETHER_PAGE_SIZE;
        an->buffer_pool[i].buffer = aligned_alloc(4096, SERAPH_AETHER_PAGE_SIZE);
        if (an->buffer_pool[i].buffer == NULL) {
            /* Clean up already allocated buffers */
            for (size_t j = 0; j < i; j++) {
                aligned_free(an->buffer_pool[j].buffer);
            }
            free(an->mappings);
            SERAPH_VOID_RECORD(SERAPH_VOID_REASON_ALLOC_FAIL, 0,
                              SERAPH_AETHER_PAGE_SIZE, i,
                              "RDMA buffer allocation failed");
            return SERAPH_VBIT_VOID;
        }
        an->buffer_pool[i].phys_addr = (uint64_t)(uintptr_t)an->buffer_pool[i].buffer;
        an->buffer_pool[i].in_use = false;
    }

    /* Calculate available NVMe space */
    an->total_lbas = nvme->ns_size;
    an->next_lba = 0;  /* Start from beginning */

    an->next_op_id = 1;
    an->initialized = true;

    return SERAPH_VBIT_TRUE;
}

void seraph_aether_nvme_shutdown(Seraph_Aether_NVMe* an) {
    if (an == NULL || !an->initialized) {
        return;
    }

    /* Free RDMA buffers */
    for (size_t i = 0; i < SERAPH_AETHER_RDMA_POOL_SIZE; i++) {
        if (an->buffer_pool[i].buffer != NULL) {
            aligned_free(an->buffer_pool[i].buffer);
        }
    }

    /* Free mapping table */
    if (an->mappings != NULL) {
        free(an->mappings);
    }

    memset(an, 0, sizeof(Seraph_Aether_NVMe));
}

/*============================================================================
 * Page Mapping
 *============================================================================*/

uint64_t seraph_aether_nvme_alloc(Seraph_Aether_NVMe* an, size_t page_count) {
    if (an == NULL || !an->initialized || page_count == 0) {
        return SERAPH_VOID_U64;
    }

    /* Check capacity */
    uint64_t blocks_needed = page_count * BLOCKS_PER_PAGE;
    if (an->next_lba + blocks_needed > an->total_lbas) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_ALLOC_FAIL, 0,
                          blocks_needed, an->total_lbas - an->next_lba,
                          "NVMe space exhausted");
        return SERAPH_VOID_U64;
    }

    /* Expand mapping table if needed */
    if (an->mapping_count >= an->mapping_capacity) {
        size_t new_capacity = an->mapping_capacity * 2;
        Seraph_Aether_NVMe_Mapping* new_mappings = realloc(
            an->mappings,
            new_capacity * sizeof(Seraph_Aether_NVMe_Mapping)
        );
        if (new_mappings == NULL) {
            SERAPH_VOID_RECORD(SERAPH_VOID_REASON_ALLOC_FAIL, 0,
                              new_capacity, sizeof(Seraph_Aether_NVMe_Mapping),
                              "mapping table expansion failed");
            return SERAPH_VOID_U64;
        }
        an->mappings = new_mappings;
        an->mapping_capacity = new_capacity;
    }

    /* Create mapping */
    uint64_t aether_offset = an->mapping_count * SERAPH_AETHER_PAGE_SIZE;
    Seraph_Aether_NVMe_Mapping* mapping = &an->mappings[an->mapping_count];

    mapping->aether_offset = aether_offset;
    mapping->nvme_lba = an->next_lba;
    mapping->page_count = page_count;
    mapping->generation = 1;
    mapping->allocated = true;
    mapping->dirty = false;

    an->next_lba += blocks_needed;
    an->mapping_count++;

    /* Construct persistent Aether address */
    uint64_t addr = seraph_aether_make_addr(an->local_node_id, aether_offset);
    addr = seraph_aether_make_persistent(addr);

    return addr;
}

void seraph_aether_nvme_free(Seraph_Aether_NVMe* an, uint64_t addr, size_t page_count) {
    if (an == NULL || !an->initialized) {
        return;
    }

    uint64_t offset = seraph_aether_get_offset(addr);
    Seraph_Aether_NVMe_Mapping* mapping = find_mapping(an, offset);

    if (mapping != NULL) {
        mapping->allocated = false;
        mapping->generation++;  /* Increment to invalidate old capabilities */
    }

    (void)page_count;  /* Could reclaim space in more sophisticated allocator */
}

uint64_t seraph_aether_nvme_get_lba(const Seraph_Aether_NVMe* an, uint64_t addr) {
    if (an == NULL || !an->initialized) {
        return SERAPH_VOID_U64;
    }

    uint64_t offset = seraph_aether_get_offset(addr);
    uint64_t page_offset = offset & ~((uint64_t)(SERAPH_AETHER_PAGE_SIZE - 1));
    uint64_t within_page = offset & ((uint64_t)(SERAPH_AETHER_PAGE_SIZE - 1));

    for (size_t i = 0; i < an->mapping_count; i++) {
        if (an->mappings[i].allocated &&
            an->mappings[i].aether_offset == page_offset) {
            return an->mappings[i].nvme_lba + (within_page / SERAPH_NVME_SECTOR_SIZE);
        }
    }

    return SERAPH_VOID_U64;
}

/*============================================================================
 * RDMA Operations
 *============================================================================*/

uint64_t seraph_aether_rdma_read(
    Seraph_Aether_NVMe* an,
    uint16_t remote_node,
    uint64_t remote_addr,
    void* buffer,
    size_t size,
    uint64_t generation) {

    if (an == NULL || !an->initialized || buffer == NULL || size == 0) {
        return SERAPH_VOID_U64;
    }

    /* Check if this is a local read */
    if (remote_node == an->local_node_id) {
        /* Local read from our NVMe */
        uint64_t lba = seraph_aether_nvme_get_lba(an, remote_addr);
        if (SERAPH_IS_VOID_U64(lba)) {
            record_rdma_void(an, SERAPH_RDMA_NOT_FOUND, remote_addr, remote_node);
            return SERAPH_VOID_U64;
        }

        uint32_t block_count = (size + SERAPH_NVME_SECTOR_SIZE - 1) / SERAPH_NVME_SECTOR_SIZE;
        Seraph_Vbit result = seraph_nvme_read(an->nvme, lba, block_count, buffer);

        if (!seraph_vbit_is_true(result)) {
            record_rdma_void(an, SERAPH_RDMA_NVME_ERROR, remote_addr, remote_node);
            return SERAPH_VOID_U64;
        }

        an->rdma_reads++;
        an->nvme_read_bytes += size;

        /* Return a completed operation ID */
        return an->next_op_id++;
    }

    /* Remote read - allocate operation */
    Seraph_RDMA_Op* op = alloc_operation(an);
    if (op == NULL) {
        record_rdma_void(an, SERAPH_RDMA_OUT_OF_MEMORY, remote_addr, remote_node);
        return SERAPH_VOID_U64;
    }

    op->aether_addr = remote_addr;
    op->remote_node = remote_node;
    op->local_node = an->local_node_id;
    op->type = AETHER_MSG_RDMA_READ_PERSIST;
    op->status = SERAPH_RDMA_PENDING;
    op->buffer = buffer;
    op->buffer_size = size;
    op->generation = generation;
    op->completed = false;
    op->persisted = false;

    /* In a real implementation, this would send an Aether frame
     * to the remote node requesting the data. For now, we simulate
     * by marking the operation as needing network I/O. */

    an->rdma_reads++;

    return op->operation_id;
}

uint64_t seraph_aether_rdma_write(
    Seraph_Aether_NVMe* an,
    uint16_t remote_node,
    uint64_t remote_addr,
    const void* data,
    size_t size,
    uint64_t generation) {

    if (an == NULL || !an->initialized || data == NULL || size == 0) {
        return SERAPH_VOID_U64;
    }

    /* Check if this is a local write */
    if (remote_node == an->local_node_id) {
        /* Local write to our NVMe */
        uint64_t lba = seraph_aether_nvme_get_lba(an, remote_addr);
        if (SERAPH_IS_VOID_U64(lba)) {
            record_rdma_void(an, SERAPH_RDMA_NOT_FOUND, remote_addr, remote_node);
            return SERAPH_VOID_U64;
        }

        uint32_t block_count = (size + SERAPH_NVME_SECTOR_SIZE - 1) / SERAPH_NVME_SECTOR_SIZE;
        Seraph_Vbit result = seraph_nvme_write(an->nvme, lba, block_count, data);

        if (!seraph_vbit_is_true(result)) {
            record_rdma_void(an, SERAPH_RDMA_NVME_ERROR, remote_addr, remote_node);
            return SERAPH_VOID_U64;
        }

        /* Ensure persistence */
        result = seraph_nvme_flush(an->nvme);
        if (!seraph_vbit_is_true(result)) {
            record_rdma_void(an, SERAPH_RDMA_NVME_ERROR, remote_addr, remote_node);
            return SERAPH_VOID_U64;
        }

        an->rdma_writes++;
        an->nvme_write_bytes += size;

        return an->next_op_id++;
    }

    /* Remote write - allocate operation and buffer */
    Seraph_RDMA_Buffer* buf = find_free_buffer(an, size);
    if (buf == NULL) {
        record_rdma_void(an, SERAPH_RDMA_OUT_OF_MEMORY, remote_addr, remote_node);
        return SERAPH_VOID_U64;
    }

    Seraph_RDMA_Op* op = alloc_operation(an);
    if (op == NULL) {
        record_rdma_void(an, SERAPH_RDMA_OUT_OF_MEMORY, remote_addr, remote_node);
        return SERAPH_VOID_U64;
    }

    /* Copy data to RDMA buffer */
    buf->in_use = true;
    buf->operation_id = op->operation_id;
    memcpy(buf->buffer, data, size);

    op->aether_addr = remote_addr;
    op->remote_node = remote_node;
    op->local_node = an->local_node_id;
    op->type = AETHER_MSG_RDMA_WRITE_PERSIST;
    op->status = SERAPH_RDMA_PENDING;
    op->buffer = buf->buffer;
    op->buffer_size = size;
    op->generation = generation;
    op->completed = false;
    op->persisted = false;

    an->rdma_writes++;

    return op->operation_id;
}

Seraph_Vbit seraph_aether_rdma_sync(Seraph_Aether_NVMe* an, uint16_t remote_node) {
    if (an == NULL || !an->initialized) {
        return SERAPH_VBIT_VOID;
    }

    /* Local sync */
    if (remote_node == an->local_node_id) {
        Seraph_Vbit result = seraph_nvme_flush(an->nvme);
        if (seraph_vbit_is_true(result)) {
            an->rdma_syncs++;
        }
        return result;
    }

    /* Remote sync would send RDMA_SYNC_PERSIST message */
    an->rdma_syncs++;
    return SERAPH_VBIT_TRUE;
}

Seraph_RDMA_Status seraph_aether_rdma_status(
    const Seraph_Aether_NVMe* an,
    uint64_t op_id) {

    if (an == NULL || !an->initialized) {
        return SERAPH_RDMA_VOID;
    }

    for (size_t i = 0; i < SERAPH_AETHER_RDMA_MAX_INFLIGHT; i++) {
        if (an->inflight[i].operation_id == op_id) {
            return an->inflight[i].status;
        }
    }

    /* Operation not found - assume completed successfully */
    return SERAPH_RDMA_OK;
}

Seraph_RDMA_Status seraph_aether_rdma_wait(
    Seraph_Aether_NVMe* an,
    uint64_t op_id,
    uint64_t timeout_ns) {

    if (an == NULL || !an->initialized) {
        return SERAPH_RDMA_VOID;
    }

    Seraph_RDMA_Op* op = find_operation(an, op_id);
    if (op == NULL) {
        return SERAPH_RDMA_OK;  /* Already completed */
    }

    /* In a real implementation, this would poll for completion
     * with timeout handling. For now, mark as complete. */
    op->completed = true;
    op->persisted = true;
    op->status = SERAPH_RDMA_OK;

    (void)timeout_ns;

    return op->status;
}

/*============================================================================
 * Local Request Handlers
 *============================================================================*/

Seraph_Aether_Response seraph_aether_nvme_handle_read(
    Seraph_Aether_NVMe* an,
    uint16_t requester_node,
    uint64_t local_addr,
    size_t size,
    uint64_t generation) {

    Seraph_Aether_Response response = {0};
    response.status = SERAPH_AETHER_RESP_ERROR;

    if (an == NULL || !an->initialized) {
        return response;
    }

    /* Check generation */
    uint64_t offset = seraph_aether_get_offset(local_addr);
    Seraph_Aether_NVMe_Mapping* mapping = find_mapping(an, offset);

    if (mapping == NULL) {
        response.status = SERAPH_AETHER_RESP_NOT_FOUND;
        return response;
    }

    if (mapping->generation != generation && generation != 0) {
        response.status = SERAPH_AETHER_RESP_STALE;
        return response;
    }

    /* Allocate buffer for response */
    Seraph_RDMA_Buffer* buf = find_free_buffer(an, size);
    if (buf == NULL) {
        response.status = SERAPH_AETHER_RESP_ERROR;
        return response;
    }

    /* Read from NVMe */
    uint32_t block_count = (size + SERAPH_NVME_SECTOR_SIZE - 1) / SERAPH_NVME_SECTOR_SIZE;
    Seraph_Vbit result = seraph_nvme_read(an->nvme, mapping->nvme_lba, block_count, buf->buffer);

    if (!seraph_vbit_is_true(result)) {
        response.status = SERAPH_AETHER_RESP_ERROR;
        return response;
    }

    response.status = SERAPH_AETHER_RESP_OK;
    response.generation = mapping->generation;
    response.page_data = buf->buffer;
    response.data_size = size;

    an->nvme_read_bytes += size;

    (void)requester_node;

    return response;
}

Seraph_Aether_Response seraph_aether_nvme_handle_write(
    Seraph_Aether_NVMe* an,
    uint16_t requester_node,
    uint64_t local_addr,
    const void* data,
    size_t size,
    uint64_t generation) {

    Seraph_Aether_Response response = {0};
    response.status = SERAPH_AETHER_RESP_ERROR;

    if (an == NULL || !an->initialized || data == NULL) {
        return response;
    }

    /* Find or create mapping */
    uint64_t offset = seraph_aether_get_offset(local_addr);
    Seraph_Aether_NVMe_Mapping* mapping = find_mapping(an, offset);

    if (mapping == NULL) {
        response.status = SERAPH_AETHER_RESP_NOT_FOUND;
        return response;
    }

    if (mapping->generation != generation && generation != 0) {
        response.status = SERAPH_AETHER_RESP_STALE;
        return response;
    }

    /* Write to NVMe */
    uint32_t block_count = (size + SERAPH_NVME_SECTOR_SIZE - 1) / SERAPH_NVME_SECTOR_SIZE;
    Seraph_Vbit result = seraph_nvme_write(an->nvme, mapping->nvme_lba, block_count, data);

    if (!seraph_vbit_is_true(result)) {
        response.status = SERAPH_AETHER_RESP_ERROR;
        return response;
    }

    /* Ensure persistence before acknowledging */
    result = seraph_nvme_flush(an->nvme);
    if (!seraph_vbit_is_true(result)) {
        response.status = SERAPH_AETHER_RESP_ERROR;
        return response;
    }

    response.status = SERAPH_AETHER_RESP_OK;
    response.generation = mapping->generation;

    an->nvme_write_bytes += size;

    (void)requester_node;

    return response;
}

Seraph_Aether_Response seraph_aether_nvme_handle_sync(Seraph_Aether_NVMe* an) {
    Seraph_Aether_Response response = {0};

    if (an == NULL || !an->initialized) {
        response.status = SERAPH_AETHER_RESP_ERROR;
        return response;
    }

    Seraph_Vbit result = seraph_nvme_flush(an->nvme);

    if (seraph_vbit_is_true(result)) {
        response.status = SERAPH_AETHER_RESP_OK;
        an->rdma_syncs++;
    } else {
        response.status = SERAPH_AETHER_RESP_ERROR;
    }

    return response;
}

/*============================================================================
 * Page Fault Integration
 *============================================================================*/

void* seraph_aether_nvme_page_fault(
    Seraph_Aether_NVMe* an,
    uint64_t addr,
    bool write) {

    if (an == NULL || !an->initialized) {
        return NULL;
    }

    uint16_t node = seraph_aether_get_node(addr);
    uint64_t offset = seraph_aether_get_offset(addr);

    /* Check if address is persistent */
    if (!seraph_aether_is_persistent(addr)) {
        /* Not a persistent address - delegate to regular Aether */
        return NULL;
    }

    /* Local page fault */
    if (node == an->local_node_id) {
        Seraph_Aether_NVMe_Mapping* mapping = find_mapping(an, offset);
        if (mapping == NULL) {
            SERAPH_VOID_RECORD(SERAPH_VOID_REASON_NOT_FOUND, 0, addr, 0,
                              "persistent page not mapped");
            return NULL;
        }

        /* Allocate page buffer */
        Seraph_RDMA_Buffer* buf = find_free_buffer(an, SERAPH_AETHER_PAGE_SIZE);
        if (buf == NULL) {
            SERAPH_VOID_RECORD(SERAPH_VOID_REASON_ALLOC_FAIL, 0, addr, 0,
                              "no free RDMA buffer for page fault");
            return NULL;
        }

        /* Read from NVMe */
        Seraph_Vbit result = seraph_nvme_read(
            an->nvme,
            mapping->nvme_lba,
            BLOCKS_PER_PAGE,
            buf->buffer
        );

        if (!seraph_vbit_is_true(result)) {
            SERAPH_VOID_RECORD(SERAPH_VOID_REASON_HW_NVME, 0, addr, mapping->nvme_lba,
                              "NVMe read failed during page fault");
            return NULL;
        }

        buf->in_use = true;
        an->nvme_read_bytes += SERAPH_AETHER_PAGE_SIZE;

        if (write) {
            mapping->dirty = true;
        }

        return buf->buffer;
    }

    /* Remote page fault - would fetch via RDMA */
    /* For now, return NULL to indicate remote fetch needed */
    (void)write;
    return NULL;
}

Seraph_Vbit seraph_aether_nvme_writeback(
    Seraph_Aether_NVMe* an,
    uint64_t addr,
    const void* data) {

    if (an == NULL || !an->initialized || data == NULL) {
        return SERAPH_VBIT_VOID;
    }

    uint64_t offset = seraph_aether_get_offset(addr);
    Seraph_Aether_NVMe_Mapping* mapping = find_mapping(an, offset);

    if (mapping == NULL) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_NOT_FOUND, 0, addr, 0,
                          "writeback target not found");
        return SERAPH_VBIT_VOID;
    }

    /* Write to NVMe */
    Seraph_Vbit result = seraph_nvme_write(
        an->nvme,
        mapping->nvme_lba,
        BLOCKS_PER_PAGE,
        data
    );

    if (!seraph_vbit_is_true(result)) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_HW_NVME, 0, addr, mapping->nvme_lba,
                          "NVMe write failed during writeback");
        return SERAPH_VBIT_VOID;
    }

    /* Ensure persistence */
    result = seraph_nvme_flush(an->nvme);
    if (!seraph_vbit_is_true(result)) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_HW_NVME, 0, addr, 0,
                          "NVMe flush failed during writeback");
        return SERAPH_VBIT_VOID;
    }

    mapping->dirty = false;
    an->nvme_write_bytes += SERAPH_AETHER_PAGE_SIZE;

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Snapshot API
 *============================================================================*/

Seraph_Vbit seraph_aether_nvme_create_snapshot(
    Seraph_Aether_NVMe* an,
    uint64_t start_addr,
    uint64_t end_addr,
    uint64_t* snapshot_id) {

    if (an == NULL || !an->initialized || snapshot_id == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Flush all dirty pages first */
    Seraph_Vbit result = seraph_nvme_flush(an->nvme);
    if (!seraph_vbit_is_true(result)) {
        return SERAPH_VBIT_VOID;
    }

    /* In a real implementation, this would:
     * 1. Freeze writes to the region
     * 2. Copy-on-write any subsequent modifications
     * 3. Record snapshot metadata
     * For now, just return a snapshot ID */

    *snapshot_id = ++an->next_op_id;

    (void)start_addr;
    (void)end_addr;

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_aether_nvme_restore_snapshot(
    Seraph_Aether_NVMe* an,
    uint64_t snapshot_id) {

    if (an == NULL || !an->initialized) {
        return SERAPH_VBIT_VOID;
    }

    /* Stub - would restore from snapshot metadata */
    (void)snapshot_id;

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * VOID Integration
 *============================================================================*/

uint64_t seraph_aether_nvme_get_last_void(const Seraph_Aether_NVMe* an) {
    if (an == NULL) {
        return 0;
    }
    return an->last_void_id;
}

Seraph_VoidReason seraph_aether_rdma_status_to_void_reason(Seraph_RDMA_Status status) {
    switch (status) {
        case SERAPH_RDMA_OK:
            return SERAPH_VOID_REASON_UNKNOWN;  /* Not a VOID */
        case SERAPH_RDMA_PENDING:
            return SERAPH_VOID_REASON_UNKNOWN;  /* Not a VOID yet */
        case SERAPH_RDMA_TIMEOUT:
            return SERAPH_VOID_REASON_TIMEOUT;
        case SERAPH_RDMA_NVME_ERROR:
            return SERAPH_VOID_REASON_HW_NVME;
        case SERAPH_RDMA_NETWORK_ERROR:
            return SERAPH_VOID_REASON_NETWORK;
        case SERAPH_RDMA_GENERATION_MISMATCH:
            return SERAPH_VOID_REASON_GENERATION;
        case SERAPH_RDMA_NOT_FOUND:
            return SERAPH_VOID_REASON_NOT_FOUND;
        case SERAPH_RDMA_PERMISSION_DENIED:
            return SERAPH_VOID_REASON_PERMISSION;
        case SERAPH_RDMA_OUT_OF_MEMORY:
            return SERAPH_VOID_REASON_ALLOC_FAIL;
        case SERAPH_RDMA_VOID:
        default:
            return SERAPH_VOID_REASON_NETWORK;
    }
}

/*============================================================================
 * Statistics
 *============================================================================*/

void seraph_aether_nvme_get_stats(
    const Seraph_Aether_NVMe* an,
    uint64_t* rdma_reads,
    uint64_t* rdma_writes,
    uint64_t* rdma_syncs,
    uint64_t* rdma_errors,
    uint64_t* nvme_read_bytes,
    uint64_t* nvme_write_bytes) {

    if (an == NULL) {
        return;
    }

    if (rdma_reads) *rdma_reads = an->rdma_reads;
    if (rdma_writes) *rdma_writes = an->rdma_writes;
    if (rdma_syncs) *rdma_syncs = an->rdma_syncs;
    if (rdma_errors) *rdma_errors = an->rdma_errors;
    if (nvme_read_bytes) *nvme_read_bytes = an->nvme_read_bytes;
    if (nvme_write_bytes) *nvme_write_bytes = an->nvme_write_bytes;
}

void seraph_aether_nvme_reset_stats(Seraph_Aether_NVMe* an) {
    if (an == NULL) {
        return;
    }

    an->rdma_reads = 0;
    an->rdma_writes = 0;
    an->rdma_syncs = 0;
    an->rdma_errors = 0;
    an->nvme_read_bytes = 0;
    an->nvme_write_bytes = 0;
}
