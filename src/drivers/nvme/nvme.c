/**
 * @file nvme.c
 * @brief MC24: The Infinite Drive - Core NVMe Driver
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module implements the core NVMe driver functionality:
 *   - Controller initialization and shutdown
 *   - Admin command processing
 *   - I/O command processing (read/write/flush)
 *
 * The driver is designed for simplicity and correctness over raw performance.
 * A production implementation would use interrupt-driven completion,
 * multiple I/O queues per CPU, and more sophisticated error handling.
 */

#include "seraph/drivers/nvme.h"
#include "seraph/void.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*============================================================================
 * Platform Abstraction
 *
 * These would be replaced with actual kernel functions in a real OS.
 *============================================================================*/

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#endif

/**
 * @brief Allocate DMA-capable memory
 *
 * Returns page-aligned memory suitable for DMA transfers.
 * In userspace simulation, we just use aligned_alloc.
 */
static void* nvme_alloc_dma(size_t size, uint64_t* phys_addr) {
    void* ptr = NULL;

#ifdef _WIN32
    ptr = _aligned_malloc(size, 4096);
#else
    if (posix_memalign(&ptr, 4096, size) != 0) {
        ptr = NULL;
    }
#endif

    if (ptr != NULL) {
        memset(ptr, 0, size);
        /* In userspace, physical address = virtual address (simulation) */
        *phys_addr = (uint64_t)(uintptr_t)ptr;
    } else {
        *phys_addr = 0;
    }

    return ptr;
}

/**
 * @brief Free DMA memory
 */
static void nvme_free_dma(void* ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/**
 * @brief Memory barrier
 */
static inline void nvme_mb(void) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("mfence" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
    __faststorefence();
#endif
}

/**
 * @brief Get current time in milliseconds
 */
static uint64_t nvme_get_time_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

/**
 * @brief Sleep for milliseconds
 */
static void nvme_sleep_ms(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

/*============================================================================
 * Register Access
 *============================================================================*/

/**
 * @brief Read 32-bit register
 */
static inline uint32_t nvme_read32(volatile void* bar, uint32_t offset) {
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)bar + offset);
    uint32_t val = *reg;
    nvme_mb();
    return val;
}

/**
 * @brief Write 32-bit register
 */
static inline void nvme_write32(volatile void* bar, uint32_t offset, uint32_t value) {
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)bar + offset);
    *reg = value;
    nvme_mb();
}

/**
 * @brief Read 64-bit register
 */
static inline uint64_t nvme_read64(volatile void* bar, uint32_t offset) {
    volatile uint64_t* reg = (volatile uint64_t*)((uint8_t*)bar + offset);
    uint64_t val = *reg;
    nvme_mb();
    return val;
}

/**
 * @brief Write 64-bit register
 */
static inline void nvme_write64(volatile void* bar, uint32_t offset, uint64_t value) {
    volatile uint64_t* reg = (volatile uint64_t*)((uint8_t*)bar + offset);
    *reg = value;
    nvme_mb();
}

/*============================================================================
 * Controller Initialization
 *============================================================================*/

/**
 * @brief Wait for controller to become ready
 */
static Seraph_Vbit nvme_wait_ready(Seraph_NVMe* nvme, bool expected_ready) {
    uint64_t start = nvme_get_time_ms();
    uint32_t expected = expected_ready ? SERAPH_NVME_CSTS_RDY : 0;

    while ((nvme_get_time_ms() - start) < nvme->timeout_ms) {
        uint32_t csts = nvme_read32(nvme->bar0, SERAPH_NVME_REG_CSTS);

        /* Check for fatal error */
        if (csts & SERAPH_NVME_CSTS_CFS) {
            SERAPH_VOID_RECORD(SERAPH_VOID_REASON_IO, 0, csts, 0,
                               "NVMe controller fatal status");
            return SERAPH_VBIT_VOID;
        }

        if ((csts & SERAPH_NVME_CSTS_RDY) == expected) {
            return SERAPH_VBIT_TRUE;
        }

        nvme_sleep_ms(1);
    }

    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_TIMEOUT, 0, 0, 0,
                       "NVMe controller ready timeout");
    return SERAPH_VBIT_VOID;
}

/**
 * @brief Initialize admin queue
 */
static Seraph_Vbit nvme_init_admin_queue(Seraph_NVMe* nvme) {
    Seraph_NVMe_Queue* aq = &nvme->admin_queue;

    /* Allocate submission queue */
    aq->sq = nvme_alloc_dma(SERAPH_NVME_QUEUE_DEPTH * sizeof(Seraph_NVMe_Cmd),
                             &aq->sq_phys);
    if (aq->sq == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Allocate completion queue */
    aq->cq = nvme_alloc_dma(SERAPH_NVME_QUEUE_DEPTH * sizeof(Seraph_NVMe_Cpl),
                             &aq->cq_phys);
    if (aq->cq == NULL) {
        nvme_free_dma(aq->sq);
        aq->sq = NULL;
        return SERAPH_VBIT_VOID;
    }

    /* Initialize queue state */
    aq->depth = SERAPH_NVME_QUEUE_DEPTH;
    aq->sq_tail = 0;
    aq->cq_head = 0;
    aq->phase = 1;  /* CQ phase bit starts at 1 */
    aq->qid = 0;    /* Admin queue is always QID 0 */
    aq->next_cid = 0;

    /* Set doorbell pointers */
    aq->sq_doorbell = (volatile uint32_t*)((uint8_t*)nvme->bar0 +
                                            SERAPH_NVME_REG_SQ0TDBL);
    aq->cq_doorbell = (volatile uint32_t*)((uint8_t*)nvme->bar0 +
                                            SERAPH_NVME_REG_SQ0TDBL +
                                            nvme->doorbell_stride);

    /* Configure admin queue in controller */
    uint32_t aqa = ((SERAPH_NVME_QUEUE_DEPTH - 1) << 16) |  /* ACQS */
                   (SERAPH_NVME_QUEUE_DEPTH - 1);            /* ASQS */
    nvme_write32(nvme->bar0, SERAPH_NVME_REG_AQA, aqa);
    nvme_write64(nvme->bar0, SERAPH_NVME_REG_ASQ, aq->sq_phys);
    nvme_write64(nvme->bar0, SERAPH_NVME_REG_ACQ, aq->cq_phys);

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Create I/O completion queue
 */
static Seraph_Vbit nvme_create_io_cq(Seraph_NVMe* nvme) {
    Seraph_NVMe_Cmd cmd;
    memset(&cmd, 0, sizeof(cmd));

    seraph_nvme_cmd_create_cq(&cmd, 1,  /* QID 1 */
                               nvme->io_queue.cq_phys,
                               SERAPH_NVME_QUEUE_DEPTH - 1,
                               0);  /* Interrupt vector 0 */

    uint16_t cid = seraph_nvme_submit(nvme, &nvme->admin_queue, &cmd);
    if (cid == SERAPH_VOID_U16) {
        return SERAPH_VBIT_VOID;
    }

    return seraph_nvme_poll_completion(nvme, &nvme->admin_queue, cid);
}

/**
 * @brief Create I/O submission queue
 */
static Seraph_Vbit nvme_create_io_sq(Seraph_NVMe* nvme) {
    Seraph_NVMe_Cmd cmd;
    memset(&cmd, 0, sizeof(cmd));

    seraph_nvme_cmd_create_sq(&cmd, 1,  /* QID 1 */
                               nvme->io_queue.sq_phys,
                               SERAPH_NVME_QUEUE_DEPTH - 1,
                               1);  /* Associated CQ ID */

    uint16_t cid = seraph_nvme_submit(nvme, &nvme->admin_queue, &cmd);
    if (cid == SERAPH_VOID_U16) {
        return SERAPH_VBIT_VOID;
    }

    return seraph_nvme_poll_completion(nvme, &nvme->admin_queue, cid);
}

/**
 * @brief Initialize I/O queue
 */
static Seraph_Vbit nvme_init_io_queue(Seraph_NVMe* nvme) {
    Seraph_NVMe_Queue* ioq = &nvme->io_queue;

    /* Allocate queue memory */
    ioq->sq = nvme_alloc_dma(SERAPH_NVME_QUEUE_DEPTH * sizeof(Seraph_NVMe_Cmd),
                              &ioq->sq_phys);
    if (ioq->sq == NULL) {
        return SERAPH_VBIT_VOID;
    }

    ioq->cq = nvme_alloc_dma(SERAPH_NVME_QUEUE_DEPTH * sizeof(Seraph_NVMe_Cpl),
                              &ioq->cq_phys);
    if (ioq->cq == NULL) {
        nvme_free_dma(ioq->sq);
        ioq->sq = NULL;
        return SERAPH_VBIT_VOID;
    }

    /* Initialize queue state */
    ioq->depth = SERAPH_NVME_QUEUE_DEPTH;
    ioq->sq_tail = 0;
    ioq->cq_head = 0;
    ioq->phase = 1;
    ioq->qid = 1;
    ioq->next_cid = 0;

    /* Set doorbell pointers (QID 1) */
    uint32_t sq_offset = SERAPH_NVME_REG_SQ0TDBL + (2 * 1) * nvme->doorbell_stride;
    uint32_t cq_offset = SERAPH_NVME_REG_SQ0TDBL + (2 * 1 + 1) * nvme->doorbell_stride;
    ioq->sq_doorbell = (volatile uint32_t*)((uint8_t*)nvme->bar0 + sq_offset);
    ioq->cq_doorbell = (volatile uint32_t*)((uint8_t*)nvme->bar0 + cq_offset);

    /* Create queues on controller */
    Seraph_Vbit result = nvme_create_io_cq(nvme);
    if (!seraph_vbit_is_true(result)) {
        nvme_free_dma(ioq->sq);
        nvme_free_dma(ioq->cq);
        ioq->sq = NULL;
        ioq->cq = NULL;
        return result;
    }

    result = nvme_create_io_sq(nvme);
    if (!seraph_vbit_is_true(result)) {
        /* TODO: Delete the CQ we created */
        nvme_free_dma(ioq->sq);
        nvme_free_dma(ioq->cq);
        ioq->sq = NULL;
        ioq->cq = NULL;
        return result;
    }

    nvme->io_queue_created = true;
    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Identify controller
 */
static Seraph_Vbit nvme_identify_controller(Seraph_NVMe* nvme) {
    uint64_t phys;
    nvme->ctrl_data = nvme_alloc_dma(sizeof(Seraph_NVMe_Identify_Controller), &phys);
    if (nvme->ctrl_data == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_NVMe_Cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    seraph_nvme_cmd_identify_ctrl(&cmd, phys);

    uint16_t cid = seraph_nvme_submit(nvme, &nvme->admin_queue, &cmd);
    if (cid == SERAPH_VOID_U16) {
        nvme_free_dma(nvme->ctrl_data);
        nvme->ctrl_data = NULL;
        return SERAPH_VBIT_VOID;
    }

    return seraph_nvme_poll_completion(nvme, &nvme->admin_queue, cid);
}

/**
 * @brief Identify namespace
 */
static Seraph_Vbit nvme_identify_namespace(Seraph_NVMe* nvme, uint32_t nsid) {
    uint64_t phys;
    nvme->ns_data = nvme_alloc_dma(sizeof(Seraph_NVMe_Identify_Namespace), &phys);
    if (nvme->ns_data == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_NVMe_Cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    seraph_nvme_cmd_identify_ns(&cmd, nsid, phys);

    uint16_t cid = seraph_nvme_submit(nvme, &nvme->admin_queue, &cmd);
    if (cid == SERAPH_VOID_U16) {
        nvme_free_dma(nvme->ns_data);
        nvme->ns_data = NULL;
        return SERAPH_VBIT_VOID;
    }

    Seraph_Vbit result = seraph_nvme_poll_completion(nvme, &nvme->admin_queue, cid);
    if (seraph_vbit_is_true(result)) {
        /* Parse namespace data */
        nvme->ns_id = nsid;
        nvme->ns_size = nvme->ns_data->nsze;

        /* Get block size from formatted LBA format */
        uint8_t flbas = nvme->ns_data->flbas & 0x0F;
        uint8_t lbads = nvme->ns_data->lbaf[flbas].lbads;
        nvme->block_size = 1u << lbads;
    }

    return result;
}

/*============================================================================
 * Public API
 *============================================================================*/

/**
 * @brief Initialize NVMe controller
 */
Seraph_Vbit seraph_nvme_init(Seraph_NVMe* nvme, uint64_t bar0_phys) {
    if (nvme == NULL) {
        return SERAPH_VBIT_VOID;
    }

    memset(nvme, 0, sizeof(*nvme));

    /* Map BAR0 - in userspace simulation, we'd normally mmap the device.
     * For testing, we might use a simulated BAR0. */
    nvme->bar0 = (volatile void*)(uintptr_t)bar0_phys;
    if (nvme->bar0 == NULL) {
        SERAPH_VOID_RECORD(SERAPH_VOID_REASON_NULL_PTR, 0, bar0_phys, 0,
                           "NVMe BAR0 mapping failed");
        return SERAPH_VBIT_VOID;
    }

    /* Read controller capabilities */
    nvme->cap = nvme_read64(nvme->bar0, SERAPH_NVME_REG_CAP);
    nvme->max_queue_entries = SERAPH_NVME_CAP_MQES(nvme->cap) + 1;
    nvme->doorbell_stride = 4u << SERAPH_NVME_CAP_DSTRD(nvme->cap);
    nvme->timeout_ms = 500 * (SERAPH_NVME_CAP_TO(nvme->cap) + 1);

    /* Disable controller if enabled */
    uint32_t cc = nvme_read32(nvme->bar0, SERAPH_NVME_REG_CC);
    if (cc & SERAPH_NVME_CC_EN) {
        nvme_write32(nvme->bar0, SERAPH_NVME_REG_CC, cc & ~SERAPH_NVME_CC_EN);
        Seraph_Vbit result = nvme_wait_ready(nvme, false);
        if (!seraph_vbit_is_true(result)) {
            return result;
        }
    }

    /* Initialize admin queue */
    Seraph_Vbit result = nvme_init_admin_queue(nvme);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Configure and enable controller */
    cc = SERAPH_NVME_CC_EN |
         SERAPH_NVME_CC_CSS(0) |    /* NVM command set */
         SERAPH_NVME_CC_MPS(0) |    /* 4KB pages (2^(12+0)) */
         SERAPH_NVME_CC_IOSQES(6) | /* 64-byte SQ entries (2^6) */
         SERAPH_NVME_CC_IOCQES(4);  /* 16-byte CQ entries (2^4) */
    nvme_write32(nvme->bar0, SERAPH_NVME_REG_CC, cc);

    result = nvme_wait_ready(nvme, true);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Identify controller */
    result = nvme_identify_controller(nvme);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Identify namespace 1 */
    result = nvme_identify_namespace(nvme, 1);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    /* Create I/O queue */
    result = nvme_init_io_queue(nvme);
    if (!seraph_vbit_is_true(result)) {
        return result;
    }

    nvme->initialized = true;
    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Shutdown NVMe controller
 */
void seraph_nvme_shutdown(Seraph_NVMe* nvme) {
    if (nvme == NULL || !nvme->initialized) {
        return;
    }

    /* Send shutdown notification */
    uint32_t cc = nvme_read32(nvme->bar0, SERAPH_NVME_REG_CC);
    cc = (cc & ~(3u << 14)) | (1u << 14);  /* Normal shutdown */
    nvme_write32(nvme->bar0, SERAPH_NVME_REG_CC, cc);

    /* Wait for shutdown complete */
    uint64_t start = nvme_get_time_ms();
    while ((nvme_get_time_ms() - start) < nvme->timeout_ms) {
        uint32_t csts = nvme_read32(nvme->bar0, SERAPH_NVME_REG_CSTS);
        if ((csts & SERAPH_NVME_CSTS_SHST) == (2u << 2)) {
            break;  /* Shutdown processing complete */
        }
        nvme_sleep_ms(1);
    }

    /* Free resources */
    if (nvme->admin_queue.sq) nvme_free_dma(nvme->admin_queue.sq);
    if (nvme->admin_queue.cq) nvme_free_dma(nvme->admin_queue.cq);
    if (nvme->io_queue.sq) nvme_free_dma(nvme->io_queue.sq);
    if (nvme->io_queue.cq) nvme_free_dma(nvme->io_queue.cq);
    if (nvme->ctrl_data) nvme_free_dma(nvme->ctrl_data);
    if (nvme->ns_data) nvme_free_dma(nvme->ns_data);

    memset(nvme, 0, sizeof(*nvme));
}

/**
 * @brief Read blocks from NVMe
 */
Seraph_Vbit seraph_nvme_read(Seraph_NVMe* nvme, uint64_t lba,
                              uint32_t block_count, void* buffer) {
    if (nvme == NULL || !nvme->initialized || buffer == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (block_count == 0 || lba + block_count > nvme->ns_size) {
        return SERAPH_VBIT_VOID;
    }

    /* Calculate transfer size and handle multi-page transfers */
    size_t transfer_size = (size_t)block_count * nvme->block_size;
    uint64_t prp1 = (uint64_t)(uintptr_t)buffer;
    uint64_t prp2 = 0;

    if (transfer_size > 4096) {
        /* Multi-page transfer: need PRP list or PRP2 */
        if (transfer_size <= 8192) {
            /* Two pages: PRP2 points to second page */
            prp2 = prp1 + 4096;
        } else {
            /* More than 2 pages: need PRP list */
            /* Allocate PRP list in DMA memory */
            uint64_t prp_list_phys;
            size_t num_prps = (transfer_size + 4095) / 4096 - 1;  /* -1 for PRP1 */
            uint64_t* prp_list = nvme_alloc_dma(num_prps * sizeof(uint64_t), &prp_list_phys);
            if (prp_list == NULL) {
                return SERAPH_VBIT_VOID;
            }

            /* Fill PRP list with page addresses */
            for (size_t i = 0; i < num_prps; i++) {
                prp_list[i] = prp1 + (i + 1) * 4096;
            }

            prp2 = prp_list_phys;
            /* Note: In production, we'd need to free this after command completes */
        }
    }

    Seraph_NVMe_Cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    seraph_nvme_cmd_read(&cmd, nvme->ns_id, lba, (uint16_t)(block_count - 1),
                          prp1, prp2);

    uint16_t cid = seraph_nvme_submit(nvme, &nvme->io_queue, &cmd);
    if (cid == SERAPH_VOID_U16) {
        return SERAPH_VBIT_VOID;
    }

    return seraph_nvme_poll_completion(nvme, &nvme->io_queue, cid);
}

/**
 * @brief Write blocks to NVMe
 */
Seraph_Vbit seraph_nvme_write(Seraph_NVMe* nvme, uint64_t lba,
                               uint32_t block_count, const void* buffer) {
    if (nvme == NULL || !nvme->initialized || buffer == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (block_count == 0 || lba + block_count > nvme->ns_size) {
        return SERAPH_VBIT_VOID;
    }

    /* Calculate transfer size and handle multi-page transfers */
    size_t transfer_size = (size_t)block_count * nvme->block_size;
    uint64_t prp1 = (uint64_t)(uintptr_t)buffer;
    uint64_t prp2 = 0;

    if (transfer_size > 4096) {
        /* Multi-page transfer: need PRP list or PRP2 */
        if (transfer_size <= 8192) {
            /* Two pages: PRP2 points to second page */
            prp2 = prp1 + 4096;
        } else {
            /* More than 2 pages: need PRP list */
            uint64_t prp_list_phys;
            size_t num_prps = (transfer_size + 4095) / 4096 - 1;
            uint64_t* prp_list = nvme_alloc_dma(num_prps * sizeof(uint64_t), &prp_list_phys);
            if (prp_list == NULL) {
                return SERAPH_VBIT_VOID;
            }

            for (size_t i = 0; i < num_prps; i++) {
                prp_list[i] = prp1 + (i + 1) * 4096;
            }

            prp2 = prp_list_phys;
        }
    }

    Seraph_NVMe_Cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    seraph_nvme_cmd_write(&cmd, nvme->ns_id, lba, (uint16_t)(block_count - 1),
                           prp1, prp2);

    uint16_t cid = seraph_nvme_submit(nvme, &nvme->io_queue, &cmd);
    if (cid == SERAPH_VOID_U16) {
        return SERAPH_VBIT_VOID;
    }

    return seraph_nvme_poll_completion(nvme, &nvme->io_queue, cid);
}

/**
 * @brief Flush data to NVMe
 */
Seraph_Vbit seraph_nvme_flush(Seraph_NVMe* nvme) {
    if (nvme == NULL || !nvme->initialized) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_NVMe_Cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    seraph_nvme_cmd_flush(&cmd, nvme->ns_id);

    uint16_t cid = seraph_nvme_submit(nvme, &nvme->io_queue, &cmd);
    if (cid == SERAPH_VOID_U16) {
        return SERAPH_VBIT_VOID;
    }

    return seraph_nvme_poll_completion(nvme, &nvme->io_queue, cid);
}

/**
 * @brief Get status string
 */
const char* seraph_nvme_status_str(uint16_t status) {
    uint8_t sct = SERAPH_NVME_STATUS_TYPE(status);
    uint8_t sc = SERAPH_NVME_STATUS_CODE(status);

    if (sc == 0) {
        return "Success";
    }

    switch (sct) {
        case 0:  /* Generic */
            switch (sc) {
                case 0x01: return "Invalid Command Opcode";
                case 0x02: return "Invalid Field in Command";
                case 0x03: return "Command ID Conflict";
                case 0x04: return "Data Transfer Error";
                case 0x05: return "Commands Aborted - Power Loss";
                case 0x06: return "Internal Error";
                case 0x07: return "Command Abort Requested";
                case 0x08: return "Command Aborted - SQ Deleted";
                case 0x09: return "Command Aborted - Failed Fused";
                case 0x0A: return "Command Aborted - Missing Fused";
                case 0x0B: return "Invalid Namespace or Format";
                case 0x0C: return "Command Sequence Error";
                default: return "Generic Command Error";
            }
        case 1:  /* Command Specific */
            return "Command Specific Error";
        case 2:  /* Media Errors */
            switch (sc) {
                case 0x80: return "LBA Out of Range";
                case 0x81: return "Capacity Exceeded";
                case 0x82: return "Namespace Not Ready";
                default: return "Media Error";
            }
        default:
            return "Unknown Error";
    }
}
