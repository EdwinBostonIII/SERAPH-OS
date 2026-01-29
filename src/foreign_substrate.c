/**
 * @file foreign_substrate.c
 * @brief Foreign Substrate Layer - Linux VM for Hardware Driver Support
 *
 * The Foreign Substrate provides a mechanism to run Linux as a guest VM
 * to handle hardware drivers that SERAPH doesn't natively support. This
 * allows SERAPH to leverage the vast Linux driver ecosystem while
 * maintaining its own kernel architecture.
 *
 * Architecture:
 *   +----------------+
 *   |    SERAPH      |  (Host hypervisor)
 *   +----------------+
 *          |
 *   +----------------+
 *   | Foreign        |  (VMX-based isolation)
 *   | Substrate      |
 *   +----------------+
 *          |
 *   +----------------+
 *   |    Linux       |  (Guest VM running drivers)
 *   +----------------+
 *          |
 *   +----------------+
 *   |   Hardware     |  (Accessed through passthrough/MMIO)
 *   +----------------+
 *
 * Communication:
 *   - Hypercalls: Guest-to-host communication via VMCALL
 *   - Ring buffers: Shared memory queues for async I/O
 *   - Interrupt injection: Host-to-guest notification
 *
 * Device Passthrough:
 *   - MMIO mapping: Map device registers into guest EPT
 *   - DMA: Share DMA buffers between host and guest
 *   - IRQ routing: Forward device interrupts to guest
 */

#include "seraph/vmx.h"
#include "seraph/kmalloc.h"
#include "seraph/pmm.h"
#include "seraph/vmm.h"

#include <string.h>

#ifdef SERAPH_KERNEL

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum number of Foreign Substrate instances */
#define SERAPH_FS_MAX_INSTANCES         4

/** Default guest memory size (256 MB) */
#define SERAPH_FS_DEFAULT_MEM_SIZE      (256 * 1024 * 1024)

/** Ring buffer size (must be power of 2) */
#define SERAPH_FS_RING_SIZE             4096

/** Maximum number of ring buffers per substrate */
#define SERAPH_FS_MAX_RINGS             16

/** Maximum number of passthrough devices */
#define SERAPH_FS_MAX_DEVICES           8

/** Magic value for substrate identification */
#define SERAPH_FS_MAGIC                 0x53455241464853ULL  /* "SERAFHS" */

/*============================================================================
 * Ring Buffer Structure (Virtio-like)
 *
 * The ring buffer provides asynchronous communication between SERAPH
 * and the guest. It's based on virtio's virtqueue design.
 *
 * Memory layout:
 *   +------------------+
 *   | Ring Header      |  (control structure)
 *   +------------------+
 *   | Descriptor Table |  (array of buffer descriptors)
 *   +------------------+
 *   | Available Ring   |  (indices available for device)
 *   +------------------+
 *   | Used Ring        |  (indices used by device)
 *   +------------------+
 *============================================================================*/

/**
 * @brief Ring buffer descriptor
 *
 * Describes a single buffer in the ring.
 */
typedef struct {
    uint64_t addr;          /**< Guest physical address of buffer */
    uint32_t len;           /**< Length of buffer in bytes */
    uint16_t flags;         /**< Descriptor flags */
    uint16_t next;          /**< Next descriptor in chain (if chained) */
} Seraph_FS_Descriptor;

/** Descriptor flags */
#define SERAPH_FS_DESC_F_NEXT       (1 << 0)  /**< Descriptor chains to next */
#define SERAPH_FS_DESC_F_WRITE      (1 << 1)  /**< Buffer is write-only (device writes) */
#define SERAPH_FS_DESC_F_INDIRECT   (1 << 2)  /**< Buffer contains indirect descriptors */

/**
 * @brief Available ring structure
 *
 * Guest writes available descriptors here, host reads.
 */
typedef struct {
    uint16_t flags;         /**< Ring flags */
    uint16_t idx;           /**< Next available index (wraps) */
    uint16_t ring[];        /**< Array of descriptor indices */
} Seraph_FS_AvailRing;

/** Available ring flags */
#define SERAPH_FS_AVAIL_F_NO_INTERRUPT  (1 << 0)  /**< Don't interrupt on consume */

/**
 * @brief Used ring element
 */
typedef struct {
    uint32_t id;            /**< Descriptor index */
    uint32_t len;           /**< Number of bytes written */
} Seraph_FS_UsedElem;

/**
 * @brief Used ring structure
 *
 * Host writes used descriptors here, guest reads.
 */
typedef struct {
    uint16_t flags;         /**< Ring flags */
    uint16_t idx;           /**< Next used index (wraps) */
    Seraph_FS_UsedElem ring[];  /**< Array of used elements */
} Seraph_FS_UsedRing;

/** Used ring flags */
#define SERAPH_FS_USED_F_NO_NOTIFY  (1 << 0)  /**< Don't notify on add */

/**
 * @brief Complete ring buffer
 */
typedef struct {
    uint32_t id;                    /**< Ring identifier */
    uint32_t num_descs;             /**< Number of descriptors */

    /* Host-side pointers (virtual addresses) */
    Seraph_FS_Descriptor* descs;    /**< Descriptor table */
    Seraph_FS_AvailRing* avail;     /**< Available ring */
    Seraph_FS_UsedRing* used;       /**< Used ring */

    /* Guest-side addresses (for guest access) */
    uint64_t guest_descs_phys;      /**< Guest physical addr of descs */
    uint64_t guest_avail_phys;      /**< Guest physical addr of avail */
    uint64_t guest_used_phys;       /**< Guest physical addr of used */

    /* State tracking */
    uint16_t last_avail_idx;        /**< Last seen available index */
    uint16_t last_used_idx;         /**< Last used index we wrote */

    /* Synchronization */
    bool active;                    /**< Is ring active? */
    bool notify_pending;            /**< Notification pending for guest */
} Seraph_FS_Ring;

/*============================================================================
 * Device Passthrough Structure
 *============================================================================*/

/**
 * @brief Device passthrough types
 */
typedef enum {
    SERAPH_FS_DEV_NONE = 0,         /**< No device */
    SERAPH_FS_DEV_MMIO,             /**< MMIO-mapped device */
    SERAPH_FS_DEV_PIO,              /**< Port I/O device */
    SERAPH_FS_DEV_PCI,              /**< PCI device (full passthrough) */
} Seraph_FS_DeviceType;

/**
 * @brief Passthrough device descriptor
 */
typedef struct {
    Seraph_FS_DeviceType type;      /**< Device type */
    uint32_t device_id;             /**< Device identifier */

    /* MMIO mapping */
    uint64_t host_mmio_base;        /**< Host physical MMIO base */
    uint64_t guest_mmio_base;       /**< Guest physical MMIO base */
    uint64_t mmio_size;             /**< MMIO region size */

    /* Port I/O range */
    uint16_t pio_base;              /**< Base I/O port */
    uint16_t pio_size;              /**< Number of ports */

    /* Interrupt routing */
    uint8_t host_irq;               /**< Host interrupt number */
    uint8_t guest_irq;              /**< Guest interrupt vector */
    bool irq_enabled;               /**< IRQ forwarding enabled */

    /* Associated ring buffer (for async I/O) */
    uint32_t ring_id;               /**< Ring buffer for device I/O */
} Seraph_FS_Device;

/*============================================================================
 * Foreign Substrate Context
 *============================================================================*/

/**
 * @brief Substrate state
 */
typedef enum {
    SERAPH_FS_STATE_UNINITIALIZED = 0,
    SERAPH_FS_STATE_CREATED,        /**< Created but not started */
    SERAPH_FS_STATE_RUNNING,        /**< Guest is running */
    SERAPH_FS_STATE_PAUSED,         /**< Guest is paused */
    SERAPH_FS_STATE_STOPPED,        /**< Guest has stopped */
    SERAPH_FS_STATE_ERROR,          /**< Error state */
} Seraph_FS_State;

/**
 * @brief Foreign Substrate instance
 *
 * Represents a single Linux guest VM running for driver support.
 */
typedef struct {
    uint64_t magic;                 /**< Magic value for validation */
    uint32_t id;                    /**< Substrate instance ID */
    Seraph_FS_State state;          /**< Current state */

    /* VMX context */
    Seraph_VMX_Context vmx;         /**< VMX state */

    /* Memory */
    void* guest_memory;             /**< Guest memory (contiguous) */
    uint64_t guest_memory_phys;     /**< Physical address of guest memory */
    uint64_t guest_memory_size;     /**< Size of guest memory */

    /* Ring buffers */
    Seraph_FS_Ring rings[SERAPH_FS_MAX_RINGS];
    uint32_t num_rings;             /**< Number of active rings */

    /* Passthrough devices */
    Seraph_FS_Device devices[SERAPH_FS_MAX_DEVICES];
    uint32_t num_devices;           /**< Number of passthrough devices */

    /* Statistics */
    uint64_t vm_entries;            /**< Number of VM entries */
    uint64_t vm_exits;              /**< Number of VM exits */
    uint64_t hypercalls;            /**< Number of hypercalls */

    /* Configuration */
    bool hide_vmx;                  /**< Hide VMX from guest CPUID */
    bool enable_debugging;          /**< Enable debug features */
} Seraph_FS_Context;

/*============================================================================
 * Global State
 *============================================================================*/

/** Array of substrate instances */
static Seraph_FS_Context* g_substrates[SERAPH_FS_MAX_INSTANCES] = { NULL };

/** Number of active substrates */
static uint32_t g_num_substrates = 0;

/** Substrate ID counter */
static uint32_t g_next_substrate_id = 1;

/*============================================================================
 * Internal Helper Functions
 *============================================================================*/

/**
 * @brief Find a free substrate slot
 */
static int find_free_slot(void) {
    for (int i = 0; i < SERAPH_FS_MAX_INSTANCES; i++) {
        if (g_substrates[i] == NULL) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Validate substrate context
 */
static bool validate_context(Seraph_FS_Context* ctx) {
    return ctx && ctx->magic == SERAPH_FS_MAGIC;
}

/**
 * @brief Calculate ring buffer memory requirements
 */
static uint64_t calc_ring_size(uint32_t num_descs) {
    /* Descriptor table */
    uint64_t size = num_descs * sizeof(Seraph_FS_Descriptor);

    /* Available ring (header + num_descs entries) */
    size += sizeof(Seraph_FS_AvailRing) + num_descs * sizeof(uint16_t);

    /* Padding for alignment */
    size = (size + 4095) & ~4095ULL;

    /* Used ring (header + num_descs entries) */
    size += sizeof(Seraph_FS_UsedRing) + num_descs * sizeof(Seraph_FS_UsedElem);

    /* Round up to page size */
    return (size + 4095) & ~4095ULL;
}

/*============================================================================
 * Ring Buffer Management
 *============================================================================*/

/**
 * @brief Create a ring buffer for substrate communication
 *
 * @param ctx Substrate context
 * @param num_descs Number of descriptors (must be power of 2)
 * @return Ring ID, or -1 on failure
 */
static int fs_ring_create(Seraph_FS_Context* ctx, uint32_t num_descs) {
    if (!ctx || ctx->num_rings >= SERAPH_FS_MAX_RINGS) {
        return -1;
    }

    /* Ensure power of 2 */
    if (num_descs == 0 || (num_descs & (num_descs - 1)) != 0) {
        return -1;
    }

    /* Find free ring slot */
    int idx = -1;
    for (uint32_t i = 0; i < SERAPH_FS_MAX_RINGS; i++) {
        if (!ctx->rings[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;

    Seraph_FS_Ring* ring = &ctx->rings[idx];

    /* Calculate memory size */
    uint64_t total_size = calc_ring_size(num_descs);
    uint64_t num_pages = (total_size + 4095) / 4096;

    /* Allocate ring memory */
    void* ring_mem = seraph_kmalloc_pages(num_pages);
    if (!ring_mem) {
        return -1;
    }
    memset(ring_mem, 0, total_size);

    /* Get physical address for guest mapping */
    uint64_t ring_phys = seraph_virt_to_phys_direct(ring_mem);

    /* Set up pointers within ring memory */
    uint8_t* ptr = (uint8_t*)ring_mem;

    ring->descs = (Seraph_FS_Descriptor*)ptr;
    ptr += num_descs * sizeof(Seraph_FS_Descriptor);

    ring->avail = (Seraph_FS_AvailRing*)ptr;
    ptr += sizeof(Seraph_FS_AvailRing) + num_descs * sizeof(uint16_t);

    /* Align to page for used ring */
    ptr = (uint8_t*)(((uint64_t)ptr + 4095) & ~4095ULL);
    ring->used = (Seraph_FS_UsedRing*)ptr;

    /* Set guest physical addresses */
    ring->guest_descs_phys = ring_phys;
    ring->guest_avail_phys = ring_phys + ((uint8_t*)ring->avail - (uint8_t*)ring_mem);
    ring->guest_used_phys = ring_phys + ((uint8_t*)ring->used - (uint8_t*)ring_mem);

    /* Map ring into guest EPT */
    if (!seraph_vmx_ept_map(&ctx->vmx.ept, ring_phys, ring_phys, total_size,
                            SERAPH_EPT_RWX | SERAPH_EPT_MT_WB)) {
        seraph_kfree_pages(ring_mem, num_pages);
        return -1;
    }

    /* Initialize ring state */
    ring->id = idx;
    ring->num_descs = num_descs;
    ring->last_avail_idx = 0;
    ring->last_used_idx = 0;
    ring->active = true;
    ring->notify_pending = false;

    ctx->num_rings++;

    return idx;
}

/**
 * @brief Destroy a ring buffer
 *
 * @param ctx Substrate context
 * @param ring_id Ring ID to destroy
 */
static void fs_ring_destroy(Seraph_FS_Context* ctx, uint32_t ring_id) {
    if (!ctx || ring_id >= SERAPH_FS_MAX_RINGS) {
        return;
    }

    Seraph_FS_Ring* ring = &ctx->rings[ring_id];
    if (!ring->active) {
        return;
    }

    /* Unmap from guest EPT */
    uint64_t total_size = calc_ring_size(ring->num_descs);
    seraph_vmx_ept_unmap(&ctx->vmx.ept, ring->guest_descs_phys, total_size);

    /* Free ring memory */
    uint64_t num_pages = (total_size + 4095) / 4096;
    seraph_kfree_pages(ring->descs, num_pages);

    /* Clear ring state */
    memset(ring, 0, sizeof(*ring));

    ctx->num_rings--;
}

/**
 * @brief Check if ring has available buffers
 *
 * @param ring Ring to check
 * @return true if buffers are available
 */
static bool fs_ring_has_available(Seraph_FS_Ring* ring) {
    if (!ring || !ring->active || !ring->avail) {
        return false;
    }

    /* Memory barrier to ensure we see latest index */
    __asm__ __volatile__("mfence" ::: "memory");

    return ring->avail->idx != ring->last_avail_idx;
}

/**
 * @brief Get next available descriptor
 *
 * @param ring Ring buffer
 * @param desc_idx Output: descriptor index
 * @return true if descriptor available
 */
static bool fs_ring_get_available(Seraph_FS_Ring* ring, uint16_t* desc_idx) {
    if (!fs_ring_has_available(ring) || !desc_idx) {
        return false;
    }

    /* Get descriptor index from available ring */
    uint16_t avail_idx = ring->last_avail_idx % ring->num_descs;
    *desc_idx = ring->avail->ring[avail_idx];
    ring->last_avail_idx++;

    return true;
}

/**
 * @brief Mark descriptor as used
 *
 * @param ring Ring buffer
 * @param desc_idx Descriptor index
 * @param len Number of bytes written
 */
static void fs_ring_put_used(Seraph_FS_Ring* ring, uint16_t desc_idx, uint32_t len) {
    if (!ring || !ring->active || !ring->used) {
        return;
    }

    uint16_t used_idx = ring->last_used_idx % ring->num_descs;
    ring->used->ring[used_idx].id = desc_idx;
    ring->used->ring[used_idx].len = len;

    /* Memory barrier before updating index */
    __asm__ __volatile__("mfence" ::: "memory");

    ring->last_used_idx++;
    ring->used->idx = ring->last_used_idx;

    /* Check if we should notify guest */
    if (!(ring->avail->flags & SERAPH_FS_AVAIL_F_NO_INTERRUPT)) {
        ring->notify_pending = true;
    }
}

/*============================================================================
 * Hypercall Handlers (Extended for Foreign Substrate)
 *============================================================================*/

/**
 * @brief Handle memory-related hypercalls
 */
static int64_t fs_handle_memory_hypercall(Seraph_FS_Context* ctx, uint64_t hc_num,
                                           uint64_t param1, uint64_t param2,
                                           uint64_t param3) {
    switch (hc_num) {
        case SERAPH_HC_MAP_MMIO: {
            /*
             * Map MMIO region into guest
             * param1: Host physical address
             * param2: Guest physical address
             * param3: Size
             */
            uint64_t flags = SERAPH_EPT_RWX | SERAPH_EPT_MT_UC;  /* Uncacheable for MMIO */
            if (seraph_vmx_ept_map(&ctx->vmx.ept, param2, param1, param3, flags)) {
                seraph_vmx_ept_invalidate(&ctx->vmx.ept);
                return SERAPH_HC_SUCCESS;
            }
            return SERAPH_HC_NO_MEMORY;
        }

        case SERAPH_HC_UNMAP_MMIO: {
            /*
             * Unmap MMIO region
             * param1: Guest physical address
             * param2: Size
             */
            seraph_vmx_ept_unmap(&ctx->vmx.ept, param1, param2);
            seraph_vmx_ept_invalidate(&ctx->vmx.ept);
            return SERAPH_HC_SUCCESS;
        }

        case SERAPH_HC_SHARE_MEMORY: {
            /*
             * Share host memory with guest
             * param1: Host physical address
             * param2: Guest physical address
             * param3: Size
             */
            uint64_t flags = SERAPH_EPT_RWX | SERAPH_EPT_MT_WB;
            if (seraph_vmx_ept_map(&ctx->vmx.ept, param2, param1, param3, flags)) {
                seraph_vmx_ept_invalidate(&ctx->vmx.ept);
                return SERAPH_HC_SUCCESS;
            }
            return SERAPH_HC_NO_MEMORY;
        }

        case SERAPH_HC_DMA_ALLOC: {
            /*
             * Allocate DMA-capable memory
             * param1: Size in bytes
             * Returns: Guest physical address (also host physical due to identity map)
             */
            uint64_t pages = (param1 + 4095) / 4096;
            void* mem = seraph_kmalloc_pages(pages);
            if (!mem) {
                return SERAPH_HC_NO_MEMORY;
            }

            uint64_t phys = seraph_virt_to_phys_direct(mem);

            /* Map into guest with same physical address */
            if (!seraph_vmx_ept_map(&ctx->vmx.ept, phys, phys, pages * 4096,
                                     SERAPH_EPT_RWX | SERAPH_EPT_MT_WB)) {
                seraph_kfree_pages(mem, pages);
                return SERAPH_HC_NO_MEMORY;
            }

            return (int64_t)phys;
        }

        default:
            return SERAPH_HC_INVALID_CALL;
    }
}

/**
 * @brief Handle device-related hypercalls
 */
static int64_t fs_handle_device_hypercall(Seraph_FS_Context* ctx, uint64_t hc_num,
                                           uint64_t param1, uint64_t param2,
                                           uint64_t param3) {
    (void)param3;

    switch (hc_num) {
        case SERAPH_HC_DEVICE_PROBE: {
            /*
             * Probe for device
             * param1: Device type
             * param2: Device ID
             * Returns: 1 if device exists, 0 otherwise
             */
            for (uint32_t i = 0; i < ctx->num_devices; i++) {
                if (ctx->devices[i].type == (Seraph_FS_DeviceType)param1 &&
                    ctx->devices[i].device_id == (uint32_t)param2) {
                    return 1;
                }
            }
            return 0;
        }

        case SERAPH_HC_DEVICE_IRQ_ACK: {
            /*
             * Acknowledge device interrupt
             * param1: Device ID
             */
            for (uint32_t i = 0; i < ctx->num_devices; i++) {
                if (ctx->devices[i].device_id == (uint32_t)param1) {
                    /* Clear interrupt pending state */
                    return SERAPH_HC_SUCCESS;
                }
            }
            return SERAPH_HC_INVALID_PARAM;
        }

        case SERAPH_HC_DEVICE_IRQ_ENABLE: {
            /*
             * Enable device interrupt forwarding
             * param1: Device ID
             */
            for (uint32_t i = 0; i < ctx->num_devices; i++) {
                if (ctx->devices[i].device_id == (uint32_t)param1) {
                    ctx->devices[i].irq_enabled = true;
                    return SERAPH_HC_SUCCESS;
                }
            }
            return SERAPH_HC_INVALID_PARAM;
        }

        case SERAPH_HC_DEVICE_IRQ_DISABLE: {
            /*
             * Disable device interrupt forwarding
             * param1: Device ID
             */
            for (uint32_t i = 0; i < ctx->num_devices; i++) {
                if (ctx->devices[i].device_id == (uint32_t)param1) {
                    ctx->devices[i].irq_enabled = false;
                    return SERAPH_HC_SUCCESS;
                }
            }
            return SERAPH_HC_INVALID_PARAM;
        }

        default:
            return SERAPH_HC_INVALID_CALL;
    }
}

/**
 * @brief Handle ring buffer hypercalls
 */
static int64_t fs_handle_ring_hypercall(Seraph_FS_Context* ctx, uint64_t hc_num,
                                         uint64_t param1, uint64_t param2,
                                         uint64_t param3) {
    (void)param2;
    (void)param3;

    switch (hc_num) {
        case SERAPH_HC_RING_CREATE: {
            /*
             * Create ring buffer
             * param1: Number of descriptors
             * Returns: Ring ID
             */
            int ring_id = fs_ring_create(ctx, (uint32_t)param1);
            if (ring_id < 0) {
                return SERAPH_HC_NO_MEMORY;
            }
            return ring_id;
        }

        case SERAPH_HC_RING_DESTROY: {
            /*
             * Destroy ring buffer
             * param1: Ring ID
             */
            fs_ring_destroy(ctx, (uint32_t)param1);
            return SERAPH_HC_SUCCESS;
        }

        case SERAPH_HC_RING_NOTIFY: {
            /*
             * Notify host of ring update
             * param1: Ring ID
             *
             * This wakes up any waiting host thread.
             */
            if (param1 >= SERAPH_FS_MAX_RINGS || !ctx->rings[param1].active) {
                return SERAPH_HC_INVALID_PARAM;
            }
            /* Would trigger host-side processing */
            return SERAPH_HC_SUCCESS;
        }

        case SERAPH_HC_RING_WAIT: {
            /*
             * Wait for ring notification
             * param1: Ring ID
             *
             * This would block guest until host signals ring.
             */
            if (param1 >= SERAPH_FS_MAX_RINGS || !ctx->rings[param1].active) {
                return SERAPH_HC_INVALID_PARAM;
            }
            /* Would put guest to sleep until notification */
            return SERAPH_HC_SUCCESS;
        }

        default:
            return SERAPH_HC_INVALID_CALL;
    }
}

/**
 * @brief Main hypercall handler for Foreign Substrate
 *
 * Called from VMX exit handler when VMCALL is executed.
 */
static bool fs_handle_hypercall(Seraph_VMX_Context* vmx_ctx, uint64_t qualification) {
    (void)qualification;

    Seraph_FS_Context* ctx = (Seraph_FS_Context*)((uint8_t*)vmx_ctx -
                              offsetof(Seraph_FS_Context, vmx));

    if (!validate_context(ctx)) {
        vmx_ctx->guest_regs.rax = SERAPH_HC_ERROR;
        seraph_vmx_advance_rip(vmx_ctx);
        return true;
    }

    ctx->hypercalls++;

    uint64_t hc_num = vmx_ctx->guest_regs.rax;
    uint64_t param1 = vmx_ctx->guest_regs.rbx;
    uint64_t param2 = vmx_ctx->guest_regs.rcx;
    uint64_t param3 = vmx_ctx->guest_regs.rdx;

    int64_t result;

    /* Route hypercall to appropriate handler */
    if (hc_num >= 0x0100 && hc_num < 0x0200) {
        result = fs_handle_memory_hypercall(ctx, hc_num, param1, param2, param3);
    } else if (hc_num >= 0x0200 && hc_num < 0x0300) {
        result = fs_handle_device_hypercall(ctx, hc_num, param1, param2, param3);
    } else if (hc_num >= 0x0300 && hc_num < 0x0400) {
        result = fs_handle_ring_hypercall(ctx, hc_num, param1, param2, param3);
    } else {
        /* Let base VMX handler deal with it */
        return seraph_vmx_handle_vmcall(vmx_ctx, qualification);
    }

    vmx_ctx->guest_regs.rax = (uint64_t)result;
    seraph_vmx_advance_rip(vmx_ctx);

    return true;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

/**
 * @brief Create a new Foreign Substrate instance
 *
 * Allocates and initializes a substrate for running a Linux guest.
 *
 * @param memory_size Guest memory size in bytes (0 for default)
 * @return Substrate handle, or NULL on failure
 */
Seraph_FS_Context* seraph_fs_create(uint64_t memory_size) {
    int slot = find_free_slot();
    if (slot < 0) {
        return NULL;
    }

    if (memory_size == 0) {
        memory_size = SERAPH_FS_DEFAULT_MEM_SIZE;
    }

    /* Allocate substrate context */
    Seraph_FS_Context* ctx = seraph_kmalloc(sizeof(Seraph_FS_Context));
    if (!ctx) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->magic = SERAPH_FS_MAGIC;
    ctx->id = g_next_substrate_id++;
    ctx->state = SERAPH_FS_STATE_UNINITIALIZED;
    ctx->hide_vmx = true;

    /* Enable VMX on this CPU */
    if (!seraph_vmx_enable(&ctx->vmx.vcpu)) {
        seraph_kfree(ctx);
        return NULL;
    }

    /* Allocate VMCS */
    if (!seraph_vmx_alloc_vmcs(&ctx->vmx.vcpu)) {
        seraph_vmx_disable(&ctx->vmx.vcpu);
        seraph_kfree(ctx);
        return NULL;
    }

    /* Load VMCS */
    if (!seraph_vmx_load_vmcs(&ctx->vmx.vcpu)) {
        seraph_vmx_free_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_disable(&ctx->vmx.vcpu);
        seraph_kfree(ctx);
        return NULL;
    }

    /* Allocate guest memory */
    uint64_t num_pages = (memory_size + 4095) / 4096;
    ctx->guest_memory = seraph_kmalloc_pages(num_pages);
    if (!ctx->guest_memory) {
        seraph_vmx_clear_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_free_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_disable(&ctx->vmx.vcpu);
        seraph_kfree(ctx);
        return NULL;
    }
    memset(ctx->guest_memory, 0, memory_size);

    ctx->guest_memory_phys = seraph_virt_to_phys_direct(ctx->guest_memory);
    ctx->guest_memory_size = memory_size;

    /* Initialize EPT with identity mapping for guest memory */
    if (!seraph_vmx_ept_init(&ctx->vmx.ept, memory_size, false)) {
        seraph_kfree_pages(ctx->guest_memory, num_pages);
        seraph_vmx_clear_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_free_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_disable(&ctx->vmx.vcpu);
        seraph_kfree(ctx);
        return NULL;
    }

    /* Map guest memory at base address 0 (typical for Linux) */
    if (!seraph_vmx_ept_map(&ctx->vmx.ept, 0, ctx->guest_memory_phys, memory_size,
                            SERAPH_EPT_RWX | SERAPH_EPT_MT_WB)) {
        seraph_vmx_ept_destroy(&ctx->vmx.ept);
        seraph_kfree_pages(ctx->guest_memory, num_pages);
        seraph_vmx_clear_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_free_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_disable(&ctx->vmx.vcpu);
        seraph_kfree(ctx);
        return NULL;
    }

    /* Set up host state (current SERAPH state) */
    if (!seraph_vmx_setup_host_state(&ctx->vmx)) {
        seraph_vmx_ept_destroy(&ctx->vmx.ept);
        seraph_kfree_pages(ctx->guest_memory, num_pages);
        seraph_vmx_clear_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_free_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_disable(&ctx->vmx.vcpu);
        seraph_kfree(ctx);
        return NULL;
    }

    /* Set up VM-execution controls */
    if (!seraph_vmx_setup_controls(&ctx->vmx)) {
        seraph_vmx_ept_destroy(&ctx->vmx.ept);
        seraph_kfree_pages(ctx->guest_memory, num_pages);
        seraph_vmx_clear_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_free_vmcs(&ctx->vmx.vcpu);
        seraph_vmx_disable(&ctx->vmx.vcpu);
        seraph_kfree(ctx);
        return NULL;
    }

    /* Register our hypercall handler */
    seraph_vmx_register_exit_handler(SERAPH_EXIT_REASON_VMCALL, fs_handle_hypercall);

    /* Store in global array */
    g_substrates[slot] = ctx;
    g_num_substrates++;

    ctx->state = SERAPH_FS_STATE_CREATED;
    ctx->vmx.guest_id = ctx->id;

    return ctx;
}

/**
 * @brief Load a kernel image into the substrate
 *
 * Copies a kernel image to guest memory and sets up the entry point.
 *
 * @param ctx Substrate context
 * @param kernel_data Kernel image data
 * @param kernel_size Size of kernel image
 * @param entry_point Entry point address (or 0 for default)
 * @return true on success
 */
bool seraph_fs_load_kernel(Seraph_FS_Context* ctx, const void* kernel_data,
                            uint64_t kernel_size, uint64_t entry_point) {
    if (!validate_context(ctx) || ctx->state != SERAPH_FS_STATE_CREATED) {
        return false;
    }

    if (!kernel_data || kernel_size == 0) {
        return false;
    }

    if (kernel_size > ctx->guest_memory_size) {
        return false;
    }

    /* Copy kernel to guest memory */
    /* Typically loaded at 1MB for Linux */
    uint64_t load_addr = 0x100000;  /* 1MB */
    if (load_addr + kernel_size > ctx->guest_memory_size) {
        load_addr = 0;
    }

    memcpy((uint8_t*)ctx->guest_memory + load_addr, kernel_data, kernel_size);

    /* Set up guest state for kernel entry */
    if (entry_point == 0) {
        entry_point = load_addr;  /* Default: entry at load address */
    }

    /* Stack at top of guest memory minus some space */
    uint64_t stack_ptr = ctx->guest_memory_size - 0x1000;

    /*
     * For Linux, we'd need to set up:
     * 1. Real-mode entry (protected mode is set up by vmx_setup_guest_state)
     * 2. Boot parameters at known location
     * 3. Initial page tables (if not using unrestricted guest)
     *
     * For simplicity, we set up 64-bit entry assuming a 64-bit kernel.
     */

    /* Create initial page tables for guest */
    /* For now, use identity mapping in guest as well */
    uint64_t guest_cr3 = 0x1000;  /* Page table at 4KB in guest memory */

    /* Set up minimal 4-level page tables for identity mapping */
    uint64_t* pml4 = (uint64_t*)((uint8_t*)ctx->guest_memory + 0x1000);
    uint64_t* pdpt = (uint64_t*)((uint8_t*)ctx->guest_memory + 0x2000);
    uint64_t* pd   = (uint64_t*)((uint8_t*)ctx->guest_memory + 0x3000);

    memset(pml4, 0, 4096);
    memset(pdpt, 0, 4096);
    memset(pd, 0, 4096);

    /* PML4[0] -> PDPT */
    pml4[0] = 0x2003;  /* Present, writable, at 0x2000 */

    /* PDPT[0] -> PD */
    pdpt[0] = 0x3003;

    /* PD entries: 2MB pages covering guest memory */
    uint64_t num_2mb_pages = (ctx->guest_memory_size + 0x1FFFFF) / 0x200000;
    for (uint64_t i = 0; i < num_2mb_pages && i < 512; i++) {
        /* 2MB page, present, writable */
        pd[i] = (i * 0x200000) | 0x83;
    }

    if (!seraph_vmx_setup_guest_state(&ctx->vmx, entry_point, stack_ptr, guest_cr3)) {
        return false;
    }

    return true;
}

/**
 * @brief Start the Foreign Substrate guest
 *
 * @param ctx Substrate context
 * @return true if started successfully
 */
bool seraph_fs_start(Seraph_FS_Context* ctx) {
    if (!validate_context(ctx)) {
        return false;
    }

    if (ctx->state != SERAPH_FS_STATE_CREATED &&
        ctx->state != SERAPH_FS_STATE_PAUSED) {
        return false;
    }

    ctx->state = SERAPH_FS_STATE_RUNNING;

    return true;
}

/**
 * @brief Run the Foreign Substrate guest until exit
 *
 * Enters guest execution and handles VM-exits until the guest
 * halts or an unhandled exit occurs.
 *
 * @param ctx Substrate context
 * @return Exit reason on return
 */
uint32_t seraph_fs_run(Seraph_FS_Context* ctx) {
    if (!validate_context(ctx) || ctx->state != SERAPH_FS_STATE_RUNNING) {
        return (uint32_t)-1;
    }

    uint32_t exit_reason;
    bool first_entry = true;
    bool keep_running = true;

    while (keep_running && ctx->state == SERAPH_FS_STATE_RUNNING) {
        ctx->vm_entries++;

        /* Launch or resume guest */
        if (first_entry) {
            exit_reason = seraph_vmx_launch(&ctx->vmx);
            first_entry = false;
        } else {
            exit_reason = seraph_vmx_resume(&ctx->vmx);
        }

        ctx->vm_exits++;

        /* Handle the exit */
        keep_running = seraph_vmx_handle_exit(&ctx->vmx);
    }

    if (!keep_running) {
        ctx->state = SERAPH_FS_STATE_STOPPED;
    }

    return exit_reason;
}

/**
 * @brief Pause the Foreign Substrate guest
 *
 * @param ctx Substrate context
 * @return true on success
 */
bool seraph_fs_pause(Seraph_FS_Context* ctx) {
    if (!validate_context(ctx) || ctx->state != SERAPH_FS_STATE_RUNNING) {
        return false;
    }

    ctx->state = SERAPH_FS_STATE_PAUSED;
    return true;
}

/**
 * @brief Stop the Foreign Substrate guest
 *
 * @param ctx Substrate context
 * @return true on success
 */
bool seraph_fs_stop(Seraph_FS_Context* ctx) {
    if (!validate_context(ctx)) {
        return false;
    }

    ctx->state = SERAPH_FS_STATE_STOPPED;
    return true;
}

/**
 * @brief Destroy a Foreign Substrate instance
 *
 * Frees all resources associated with the substrate.
 *
 * @param ctx Substrate context
 */
void seraph_fs_destroy(Seraph_FS_Context* ctx) {
    if (!validate_context(ctx)) {
        return;
    }

    /* Stop if running */
    if (ctx->state == SERAPH_FS_STATE_RUNNING) {
        ctx->state = SERAPH_FS_STATE_STOPPED;
    }

    /* Destroy all ring buffers */
    for (uint32_t i = 0; i < SERAPH_FS_MAX_RINGS; i++) {
        if (ctx->rings[i].active) {
            fs_ring_destroy(ctx, i);
        }
    }

    /* Destroy EPT */
    seraph_vmx_ept_destroy(&ctx->vmx.ept);

    /* Free guest memory */
    if (ctx->guest_memory) {
        uint64_t num_pages = (ctx->guest_memory_size + 4095) / 4096;
        seraph_kfree_pages(ctx->guest_memory, num_pages);
    }

    /* Clean up VMX */
    if (ctx->vmx.vcpu.vmcs_loaded) {
        seraph_vmx_clear_vmcs(&ctx->vmx.vcpu);
    }
    seraph_vmx_free_vmcs(&ctx->vmx.vcpu);
    seraph_vmx_disable(&ctx->vmx.vcpu);

    /* Remove from global array */
    for (int i = 0; i < SERAPH_FS_MAX_INSTANCES; i++) {
        if (g_substrates[i] == ctx) {
            g_substrates[i] = NULL;
            g_num_substrates--;
            break;
        }
    }

    /* Clear magic and free */
    ctx->magic = 0;
    seraph_kfree(ctx);
}

/**
 * @brief Add a passthrough device to the substrate
 *
 * @param ctx Substrate context
 * @param type Device type
 * @param device_id Device identifier
 * @param mmio_base Host MMIO base address
 * @param mmio_size MMIO region size
 * @param irq Device IRQ number
 * @return Device index, or -1 on failure
 */
int seraph_fs_add_device(Seraph_FS_Context* ctx, Seraph_FS_DeviceType type,
                          uint32_t device_id, uint64_t mmio_base,
                          uint64_t mmio_size, uint8_t irq) {
    if (!validate_context(ctx) || ctx->num_devices >= SERAPH_FS_MAX_DEVICES) {
        return -1;
    }

    int idx = ctx->num_devices;
    Seraph_FS_Device* dev = &ctx->devices[idx];

    dev->type = type;
    dev->device_id = device_id;
    dev->host_mmio_base = mmio_base;
    dev->mmio_size = mmio_size;
    dev->host_irq = irq;
    dev->irq_enabled = false;

    /* Assign guest MMIO address (same as host for simplicity) */
    dev->guest_mmio_base = mmio_base;

    /* Map MMIO into guest EPT */
    if (mmio_size > 0 && mmio_base != 0) {
        if (!seraph_vmx_ept_map(&ctx->vmx.ept, mmio_base, mmio_base, mmio_size,
                                 SERAPH_EPT_RWX | SERAPH_EPT_MT_UC)) {
            return -1;
        }
    }

    /* Create ring buffer for async I/O */
    int ring_id = fs_ring_create(ctx, 256);
    if (ring_id >= 0) {
        dev->ring_id = ring_id;
    }

    ctx->num_devices++;
    return idx;
}

/**
 * @brief Inject an interrupt into the guest
 *
 * Used to notify the guest of device events.
 *
 * @param ctx Substrate context
 * @param vector Interrupt vector
 * @return true on success
 */
bool seraph_fs_inject_interrupt(Seraph_FS_Context* ctx, uint8_t vector) {
    if (!validate_context(ctx)) {
        return false;
    }

    /* Type 0 = external interrupt */
    return seraph_vmx_inject_event(vector, 0, 0, false);
}

/**
 * @brief Get substrate statistics
 *
 * @param ctx Substrate context
 * @param entries Output: VM entries count
 * @param exits Output: VM exits count
 * @param hypercalls Output: Hypercall count
 */
void seraph_fs_get_stats(Seraph_FS_Context* ctx, uint64_t* entries,
                          uint64_t* exits, uint64_t* hypercalls) {
    if (!validate_context(ctx)) {
        if (entries) *entries = 0;
        if (exits) *exits = 0;
        if (hypercalls) *hypercalls = 0;
        return;
    }

    if (entries) *entries = ctx->vm_entries;
    if (exits) *exits = ctx->vm_exits;
    if (hypercalls) *hypercalls = ctx->hypercalls;
}

/**
 * @brief Get substrate state
 *
 * @param ctx Substrate context
 * @return Current state
 */
Seraph_FS_State seraph_fs_get_state(Seraph_FS_Context* ctx) {
    if (!validate_context(ctx)) {
        return SERAPH_FS_STATE_ERROR;
    }
    return ctx->state;
}

/**
 * @brief Process pending I/O for all ring buffers
 *
 * Should be called periodically to handle async device I/O.
 *
 * @param ctx Substrate context
 * @return Number of requests processed
 */
uint32_t seraph_fs_process_io(Seraph_FS_Context* ctx) {
    if (!validate_context(ctx)) {
        return 0;
    }

    uint32_t processed = 0;

    for (uint32_t i = 0; i < SERAPH_FS_MAX_RINGS; i++) {
        Seraph_FS_Ring* ring = &ctx->rings[i];
        if (!ring->active) continue;

        /* Process available descriptors */
        uint16_t desc_idx;
        while (fs_ring_get_available(ring, &desc_idx)) {
            Seraph_FS_Descriptor* desc = &ring->descs[desc_idx];

            /*
             * Process the descriptor.
             * In a real implementation, this would:
             * 1. Translate guest address to host address via EPT
             * 2. Perform the requested I/O operation
             * 3. Mark as used with result
             */

            /* For now, just mark as used immediately */
            fs_ring_put_used(ring, desc_idx, desc->len);
            processed++;
        }

        /* Send notification if needed */
        if (ring->notify_pending) {
            /* Find associated device and inject its interrupt */
            for (uint32_t j = 0; j < ctx->num_devices; j++) {
                if (ctx->devices[j].ring_id == i && ctx->devices[j].irq_enabled) {
                    seraph_fs_inject_interrupt(ctx, ctx->devices[j].guest_irq);
                    break;
                }
            }
            ring->notify_pending = false;
        }
    }

    return processed;
}

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * @brief Initialize Foreign Substrate subsystem
 *
 * Should be called once during kernel initialization.
 *
 * @return true on success
 */
bool seraph_fs_init(void) {
    /* Check VMX support */
    if (!seraph_vmx_supported()) {
        return false;
    }

    /* Register default exit handlers */
    seraph_vmx_register_exit_handler(SERAPH_EXIT_REASON_CPUID, seraph_vmx_handle_cpuid);
    seraph_vmx_register_exit_handler(SERAPH_EXIT_REASON_HLT, seraph_vmx_handle_hlt);
    seraph_vmx_register_exit_handler(SERAPH_EXIT_REASON_IO, seraph_vmx_handle_io);
    seraph_vmx_register_exit_handler(SERAPH_EXIT_REASON_EPT_VIOLATION,
                                      seraph_vmx_handle_ept_violation);

    return true;
}

/**
 * @brief Shutdown Foreign Substrate subsystem
 *
 * Destroys all active substrates.
 */
void seraph_fs_shutdown(void) {
    for (int i = 0; i < SERAPH_FS_MAX_INSTANCES; i++) {
        if (g_substrates[i]) {
            seraph_fs_destroy(g_substrates[i]);
        }
    }
}

#endif /* SERAPH_KERNEL */
