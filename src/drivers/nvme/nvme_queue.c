/**
 * @file nvme_queue.c
 * @brief MC24: The Infinite Drive - NVMe Queue Management
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module implements NVMe queue operations:
 *   - Command submission to submission queues
 *   - Completion processing from completion queues
 *   - Doorbell management
 *
 * QUEUE MECHANICS:
 *
 *   Submission Queue (SQ):
 *     - Circular buffer of 64-byte commands
 *     - Host writes to tail, controller reads from head
 *     - Tail doorbell notifies controller of new commands
 *
 *   Completion Queue (CQ):
 *     - Circular buffer of 16-byte completions
 *     - Controller writes to tail, host reads from head
 *     - Phase bit indicates valid entries
 *     - Head doorbell acknowledges processed completions
 *
 *   Phase Bit:
 *     - Toggles each time queue wraps around
 *     - Allows host to detect new completions without head pointer
 *     - Valid entry: completion.phase == expected_phase
 */

#include "seraph/drivers/nvme.h"
#include "seraph/void.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Platform Abstraction
 *============================================================================*/

#ifdef _WIN32
#include <windows.h>
static uint64_t nvme_get_time_ms_queue(void) {
    return GetTickCount64();
}
static void nvme_sleep_ms_queue(uint32_t ms) {
    Sleep(ms);
}
#else
#include <unistd.h>
#include <time.h>
static uint64_t nvme_get_time_ms_queue(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static void nvme_sleep_ms_queue(uint32_t ms) {
    usleep(ms * 1000);
}
#endif

/**
 * @brief Memory barrier for queue operations
 */
static inline void nvme_queue_mb(void) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("mfence" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
    __faststorefence();
#endif
}

/*============================================================================
 * Queue Initialization/Destruction
 *============================================================================*/

/**
 * @brief Initialize a queue structure
 *
 * Note: This does not allocate memory - caller must set sq/cq pointers.
 */
Seraph_Vbit seraph_nvme_queue_init(Seraph_NVMe_Queue* queue,
                                    uint16_t qid, uint32_t depth) {
    if (queue == NULL || depth == 0 || depth > 65536) {
        return SERAPH_VBIT_VOID;
    }

    memset(queue, 0, sizeof(*queue));
    queue->depth = depth;
    queue->qid = qid;
    queue->phase = 1;  /* Phase starts at 1 */
    queue->sq_tail = 0;
    queue->cq_head = 0;
    queue->next_cid = 0;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Destroy queue (just clears structure - caller frees memory)
 */
void seraph_nvme_queue_destroy(Seraph_NVMe_Queue* queue) {
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
    }
}

/*============================================================================
 * Command Submission
 *============================================================================*/

/**
 * @brief Submit a command to a queue
 *
 * @param nvme NVMe controller state
 * @param queue Target submission/completion queue pair
 * @param cmd Command to submit
 * @return Command ID on success, SERAPH_VOID_U16 on failure
 */
uint16_t seraph_nvme_submit(Seraph_NVMe* nvme, Seraph_NVMe_Queue* queue,
                             const Seraph_NVMe_Cmd* cmd) {
    if (nvme == NULL || queue == NULL || cmd == NULL) {
        return SERAPH_VOID_U16;
    }

    if (queue->sq == NULL) {
        return SERAPH_VOID_U16;
    }

    /* Check if queue is full:
     * Queue is full when (tail + 1) % depth == head
     * We track tail locally, head comes from completions */
    uint32_t next_tail = (queue->sq_tail + 1) % queue->depth;

    /* For simplicity, we don't track outstanding commands here.
     * A real driver would maintain a list of in-flight commands. */

    /* Allocate command ID */
    uint16_t cid = queue->next_cid++;

    /* Copy command to submission queue */
    Seraph_NVMe_Cmd* sq_entry = &queue->sq[queue->sq_tail];
    memcpy(sq_entry, cmd, sizeof(Seraph_NVMe_Cmd));
    sq_entry->cid = cid;

    /* Memory barrier before doorbell write */
    nvme_queue_mb();

    /* Update tail and ring doorbell */
    queue->sq_tail = next_tail;
    *queue->sq_doorbell = next_tail;

    /* Memory barrier after doorbell */
    nvme_queue_mb();

    return cid;
}

/*============================================================================
 * Completion Processing
 *============================================================================*/

/**
 * @brief Check for a completion entry
 *
 * @param queue Queue to check
 * @param cpl Output completion entry (if available)
 * @return SERAPH_VBIT_TRUE if completion available, SERAPH_VBIT_FALSE otherwise
 */
Seraph_Vbit seraph_nvme_check_completion(Seraph_NVMe_Queue* queue,
                                          Seraph_NVMe_Cpl* cpl) {
    if (queue == NULL || cpl == NULL || queue->cq == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Read completion entry */
    Seraph_NVMe_Cpl* cq_entry = &queue->cq[queue->cq_head];

    /* Memory barrier to ensure we read fresh data */
    nvme_queue_mb();

    /* Check phase bit - indicates if this entry is valid */
    uint8_t phase = SERAPH_NVME_STATUS_PHASE(cq_entry->status);
    if (phase != queue->phase) {
        /* No new completion */
        return SERAPH_VBIT_FALSE;
    }

    /* Copy completion to output */
    memcpy(cpl, cq_entry, sizeof(Seraph_NVMe_Cpl));

    /* Advance head pointer */
    queue->cq_head++;
    if (queue->cq_head >= queue->depth) {
        queue->cq_head = 0;
        queue->phase ^= 1;  /* Toggle expected phase on wrap */
    }

    /* Ring completion doorbell to acknowledge */
    *queue->cq_doorbell = queue->cq_head;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Poll for a specific command completion
 *
 * @param nvme NVMe controller state
 * @param queue Queue to poll
 * @param cid Command ID to wait for
 * @return SERAPH_VBIT_TRUE on successful completion, SERAPH_VBIT_VOID on error
 */
Seraph_Vbit seraph_nvme_poll_completion(Seraph_NVMe* nvme,
                                         Seraph_NVMe_Queue* queue,
                                         uint16_t cid) {
    if (nvme == NULL || queue == NULL) {
        return SERAPH_VBIT_VOID;
    }

    uint32_t timeout_ms = (queue->qid == 0) ?
                          SERAPH_NVME_ADMIN_TIMEOUT_MS :
                          SERAPH_NVME_IO_TIMEOUT_MS;

    uint64_t start = nvme_get_time_ms_queue();

    while ((nvme_get_time_ms_queue() - start) < timeout_ms) {
        Seraph_NVMe_Cpl cpl;
        Seraph_Vbit result = seraph_nvme_check_completion(queue, &cpl);

        if (seraph_vbit_is_true(result)) {
            /* Got a completion - check if it's ours */
            if (cpl.cid == cid) {
                /* Check status */
                if (seraph_nvme_status_ok(cpl.status)) {
                    return SERAPH_VBIT_TRUE;
                } else {
                    /* Command failed */
                    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_IO, 0,
                                       cpl.status, cid,
                                       seraph_nvme_status_str(cpl.status));
                    return SERAPH_VBIT_VOID;
                }
            }
            /* Not our CID - could be out-of-order completion.
             * A real driver would handle this properly. */
        } else if (seraph_vbit_is_void(result)) {
            /* Error checking completion */
            return SERAPH_VBIT_VOID;
        }

        /* No completion yet - brief sleep to avoid spinning */
        nvme_sleep_ms_queue(1);
    }

    /* Timeout */
    SERAPH_VOID_RECORD(SERAPH_VOID_REASON_TIMEOUT, 0, cid, timeout_ms,
                       "NVMe command timeout");
    return SERAPH_VBIT_VOID;
}

/*============================================================================
 * Queue Statistics
 *============================================================================*/

/**
 * @brief Get number of outstanding commands in queue
 *
 * Calculates outstanding commands based on submission tail and
 * completion head tracking. The queue is a circular buffer where:
 * - sq_tail: next position to write (incremented on submit)
 * - cq_head: last acknowledged completion position
 *
 * Outstanding = (sq_tail - sq_head) mod depth
 * We derive sq_head from completed CIDs via cq_head tracking.
 */
uint32_t seraph_nvme_queue_outstanding(const Seraph_NVMe_Queue* queue) {
    if (queue == NULL || queue->depth == 0) {
        return 0;
    }

    /* Calculate outstanding based on tail position and wrapped completions
     * sq_tail tracks submissions, cq_head tracks completions acknowledged
     * The difference gives us outstanding commands */
    uint32_t head_estimate = queue->cq_head;  /* Approximation from completion side */
    uint32_t tail = queue->sq_tail;

    if (tail >= head_estimate) {
        return tail - head_estimate;
    } else {
        /* Tail wrapped around */
        return queue->depth - head_estimate + tail;
    }
}

/**
 * @brief Check if queue is empty (no outstanding commands)
 */
bool seraph_nvme_queue_empty(const Seraph_NVMe_Queue* queue) {
    if (queue == NULL) {
        return true;
    }

    /* Queue empty when submission tail equals completion head position
     * This means all submitted commands have been completed */
    return seraph_nvme_queue_outstanding(queue) == 0;
}

/**
 * @brief Check if queue is full
 *
 * Queue is full when (sq_tail + 1) % depth == sq_head.
 * We leave one slot empty to distinguish full from empty.
 */
bool seraph_nvme_queue_full(const Seraph_NVMe_Queue* queue) {
    if (queue == NULL || queue->depth == 0) {
        return true;
    }

    /* Full when outstanding commands reach (depth - 1)
     * We keep one slot empty to distinguish full from empty */
    return seraph_nvme_queue_outstanding(queue) >= (queue->depth - 1);
}
