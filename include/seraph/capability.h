/**
 * @file capability.h
 * @brief MC6: Capability Tokens - Unforgeable Memory Access Control
 *
 * Capabilities are unforgeable tokens that represent permission to access
 * a specific memory region with specific rights. They provide:
 *
 *   1. SPATIAL SAFETY: Bounds checking (base + length)
 *   2. TEMPORAL SAFETY: Generation numbers detect use-after-free
 *   3. ACCESS CONTROL: Read/Write/Execute permissions
 *   4. UNFORGEABILITY: Cannot be created from integers
 *
 * A capability is invalid (VOID) if:
 *   - Its generation doesn't match the current allocation
 *   - It references deallocated memory
 *   - Access exceeds bounds
 *   - Permission is denied
 *
 * ARCHITECTURE:
 *   Full Capability (256 bits):
 *     [base_ptr: 64] [length: 64] [generation: 32] [permissions: 8] [reserved: 24]
 *
 *   Compact Capability (64 bits) - for hot paths:
 *     [CDT_index: 32] [offset: 24] [permissions: 8]
 *     (References entry in Capability Descriptor Table)
 */

#ifndef SERAPH_CAPABILITY_H
#define SERAPH_CAPABILITY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "seraph/void.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Permission Flags
 *============================================================================*/

typedef enum {
    SERAPH_CAP_NONE    = 0x00,  /**< No permissions */
    SERAPH_CAP_READ    = 0x01,  /**< Can read from memory */
    SERAPH_CAP_WRITE   = 0x02,  /**< Can write to memory */
    SERAPH_CAP_EXEC    = 0x04,  /**< Can execute (for code) */
    SERAPH_CAP_DERIVE  = 0x08,  /**< Can create sub-capabilities */
    SERAPH_CAP_SEAL    = 0x10,  /**< Can seal (make immutable) */
    SERAPH_CAP_UNSEAL  = 0x20,  /**< Can unseal */
    SERAPH_CAP_GLOBAL  = 0x40,  /**< Survives context switch */
    SERAPH_CAP_LOCAL   = 0x80,  /**< Valid only in current context */

    /* Common combinations */
    SERAPH_CAP_RW      = SERAPH_CAP_READ | SERAPH_CAP_WRITE,
    SERAPH_CAP_RX      = SERAPH_CAP_READ | SERAPH_CAP_EXEC,
    SERAPH_CAP_RWX     = SERAPH_CAP_READ | SERAPH_CAP_WRITE | SERAPH_CAP_EXEC,
    SERAPH_CAP_ALL     = 0xFF
} Seraph_CapPerm;

/*============================================================================
 * Full Capability (256-bit)
 *============================================================================*/

/**
 * @brief Full capability with complete metadata
 *
 * This is the authoritative representation. Contains all information
 * needed for access validation without external lookup.
 */
typedef struct {
    void*    base;        /**< Base address of accessible region */
    uint64_t length;      /**< Length in bytes */
    uint32_t generation;  /**< Allocation generation (for temporal safety) */
    uint8_t  permissions; /**< Access permission flags */
    uint8_t  type;        /**< Type tag for sealed capabilities */
    uint16_t reserved;    /**< Reserved for future use */
} Seraph_Capability;

/*============================================================================
 * Compact Capability (64-bit)
 *============================================================================*/

/**
 * @brief Compact capability for hot paths
 *
 * References an entry in the Capability Descriptor Table (CDT).
 * Faster to pass around, requires CDT lookup for validation.
 */
typedef struct {
    uint32_t cdt_index;   /**< Index into CDT */
    uint32_t offset : 24; /**< Offset from base (max 16MB) */
    uint32_t perms  : 8;  /**< Cached permissions */
} Seraph_CapCompact;

/*============================================================================
 * Capability Constants
 *============================================================================*/

/** VOID capability (all bits set, represents invalid/null) */
#define SERAPH_CAP_VOID ((Seraph_Capability){ \
    SERAPH_VOID_PTR, SERAPH_VOID_U64, SERAPH_VOID_U32, \
    SERAPH_VOID_U8, SERAPH_VOID_U8, SERAPH_VOID_U16 })

/** Null capability (no access, zero length) */
#define SERAPH_CAP_NULL ((Seraph_Capability){ NULL, 0, 0, SERAPH_CAP_NONE, 0, 0 })

/** VOID compact capability */
#define SERAPH_CAP_COMPACT_VOID ((Seraph_CapCompact){ SERAPH_VOID_U32, 0xFFFFFF, 0xFF })

/*============================================================================
 * Capability Detection
 *============================================================================*/

/**
 * @brief Check if capability is VOID
 */
static inline bool seraph_cap_is_void(Seraph_Capability cap) {
    return cap.base == SERAPH_VOID_PTR ||
           cap.length == SERAPH_VOID_U64 ||
           cap.generation == SERAPH_VOID_U32;
}

/**
 * @brief Check if capability is null (valid but no access)
 */
static inline bool seraph_cap_is_null(Seraph_Capability cap) {
    return cap.base == NULL && cap.length == 0;
}

/**
 * @brief Check if capability exists (is not VOID)
 */
static inline bool seraph_cap_exists(Seraph_Capability cap) {
    return !seraph_cap_is_void(cap);
}

/**
 * @brief Check if compact capability is VOID
 */
static inline bool seraph_cap_compact_is_void(Seraph_CapCompact cap) {
    return cap.cdt_index == SERAPH_VOID_U32;
}

/*============================================================================
 * Capability Creation
 *============================================================================*/

/**
 * @brief Create a new capability for a memory region
 *
 * @param base Start address
 * @param length Size in bytes
 * @param generation Current allocation generation
 * @param perms Permission flags
 * @return New capability, or VOID if parameters invalid
 *
 * NOTE: This should only be called by trusted allocator code.
 * User code cannot forge capabilities.
 */
Seraph_Capability seraph_cap_create(void* base, uint64_t length,
                                     uint32_t generation, uint8_t perms);

/**
 * @brief Derive a sub-capability with restricted permissions
 *
 * Creates a new capability that is a subset of the parent:
 * - Same or smaller bounds
 * - Same or fewer permissions
 * - Same generation
 *
 * @param parent The capability to derive from
 * @param offset Offset from parent's base
 * @param length Length of new capability
 * @param perms Permissions (must be subset of parent's)
 * @return Derived capability, or VOID if invalid
 */
Seraph_Capability seraph_cap_derive(Seraph_Capability parent,
                                     uint64_t offset, uint64_t length,
                                     uint8_t perms);

/**
 * @brief Shrink capability bounds (monotonic restriction)
 *
 * @param cap Capability to shrink
 * @param new_base New base (must be >= old base)
 * @param new_length New length (must fit within old bounds)
 * @return Restricted capability, or VOID if invalid
 */
Seraph_Capability seraph_cap_shrink(Seraph_Capability cap,
                                     uint64_t offset, uint64_t new_length);

/**
 * @brief Remove permissions from capability (monotonic)
 *
 * Permissions can only be removed, never added.
 *
 * @param cap Capability to modify
 * @param remove_perms Permissions to remove
 * @return Capability with reduced permissions
 */
static inline Seraph_Capability seraph_cap_restrict(Seraph_Capability cap,
                                                     uint8_t remove_perms) {
    if (seraph_cap_is_void(cap)) return SERAPH_CAP_VOID;
    cap.permissions &= ~remove_perms;
    return cap;
}

/*============================================================================
 * Capability Permission Checks
 *============================================================================*/

/**
 * @brief Check if capability has specific permissions
 */
static inline bool seraph_cap_has_perm(Seraph_Capability cap, uint8_t required) {
    if (seraph_cap_is_void(cap)) return false;
    return (cap.permissions & required) == required;
}

/**
 * @brief Check if read is permitted
 */
static inline bool seraph_cap_can_read(Seraph_Capability cap) {
    return seraph_cap_has_perm(cap, SERAPH_CAP_READ);
}

/**
 * @brief Check if write is permitted
 */
static inline bool seraph_cap_can_write(Seraph_Capability cap) {
    return seraph_cap_has_perm(cap, SERAPH_CAP_WRITE);
}

/**
 * @brief Check if execute is permitted
 */
static inline bool seraph_cap_can_exec(Seraph_Capability cap) {
    return seraph_cap_has_perm(cap, SERAPH_CAP_EXEC);
}

/**
 * @brief Check if derivation is permitted
 */
static inline bool seraph_cap_can_derive(Seraph_Capability cap) {
    return seraph_cap_has_perm(cap, SERAPH_CAP_DERIVE);
}

/*============================================================================
 * Capability Bounds Checking
 *============================================================================*/

/**
 * @brief Check if offset is within bounds
 */
static inline bool seraph_cap_in_bounds(Seraph_Capability cap, uint64_t offset) {
    if (seraph_cap_is_void(cap)) return false;
    return offset < cap.length;
}

/**
 * @brief Check if range [offset, offset+size) is within bounds
 */
static inline bool seraph_cap_range_valid(Seraph_Capability cap,
                                           uint64_t offset, uint64_t size) {
    if (seraph_cap_is_void(cap)) return false;
    if (offset > cap.length) return false;
    if (size > cap.length - offset) return false;
    return true;
}

/**
 * @brief Get pointer to offset within capability (bounds-checked)
 *
 * @param cap Capability
 * @param offset Offset from base
 * @return Pointer, or VOID_PTR if out of bounds or no permission
 */
static inline void* seraph_cap_get_ptr(Seraph_Capability cap, uint64_t offset) {
    if (!seraph_cap_in_bounds(cap, offset)) return SERAPH_VOID_PTR;
    return (char*)cap.base + offset;
}

/*============================================================================
 * Capability Access Operations
 *============================================================================*/

/**
 * @brief Read uint8_t through capability
 */
uint8_t seraph_cap_read_u8(Seraph_Capability cap, uint64_t offset);

/**
 * @brief Read uint16_t through capability
 */
uint16_t seraph_cap_read_u16(Seraph_Capability cap, uint64_t offset);

/**
 * @brief Read uint32_t through capability
 */
uint32_t seraph_cap_read_u32(Seraph_Capability cap, uint64_t offset);

/**
 * @brief Read uint64_t through capability
 */
uint64_t seraph_cap_read_u64(Seraph_Capability cap, uint64_t offset);

/**
 * @brief Write uint8_t through capability
 */
Seraph_Vbit seraph_cap_write_u8(Seraph_Capability cap, uint64_t offset, uint8_t value);

/**
 * @brief Write uint16_t through capability
 */
Seraph_Vbit seraph_cap_write_u16(Seraph_Capability cap, uint64_t offset, uint16_t value);

/**
 * @brief Write uint32_t through capability
 */
Seraph_Vbit seraph_cap_write_u32(Seraph_Capability cap, uint64_t offset, uint32_t value);

/**
 * @brief Write uint64_t through capability
 */
Seraph_Vbit seraph_cap_write_u64(Seraph_Capability cap, uint64_t offset, uint64_t value);

/**
 * @brief Copy memory through capabilities
 *
 * @param dst Destination capability (must have write permission)
 * @param dst_offset Offset in destination
 * @param src Source capability (must have read permission)
 * @param src_offset Offset in source
 * @param length Bytes to copy
 * @return TRUE if success, FALSE if failed, VOID if caps invalid
 */
Seraph_Vbit seraph_cap_copy(Seraph_Capability dst, uint64_t dst_offset,
                             Seraph_Capability src, uint64_t src_offset,
                             uint64_t length);

/*============================================================================
 * Capability Descriptor Table (CDT)
 *============================================================================*/

/**
 * @brief Maximum entries in CDT
 */
#define SERAPH_CDT_MAX_ENTRIES 65536

/**
 * @brief CDT entry structure
 */
typedef struct {
    Seraph_Capability cap;  /**< The full capability */
    uint32_t refcount;      /**< Reference count */
    uint32_t flags;         /**< CDT-specific flags */
} Seraph_CDT_Entry;

/* Forward declaration for arena (avoids circular include) */
struct Seraph_Arena;

/**
 * @brief Capability Descriptor Table
 *
 * Can be allocated from heap (arena=NULL) or from an arena (for persistence).
 */
typedef struct {
    Seraph_CDT_Entry* entries;   /**< Array of entries */
    struct Seraph_Arena* arena;         /**< Arena if arena-allocated, NULL if heap */
    uint32_t capacity;           /**< Maximum entries */
    uint32_t count;              /**< Current entry count */
    uint32_t free_head;          /**< Head of free list */
    uint32_t generation;         /**< Table generation */
} Seraph_CDT;

/**
 * @brief Initialize a CDT (heap allocation)
 */
Seraph_Vbit seraph_cdt_init(Seraph_CDT* cdt, uint32_t capacity);

/**
 * @brief Initialize a CDT with arena allocation (Atlas-ready)
 *
 * Allocates the entry table from the given arena instead of the heap.
 * The CDT will persist with the arena if it's mmap-backed.
 *
 * @param cdt CDT to initialize
 * @param arena Arena to allocate from
 * @param capacity Maximum number of entries
 * @return TRUE if success, FALSE if failed, VOID if params invalid
 */
Seraph_Vbit seraph_cdt_init_arena(Seraph_CDT* cdt, struct Seraph_Arena* arena, uint32_t capacity);

/**
 * @brief Destroy a CDT
 *
 * If CDT was heap-allocated, frees memory. If arena-allocated, does nothing
 * (memory is freed when arena is reset/destroyed).
 */
void seraph_cdt_destroy(Seraph_CDT* cdt);

/**
 * @brief Allocate a CDT entry for a capability
 *
 * @param cdt The CDT
 * @param cap Capability to store
 * @return Compact capability referencing the entry, or VOID
 */
Seraph_CapCompact seraph_cdt_alloc(Seraph_CDT* cdt, Seraph_Capability cap);

/**
 * @brief Look up a compact capability in the CDT
 *
 * @param cdt The CDT
 * @param compact The compact capability
 * @return Full capability, or VOID if invalid
 */
Seraph_Capability seraph_cdt_lookup(Seraph_CDT* cdt, Seraph_CapCompact compact);

/**
 * @brief Increment reference count
 */
Seraph_Vbit seraph_cdt_addref(Seraph_CDT* cdt, Seraph_CapCompact compact);

/**
 * @brief Decrement reference count (free entry if zero)
 */
Seraph_Vbit seraph_cdt_release(Seraph_CDT* cdt, Seraph_CapCompact compact);

/*============================================================================
 * Capability Sealing (Opaque Types)
 *============================================================================*/

/**
 * @brief Seal a capability with a type tag
 *
 * Sealed capabilities cannot be dereferenced until unsealed.
 * Used for implementing opaque/abstract data types.
 *
 * @param cap Capability to seal
 * @param type Type tag (0-255)
 * @return Sealed capability
 */
Seraph_Capability seraph_cap_seal(Seraph_Capability cap, uint8_t type);

/**
 * @brief Unseal a capability
 *
 * @param cap Sealed capability
 * @param expected_type Expected type tag
 * @return Unsealed capability, or VOID if type mismatch
 */
Seraph_Capability seraph_cap_unseal(Seraph_Capability cap, uint8_t expected_type);

/**
 * @brief Check if capability is sealed
 */
static inline bool seraph_cap_is_sealed(Seraph_Capability cap) {
    return cap.type != 0;
}

/**
 * @brief Get seal type of capability
 */
static inline uint8_t seraph_cap_get_type(Seraph_Capability cap) {
    return cap.type;
}

/*============================================================================
 * Capability Comparison
 *============================================================================*/

/**
 * @brief Check if two capabilities refer to the same region
 */
static inline bool seraph_cap_same_region(Seraph_Capability a, Seraph_Capability b) {
    return a.base == b.base && a.length == b.length && a.generation == b.generation;
}

/**
 * @brief Check if capability a is a subset of capability b
 */
bool seraph_cap_is_subset(Seraph_Capability a, Seraph_Capability b);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_CAPABILITY_H */
