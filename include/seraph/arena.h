/**
 * @file arena.h
 * @brief MC8: Spectral Arena - Auto-SoA Memory Allocator
 *
 * The Spectral Arena is a high-performance memory allocator designed for
 * SERAPH's unique requirements:
 *
 *   1. BUMP ALLOCATION: O(1) allocation by incrementing a pointer. No free
 *      lists, no fragmentation, no overhead.
 *
 *   2. GENERATION-BASED DEALLOCATION: Instead of freeing individual objects,
 *      reset the entire arena. All allocations become invalid instantly (O(1)).
 *      Old capabilities fail temporal safety checks.
 *
 *   3. AUTOMATIC SoA TRANSFORMATION: Traditional Array-of-Structures (AoS)
 *      stores objects contiguously. Structure-of-Arrays (SoA) stores each
 *      field contiguously. SoA is cache-friendly for field-at-a-time access.
 *      The Spectral Arena automatically transforms between these layouts.
 *
 *   4. PRISM ABSTRACTION: A "prism" is a view into a single field across
 *      all elements. Prisms enable SIMD-friendly iteration over fields.
 *
 *   5. CAPABILITY INTEGRATION: Every allocation can produce a capability
 *      with proper bounds and generation for temporal safety.
 *
 * MEMORY LAYOUT:
 *
 *   Traditional AoS:
 *     [x0,y0,z0] [x1,y1,z1] [x2,y2,z2] ...
 *
 *   Spectral Arena SoA:
 *     [x0,x1,x2,...] [y0,y1,y2,...] [z0,z1,z2,...]
 *
 *   When iterating over all x-values, SoA achieves perfect cache utilization.
 */

#ifndef SERAPH_ARENA_H
#define SERAPH_ARENA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/capability.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Default alignment (cache line size) */
#define SERAPH_ARENA_DEFAULT_ALIGNMENT 64

/** Maximum valid generation (VOID - 1) */
#define SERAPH_ARENA_MAX_GENERATION (SERAPH_VOID_U32 - 1)

/** Arena flags */
typedef enum {
    SERAPH_ARENA_FLAG_NONE         = 0x00,
    SERAPH_ARENA_FLAG_ZERO_ON_ALLOC = 0x01,  /**< Zero-initialize allocations */
    SERAPH_ARENA_FLAG_ZERO_ON_RESET = 0x02,  /**< Zero memory on reset */
    SERAPH_ARENA_FLAG_GROW_ALLOWED  = 0x04,  /**< Allow arena to grow (not implemented) */
    SERAPH_ARENA_FLAG_MMAP         = 0x08,   /**< Use mmap instead of malloc (Atlas-ready) */
    SERAPH_ARENA_FLAG_PERSISTENT   = 0x10,   /**< Backed by file (survives process restart) */
    SERAPH_ARENA_FLAG_SHARED       = 0x20,   /**< Shared between processes */
} Seraph_ArenaFlags;

/*============================================================================
 * Arena Structure
 *============================================================================*/

/**
 * @brief The Spectral Arena allocator
 *
 * A bump-pointer allocator with generation-based temporal safety.
 * All allocations are freed together on reset.
 *
 * ATLAS-READY: When created with SERAPH_ARENA_FLAG_MMAP and
 * SERAPH_ARENA_FLAG_PERSISTENT, the arena is backed by a memory-mapped
 * file. This is the first step toward "RAM = Disk" (Atlas single-level store).
 */
typedef struct Seraph_Arena {
    uint8_t*   memory;          /**< Raw memory pool */
    size_t     capacity;        /**< Total bytes available */
    size_t     used;            /**< Bytes currently used (bump pointer offset) */
    uint32_t   generation;      /**< Allocation epoch for temporal safety */
    uint32_t   alignment;       /**< Minimum alignment for allocations */
    uint32_t   flags;           /**< Configuration flags */
    uint32_t   alloc_count;     /**< Number of allocations (for debugging) */
    /* Atlas-ready fields for mmap-backed arenas */
    intptr_t   mmap_handle;     /**< Platform-specific handle (fd on POSIX, HANDLE on Windows) */
    char*      file_path;       /**< Path to backing file (if persistent) */
} Seraph_Arena;

/*============================================================================
 * Field Descriptor (for SoA schemas)
 *============================================================================*/

/**
 * @brief Describes a single field within a structure
 */
typedef struct {
    size_t     offset;          /**< Byte offset within original struct */
    size_t     size;            /**< Size of field in bytes */
    size_t     align;           /**< Alignment requirement */
} Seraph_FieldDesc;

/*============================================================================
 * SoA Schema
 *============================================================================*/

/**
 * @brief Describes the layout of a structure for SoA transformation
 *
 * A schema defines how to scatter a struct's fields into separate arrays
 * and gather them back.
 */
typedef struct {
    Seraph_FieldDesc* fields;   /**< Array of field descriptors */
    uint32_t   field_count;     /**< Number of fields */
    size_t     struct_size;     /**< Total size of original struct */
    size_t     struct_align;    /**< Alignment of original struct */
} Seraph_SoA_Schema;

/*============================================================================
 * SoA Array
 *============================================================================*/

/**
 * @brief A Structure-of-Arrays transformed array
 *
 * Elements are scattered across multiple field arrays for cache efficiency.
 */
typedef struct {
    Seraph_Arena*      arena;           /**< Arena that owns the memory */
    Seraph_SoA_Schema* schema;          /**< Layout information */
    uint8_t**          field_arrays;    /**< Array of pointers to field arrays */
    size_t             capacity;        /**< Maximum element count */
    size_t             count;           /**< Current element count */
    uint32_t           generation;      /**< Must match arena generation */
} Seraph_SoA_Array;

/*============================================================================
 * Prism (Field View)
 *============================================================================*/

/**
 * @brief A view into a single field across all elements
 *
 * A prism provides cache-friendly, SIMD-friendly access to one field
 * of all elements in an SoA array.
 */
typedef struct {
    void*      base;            /**< Start of field array */
    size_t     stride;          /**< Bytes between consecutive elements */
    size_t     element_size;    /**< Size of each field element */
    size_t     count;           /**< Number of elements */
    uint32_t   generation;      /**< Must match source generation */
    uint8_t    permissions;     /**< Read/write flags */
} Seraph_Prism;

/** VOID prism (invalid view) */
#define SERAPH_PRISM_VOID ((Seraph_Prism){ \
    SERAPH_VOID_PTR, SERAPH_VOID_U64, SERAPH_VOID_U64, \
    SERAPH_VOID_U64, SERAPH_VOID_U32, 0 })

/*============================================================================
 * Alignment Utilities
 *============================================================================*/

/**
 * @brief Round size up to alignment boundary
 */
static inline size_t seraph_align_up(size_t size, size_t align) {
    return (size + align - 1) & ~(align - 1);
}

/**
 * @brief Check if pointer is aligned
 */
static inline bool seraph_is_aligned(const void* ptr, size_t align) {
    return ((uintptr_t)ptr & (align - 1)) == 0;
}

/**
 * @brief Align pointer up
 */
static inline void* seraph_align_ptr(void* ptr, size_t align) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + align - 1) & ~(align - 1);
    return (void*)aligned;
}

/*============================================================================
 * Arena Lifecycle
 *============================================================================*/

/**
 * @brief Create a new arena with specified capacity
 *
 * @param arena Pointer to arena to initialize
 * @param capacity Total bytes for the arena
 * @param alignment Minimum alignment (0 for default = 64)
 * @param flags Configuration flags
 * @return TRUE if success, FALSE if allocation failed, VOID if params invalid
 */
Seraph_Vbit seraph_arena_create(Seraph_Arena* arena, size_t capacity,
                                 size_t alignment, uint32_t flags);

/**
 * @brief Destroy an arena and free its memory
 *
 * @param arena Arena to destroy
 */
void seraph_arena_destroy(Seraph_Arena* arena);

/**
 * @brief Reset arena for reuse
 *
 * Increments the generation (invalidating all previous allocations)
 * and resets the bump pointer. O(1) mass deallocation.
 *
 * @param arena Arena to reset
 * @return New generation number, or VOID if arena invalid
 */
uint32_t seraph_arena_reset(Seraph_Arena* arena);

/**
 * @brief Create a persistent mmap-backed arena (Atlas foundation)
 *
 * This is the first step toward "RAM = Disk" - the arena is backed by
 * a memory-mapped file that persists across process restarts.
 *
 * @param arena Pointer to arena to initialize
 * @param file_path Path to the backing file
 * @param capacity Total bytes for the arena
 * @param alignment Minimum alignment (0 for default = 64)
 * @param shared If true, arena can be shared between processes
 * @return TRUE if success, FALSE if failed, VOID if params invalid
 */
Seraph_Vbit seraph_arena_create_persistent(Seraph_Arena* arena,
                                            const char* file_path,
                                            size_t capacity,
                                            size_t alignment,
                                            bool shared);

/**
 * @brief Sync a persistent arena to disk
 *
 * For persistent mmap arenas, this ensures all changes are written
 * to the backing file.
 *
 * @param arena The arena to sync
 * @return TRUE if success, FALSE if not persistent, VOID if invalid
 */
Seraph_Vbit seraph_arena_sync(Seraph_Arena* arena);

/**
 * @brief Check if arena is valid
 *
 * @param arena Arena to check
 * @return true if arena is valid and ready for use
 */
static inline bool seraph_arena_is_valid(const Seraph_Arena* arena) {
    return arena != NULL && arena->memory != NULL && arena->capacity > 0;
}

/*============================================================================
 * Arena Information
 *============================================================================*/

/**
 * @brief Get remaining capacity in arena
 *
 * @param arena The arena
 * @return Bytes remaining, or 0 if invalid
 */
static inline size_t seraph_arena_remaining(const Seraph_Arena* arena) {
    if (!seraph_arena_is_valid(arena)) return 0;
    return arena->capacity - arena->used;
}

/**
 * @brief Get used capacity in arena
 *
 * @param arena The arena
 * @return Bytes used, or 0 if invalid
 */
static inline size_t seraph_arena_used(const Seraph_Arena* arena) {
    if (!seraph_arena_is_valid(arena)) return 0;
    return arena->used;
}

/**
 * @brief Get current generation
 *
 * @param arena The arena
 * @return Current generation, or VOID if invalid
 */
static inline uint32_t seraph_arena_generation(const Seraph_Arena* arena) {
    if (!seraph_arena_is_valid(arena)) return SERAPH_VOID_U32;
    return arena->generation;
}

/*============================================================================
 * Basic Allocation
 *============================================================================*/

/**
 * @brief Allocate memory from arena (bump allocation)
 *
 * @param arena The arena to allocate from
 * @param size Number of bytes to allocate
 * @param align Alignment (0 for arena default)
 * @return Pointer to allocated memory, or VOID_PTR if not enough space
 */
void* seraph_arena_alloc(Seraph_Arena* arena, size_t size, size_t align);

/**
 * @brief Allocate an array from arena
 *
 * @param arena The arena
 * @param elem_size Size of each element
 * @param count Number of elements
 * @param align Alignment (0 for arena default)
 * @return Pointer to array, or VOID_PTR if not enough space
 */
void* seraph_arena_alloc_array(Seraph_Arena* arena, size_t elem_size,
                                size_t count, size_t align);

/**
 * @brief Allocate and zero-initialize memory
 *
 * @param arena The arena
 * @param size Number of bytes
 * @param align Alignment (0 for arena default)
 * @return Pointer to zeroed memory, or VOID_PTR if not enough space
 */
void* seraph_arena_calloc(Seraph_Arena* arena, size_t size, size_t align);

/*============================================================================
 * Capability Integration
 *============================================================================*/

/**
 * @brief Get a capability for an arena allocation
 *
 * The capability includes the arena's generation for temporal safety.
 * After arena reset, the capability becomes invalid.
 *
 * @param arena The arena
 * @param ptr Pointer previously returned by seraph_arena_alloc
 * @param size Size of the allocation
 * @param perms Permission flags
 * @return Capability, or VOID if invalid
 */
Seraph_Capability seraph_arena_get_capability(const Seraph_Arena* arena,
                                               void* ptr, size_t size,
                                               uint8_t perms);

/**
 * @brief Check if a capability is still valid for this arena
 *
 * Checks that the capability's generation matches the arena's generation.
 *
 * @param arena The arena
 * @param cap The capability to check
 * @return TRUE if valid, FALSE if generation mismatch, VOID if params invalid
 */
Seraph_Vbit seraph_arena_check_capability(const Seraph_Arena* arena,
                                           Seraph_Capability cap);

/*============================================================================
 * SoA Schema Operations
 *============================================================================*/

/**
 * @brief Create an SoA schema from field descriptors
 *
 * @param schema Pointer to schema to initialize
 * @param struct_size Total size of original struct
 * @param struct_align Alignment of original struct
 * @param fields Array of field descriptors
 * @param field_count Number of fields
 * @return TRUE if success, FALSE if allocation failed, VOID if params invalid
 */
Seraph_Vbit seraph_soa_schema_create(Seraph_SoA_Schema* schema,
                                      size_t struct_size, size_t struct_align,
                                      const Seraph_FieldDesc* fields,
                                      uint32_t field_count);

/**
 * @brief Destroy an SoA schema
 *
 * @param schema Schema to destroy
 */
void seraph_soa_schema_destroy(Seraph_SoA_Schema* schema);

/**
 * @brief Check if schema is valid
 */
static inline bool seraph_soa_schema_is_valid(const Seraph_SoA_Schema* schema) {
    return schema != NULL && schema->fields != NULL && schema->field_count > 0;
}

/*============================================================================
 * SoA Array Operations
 *============================================================================*/

/**
 * @brief Create an SoA array with given capacity
 *
 * Allocates field arrays from the arena.
 *
 * @param array Pointer to array to initialize
 * @param arena Arena to allocate from
 * @param schema Layout schema
 * @param capacity Maximum number of elements
 * @return TRUE if success, FALSE if allocation failed, VOID if params invalid
 */
Seraph_Vbit seraph_soa_array_create(Seraph_SoA_Array* array,
                                     Seraph_Arena* arena,
                                     Seraph_SoA_Schema* schema,
                                     size_t capacity);

/**
 * @brief Push an element to the SoA array (scatter)
 *
 * Copies fields from the element struct into the field arrays.
 *
 * @param array The SoA array
 * @param element Pointer to struct to scatter
 * @return Index of new element, or SERAPH_VOID_U64 if full/invalid
 */
size_t seraph_soa_array_push(Seraph_SoA_Array* array, const void* element);

/**
 * @brief Get an element from the SoA array (gather)
 *
 * Copies fields from field arrays into the element struct.
 *
 * @param array The SoA array
 * @param index Index of element to get
 * @param element Pointer to struct to fill
 * @return TRUE if success, FALSE if out of bounds, VOID if invalid
 */
Seraph_Vbit seraph_soa_array_get(const Seraph_SoA_Array* array,
                                  size_t index, void* element);

/**
 * @brief Set an element in the SoA array (scatter)
 *
 * @param array The SoA array
 * @param index Index of element to set
 * @param element Pointer to struct with new values
 * @return TRUE if success, FALSE if out of bounds, VOID if invalid
 */
Seraph_Vbit seraph_soa_array_set(Seraph_SoA_Array* array,
                                  size_t index, const void* element);

/**
 * @brief Get current element count
 */
static inline size_t seraph_soa_array_count(const Seraph_SoA_Array* array) {
    if (!array) return 0;
    return array->count;
}

/**
 * @brief Check if SoA array is valid
 */
static inline bool seraph_soa_array_is_valid(const Seraph_SoA_Array* array) {
    return array != NULL && array->arena != NULL &&
           array->schema != NULL && array->field_arrays != NULL;
}

/*============================================================================
 * Prism Operations
 *============================================================================*/

/**
 * @brief Get a prism (field view) from an SoA array
 *
 * @param array The SoA array
 * @param field_index Which field (0 to field_count-1)
 * @return Prism for the field, or VOID prism if invalid
 */
Seraph_Prism seraph_soa_get_prism(const Seraph_SoA_Array* array,
                                   uint32_t field_index);

/**
 * @brief Check if prism is valid
 */
static inline bool seraph_prism_is_valid(Seraph_Prism prism) {
    return prism.base != SERAPH_VOID_PTR &&
           prism.generation != SERAPH_VOID_U32;
}

/**
 * @brief Check if prism index is in bounds
 */
static inline bool seraph_prism_in_bounds(Seraph_Prism prism, size_t index) {
    return seraph_prism_is_valid(prism) && index < prism.count;
}

/**
 * @brief Get pointer to element in prism
 *
 * @param prism The prism
 * @param index Element index
 * @return Pointer to element, or VOID_PTR if out of bounds
 */
static inline void* seraph_prism_get_ptr(Seraph_Prism prism, size_t index) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VOID_PTR;
    return (uint8_t*)prism.base + (index * prism.stride);
}

/**
 * @brief Read uint8_t from prism
 */
uint8_t seraph_prism_read_u8(Seraph_Prism prism, size_t index);

/**
 * @brief Read uint16_t from prism
 */
uint16_t seraph_prism_read_u16(Seraph_Prism prism, size_t index);

/**
 * @brief Read uint32_t from prism
 */
uint32_t seraph_prism_read_u32(Seraph_Prism prism, size_t index);

/**
 * @brief Read uint64_t from prism
 */
uint64_t seraph_prism_read_u64(Seraph_Prism prism, size_t index);

/**
 * @brief Write uint8_t to prism
 */
Seraph_Vbit seraph_prism_write_u8(Seraph_Prism prism, size_t index, uint8_t value);

/**
 * @brief Write uint16_t to prism
 */
Seraph_Vbit seraph_prism_write_u16(Seraph_Prism prism, size_t index, uint16_t value);

/**
 * @brief Write uint32_t to prism
 */
Seraph_Vbit seraph_prism_write_u32(Seraph_Prism prism, size_t index, uint32_t value);

/**
 * @brief Write uint64_t to prism
 */
Seraph_Vbit seraph_prism_write_u64(Seraph_Prism prism, size_t index, uint64_t value);

/*============================================================================
 * Bulk Operations (SIMD-friendly)
 *============================================================================*/

/**
 * @brief Fill prism with a constant value
 *
 * @param prism The prism to fill
 * @param value Pointer to value (must be element_size bytes)
 * @return TRUE if success, VOID if invalid
 */
Seraph_Vbit seraph_prism_fill(Seraph_Prism prism, const void* value);

/**
 * @brief Copy data from one prism to another
 *
 * @param dst Destination prism
 * @param src Source prism
 * @return TRUE if success, FALSE if size mismatch, VOID if invalid
 */
Seraph_Vbit seraph_prism_copy(Seraph_Prism dst, Seraph_Prism src);

/*============================================================================
 * Convenience Macros for Schema Definition
 *============================================================================*/

/**
 * @brief Helper macro to define a field descriptor
 *
 * Usage:
 *   SERAPH_FIELD(MyStruct, field_name)
 */
#define SERAPH_FIELD(type, field) \
    { offsetof(type, field), sizeof(((type*)0)->field), _Alignof(((type*)0)->field) }

/**
 * @brief Helper macro to define a complete schema
 *
 * Usage:
 *   Seraph_FieldDesc fields[] = {
 *       SERAPH_FIELD(MyStruct, x),
 *       SERAPH_FIELD(MyStruct, y),
 *       SERAPH_FIELD(MyStruct, z)
 *   };
 *   seraph_soa_schema_create(&schema, sizeof(MyStruct), _Alignof(MyStruct),
 *                            fields, 3);
 */

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_ARENA_H */
