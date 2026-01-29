/**
 * @file arena.c
 * @brief MC8: Spectral Arena - Auto-SoA Memory Allocator Implementation
 *
 * ATLAS-READY: Supports mmap-backed arenas for persistent memory.
 * When SERAPH_ARENA_FLAG_MMAP is set, memory is allocated via mmap
 * instead of malloc. When SERAPH_ARENA_FLAG_PERSISTENT is also set,
 * the arena is backed by a file that survives process restart.
 */

#include "seraph/arena.h"

#ifdef SERAPH_KERNEL
    #include "seraph/kmalloc.h"
    #include "seraph/kruntime.h"
    #include "seraph/vmm.h"
#else
    #include <stdlib.h>
    #include <string.h>
    #include <stdio.h>
#endif

/* Platform-specific mmap includes */
#if defined(SERAPH_KERNEL)
    /* Kernel mode - use SERAPH's own allocators */
#elif defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

/*============================================================================
 * Platform-Specific mmap Implementation (Atlas Foundation)
 *============================================================================*/

#if defined(SERAPH_KERNEL)

/**
 * @brief Kernel-mode memory allocation using SERAPH's VMM
 *
 * In kernel mode, arenas use SERAPH's native page allocator.
 * Persistent arenas would be backed by Atlas when available.
 */
static uint8_t* arena_mmap_alloc(size_t capacity, const char* file_path,
                                  intptr_t* out_handle, bool persistent, bool shared) {
    (void)file_path;  /* Atlas persistence handled separately */
    (void)persistent;
    (void)shared;
    (void)out_handle;

    /* Allocate pages via kmalloc_pages */
    size_t pages = (capacity + 4095) / 4096;
    void* memory = seraph_kmalloc_pages(pages);

    if (memory && out_handle) {
        *out_handle = 0;  /* No special handle in kernel mode */
    }

    return (uint8_t*)memory;
}

static void arena_mmap_free(uint8_t* memory, size_t capacity, intptr_t handle) {
    (void)handle;
    if (memory) {
        size_t pages = (capacity + 4095) / 4096;
        seraph_kfree_pages(memory, pages);
    }
}

static void arena_mmap_sync(uint8_t* memory, size_t size) {
    /* No-op in kernel mode - Atlas handles persistence */
    (void)memory;
    (void)size;
}

#elif defined(_WIN32)

/**
 * @brief Windows mmap implementation using MapViewOfFile
 */
static uint8_t* arena_mmap_alloc(size_t capacity, const char* file_path,
                                  intptr_t* out_handle, bool persistent, bool shared) {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMapping = NULL;
    uint8_t* memory = NULL;
    DWORD protect = PAGE_READWRITE;
    DWORD access = FILE_MAP_ALL_ACCESS;

    if (persistent && file_path) {
        /* Create or open the backing file */
        hFile = CreateFileA(
            file_path,
            GENERIC_READ | GENERIC_WRITE,
            shared ? (FILE_SHARE_READ | FILE_SHARE_WRITE) : 0,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hFile == INVALID_HANDLE_VALUE) {
            return NULL;
        }

        /* Extend file to capacity if needed */
        LARGE_INTEGER fileSize;
        fileSize.QuadPart = (LONGLONG)capacity;
        if (!SetFilePointerEx(hFile, fileSize, NULL, FILE_BEGIN)) {
            CloseHandle(hFile);
            return NULL;
        }
        if (!SetEndOfFile(hFile)) {
            CloseHandle(hFile);
            return NULL;
        }
    }

    /* Create file mapping */
    hMapping = CreateFileMappingA(
        (persistent && hFile != INVALID_HANDLE_VALUE) ? hFile : INVALID_HANDLE_VALUE,
        NULL,
        protect,
        (DWORD)(capacity >> 32),
        (DWORD)(capacity & 0xFFFFFFFF),
        NULL
    );
    if (!hMapping) {
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
        return NULL;
    }

    /* Map view of file */
    memory = (uint8_t*)MapViewOfFile(hMapping, access, 0, 0, capacity);
    if (!memory) {
        CloseHandle(hMapping);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
        return NULL;
    }

    /* Store mapping handle for cleanup */
    if (out_handle) {
        *out_handle = (intptr_t)hMapping;
    }

    /* Close file handle (mapping keeps it alive) */
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }

    return memory;
}

static void arena_mmap_free(uint8_t* memory, size_t capacity, intptr_t handle) {
    (void)capacity;  /* Not needed on Windows */
    if (memory) {
        UnmapViewOfFile(memory);
    }
    if (handle != 0 && handle != -1) {
        CloseHandle((HANDLE)handle);
    }
}

#else /* POSIX */

/**
 * @brief POSIX mmap implementation
 */
static uint8_t* arena_mmap_alloc(size_t capacity, const char* file_path,
                                  intptr_t* out_handle, bool persistent, bool shared) {
    int fd = -1;
    int prot = PROT_READ | PROT_WRITE;
    int flags = shared ? MAP_SHARED : MAP_PRIVATE;
    uint8_t* memory = NULL;

    if (persistent && file_path) {
        /* Create or open the backing file */
        fd = open(file_path, O_RDWR | O_CREAT, 0644);
        if (fd < 0) {
            return NULL;
        }

        /* Extend file to capacity if needed */
        if (ftruncate(fd, (off_t)capacity) < 0) {
            close(fd);
            return NULL;
        }
    } else {
        /* Anonymous mapping */
        flags |= MAP_ANONYMOUS;
    }

    /* Create the mapping */
    memory = (uint8_t*)mmap(NULL, capacity, prot, flags, fd, 0);
    if (memory == MAP_FAILED) {
        if (fd >= 0) close(fd);
        return NULL;
    }

    if (out_handle) {
        *out_handle = (intptr_t)fd;
    }

    return memory;
}

static void arena_mmap_free(uint8_t* memory, size_t capacity, intptr_t handle) {
    if (memory) {
        munmap(memory, capacity);
    }
    if (handle >= 0) {
        close((int)handle);
    }
}

#endif /* Platform-specific */

/*============================================================================
 * Arena Lifecycle
 *============================================================================*/

Seraph_Vbit seraph_arena_create(Seraph_Arena* arena, size_t capacity,
                                 size_t alignment, uint32_t flags) {
    if (!arena) return SERAPH_VBIT_VOID;
    if (capacity == 0) return SERAPH_VBIT_FALSE;

    /* Default alignment to cache line size */
    if (alignment == 0) {
        alignment = SERAPH_ARENA_DEFAULT_ALIGNMENT;
    }

    /* Alignment must be power of 2 */
    if ((alignment & (alignment - 1)) != 0) {
        return SERAPH_VBIT_FALSE;
    }

    /* Initialize arena fields */
    arena->mmap_handle = -1;
    arena->file_path = NULL;

    /* Choose allocation strategy based on flags */
    if (flags & SERAPH_ARENA_FLAG_MMAP) {
        /* mmap-based allocation (Atlas-ready) */
        bool persistent = (flags & SERAPH_ARENA_FLAG_PERSISTENT) != 0;
        bool shared = (flags & SERAPH_ARENA_FLAG_SHARED) != 0;

        /* For persistent arenas, we need a file path (set later) */
        arena->memory = arena_mmap_alloc(capacity, NULL, &arena->mmap_handle,
                                          persistent, shared);
    } else {
        /* Traditional malloc-based allocation */
#if defined(SERAPH_KERNEL)
        arena->memory = (uint8_t*)aligned_alloc(alignment, capacity);
#elif defined(_WIN32)
        arena->memory = (uint8_t*)_aligned_malloc(capacity, alignment);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
        arena->memory = (uint8_t*)aligned_alloc(alignment, capacity);
#else
        /* Fallback: allocate extra and manually align */
        void* raw = malloc(capacity + alignment);
        if (!raw) {
            arena->memory = NULL;
        } else {
            arena->memory = (uint8_t*)seraph_align_ptr(raw, alignment);
        }
#endif
    }

    if (!arena->memory) return SERAPH_VBIT_FALSE;

    arena->capacity = capacity;
    arena->used = 0;
    arena->generation = 1;  /* Start at 1 (0 could be confused with uninitialized) */
    arena->alignment = (uint32_t)alignment;
    arena->flags = flags;
    arena->alloc_count = 0;

    /* Zero-initialize if requested */
    if (flags & SERAPH_ARENA_FLAG_ZERO_ON_ALLOC) {
        memset(arena->memory, 0, capacity);
    }

    return SERAPH_VBIT_TRUE;
}

void seraph_arena_destroy(Seraph_Arena* arena) {
    if (!arena) return;

    if (arena->flags & SERAPH_ARENA_FLAG_MMAP) {
        /* mmap-based deallocation */
        arena_mmap_free(arena->memory, arena->capacity, arena->mmap_handle);
    } else {
        /* Traditional malloc-based deallocation */
#if defined(SERAPH_KERNEL)
        aligned_free(arena->memory);
#elif defined(_WIN32)
        _aligned_free(arena->memory);
#else
        free(arena->memory);
#endif
    }

    /* Free file path if allocated */
    if (arena->file_path) {
        free(arena->file_path);
        arena->file_path = NULL;
    }

    arena->memory = NULL;
    arena->capacity = 0;
    arena->used = 0;
    arena->mmap_handle = -1;
    arena->generation = SERAPH_VOID_U32;
}

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
                                            bool shared) {
    if (!arena) return SERAPH_VBIT_VOID;
    if (!file_path) return SERAPH_VBIT_VOID;
    if (capacity == 0) return SERAPH_VBIT_FALSE;

    /* Default alignment to cache line size */
    if (alignment == 0) {
        alignment = SERAPH_ARENA_DEFAULT_ALIGNMENT;
    }

    /* Alignment must be power of 2 */
    if ((alignment & (alignment - 1)) != 0) {
        return SERAPH_VBIT_FALSE;
    }

    /* Initialize arena fields */
    arena->mmap_handle = -1;
    arena->file_path = NULL;

    /* Allocate with mmap backed by file */
    arena->memory = arena_mmap_alloc(capacity, file_path, &arena->mmap_handle, true, shared);
    if (!arena->memory) return SERAPH_VBIT_FALSE;

    /* Copy file path */
    size_t path_len = strlen(file_path);
    arena->file_path = (char*)malloc(path_len + 1);
    if (arena->file_path) {
        memcpy(arena->file_path, file_path, path_len + 1);
    }

    arena->capacity = capacity;
    arena->used = 0;
    arena->generation = 1;
    arena->alignment = (uint32_t)alignment;
    arena->flags = SERAPH_ARENA_FLAG_MMAP | SERAPH_ARENA_FLAG_PERSISTENT;
    if (shared) {
        arena->flags |= SERAPH_ARENA_FLAG_SHARED;
    }
    arena->alloc_count = 0;

    return SERAPH_VBIT_TRUE;
}

/**
 * @brief Sync a persistent arena to disk
 *
 * For persistent mmap arenas, this ensures all changes are written
 * to the backing file.
 *
 * @param arena The arena to sync
 * @return TRUE if success, FALSE if not persistent, VOID if invalid
 */
Seraph_Vbit seraph_arena_sync(Seraph_Arena* arena) {
    if (!seraph_arena_is_valid(arena)) return SERAPH_VBIT_VOID;

    if (!(arena->flags & SERAPH_ARENA_FLAG_PERSISTENT)) {
        return SERAPH_VBIT_FALSE;  /* Not a persistent arena */
    }

#if defined(SERAPH_KERNEL)
    /* Kernel mode sync is handled by Atlas */
    arena_mmap_sync(arena->memory, arena->capacity);
#elif defined(_WIN32)
    if (!FlushViewOfFile(arena->memory, arena->capacity)) {
        return SERAPH_VBIT_FALSE;
    }
#else
    if (msync(arena->memory, arena->capacity, MS_SYNC) != 0) {
        return SERAPH_VBIT_FALSE;
    }
#endif

    return SERAPH_VBIT_TRUE;
}

uint32_t seraph_arena_reset(Seraph_Arena* arena) {
    if (!seraph_arena_is_valid(arena)) return SERAPH_VOID_U32;

    /* Zero memory if requested */
    if (arena->flags & SERAPH_ARENA_FLAG_ZERO_ON_RESET) {
        memset(arena->memory, 0, arena->used);
    }

    /* Reset bump pointer */
    arena->used = 0;
    arena->alloc_count = 0;

    /* Increment generation (invalidates all old allocations) */
    if (arena->generation >= SERAPH_ARENA_MAX_GENERATION) {
        /* Wrap around (extremely rare) */
        arena->generation = 1;
    } else {
        arena->generation++;
    }

    return arena->generation;
}

/*============================================================================
 * Basic Allocation
 *============================================================================*/

void* seraph_arena_alloc(Seraph_Arena* arena, size_t size, size_t align) {
    if (!seraph_arena_is_valid(arena)) return SERAPH_VOID_PTR;
    if (size == 0) return SERAPH_VOID_PTR;

    /* Use arena default if no alignment specified */
    if (align == 0) {
        align = arena->alignment;
    }

    /* Alignment must be power of 2 */
    if ((align & (align - 1)) != 0) {
        return SERAPH_VOID_PTR;
    }

    /*
     * Calculate aligned offset based on ACTUAL MEMORY ADDRESS, not just offset.
     * This ensures proper alignment even if arena->memory isn't aligned to
     * the requested alignment.
     *
     * Example: arena->memory = 0x1010 (16-byte aligned)
     *          current = 0
     *          align = 64
     *          Actual address = 0x1010, needs to be 0x1040
     *          So aligned_offset = 0x30 (48), not 0
     */
    uintptr_t current_addr = (uintptr_t)(arena->memory + arena->used);
    uintptr_t aligned_addr = (current_addr + align - 1) & ~(align - 1);
    size_t aligned_offset = arena->used + (aligned_addr - current_addr);

    /* Check for overflow (aligned_offset should always be >= arena->used) */
    if (aligned_offset < arena->used) return SERAPH_VOID_PTR;

    /* Check if we have enough space */
    size_t new_used = aligned_offset + size;
    if (new_used > arena->capacity || new_used < aligned_offset) {
        return SERAPH_VOID_PTR;  /* Not enough space or overflow */
    }

    /* Bump the pointer */
    void* ptr = arena->memory + aligned_offset;
    arena->used = new_used;
    arena->alloc_count++;

    /* Zero if flag set */
    if (arena->flags & SERAPH_ARENA_FLAG_ZERO_ON_ALLOC) {
        memset(ptr, 0, size);
    }

    return ptr;
}

void* seraph_arena_alloc_array(Seraph_Arena* arena, size_t elem_size,
                                size_t count, size_t align) {
    if (count == 0 || elem_size == 0) return SERAPH_VOID_PTR;

    /* Check for multiplication overflow */
    size_t total = elem_size * count;
    if (total / elem_size != count) return SERAPH_VOID_PTR;

    return seraph_arena_alloc(arena, total, align);
}

void* seraph_arena_calloc(Seraph_Arena* arena, size_t size, size_t align) {
    void* ptr = seraph_arena_alloc(arena, size, align);
    if (ptr != SERAPH_VOID_PTR) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*============================================================================
 * Capability Integration
 *============================================================================*/

Seraph_Capability seraph_arena_get_capability(const Seraph_Arena* arena,
                                               void* ptr, size_t size,
                                               uint8_t perms) {
    if (!seraph_arena_is_valid(arena)) return SERAPH_CAP_VOID;
    if (ptr == NULL || ptr == SERAPH_VOID_PTR) return SERAPH_CAP_VOID;
    if (size == 0) return SERAPH_CAP_VOID;

    /* Verify ptr is within arena bounds */
    uintptr_t arena_start = (uintptr_t)arena->memory;
    uintptr_t arena_end = arena_start + arena->used;
    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t ptr_end = ptr_addr + size;

    if (ptr_addr < arena_start || ptr_end > arena_end || ptr_end < ptr_addr) {
        return SERAPH_CAP_VOID;
    }

    return seraph_cap_create(ptr, size, arena->generation, perms);
}

Seraph_Vbit seraph_arena_check_capability(const Seraph_Arena* arena,
                                           Seraph_Capability cap) {
    if (!seraph_arena_is_valid(arena)) return SERAPH_VBIT_VOID;
    if (seraph_cap_is_void(cap)) return SERAPH_VBIT_VOID;

    /* Check generation matches */
    if (cap.generation != arena->generation) {
        return SERAPH_VBIT_FALSE;
    }

    /* Verify cap is within arena bounds */
    uintptr_t arena_start = (uintptr_t)arena->memory;
    uintptr_t arena_end = arena_start + arena->capacity;
    uintptr_t cap_start = (uintptr_t)cap.base;
    uintptr_t cap_end = cap_start + cap.length;

    if (cap_start < arena_start || cap_end > arena_end || cap_end < cap_start) {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * SoA Schema Operations
 *============================================================================*/

Seraph_Vbit seraph_soa_schema_create(Seraph_SoA_Schema* schema,
                                      size_t struct_size, size_t struct_align,
                                      const Seraph_FieldDesc* fields,
                                      uint32_t field_count) {
    if (!schema) return SERAPH_VBIT_VOID;
    if (!fields || field_count == 0) return SERAPH_VBIT_FALSE;
    if (struct_size == 0) return SERAPH_VBIT_FALSE;

    /* Allocate field descriptors */
    schema->fields = (Seraph_FieldDesc*)malloc(field_count * sizeof(Seraph_FieldDesc));
    if (!schema->fields) return SERAPH_VBIT_FALSE;

    /* Copy field descriptors */
    memcpy(schema->fields, fields, field_count * sizeof(Seraph_FieldDesc));

    schema->field_count = field_count;
    schema->struct_size = struct_size;
    schema->struct_align = struct_align;

    return SERAPH_VBIT_TRUE;
}

void seraph_soa_schema_destroy(Seraph_SoA_Schema* schema) {
    if (!schema) return;

    free(schema->fields);
    schema->fields = NULL;
    schema->field_count = 0;
}

/*============================================================================
 * SoA Array Operations
 *============================================================================*/

Seraph_Vbit seraph_soa_array_create(Seraph_SoA_Array* array,
                                     Seraph_Arena* arena,
                                     Seraph_SoA_Schema* schema,
                                     size_t capacity) {
    if (!array) return SERAPH_VBIT_VOID;
    if (!seraph_arena_is_valid(arena)) return SERAPH_VBIT_VOID;
    if (!seraph_soa_schema_is_valid(schema)) return SERAPH_VBIT_VOID;
    if (capacity == 0) return SERAPH_VBIT_FALSE;

    /* Allocate array of field pointers (from arena) */
    array->field_arrays = (uint8_t**)seraph_arena_alloc(
        arena,
        schema->field_count * sizeof(uint8_t*),
        sizeof(void*)
    );
    if (array->field_arrays == SERAPH_VOID_PTR) {
        return SERAPH_VBIT_FALSE;
    }

    /* Allocate each field array from arena */
    for (uint32_t i = 0; i < schema->field_count; i++) {
        size_t field_array_size = schema->fields[i].size * capacity;
        array->field_arrays[i] = (uint8_t*)seraph_arena_alloc(
            arena,
            field_array_size,
            schema->fields[i].align
        );
        if (array->field_arrays[i] == SERAPH_VOID_PTR) {
            /* Allocation failed - can't individually free from arena,
               but arena reset will clean up */
            return SERAPH_VBIT_FALSE;
        }
    }

    array->arena = arena;
    array->schema = schema;
    array->capacity = capacity;
    array->count = 0;
    array->generation = arena->generation;

    return SERAPH_VBIT_TRUE;
}

size_t seraph_soa_array_push(Seraph_SoA_Array* array, const void* element) {
    if (!seraph_soa_array_is_valid(array)) return SERAPH_VOID_U64;
    if (!element) return SERAPH_VOID_U64;
    if (array->count >= array->capacity) return SERAPH_VOID_U64;

    /* Check generation still valid */
    if (array->generation != array->arena->generation) {
        return SERAPH_VOID_U64;
    }

    /* Scatter fields to their arrays */
    const uint8_t* src = (const uint8_t*)element;
    size_t index = array->count;

    for (uint32_t i = 0; i < array->schema->field_count; i++) {
        Seraph_FieldDesc* field = &array->schema->fields[i];
        uint8_t* dst = array->field_arrays[i] + (index * field->size);
        memcpy(dst, src + field->offset, field->size);
    }

    array->count++;
    return index;
}

Seraph_Vbit seraph_soa_array_get(const Seraph_SoA_Array* array,
                                  size_t index, void* element) {
    if (!seraph_soa_array_is_valid(array)) return SERAPH_VBIT_VOID;
    if (!element) return SERAPH_VBIT_VOID;
    if (index >= array->count) return SERAPH_VBIT_FALSE;

    /* Check generation still valid */
    if (array->generation != array->arena->generation) {
        return SERAPH_VBIT_VOID;
    }

    /* Gather fields from arrays */
    uint8_t* dst = (uint8_t*)element;

    for (uint32_t i = 0; i < array->schema->field_count; i++) {
        Seraph_FieldDesc* field = &array->schema->fields[i];
        const uint8_t* src = array->field_arrays[i] + (index * field->size);
        memcpy(dst + field->offset, src, field->size);
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_soa_array_set(Seraph_SoA_Array* array,
                                  size_t index, const void* element) {
    if (!seraph_soa_array_is_valid(array)) return SERAPH_VBIT_VOID;
    if (!element) return SERAPH_VBIT_VOID;
    if (index >= array->count) return SERAPH_VBIT_FALSE;

    /* Check generation still valid */
    if (array->generation != array->arena->generation) {
        return SERAPH_VBIT_VOID;
    }

    /* Scatter fields to arrays */
    const uint8_t* src = (const uint8_t*)element;

    for (uint32_t i = 0; i < array->schema->field_count; i++) {
        Seraph_FieldDesc* field = &array->schema->fields[i];
        uint8_t* dst = array->field_arrays[i] + (index * field->size);
        memcpy(dst, src + field->offset, field->size);
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Prism Operations
 *============================================================================*/

Seraph_Prism seraph_soa_get_prism(const Seraph_SoA_Array* array,
                                   uint32_t field_index) {
    if (!seraph_soa_array_is_valid(array)) return SERAPH_PRISM_VOID;
    if (field_index >= array->schema->field_count) return SERAPH_PRISM_VOID;

    /* Check generation still valid */
    if (array->generation != array->arena->generation) {
        return SERAPH_PRISM_VOID;
    }

    Seraph_FieldDesc* field = &array->schema->fields[field_index];

    return (Seraph_Prism){
        .base = array->field_arrays[field_index],
        .stride = field->size,  /* SoA: stride = element size (contiguous) */
        .element_size = field->size,
        .count = array->count,
        .generation = array->generation,
        .permissions = SERAPH_CAP_RW
    };
}

uint8_t seraph_prism_read_u8(Seraph_Prism prism, size_t index) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VOID_U8;
    if (prism.element_size < sizeof(uint8_t)) return SERAPH_VOID_U8;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    return *ptr;
}

uint16_t seraph_prism_read_u16(Seraph_Prism prism, size_t index) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VOID_U16;
    if (prism.element_size < sizeof(uint16_t)) return SERAPH_VOID_U16;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    uint16_t value;
    memcpy(&value, ptr, sizeof(uint16_t));
    return value;
}

uint32_t seraph_prism_read_u32(Seraph_Prism prism, size_t index) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VOID_U32;
    if (prism.element_size < sizeof(uint32_t)) return SERAPH_VOID_U32;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    uint32_t value;
    memcpy(&value, ptr, sizeof(uint32_t));
    return value;
}

uint64_t seraph_prism_read_u64(Seraph_Prism prism, size_t index) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VOID_U64;
    if (prism.element_size < sizeof(uint64_t)) return SERAPH_VOID_U64;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    uint64_t value;
    memcpy(&value, ptr, sizeof(uint64_t));
    return value;
}

Seraph_Vbit seraph_prism_write_u8(Seraph_Prism prism, size_t index, uint8_t value) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VBIT_FALSE;
    if (prism.element_size < sizeof(uint8_t)) return SERAPH_VBIT_FALSE;
    if (!(prism.permissions & SERAPH_CAP_WRITE)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U8(value)) return SERAPH_VBIT_FALSE;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    *ptr = value;
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_prism_write_u16(Seraph_Prism prism, size_t index, uint16_t value) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VBIT_FALSE;
    if (prism.element_size < sizeof(uint16_t)) return SERAPH_VBIT_FALSE;
    if (!(prism.permissions & SERAPH_CAP_WRITE)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U16(value)) return SERAPH_VBIT_FALSE;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    memcpy(ptr, &value, sizeof(uint16_t));
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_prism_write_u32(Seraph_Prism prism, size_t index, uint32_t value) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VBIT_FALSE;
    if (prism.element_size < sizeof(uint32_t)) return SERAPH_VBIT_FALSE;
    if (!(prism.permissions & SERAPH_CAP_WRITE)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U32(value)) return SERAPH_VBIT_FALSE;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    memcpy(ptr, &value, sizeof(uint32_t));
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_prism_write_u64(Seraph_Prism prism, size_t index, uint64_t value) {
    if (!seraph_prism_in_bounds(prism, index)) return SERAPH_VBIT_FALSE;
    if (prism.element_size < sizeof(uint64_t)) return SERAPH_VBIT_FALSE;
    if (!(prism.permissions & SERAPH_CAP_WRITE)) return SERAPH_VBIT_FALSE;
    if (SERAPH_IS_VOID_U64(value)) return SERAPH_VBIT_FALSE;

    uint8_t* ptr = (uint8_t*)prism.base + (index * prism.stride);
    memcpy(ptr, &value, sizeof(uint64_t));
    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Bulk Operations
 *============================================================================*/

Seraph_Vbit seraph_prism_fill(Seraph_Prism prism, const void* value) {
    if (!seraph_prism_is_valid(prism)) return SERAPH_VBIT_VOID;
    if (!value) return SERAPH_VBIT_VOID;
    if (!(prism.permissions & SERAPH_CAP_WRITE)) return SERAPH_VBIT_FALSE;

    for (size_t i = 0; i < prism.count; i++) {
        uint8_t* dst = (uint8_t*)prism.base + (i * prism.stride);
        memcpy(dst, value, prism.element_size);
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_prism_copy(Seraph_Prism dst, Seraph_Prism src) {
    if (!seraph_prism_is_valid(dst)) return SERAPH_VBIT_VOID;
    if (!seraph_prism_is_valid(src)) return SERAPH_VBIT_VOID;
    if (dst.element_size != src.element_size) return SERAPH_VBIT_FALSE;
    if (dst.count != src.count) return SERAPH_VBIT_FALSE;
    if (!(dst.permissions & SERAPH_CAP_WRITE)) return SERAPH_VBIT_FALSE;

    /* Optimized path: if both are contiguous and same stride */
    if (dst.stride == dst.element_size && src.stride == src.element_size) {
        memcpy(dst.base, src.base, dst.count * dst.element_size);
    } else {
        /* General path: element by element */
        for (size_t i = 0; i < dst.count; i++) {
            uint8_t* d = (uint8_t*)dst.base + (i * dst.stride);
            const uint8_t* s = (const uint8_t*)src.base + (i * src.stride);
            memcpy(d, s, dst.element_size);
        }
    }

    return SERAPH_VBIT_TRUE;
}
