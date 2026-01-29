/**
 * @file capability.c
 * @brief MC6: Capability Tokens Implementation
 *
 * Supports both heap allocation (calloc/free) and arena allocation
 * for Atlas-ready persistent memory. Use the _arena variants for
 * capabilities that should persist with the arena.
 */

#include "seraph/capability.h"
#include "seraph/arena.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Capability Creation
 *============================================================================*/

Seraph_Capability seraph_cap_create(void* base, uint64_t length,
                                     uint32_t generation, uint8_t perms) {
    /* Validate parameters (branchless where possible) */
    uint64_t base_void = seraph_void_mask_u64((uint64_t)(uintptr_t)base);
    uint64_t len_void = seraph_void_mask_u64(length);
    uint32_t gen_void = seraph_void_mask_u32(generation);

    uint64_t any_void = base_void | len_void | (uint64_t)gen_void;

    Seraph_Capability result = {
        base, length, generation, perms, 0, 0
    };

    /* Return VOID if any parameter is VOID */
    Seraph_Capability void_result = SERAPH_CAP_VOID;

    /* Branchless select */
    result.base = any_void ? void_result.base : result.base;
    result.length = any_void ? void_result.length : result.length;
    result.generation = any_void ? void_result.generation : result.generation;
    result.permissions = any_void ? void_result.permissions : result.permissions;

    return result;
}

Seraph_Capability seraph_cap_derive(Seraph_Capability parent,
                                     uint64_t offset, uint64_t length,
                                     uint8_t perms) {
    if (seraph_cap_is_void(parent)) return SERAPH_CAP_VOID;

    /* Check derivation permission */
    if (!seraph_cap_can_derive(parent)) return SERAPH_CAP_VOID;

    /* Bounds check: new capability must be within parent */
    if (offset > parent.length) return SERAPH_CAP_VOID;
    if (length > parent.length - offset) return SERAPH_CAP_VOID;

    /* Permissions must be subset of parent */
    uint8_t allowed_perms = perms & parent.permissions;
    if (allowed_perms != perms) return SERAPH_CAP_VOID;

    return (Seraph_Capability){
        .base = (char*)parent.base + offset,
        .length = length,
        .generation = parent.generation,
        .permissions = allowed_perms,
        .type = 0,  /* Derived capabilities are unsealed */
        .reserved = 0
    };
}

Seraph_Capability seraph_cap_shrink(Seraph_Capability cap,
                                     uint64_t offset, uint64_t new_length) {
    if (seraph_cap_is_void(cap)) return SERAPH_CAP_VOID;

    /* Bounds check */
    if (offset > cap.length) return SERAPH_CAP_VOID;
    if (new_length > cap.length - offset) return SERAPH_CAP_VOID;

    return (Seraph_Capability){
        .base = (char*)cap.base + offset,
        .length = new_length,
        .generation = cap.generation,
        .permissions = cap.permissions,
        .type = cap.type,
        .reserved = 0
    };
}

/*============================================================================
 * Capability Access Operations
 *============================================================================*/

uint8_t seraph_cap_read_u8(Seraph_Capability cap, uint64_t offset) {
    if (seraph_cap_is_void(cap)) return SERAPH_VOID_U8;
    if (!seraph_cap_can_read(cap)) return SERAPH_VOID_U8;
    if (!seraph_cap_in_bounds(cap, offset)) return SERAPH_VOID_U8;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VOID_U8;

    return *((uint8_t*)((char*)cap.base + offset));
}

uint16_t seraph_cap_read_u16(Seraph_Capability cap, uint64_t offset) {
    if (seraph_cap_is_void(cap)) return SERAPH_VOID_U16;
    if (!seraph_cap_can_read(cap)) return SERAPH_VOID_U16;
    if (!seraph_cap_range_valid(cap, offset, sizeof(uint16_t))) return SERAPH_VOID_U16;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VOID_U16;

    uint16_t value;
    memcpy(&value, (char*)cap.base + offset, sizeof(uint16_t));
    return value;
}

uint32_t seraph_cap_read_u32(Seraph_Capability cap, uint64_t offset) {
    if (seraph_cap_is_void(cap)) return SERAPH_VOID_U32;
    if (!seraph_cap_can_read(cap)) return SERAPH_VOID_U32;
    if (!seraph_cap_range_valid(cap, offset, sizeof(uint32_t))) return SERAPH_VOID_U32;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VOID_U32;

    uint32_t value;
    memcpy(&value, (char*)cap.base + offset, sizeof(uint32_t));
    return value;
}

uint64_t seraph_cap_read_u64(Seraph_Capability cap, uint64_t offset) {
    if (seraph_cap_is_void(cap)) return SERAPH_VOID_U64;
    if (!seraph_cap_can_read(cap)) return SERAPH_VOID_U64;
    if (!seraph_cap_range_valid(cap, offset, sizeof(uint64_t))) return SERAPH_VOID_U64;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VOID_U64;

    uint64_t value;
    memcpy(&value, (char*)cap.base + offset, sizeof(uint64_t));
    return value;
}

Seraph_Vbit seraph_cap_write_u8(Seraph_Capability cap, uint64_t offset, uint8_t value) {
    if (seraph_cap_is_void(cap)) return SERAPH_VBIT_VOID;
    if (!seraph_cap_can_write(cap)) return SERAPH_VBIT_FALSE;
    if (!seraph_cap_in_bounds(cap, offset)) return SERAPH_VBIT_FALSE;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U8(value)) return SERAPH_VBIT_FALSE;

    *((uint8_t*)((char*)cap.base + offset)) = value;
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_cap_write_u16(Seraph_Capability cap, uint64_t offset, uint16_t value) {
    if (seraph_cap_is_void(cap)) return SERAPH_VBIT_VOID;
    if (!seraph_cap_can_write(cap)) return SERAPH_VBIT_FALSE;
    if (!seraph_cap_range_valid(cap, offset, sizeof(uint16_t))) return SERAPH_VBIT_FALSE;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U16(value)) return SERAPH_VBIT_FALSE;

    memcpy((char*)cap.base + offset, &value, sizeof(uint16_t));
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_cap_write_u32(Seraph_Capability cap, uint64_t offset, uint32_t value) {
    if (seraph_cap_is_void(cap)) return SERAPH_VBIT_VOID;
    if (!seraph_cap_can_write(cap)) return SERAPH_VBIT_FALSE;
    if (!seraph_cap_range_valid(cap, offset, sizeof(uint32_t))) return SERAPH_VBIT_FALSE;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U32(value)) return SERAPH_VBIT_FALSE;

    memcpy((char*)cap.base + offset, &value, sizeof(uint32_t));
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_cap_write_u64(Seraph_Capability cap, uint64_t offset, uint64_t value) {
    if (seraph_cap_is_void(cap)) return SERAPH_VBIT_VOID;
    if (!seraph_cap_can_write(cap)) return SERAPH_VBIT_FALSE;
    if (!seraph_cap_range_valid(cap, offset, sizeof(uint64_t))) return SERAPH_VBIT_FALSE;
    if (seraph_cap_is_sealed(cap)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U64(value)) return SERAPH_VBIT_FALSE;

    memcpy((char*)cap.base + offset, &value, sizeof(uint64_t));
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_cap_copy(Seraph_Capability dst, uint64_t dst_offset,
                             Seraph_Capability src, uint64_t src_offset,
                             uint64_t length) {
    if (seraph_cap_is_void(dst) || seraph_cap_is_void(src)) {
        return SERAPH_VBIT_VOID;
    }
    if (!seraph_cap_can_write(dst)) return SERAPH_VBIT_FALSE;
    if (!seraph_cap_can_read(src)) return SERAPH_VBIT_FALSE;
    if (!seraph_cap_range_valid(dst, dst_offset, length)) return SERAPH_VBIT_FALSE;
    if (!seraph_cap_range_valid(src, src_offset, length)) return SERAPH_VBIT_FALSE;
    if (seraph_cap_is_sealed(dst) || seraph_cap_is_sealed(src)) return SERAPH_VBIT_FALSE;

    memmove((char*)dst.base + dst_offset,
            (char*)src.base + src_offset,
            length);

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Capability Descriptor Table (CDT)
 *============================================================================*/

/**
 * @brief Internal helper to initialize CDT entries after allocation
 */
static void cdt_init_entries(Seraph_CDT* cdt, uint32_t capacity) {
    cdt->capacity = capacity;
    cdt->count = 0;
    cdt->generation = 1;

    /* Initialize free list: each entry points to next */
    for (uint32_t i = 0; i < capacity - 1; i++) {
        cdt->entries[i].flags = i + 1;  /* Next free index */
    }
    cdt->entries[capacity - 1].flags = SERAPH_VOID_U32;  /* End of list */
    cdt->free_head = 0;
}

Seraph_Vbit seraph_cdt_init(Seraph_CDT* cdt, uint32_t capacity) {
    if (!cdt) return SERAPH_VBIT_VOID;
    if (capacity == 0 || capacity > SERAPH_CDT_MAX_ENTRIES) {
        return SERAPH_VBIT_FALSE;
    }

    cdt->entries = (Seraph_CDT_Entry*)calloc(capacity, sizeof(Seraph_CDT_Entry));
    if (!cdt->entries) return SERAPH_VBIT_FALSE;

    cdt->arena = NULL;  /* Heap allocated */
    cdt_init_entries(cdt, capacity);

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Initialize CDT with arena allocation (Atlas-ready)
 *
 * Allocates the entry table from the given arena instead of the heap.
 * The CDT will persist with the arena if it's mmap-backed.
 *
 * @param cdt CDT to initialize
 * @param arena Arena to allocate from
 * @param capacity Maximum number of entries
 * @return TRUE if success, FALSE if failed, VOID if params invalid
 */
Seraph_Vbit seraph_cdt_init_arena(Seraph_CDT* cdt, struct Seraph_Arena* arena, uint32_t capacity) {
    if (!cdt) return SERAPH_VBIT_VOID;
    if (!seraph_arena_is_valid(arena)) return SERAPH_VBIT_VOID;
    if (capacity == 0 || capacity > SERAPH_CDT_MAX_ENTRIES) {
        return SERAPH_VBIT_FALSE;
    }

    cdt->entries = (Seraph_CDT_Entry*)seraph_arena_calloc(
        arena, capacity * sizeof(Seraph_CDT_Entry), sizeof(void*));
    if (cdt->entries == SERAPH_VOID_PTR) {
        cdt->entries = NULL;
        return SERAPH_VBIT_FALSE;
    }

    cdt->arena = arena;  /* Arena allocated */
    cdt_init_entries(cdt, capacity);

    return SERAPH_VBIT_TRUE;
}

void seraph_cdt_destroy(Seraph_CDT* cdt) {
    if (!cdt) return;

    /* Only free if heap allocated (arena = NULL) */
    if (cdt->arena == NULL && cdt->entries != NULL) {
        free(cdt->entries);
    }
    /* Arena-allocated entries are freed when arena is reset/destroyed */

    cdt->entries = NULL;
    cdt->arena = NULL;
    cdt->capacity = 0;
    cdt->count = 0;
}

Seraph_CapCompact seraph_cdt_alloc(Seraph_CDT* cdt, Seraph_Capability cap) {
    if (!cdt || !cdt->entries) return SERAPH_CAP_COMPACT_VOID;
    if (seraph_cap_is_void(cap)) return SERAPH_CAP_COMPACT_VOID;
    if (cdt->free_head == SERAPH_VOID_U32) return SERAPH_CAP_COMPACT_VOID;

    uint32_t index = cdt->free_head;
    Seraph_CDT_Entry* entry = &cdt->entries[index];

    /* Update free list */
    cdt->free_head = entry->flags;

    /* Initialize entry */
    entry->cap = cap;
    entry->refcount = 1;
    entry->flags = 0;

    cdt->count++;

    return (Seraph_CapCompact){
        .cdt_index = index,
        .offset = 0,
        .perms = cap.permissions
    };
}

Seraph_Capability seraph_cdt_lookup(Seraph_CDT* cdt, Seraph_CapCompact compact) {
    if (!cdt || !cdt->entries) return SERAPH_CAP_VOID;
    if (seraph_cap_compact_is_void(compact)) return SERAPH_CAP_VOID;
    if (compact.cdt_index >= cdt->capacity) return SERAPH_CAP_VOID;

    Seraph_CDT_Entry* entry = &cdt->entries[compact.cdt_index];
    if (entry->refcount == 0) return SERAPH_CAP_VOID;  /* Entry not in use */

    Seraph_Capability result = entry->cap;

    /* Apply offset from compact capability */
    if (compact.offset > 0) {
        if (compact.offset > result.length) return SERAPH_CAP_VOID;
        result.base = (char*)result.base + compact.offset;
        result.length -= compact.offset;
    }

    /* Apply permission restriction from compact */
    result.permissions &= compact.perms;

    return result;
}

Seraph_Vbit seraph_cdt_addref(Seraph_CDT* cdt, Seraph_CapCompact compact) {
    if (!cdt || !cdt->entries) return SERAPH_VBIT_VOID;
    if (seraph_cap_compact_is_void(compact)) return SERAPH_VBIT_VOID;
    if (compact.cdt_index >= cdt->capacity) return SERAPH_VBIT_FALSE;

    Seraph_CDT_Entry* entry = &cdt->entries[compact.cdt_index];
    if (entry->refcount == 0) return SERAPH_VBIT_FALSE;

    entry->refcount++;
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_cdt_release(Seraph_CDT* cdt, Seraph_CapCompact compact) {
    if (!cdt || !cdt->entries) return SERAPH_VBIT_VOID;
    if (seraph_cap_compact_is_void(compact)) return SERAPH_VBIT_VOID;
    if (compact.cdt_index >= cdt->capacity) return SERAPH_VBIT_FALSE;

    Seraph_CDT_Entry* entry = &cdt->entries[compact.cdt_index];
    if (entry->refcount == 0) return SERAPH_VBIT_FALSE;

    entry->refcount--;

    if (entry->refcount == 0) {
        /* Free the entry: add to free list */
        entry->cap = SERAPH_CAP_VOID;
        entry->flags = cdt->free_head;
        cdt->free_head = compact.cdt_index;
        cdt->count--;
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Capability Sealing
 *============================================================================*/

Seraph_Capability seraph_cap_seal(Seraph_Capability cap, uint8_t type) {
    if (seraph_cap_is_void(cap)) return SERAPH_CAP_VOID;
    if (!seraph_cap_has_perm(cap, SERAPH_CAP_SEAL)) return SERAPH_CAP_VOID;
    if (type == 0) return SERAPH_CAP_VOID;  /* Type 0 means unsealed */

    cap.type = type;
    /* Remove SEAL permission after sealing (can only seal once) */
    cap.permissions &= ~SERAPH_CAP_SEAL;

    return cap;
}

Seraph_Capability seraph_cap_unseal(Seraph_Capability cap, uint8_t expected_type) {
    if (seraph_cap_is_void(cap)) return SERAPH_CAP_VOID;
    if (!seraph_cap_has_perm(cap, SERAPH_CAP_UNSEAL)) return SERAPH_CAP_VOID;
    if (cap.type != expected_type) return SERAPH_CAP_VOID;
    if (expected_type == 0) return SERAPH_CAP_VOID;

    cap.type = 0;  /* Unsealed */
    /* Remove UNSEAL permission */
    cap.permissions &= ~SERAPH_CAP_UNSEAL;

    return cap;
}

/*============================================================================
 * Capability Comparison
 *============================================================================*/

bool seraph_cap_is_subset(Seraph_Capability a, Seraph_Capability b) {
    if (seraph_cap_is_void(a) || seraph_cap_is_void(b)) return false;

    /* Check generation matches */
    if (a.generation != b.generation) return false;

    /* Check a's base is >= b's base */
    if ((uintptr_t)a.base < (uintptr_t)b.base) return false;

    /* Check a's end is <= b's end */
    uintptr_t a_end = (uintptr_t)a.base + a.length;
    uintptr_t b_end = (uintptr_t)b.base + b.length;
    if (a_end > b_end) return false;

    /* Check a's permissions are subset of b's */
    if ((a.permissions & ~b.permissions) != 0) return false;

    return true;
}
