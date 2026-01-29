/**
 * @file atlas.c
 * @brief MC27: Atlas - The Single-Level Store Implementation
 *
 * "There is no disk. There is no file system. There is only memory that remembers."
 *
 * This implementation provides a userspace simulation of Atlas using
 * file-backed memory mapping. On Windows, we use CreateFileMapping/MapViewOfFile.
 * On POSIX systems, we use mmap with MAP_SHARED.
 */

#include "seraph/atlas.h"

/* Common includes needed by both kernel and userspace */
#include <string.h>
#include <stdio.h>

#ifdef SERAPH_KERNEL
    #include "seraph/kmalloc.h"
    #include "seraph/kruntime.h"
    #include "seraph/vmm.h"
    /* Kernel mode uses NVMe backend - forward declaration */
    extern bool seraph_atlas_nvme_init(Seraph_Atlas* atlas, uint64_t size);
    extern void seraph_atlas_nvme_sync(Seraph_Atlas* atlas);
    extern void seraph_atlas_nvme_close(Seraph_Atlas* atlas);
#else
    #include <stdlib.h>
#endif

#if defined(SERAPH_KERNEL)
    /* Kernel mode - uses NVMe backend */
#elif defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

/*============================================================================
 * Internal Constants
 *============================================================================*/

/** Genesis magic for generation table */
#define SERAPH_ATLAS_GEN_TABLE_MAGIC 0x47454E5441424C45ULL  /* "GENTABLE" */

/** Minimum allocation alignment */
#define SERAPH_ATLAS_ALIGN 8

/** Header size (Genesis + Gen Table + some padding) */
#define SERAPH_ATLAS_HEADER_SIZE (SERAPH_PAGE_SIZE * 4)  /* 16KB header */

/*============================================================================
 * Internal State
 *============================================================================*/

/* Transaction ID counter is now per-Atlas instance (atlas->next_tx_id) */

/*============================================================================
 * Platform-Specific Helpers
 *============================================================================*/

#if defined(SERAPH_KERNEL)
/* Kernel mode uses external NVMe backend - no local helpers needed */

#elif defined(_WIN32)

/**
 * @brief Open or create backing file and map it (Windows)
 */
static bool atlas_mmap_windows(Seraph_Atlas* atlas, bool create_new) {
    DWORD access = atlas->read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD share = FILE_SHARE_READ;
    DWORD creation = create_new ? CREATE_ALWAYS : OPEN_EXISTING;
    DWORD flags = FILE_ATTRIBUTE_NORMAL;

    /* Open or create file */
    atlas->file_handle = CreateFileA(
        atlas->path,
        access,
        share,
        NULL,
        creation,
        flags,
        NULL
    );

    if (atlas->file_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    /* Set file size if creating new */
    if (create_new) {
        LARGE_INTEGER size;
        size.QuadPart = (LONGLONG)atlas->size;
        if (!SetFilePointerEx(atlas->file_handle, size, NULL, FILE_BEGIN) ||
            !SetEndOfFile(atlas->file_handle)) {
            CloseHandle(atlas->file_handle);
            return false;
        }
    }

    /* Create file mapping */
    DWORD protect = atlas->read_only ? PAGE_READONLY : PAGE_READWRITE;
    atlas->mapping_handle = CreateFileMappingA(
        atlas->file_handle,
        NULL,
        protect,
        (DWORD)(atlas->size >> 32),
        (DWORD)(atlas->size & 0xFFFFFFFF),
        NULL
    );

    if (atlas->mapping_handle == NULL) {
        CloseHandle(atlas->file_handle);
        return false;
    }

    /* Map view of file */
    DWORD map_access = atlas->read_only ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;
    atlas->base = MapViewOfFile(
        atlas->mapping_handle,
        map_access,
        0,
        0,
        atlas->size
    );

    if (atlas->base == NULL) {
        CloseHandle(atlas->mapping_handle);
        CloseHandle(atlas->file_handle);
        return false;
    }

    return true;
}

/**
 * @brief Unmap and close (Windows)
 */
static void atlas_munmap_windows(Seraph_Atlas* atlas) {
    if (atlas->base != NULL) {
        FlushViewOfFile(atlas->base, atlas->size);
        UnmapViewOfFile(atlas->base);
        atlas->base = NULL;
    }
    if (atlas->mapping_handle != NULL) {
        CloseHandle(atlas->mapping_handle);
        atlas->mapping_handle = NULL;
    }
    if (atlas->file_handle != NULL && atlas->file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(atlas->file_handle);
        atlas->file_handle = NULL;
    }
}

/**
 * @brief Sync to disk (Windows)
 */
static bool atlas_sync_windows(Seraph_Atlas* atlas, void* ptr, size_t size) {
    if (ptr == NULL) {
        ptr = atlas->base;
        size = atlas->size;
    }
    return FlushViewOfFile(ptr, size) != 0;
}

#else /* POSIX */

/**
 * @brief Open or create backing file and map it (POSIX)
 */
static bool atlas_mmap_posix(Seraph_Atlas* atlas, bool create_new) {
    int flags = atlas->read_only ? O_RDONLY : O_RDWR;
    if (create_new) {
        flags |= O_CREAT | O_TRUNC;
    }

    atlas->fd = open(atlas->path, flags, 0644);
    if (atlas->fd < 0) {
        return false;
    }

    /* Set file size if creating new */
    if (create_new) {
        if (ftruncate(atlas->fd, (off_t)atlas->size) < 0) {
            close(atlas->fd);
            return false;
        }
    }

    /* Map file */
    int prot = atlas->read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
    atlas->base = mmap(
        NULL,
        atlas->size,
        prot,
        MAP_SHARED,
        atlas->fd,
        0
    );

    if (atlas->base == MAP_FAILED) {
        atlas->base = NULL;
        close(atlas->fd);
        return false;
    }

    return true;
}

/**
 * @brief Unmap and close (POSIX)
 */
static void atlas_munmap_posix(Seraph_Atlas* atlas) {
    if (atlas->base != NULL) {
        msync(atlas->base, atlas->size, MS_SYNC);
        munmap(atlas->base, atlas->size);
        atlas->base = NULL;
    }
    if (atlas->fd >= 0) {
        close(atlas->fd);
        atlas->fd = -1;
    }
}

/**
 * @brief Sync to disk (POSIX)
 */
static bool atlas_sync_posix(Seraph_Atlas* atlas, void* ptr, size_t size) {
    if (ptr == NULL) {
        ptr = atlas->base;
        size = atlas->size;
    }
    return msync(ptr, size, MS_SYNC) == 0;
}

#endif

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Check if file exists
 */
static bool file_exists(const char* path) {
#if defined(SERAPH_KERNEL)
    /* Kernel mode: Atlas uses NVMe backend, files don't exist in traditional sense */
    (void)path;
    return false;  /* Force creation of new Atlas */
#elif defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES &&
            !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
#endif
}

/**
 * @brief Get file size
 */
static size_t get_file_size(const char* path) {
#if defined(SERAPH_KERNEL)
    /* Kernel mode: size managed by NVMe backend */
    (void)path;
    return 0;
#elif defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return 0;
    }
    return ((size_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return (size_t)st.st_size;
#endif
}

/**
 * @brief Format a new Atlas
 */
static void atlas_format(Seraph_Atlas* atlas) {
    /* Zero the entire region */
    memset(atlas->base, 0, atlas->size);

    /* Initialize Genesis */
    Seraph_Atlas_Genesis* genesis = (Seraph_Atlas_Genesis*)atlas->base;
    genesis->magic = SERAPH_ATLAS_MAGIC;
    genesis->version = SERAPH_ATLAS_VERSION;
    genesis->generation = 1;
    genesis->root_offset = 0;
    genesis->free_list_offset = 0;
    genesis->gen_table_offset = sizeof(Seraph_Atlas_Genesis);
    genesis->next_alloc_offset = SERAPH_ATLAS_HEADER_SIZE;
    genesis->total_allocated = 0;
    genesis->total_freed = 0;
    genesis->created_at = 0;  /* Would be seraph_chronon_now() */
    genesis->modified_at = genesis->created_at;
    genesis->last_commit_at = 0;
    genesis->commit_count = 0;
    genesis->abort_count = 0;

    /* Initialize generation table */
    Seraph_Atlas_Gen_Table* gen_table =
        (Seraph_Atlas_Gen_Table*)((uint8_t*)atlas->base + genesis->gen_table_offset);
    gen_table->magic = SERAPH_ATLAS_GEN_TABLE_MAGIC;
    gen_table->entry_count = 0;
    gen_table->next_generation = 1;
    memset(gen_table->generations, 0, sizeof(gen_table->generations));

    atlas->current_epoch = 1;
}

/**
 * @brief Validate and recover existing Atlas
 */
static bool atlas_recover(Seraph_Atlas* atlas) {
    Seraph_Atlas_Genesis* genesis = (Seraph_Atlas_Genesis*)atlas->base;

    /* Validate magic */
    if (genesis->magic != SERAPH_ATLAS_MAGIC) {
        return false;
    }

    /* Validate version */
    if (genesis->version != SERAPH_ATLAS_VERSION) {
        return false;
    }

    /* Validate generation table */
    if (genesis->gen_table_offset >= atlas->size) {
        return false;
    }

    Seraph_Atlas_Gen_Table* gen_table =
        (Seraph_Atlas_Gen_Table*)((uint8_t*)atlas->base + genesis->gen_table_offset);

    if (gen_table->magic != SERAPH_ATLAS_GEN_TABLE_MAGIC) {
        return false;
    }

    /* Recovery is instant with copy-on-write:
     * Genesis always points to last committed state.
     * Uncommitted data is orphaned and will be reclaimed. */

    atlas->current_epoch = genesis->commit_count + 1;

    return true;
}

/**
 * @brief Align value up to alignment
 */
static inline size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/*============================================================================
 * Initialization and Cleanup
 *============================================================================*/

Seraph_Vbit seraph_atlas_init(
    Seraph_Atlas* atlas,
    const char* path,
    size_t size
) {
    if (atlas == NULL || path == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Initialize structure */
    memset(atlas, 0, sizeof(Seraph_Atlas));
    strncpy(atlas->path, path, SERAPH_ATLAS_MAX_PATH - 1);
    atlas->path[SERAPH_ATLAS_MAX_PATH - 1] = '\0';

#if defined(SERAPH_KERNEL)
    /* Kernel mode: handles managed by NVMe backend */
#elif defined(_WIN32)
    atlas->file_handle = NULL;
    atlas->mapping_handle = NULL;
#else
    atlas->fd = -1;
#endif

    /* Determine if we need to create or open */
    bool create_new = !file_exists(path);

    if (create_new) {
        /* Use provided size or default */
        atlas->size = (size > 0) ? size : SERAPH_ATLAS_DEFAULT_SIZE;

        /* Enforce minimum size */
        if (atlas->size < SERAPH_ATLAS_HEADER_SIZE * 2) {
            atlas->size = SERAPH_ATLAS_HEADER_SIZE * 2;
        }

        /* Align to page size */
        atlas->size = align_up(atlas->size, SERAPH_PAGE_SIZE);
    } else {
        /* Use existing file size */
        atlas->size = get_file_size(path);
        if (atlas->size == 0) {
            return SERAPH_VBIT_VOID;
        }
    }

    /* Perform platform-specific mapping */
#if defined(SERAPH_KERNEL)
    if (!seraph_atlas_nvme_init(atlas, atlas->size)) {
        return SERAPH_VBIT_VOID;
    }
#elif defined(_WIN32)
    if (!atlas_mmap_windows(atlas, create_new)) {
        return SERAPH_VBIT_VOID;
    }
#else
    if (!atlas_mmap_posix(atlas, create_new)) {
        return SERAPH_VBIT_VOID;
    }
#endif

    /* Format or recover */
    if (create_new) {
        atlas_format(atlas);
    } else {
        if (!atlas_recover(atlas)) {
            seraph_atlas_destroy(atlas);
            return SERAPH_VBIT_VOID;
        }
    }

    atlas->initialized = true;
    atlas->next_tx_id = 1;

    /* Initialize snapshot state */
    atlas->next_snapshot_id = 1;
    atlas->local_node_id = 0;
    atlas->node_count = 1;  /* Single node by default */
    memset(atlas->current_vclock, 0, sizeof(atlas->current_vclock));
    atlas->current_vclock[0] = 1;  /* Start with timestamp 1 */
    memset(atlas->snapshots, 0, sizeof(atlas->snapshots));

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_atlas_init_default(Seraph_Atlas* atlas) {
    return seraph_atlas_init(atlas, "seraph_atlas.dat", SERAPH_ATLAS_DEFAULT_SIZE);
}

void seraph_atlas_destroy(Seraph_Atlas* atlas) {
    if (atlas == NULL) {
        return;
    }

    if (atlas->initialized && atlas->base != NULL) {
        /* Abort any active snapshots */
        for (int i = 0; i < SERAPH_ATLAS_MAX_SNAPSHOTS; i++) {
            if (atlas->snapshots[i] != NULL &&
                atlas->snapshots[i]->state == SERAPH_ATLAS_SNAP_ACTIVE) {
                seraph_atlas_snapshot_abort(atlas, atlas->snapshots[i]);
            }
        }

        /* Sync before unmapping */
        seraph_atlas_sync(atlas);

#if defined(SERAPH_KERNEL)
        seraph_atlas_nvme_close(atlas);
#elif defined(_WIN32)
        atlas_munmap_windows(atlas);
#else
        atlas_munmap_posix(atlas);
#endif
    }

    /* Clear snapshot references */
    memset(atlas->snapshots, 0, sizeof(atlas->snapshots));

    atlas->initialized = false;
    atlas->base = NULL;
    atlas->size = 0;
}

/*============================================================================
 * Genesis Access
 *============================================================================*/

Seraph_Atlas_Genesis* seraph_atlas_genesis(Seraph_Atlas* atlas) {
    if (!seraph_atlas_is_valid(atlas)) {
        return NULL;
    }
    return (Seraph_Atlas_Genesis*)atlas->base;
}

void* seraph_atlas_get_root(Seraph_Atlas* atlas) {
    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);
    if (genesis == NULL || genesis->root_offset == 0) {
        return NULL;
    }
    return (uint8_t*)atlas->base + genesis->root_offset;
}

Seraph_Vbit seraph_atlas_set_root(Seraph_Atlas* atlas, void* root) {
    if (!seraph_atlas_is_valid(atlas)) {
        return SERAPH_VBIT_VOID;
    }

    if (root != NULL && !seraph_atlas_contains(atlas, root)) {
        return SERAPH_VBIT_VOID;  /* Root must be within Atlas */
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);
    genesis->root_offset = (root == NULL) ? 0 :
        seraph_atlas_ptr_to_offset(atlas, root);
    genesis->modified_at = 0;  /* Would be seraph_chronon_now() */

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Allocation
 *============================================================================*/

void* seraph_atlas_alloc(Seraph_Atlas* atlas, size_t size) {
    if (!seraph_atlas_is_valid(atlas) || size == 0) {
        return NULL;
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Align size */
    size = align_up(size, SERAPH_ATLAS_ALIGN);

    /* Check if there's enough space */
    if (genesis->next_alloc_offset + size > atlas->size) {
        return NULL;  /* Out of space */
    }

    /* Simple bump allocation */
    void* ptr = (uint8_t*)atlas->base + genesis->next_alloc_offset;
    genesis->next_alloc_offset += size;
    genesis->total_allocated += size;
    genesis->modified_at = 0;  /* Would be seraph_chronon_now() */

    return ptr;
}

void* seraph_atlas_calloc(Seraph_Atlas* atlas, size_t size) {
    void* ptr = seraph_atlas_alloc(atlas, size);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* seraph_atlas_alloc_pages(Seraph_Atlas* atlas, size_t size) {
    if (!seraph_atlas_is_valid(atlas) || size == 0) {
        return NULL;
    }

    /* Round up to page size */
    size = align_up(size, SERAPH_PAGE_SIZE);

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Align allocation offset to page boundary */
    uint64_t aligned_offset = align_up(genesis->next_alloc_offset, SERAPH_PAGE_SIZE);

    /* Check if there's enough space */
    if (aligned_offset + size > atlas->size) {
        return NULL;
    }

    void* ptr = (uint8_t*)atlas->base + aligned_offset;
    genesis->next_alloc_offset = aligned_offset + size;
    genesis->total_allocated += size;
    genesis->modified_at = 0;

    return ptr;
}

void seraph_atlas_free(Seraph_Atlas* atlas, void* ptr, size_t size) {
    if (!seraph_atlas_is_valid(atlas) || ptr == NULL || size == 0) {
        return;
    }

    if (!seraph_atlas_contains(atlas, ptr)) {
        return;  /* Not our memory */
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Add to free list */
    uint64_t offset = seraph_atlas_ptr_to_offset(atlas, ptr);

    Seraph_Atlas_Free_Entry* entry = (Seraph_Atlas_Free_Entry*)ptr;
    entry->next_offset = genesis->free_list_offset;
    entry->size = size;
    entry->freed_generation = genesis->generation;

    genesis->free_list_offset = offset;
    genesis->total_freed += size;
    genesis->modified_at = 0;
}

size_t seraph_atlas_available(const Seraph_Atlas* atlas) {
    if (!seraph_atlas_is_valid(atlas)) {
        return 0;
    }

    const Seraph_Atlas_Genesis* genesis = (const Seraph_Atlas_Genesis*)atlas->base;
    return atlas->size - genesis->next_alloc_offset;
}

/*============================================================================
 * Pointer Utilities
 *============================================================================*/

bool seraph_atlas_contains(const Seraph_Atlas* atlas, const void* ptr) {
    if (atlas == NULL || atlas->base == NULL || ptr == NULL) {
        return false;
    }

    const uint8_t* p = (const uint8_t*)ptr;
    const uint8_t* base = (const uint8_t*)atlas->base;

    return (p >= base && p < base + atlas->size);
}

uint64_t seraph_atlas_ptr_to_offset(const Seraph_Atlas* atlas, const void* ptr) {
    if (!seraph_atlas_contains(atlas, ptr)) {
        return SERAPH_VOID_U64;
    }

    return (uint64_t)((const uint8_t*)ptr - (const uint8_t*)atlas->base);
}

void* seraph_atlas_offset_to_ptr(const Seraph_Atlas* atlas, uint64_t offset) {
    if (!seraph_atlas_is_valid(atlas)) {
        return NULL;
    }

    if (offset >= atlas->size || offset == SERAPH_VOID_U64) {
        return NULL;
    }

    return (uint8_t*)atlas->base + offset;
}

/*============================================================================
 * Transactions
 *============================================================================*/

Seraph_Atlas_Transaction* seraph_atlas_begin(Seraph_Atlas* atlas) {
    if (!seraph_atlas_is_valid(atlas)) {
        return NULL;
    }

    /* Find a free transaction slot */
    Seraph_Atlas_Transaction* tx = NULL;
    for (int i = 0; i < SERAPH_ATLAS_MAX_TRANSACTIONS; i++) {
        if (atlas->transactions[i].state == SERAPH_ATLAS_TX_VOID ||
            atlas->transactions[i].state == SERAPH_ATLAS_TX_COMMITTED ||
            atlas->transactions[i].state == SERAPH_ATLAS_TX_ABORTED) {
            tx = &atlas->transactions[i];
            break;
        }
    }

    if (tx == NULL) {
        return NULL;  /* No free transaction slots */
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Initialize transaction */
    memset(tx, 0, sizeof(Seraph_Atlas_Transaction));
    tx->tx_id = atlas->next_tx_id++;
    tx->epoch = atlas->current_epoch;
    tx->start_generation = genesis->generation;
    tx->start_chronon = 0;  /* Would be seraph_chronon_now() */
    tx->state = SERAPH_ATLAS_TX_ACTIVE;
    tx->dirty_count = 0;

    return tx;
}

Seraph_Vbit seraph_atlas_commit(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Transaction* tx
) {
    if (!seraph_atlas_is_valid(atlas) || tx == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (tx->state != SERAPH_ATLAS_TX_ACTIVE) {
        return SERAPH_VBIT_VOID;  /* Can only commit active transactions */
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Check for conflicts (optimistic concurrency) */
    if (genesis->generation != tx->start_generation) {
        /* Another transaction modified data - conflict */
        tx->state = SERAPH_ATLAS_TX_ABORTED;
        genesis->abort_count++;
        return SERAPH_VBIT_FALSE;
    }

    /* Increment generation to make this commit visible */
    genesis->generation++;
    genesis->modified_at = 0;  /* Would be seraph_chronon_now() */
    genesis->last_commit_at = genesis->modified_at;
    genesis->commit_count++;

    /* Sync all data to disk */
    seraph_atlas_sync(atlas);

    /* Mark transaction as committed */
    tx->state = SERAPH_ATLAS_TX_COMMITTED;
    atlas->current_epoch++;

    return SERAPH_VBIT_TRUE;
}

void seraph_atlas_abort(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Transaction* tx
) {
    if (atlas == NULL || tx == NULL) {
        return;
    }

    if (tx->state != SERAPH_ATLAS_TX_ACTIVE) {
        return;  /* Already finished */
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);
    genesis->abort_count++;

    /* Mark as aborted - dirty pages become garbage */
    tx->state = SERAPH_ATLAS_TX_ABORTED;
}

Seraph_Vbit seraph_atlas_tx_mark_dirty(
    Seraph_Atlas_Transaction* tx,
    void* ptr,
    size_t size
) {
    if (tx == NULL || tx->state != SERAPH_ATLAS_TX_ACTIVE) {
        return SERAPH_VBIT_VOID;
    }

    if (tx->dirty_count >= SERAPH_ATLAS_MAX_DIRTY_PAGES) {
        return SERAPH_VBIT_FALSE;  /* Too many dirty pages */
    }

    /* Record the dirty region */
    tx->dirty_pages[tx->dirty_count].offset = (uint64_t)(uintptr_t)ptr;
    tx->dirty_pages[tx->dirty_count].size = size;
    tx->dirty_pages[tx->dirty_count].original = NULL;  /* Could store copy for rollback */
    tx->dirty_count++;

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Persistence Operations
 *============================================================================*/

Seraph_Vbit seraph_atlas_sync(Seraph_Atlas* atlas) {
    if (!seraph_atlas_is_valid(atlas)) {
        return SERAPH_VBIT_VOID;
    }

#if defined(SERAPH_KERNEL)
    seraph_atlas_nvme_sync(atlas);
#elif defined(_WIN32)
    if (!atlas_sync_windows(atlas, NULL, 0)) {
        return SERAPH_VBIT_VOID;
    }
#else
    if (!atlas_sync_posix(atlas, NULL, 0)) {
        return SERAPH_VBIT_VOID;
    }
#endif

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_atlas_sync_range(
    Seraph_Atlas* atlas,
    void* ptr,
    size_t size
) {
    if (!seraph_atlas_is_valid(atlas) || ptr == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (!seraph_atlas_contains(atlas, ptr)) {
        return SERAPH_VBIT_VOID;
    }

#if defined(SERAPH_KERNEL)
    /* Kernel mode: range sync through NVMe backend */
    (void)ptr;
    (void)size;
    seraph_atlas_nvme_sync(atlas);
#elif defined(_WIN32)
    if (!atlas_sync_windows(atlas, ptr, size)) {
        return SERAPH_VBIT_VOID;
    }
#else
    if (!atlas_sync_posix(atlas, ptr, size)) {
        return SERAPH_VBIT_VOID;
    }
#endif

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Generation Table (Capability Persistence)
 *============================================================================*/

Seraph_Atlas_Gen_Table* seraph_atlas_get_gen_table(Seraph_Atlas* atlas) {
    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);
    if (genesis == NULL || genesis->gen_table_offset == 0) {
        return NULL;
    }

    return (Seraph_Atlas_Gen_Table*)((uint8_t*)atlas->base + genesis->gen_table_offset);
}

uint64_t seraph_atlas_alloc_generation(Seraph_Atlas* atlas) {
    Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(atlas);
    if (table == NULL) {
        return SERAPH_VOID_U64;
    }

    if (table->entry_count >= SERAPH_ATLAS_GEN_TABLE_SIZE) {
        return SERAPH_VOID_U64;  /* Table full */
    }

    uint64_t alloc_id = table->entry_count;
    table->generations[alloc_id] = table->next_generation++;
    table->entry_count++;

    return alloc_id;
}

uint64_t seraph_atlas_revoke(Seraph_Atlas* atlas, uint64_t alloc_id) {
    Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(atlas);
    if (table == NULL) {
        return SERAPH_VOID_U64;
    }

    if (alloc_id >= table->entry_count) {
        return SERAPH_VOID_U64;  /* Invalid allocation ID */
    }

    /* Increment generation - all capabilities with old generation become invalid */
    table->generations[alloc_id]++;

    return table->generations[alloc_id];
}

Seraph_Vbit seraph_atlas_check_generation(
    Seraph_Atlas* atlas,
    uint64_t alloc_id,
    uint64_t generation
) {
    Seraph_Atlas_Gen_Table* table = seraph_atlas_get_gen_table(atlas);
    if (table == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (alloc_id >= table->entry_count) {
        return SERAPH_VBIT_VOID;  /* Invalid allocation ID */
    }

    /* Capability is valid only if generations match */
    return (generation == table->generations[alloc_id])
        ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Statistics
 *============================================================================*/

Seraph_Atlas_Stats seraph_atlas_get_stats(const Seraph_Atlas* atlas) {
    Seraph_Atlas_Stats stats = {0};

    if (!seraph_atlas_is_valid(atlas)) {
        return stats;
    }

    const Seraph_Atlas_Genesis* genesis = (const Seraph_Atlas_Genesis*)atlas->base;

    stats.total_size = atlas->size;
    stats.used_size = genesis->next_alloc_offset;
    stats.free_size = atlas->size - genesis->next_alloc_offset;
    stats.alloc_count = genesis->total_allocated;
    stats.free_count = genesis->total_freed;
    stats.commit_count = genesis->commit_count;
    stats.abort_count = genesis->abort_count;
    stats.initialized = atlas->initialized;

    return stats;
}

/*============================================================================
 * Causal Snapshot Implementation
 *
 * DESIGN PHILOSOPHY:
 *
 * Causal snapshots provide point-in-time captures of Atlas state that
 * respect the happens-before relationship defined by vector clocks.
 * This ensures that if event A causally preceded event B, and B's effects
 * are in the snapshot, then A's effects are also in the snapshot.
 *
 * COPY-ON-WRITE MECHANISM:
 *
 * When a snapshot is ACTIVE, writes to included pages trigger COW:
 *   1. Check if page is in snapshot's include set
 *   2. If not already copied, allocate COW storage
 *   3. Copy original page data to COW storage
 *   4. Allow write to proceed on original page
 *   5. Snapshot readers see COW copy; live readers see modified page
 *
 * CAUSALITY TRACKING:
 *
 * Each snapshot captures the vector clock at creation time. This enables:
 *   - Comparison of snapshots for causal ordering
 *   - Proper restore semantics (fork a new causal timeline)
 *   - Replication with causality preservation
 *
 * PERSISTENCE:
 *
 * Committed snapshots are stored within Atlas itself, making them
 * persistent across restarts. The snapshot metadata and COW pages
 * are allocated from Atlas and linked from Genesis.
 *============================================================================*/

/*--- Internal Helpers ---*/

/**
 * @brief Find COW entry for a page
 */
static Seraph_Atlas_COW_Page* snapshot_find_cow_page(
    Seraph_Atlas_Snapshot* snapshot,
    uint64_t page_offset
) {
    for (uint32_t i = 0; i < snapshot->cow_page_count; i++) {
        if ((snapshot->cow_pages[i].flags & SERAPH_ATLAS_COW_VALID) &&
            snapshot->cow_pages[i].page_offset == page_offset) {
            return &snapshot->cow_pages[i];
        }
    }
    return NULL;
}

/**
 * @brief Capture current vector clock into snapshot
 */
static void snapshot_capture_vclock(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot,
    const Seraph_VectorClock* vclock
) {
    if (vclock != NULL && vclock->timestamps != NULL) {
        /* Copy from provided vector clock */
        snapshot->vclock_node_count = vclock->node_count;
        snapshot->vclock_self_id = vclock->self_id;
        for (uint32_t i = 0; i < vclock->node_count && i < SERAPH_ATLAS_VCLOCK_MAX_NODES; i++) {
            snapshot->vclock[i] = vclock->timestamps[i];
        }
    } else {
        /* Use Atlas's internal vector clock */
        snapshot->vclock_node_count = atlas->node_count > 0 ? atlas->node_count : 1;
        snapshot->vclock_self_id = atlas->local_node_id;
        for (uint32_t i = 0; i < snapshot->vclock_node_count && i < SERAPH_ATLAS_VCLOCK_MAX_NODES; i++) {
            snapshot->vclock[i] = atlas->current_vclock[i];
        }
    }
}

/**
 * @brief Compare two vector clocks stored in snapshots
 */
static Seraph_CausalOrder snapshot_compare_vclocks(
    const Seraph_Chronon* a, uint32_t a_count,
    const Seraph_Chronon* b, uint32_t b_count
) {
    if (a == NULL || b == NULL || a_count == 0 || b_count == 0) {
        return SERAPH_CAUSAL_VOID;
    }

    /* Vector clocks must have same dimension for comparison */
    if (a_count != b_count) {
        return SERAPH_CAUSAL_VOID;
    }

    bool a_le_b = true;  /* a[i] <= b[i] for all i */
    bool b_le_a = true;  /* b[i] <= a[i] for all i */
    bool a_lt_b = false; /* a[i] < b[i] for some i */
    bool b_lt_a = false; /* b[i] < a[i] for some i */

    for (uint32_t i = 0; i < a_count; i++) {
        if (a[i] > b[i]) {
            a_le_b = false;
            b_lt_a = true;
        }
        if (a[i] < b[i]) {
            b_le_a = false;
            a_lt_b = true;
        }
    }

    if (a_le_b && b_le_a) {
        return SERAPH_CAUSAL_EQUAL;     /* a == b */
    }
    if (a_le_b && a_lt_b) {
        return SERAPH_CAUSAL_BEFORE;    /* a -> b */
    }
    if (b_le_a && b_lt_a) {
        return SERAPH_CAUSAL_AFTER;     /* b -> a */
    }
    return SERAPH_CAUSAL_CONCURRENT;    /* a || b */
}

/*============================================================================
 * Causal Snapshot API Implementation
 *============================================================================*/

Seraph_Atlas_Snapshot* seraph_atlas_snapshot_begin(
    Seraph_Atlas* atlas,
    const Seraph_VectorClock* vclock
) {
    if (!seraph_atlas_is_valid(atlas)) {
        return NULL;
    }

    /* Find or allocate a snapshot slot */
    Seraph_Atlas_Snapshot* snapshot = NULL;

    for (int i = 0; i < SERAPH_ATLAS_MAX_SNAPSHOTS; i++) {
        if (atlas->snapshots[i] == NULL) {
            /* Allocate new snapshot in Atlas (persistent) */
            snapshot = (Seraph_Atlas_Snapshot*)seraph_atlas_calloc(atlas,
                sizeof(Seraph_Atlas_Snapshot));
            if (snapshot == NULL) {
                return NULL;  /* Allocation failed */
            }
            atlas->snapshots[i] = snapshot;
            break;
        }
        if (atlas->snapshots[i]->state == SERAPH_ATLAS_SNAP_VOID ||
            atlas->snapshots[i]->state == SERAPH_ATLAS_SNAP_FAILED) {
            snapshot = atlas->snapshots[i];
            memset(snapshot, 0, sizeof(Seraph_Atlas_Snapshot));
            break;
        }
    }

    if (snapshot == NULL) {
        return NULL;  /* No free slots */
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Initialize snapshot header */
    snapshot->magic = SERAPH_ATLAS_SNAPSHOT_MAGIC;
    snapshot->version = SERAPH_ATLAS_SNAPSHOT_VERSION;
    snapshot->state = SERAPH_ATLAS_SNAP_PREPARING;
    snapshot->snapshot_id = atlas->next_snapshot_id++;

    /* Capture temporal context */
    snapshot->timestamp = atlas->current_epoch;  /* Logical timestamp */
    snapshot->wall_clock = 0;  /* Would be real time */
    snapshot->generation = genesis->generation;
    snapshot->epoch = atlas->current_epoch;

    /* Capture vector clock for causality tracking */
    snapshot_capture_vclock(atlas, snapshot, vclock);

    /* Initialize page tracking */
    snapshot->total_page_count = (uint32_t)(atlas->size / SERAPH_PAGE_SIZE);
    snapshot->included_page_count = 0;
    snapshot->included_pages = 0;

    /* Initialize COW state */
    snapshot->cow_page_count = 0;
    snapshot->cow_storage_offset = 0;
    snapshot->cow_storage_size = 0;

    /* Copy Genesis for restore */
    memcpy(&snapshot->genesis_copy, genesis, sizeof(Seraph_Atlas_Genesis));

    /* Set creation time */
    snapshot->creation_time = atlas->current_epoch;
    snapshot->commit_time = 0;

    /* Increment local vector clock component (this is a causal event) */
    if (atlas->local_node_id < SERAPH_ATLAS_VCLOCK_MAX_NODES) {
        atlas->current_vclock[atlas->local_node_id]++;
    }

    return snapshot;
}

Seraph_Vbit seraph_atlas_snapshot_include(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot,
    const void* ptr,
    size_t size
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->state != SERAPH_ATLAS_SNAP_PREPARING) {
        return SERAPH_VBIT_VOID;  /* Can only add pages in PREPARING state */
    }

    if (!seraph_atlas_contains(atlas, ptr)) {
        return SERAPH_VBIT_VOID;
    }

    /* Calculate page range */
    uint64_t start_offset = seraph_atlas_ptr_to_offset(atlas, ptr);
    uint64_t end_offset = start_offset + size;

    /* Align to page boundaries */
    uint64_t page_start = (start_offset / SERAPH_PAGE_SIZE) * SERAPH_PAGE_SIZE;
    uint64_t page_end = ((end_offset + SERAPH_PAGE_SIZE - 1) / SERAPH_PAGE_SIZE) * SERAPH_PAGE_SIZE;

    if (page_end > atlas->size) {
        page_end = atlas->size;
    }

    /* Count pages to include */
    uint32_t page_count = (uint32_t)((page_end - page_start) / SERAPH_PAGE_SIZE);

    /* Check if we can track all pages */
    if (snapshot->included_page_count + page_count > SERAPH_ATLAS_SNAPSHOT_MAX_PAGES) {
        return SERAPH_VBIT_FALSE;  /* Too many pages */
    }

    /* Mark pages as included (simplified - just track count) */
    snapshot->included_page_count += page_count;

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_atlas_snapshot_include_all(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->state != SERAPH_ATLAS_SNAP_PREPARING) {
        return SERAPH_VBIT_VOID;
    }

    /* Include entire Atlas region */
    snapshot->included_page_count = snapshot->total_page_count;
    snapshot->included_pages = SERAPH_VOID_U64;  /* All pages flag */

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_atlas_snapshot_activate(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->state != SERAPH_ATLAS_SNAP_PREPARING) {
        if (snapshot->state == SERAPH_ATLAS_SNAP_ACTIVE) {
            return SERAPH_VBIT_FALSE;  /* Already active */
        }
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->included_page_count == 0) {
        return SERAPH_VBIT_VOID;  /* Must include at least one page */
    }

    /* Allocate COW storage region (reserve space for potential copies) */
    size_t cow_storage_size = (size_t)snapshot->included_page_count * SERAPH_PAGE_SIZE;
    void* cow_storage = seraph_atlas_alloc_pages(atlas, cow_storage_size);

    if (cow_storage == NULL) {
        /* Not enough space - still activate but COW may fail later */
        snapshot->cow_storage_offset = 0;
        snapshot->cow_storage_size = 0;
    } else {
        snapshot->cow_storage_offset = seraph_atlas_ptr_to_offset(atlas, cow_storage);
        snapshot->cow_storage_size = cow_storage_size;
    }

    /* Transition to active state */
    snapshot->state = SERAPH_ATLAS_SNAP_ACTIVE;

    /* Increment vector clock (activation is a causal event) */
    if (atlas->local_node_id < SERAPH_ATLAS_VCLOCK_MAX_NODES) {
        atlas->current_vclock[atlas->local_node_id]++;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_atlas_snapshot_commit(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->state == SERAPH_ATLAS_SNAP_COMMITTED) {
        return SERAPH_VBIT_FALSE;  /* Already committed */
    }

    if (snapshot->state != SERAPH_ATLAS_SNAP_ACTIVE &&
        snapshot->state != SERAPH_ATLAS_SNAP_PREPARING) {
        return SERAPH_VBIT_VOID;
    }

    /* Record commit time */
    snapshot->commit_time = atlas->current_epoch;

    /* Sync all COW pages to disk */
    if (snapshot->cow_storage_offset != 0 && snapshot->cow_page_count > 0) {
        void* cow_storage = seraph_atlas_offset_to_ptr(atlas, snapshot->cow_storage_offset);
        if (cow_storage != NULL) {
            seraph_atlas_sync_range(atlas, cow_storage,
                (size_t)snapshot->cow_page_count * SERAPH_PAGE_SIZE);
        }
    }

    /* Sync snapshot metadata */
    seraph_atlas_sync_range(atlas, snapshot, sizeof(Seraph_Atlas_Snapshot));

    /* Transition to committed state */
    snapshot->state = SERAPH_ATLAS_SNAP_COMMITTED;

    /* Increment vector clock (commit is a causal event) */
    if (atlas->local_node_id < SERAPH_ATLAS_VCLOCK_MAX_NODES) {
        atlas->current_vclock[atlas->local_node_id]++;
    }

    /* Sync Genesis to ensure snapshot is recorded */
    seraph_atlas_sync(atlas);

    return SERAPH_VBIT_TRUE;
}

void seraph_atlas_snapshot_abort(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
) {
    if (atlas == NULL || snapshot == NULL) {
        return;
    }

    if (snapshot->state == SERAPH_ATLAS_SNAP_COMMITTED) {
        return;  /* Cannot abort committed snapshot - use delete instead */
    }

    /* Free COW storage if allocated */
    if (snapshot->cow_storage_offset != 0) {
        void* cow_storage = seraph_atlas_offset_to_ptr(atlas, snapshot->cow_storage_offset);
        if (cow_storage != NULL) {
            seraph_atlas_free(atlas, cow_storage, snapshot->cow_storage_size);
        }
    }

    /* Mark slot as void for reuse */
    snapshot->state = SERAPH_ATLAS_SNAP_VOID;
    snapshot->magic = 0;

    /* Find and clear slot reference */
    for (int i = 0; i < SERAPH_ATLAS_MAX_SNAPSHOTS; i++) {
        if (atlas->snapshots[i] == snapshot) {
            /* Keep the allocation but mark as unused */
            break;
        }
    }
}

Seraph_Vbit seraph_atlas_snapshot_restore(
    Seraph_Atlas* atlas,
    const Seraph_Atlas_Snapshot* snapshot
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->state != SERAPH_ATLAS_SNAP_COMMITTED) {
        return SERAPH_VBIT_FALSE;  /* Can only restore committed snapshots */
    }

    if (snapshot->magic != SERAPH_ATLAS_SNAPSHOT_MAGIC) {
        return SERAPH_VBIT_VOID;  /* Invalid snapshot */
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Abort all active transactions */
    for (int i = 0; i < SERAPH_ATLAS_MAX_TRANSACTIONS; i++) {
        if (atlas->transactions[i].state == SERAPH_ATLAS_TX_ACTIVE) {
            seraph_atlas_abort(atlas, &atlas->transactions[i]);
        }
    }

    /* Restore COW pages to their original locations */
    for (uint32_t i = 0; i < snapshot->cow_page_count; i++) {
        const Seraph_Atlas_COW_Page* cow = &snapshot->cow_pages[i];

        if (!(cow->flags & SERAPH_ATLAS_COW_VALID)) {
            continue;
        }

        /* Get original page location */
        void* original_page = seraph_atlas_offset_to_ptr(atlas, cow->page_offset);
        if (original_page == NULL) {
            continue;
        }

        /* Get COW copy location */
        void* cow_copy = seraph_atlas_offset_to_ptr(atlas, cow->copy_offset);
        if (cow_copy == NULL) {
            continue;
        }

        /* Restore original page data */
        size_t page_size = (size_t)cow->page_count * SERAPH_PAGE_SIZE;
        memcpy(original_page, cow_copy, page_size);
    }

    /* Restore Genesis (except for things that should not be rolled back) */
    uint64_t current_generation = genesis->generation + 1;  /* Increment for safety */
    uint64_t current_commit_count = genesis->commit_count;
    uint64_t current_abort_count = genesis->abort_count;

    memcpy(genesis, &snapshot->genesis_copy, sizeof(Seraph_Atlas_Genesis));

    /* Preserve/increment certain fields */
    genesis->generation = current_generation;  /* New generation invalidates old capabilities */
    genesis->commit_count = current_commit_count + 1;  /* Restore counts as a commit */
    genesis->abort_count = current_abort_count;

    /* Update vector clock to reflect restore operation */
    /* The restore creates a new causal branch that happens-after both
       the snapshot and the current state */
    for (uint32_t i = 0; i < SERAPH_ATLAS_VCLOCK_MAX_NODES; i++) {
        /* Take max of snapshot vclock and current vclock */
        if (i < snapshot->vclock_node_count) {
            Seraph_Chronon snap_val = snapshot->vclock[i];
            Seraph_Chronon curr_val = atlas->current_vclock[i];
            atlas->current_vclock[i] = (snap_val > curr_val) ? snap_val : curr_val;
        }
    }

    /* Increment local component (restore is a causal event) */
    if (atlas->local_node_id < SERAPH_ATLAS_VCLOCK_MAX_NODES) {
        atlas->current_vclock[atlas->local_node_id]++;
    }

    /* Update epoch */
    atlas->current_epoch = genesis->commit_count + 1;

    /* Sync everything to disk */
    seraph_atlas_sync(atlas);

    return SERAPH_VBIT_TRUE;
}

Seraph_CausalOrder seraph_atlas_snapshot_compare(
    const Seraph_Atlas_Snapshot* a,
    const Seraph_Atlas_Snapshot* b
) {
    if (a == NULL || b == NULL) {
        return SERAPH_CAUSAL_VOID;
    }

    if (a->magic != SERAPH_ATLAS_SNAPSHOT_MAGIC ||
        b->magic != SERAPH_ATLAS_SNAPSHOT_MAGIC) {
        return SERAPH_CAUSAL_VOID;
    }

    /* Compare vector clocks */
    return snapshot_compare_vclocks(
        a->vclock, a->vclock_node_count,
        b->vclock, b->vclock_node_count
    );
}

Seraph_Atlas_Snapshot* seraph_atlas_snapshot_get(
    Seraph_Atlas* atlas,
    uint64_t snapshot_id
) {
    if (!seraph_atlas_is_valid(atlas)) {
        return NULL;
    }

    for (int i = 0; i < SERAPH_ATLAS_MAX_SNAPSHOTS; i++) {
        if (atlas->snapshots[i] != NULL &&
            atlas->snapshots[i]->snapshot_id == snapshot_id &&
            atlas->snapshots[i]->state != SERAPH_ATLAS_SNAP_VOID) {
            return atlas->snapshots[i];
        }
    }

    return NULL;
}

uint32_t seraph_atlas_snapshot_list(
    Seraph_Atlas* atlas,
    uint64_t* ids,
    uint32_t max_ids
) {
    if (!seraph_atlas_is_valid(atlas) || ids == NULL || max_ids == 0) {
        return 0;
    }

    uint32_t count = 0;

    for (int i = 0; i < SERAPH_ATLAS_MAX_SNAPSHOTS && count < max_ids; i++) {
        if (atlas->snapshots[i] != NULL &&
            atlas->snapshots[i]->state == SERAPH_ATLAS_SNAP_COMMITTED) {
            ids[count++] = atlas->snapshots[i]->snapshot_id;
        }
    }

    /* Sort by creation time (bubble sort - small array) */
    for (uint32_t i = 0; i < count - 1; i++) {
        for (uint32_t j = 0; j < count - i - 1; j++) {
            Seraph_Atlas_Snapshot* snap_j = seraph_atlas_snapshot_get(atlas, ids[j]);
            Seraph_Atlas_Snapshot* snap_j1 = seraph_atlas_snapshot_get(atlas, ids[j + 1]);

            if (snap_j != NULL && snap_j1 != NULL &&
                snap_j->creation_time > snap_j1->creation_time) {
                uint64_t temp = ids[j];
                ids[j] = ids[j + 1];
                ids[j + 1] = temp;
            }
        }
    }

    return count;
}

Seraph_Vbit seraph_atlas_snapshot_delete(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->state != SERAPH_ATLAS_SNAP_COMMITTED) {
        return SERAPH_VBIT_FALSE;  /* Use abort for non-committed snapshots */
    }

    /* Free COW storage */
    if (snapshot->cow_storage_offset != 0) {
        void* cow_storage = seraph_atlas_offset_to_ptr(atlas, snapshot->cow_storage_offset);
        if (cow_storage != NULL) {
            seraph_atlas_free(atlas, cow_storage, snapshot->cow_storage_size);
        }
    }

    /* Mark as void for reuse */
    snapshot->state = SERAPH_ATLAS_SNAP_VOID;
    snapshot->magic = 0;

    /* Sync to persist the deletion */
    seraph_atlas_sync_range(atlas, snapshot, sizeof(Seraph_Atlas_Snapshot));

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_atlas_snapshot_is_valid(
    const Seraph_Atlas_Snapshot* snapshot
) {
    if (snapshot == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->magic != SERAPH_ATLAS_SNAPSHOT_MAGIC) {
        return SERAPH_VBIT_FALSE;
    }

    if (snapshot->version != SERAPH_ATLAS_SNAPSHOT_VERSION) {
        return SERAPH_VBIT_FALSE;
    }

    if (snapshot->state == SERAPH_ATLAS_SNAP_VOID ||
        snapshot->state == SERAPH_ATLAS_SNAP_FAILED) {
        return SERAPH_VBIT_FALSE;
    }

    /* Verify Genesis copy integrity */
    if (snapshot->genesis_copy.magic != SERAPH_ATLAS_MAGIC) {
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_atlas_snapshot_cow_page(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot,
    void* page_ptr
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL || page_ptr == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (snapshot->state != SERAPH_ATLAS_SNAP_ACTIVE) {
        return SERAPH_VBIT_VOID;  /* COW only for active snapshots */
    }

    /* Align to page boundary */
    uint64_t offset = seraph_atlas_ptr_to_offset(atlas, page_ptr);
    if (offset == SERAPH_VOID_U64) {
        return SERAPH_VBIT_VOID;
    }

    uint64_t page_offset = (offset / SERAPH_PAGE_SIZE) * SERAPH_PAGE_SIZE;

    /* Check if already copied */
    if (snapshot_find_cow_page(snapshot, page_offset) != NULL) {
        return SERAPH_VBIT_FALSE;  /* Already copied */
    }

    /* Check if we have space for another COW entry */
    if (snapshot->cow_page_count >= SERAPH_ATLAS_SNAPSHOT_MAX_PAGES) {
        snapshot->state = SERAPH_ATLAS_SNAP_FAILED;
        return SERAPH_VBIT_VOID;  /* Too many COW pages */
    }

    /* Check if we have COW storage space */
    if (snapshot->cow_storage_offset == 0) {
        snapshot->state = SERAPH_ATLAS_SNAP_FAILED;
        return SERAPH_VBIT_VOID;  /* No COW storage allocated */
    }

    /* Calculate COW copy destination */
    uint64_t cow_offset = snapshot->cow_storage_offset +
        ((uint64_t)snapshot->cow_page_count * SERAPH_PAGE_SIZE);

    void* cow_dest = seraph_atlas_offset_to_ptr(atlas, cow_offset);
    void* page_src = seraph_atlas_offset_to_ptr(atlas, page_offset);

    if (cow_dest == NULL || page_src == NULL) {
        snapshot->state = SERAPH_ATLAS_SNAP_FAILED;
        return SERAPH_VBIT_VOID;
    }

    /* Copy original page data to COW storage */
    memcpy(cow_dest, page_src, SERAPH_PAGE_SIZE);

    /* Record COW entry */
    Seraph_Atlas_COW_Page* cow = &snapshot->cow_pages[snapshot->cow_page_count];
    cow->page_offset = page_offset;
    cow->copy_offset = cow_offset;
    cow->modification_time = atlas->current_epoch;
    cow->page_count = 1;
    cow->flags = SERAPH_ATLAS_COW_VALID | SERAPH_ATLAS_COW_DIRTY;

    /* Mark Genesis pages specially */
    if (page_offset < sizeof(Seraph_Atlas_Genesis)) {
        cow->flags |= SERAPH_ATLAS_COW_GENESIS;
    }

    snapshot->cow_page_count++;

    return SERAPH_VBIT_TRUE;
}

const void* seraph_atlas_snapshot_read_page(
    const Seraph_Atlas* atlas,
    const Seraph_Atlas_Snapshot* snapshot,
    const void* page_ptr
) {
    if (!seraph_atlas_is_valid(atlas) || snapshot == NULL || page_ptr == NULL) {
        return NULL;
    }

    /* Get page offset */
    uint64_t offset = seraph_atlas_ptr_to_offset(atlas, page_ptr);
    if (offset == SERAPH_VOID_U64) {
        return NULL;
    }

    uint64_t page_offset = (offset / SERAPH_PAGE_SIZE) * SERAPH_PAGE_SIZE;

    /* Check if page was modified (has COW copy) */
    for (uint32_t i = 0; i < snapshot->cow_page_count; i++) {
        const Seraph_Atlas_COW_Page* cow = &snapshot->cow_pages[i];

        if ((cow->flags & SERAPH_ATLAS_COW_VALID) &&
            cow->page_offset == page_offset) {
            /* Return COW copy (original data at snapshot time) */
            return seraph_atlas_offset_to_ptr(atlas, cow->copy_offset);
        }
    }

    /* Page not modified - return current data */
    return seraph_atlas_offset_to_ptr(atlas, page_offset);
}

/*============================================================================
 * Semantic Checkpointing Implementation
 *
 * DESIGN PHILOSOPHY:
 *
 * Semantic checkpoints go beyond raw byte snapshots to understand the
 * meaning of data structures. This enables:
 *
 *   1. VALIDATION: Detect corruption that byte-level checks miss
 *   2. RECOVERY: Automatically repair certain types of corruption
 *   3. INVARIANTS: Ensure data structure properties hold after restore
 *
 * INVARIANT TYPES:
 *
 *   - NULL_PTR:      Required pointer must not be NULL
 *   - NULLABLE_PTR:  Optional pointer, valid if non-NULL
 *   - NO_CYCLE:      Floyd's algorithm detects cycles in O(n)
 *   - ARRAY_BOUNDS:  Array indices stay within bounds
 *   - REFCOUNT:      Reference counts are valid
 *   - RANGE:         Numeric values within specified range
 *   - CUSTOM:        User-defined validation logic
 *
 * TYPE REGISTRY:
 *
 * Types are registered globally with their invariants. This allows
 * multiple checkpoints to share type definitions. The registry is
 * a static array (NIH - no external dependencies).
 *
 * VALIDATION ENGINE:
 *
 * For each entry in a checkpoint, the validation engine:
 *   1. Looks up the entry's type
 *   2. Iterates through the type's invariants
 *   3. Calls the appropriate validator for each invariant
 *   4. Records results in the validation report
 *
 * RECOVERY ENGINE:
 *
 * For each failed invariant that is marked auto_recoverable:
 *   1. Calls the appropriate recovery function
 *   2. Re-validates to confirm repair
 *   3. Records recovery results in the report
 *============================================================================*/

/*============================================================================
 * Type Registry (Global State)
 *============================================================================*/

/** Global type registry - NIH static array */
static Seraph_Atlas_Type_Info atlas_type_registry[SERAPH_ATLAS_MAX_TYPES];

/** Number of registered types */
static uint32_t atlas_type_count = 0;

/** Next checkpoint ID (global counter) */
static uint64_t atlas_next_checkpoint_id = 1;

/*============================================================================
 * CRC32 Implementation (NIH - No External Dependencies)
 *============================================================================*/

/**
 * @brief CRC32 lookup table for polynomial 0xEDB88320
 */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

/**
 * @brief Calculate CRC32 checksum
 */
static uint32_t calculate_crc32(const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

/*============================================================================
 * Invariant Validation Helpers
 *============================================================================*/

/**
 * @brief Read a pointer field from a structure
 */
static void* read_ptr_field(const void* data, size_t offset) {
    const uint8_t* bytes = (const uint8_t*)data;
    void* ptr = NULL;
    memcpy(&ptr, bytes + offset, sizeof(void*));
    return ptr;
}

/**
 * @brief Write a pointer field to a structure
 */
static void write_ptr_field(void* data, size_t offset, void* value) {
    uint8_t* bytes = (uint8_t*)data;
    memcpy(bytes + offset, &value, sizeof(void*));
}

/**
 * @brief Read a signed 64-bit field (handles various sizes)
 */
static int64_t read_int_field(const void* data, size_t offset, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    int64_t value = 0;

    switch (size) {
        case 1:
            value = (int64_t)(*((const int8_t*)(bytes + offset)));
            break;
        case 2:
            memcpy(&value, bytes + offset, 2);
            value = (int64_t)(int16_t)value;
            break;
        case 4:
            memcpy(&value, bytes + offset, 4);
            value = (int64_t)(int32_t)value;
            break;
        case 8:
            memcpy(&value, bytes + offset, 8);
            break;
        default:
            value = 0;
    }

    return value;
}

/**
 * @brief Write a signed 64-bit field (handles various sizes)
 */
static void write_int_field(void* data, size_t offset, size_t size, int64_t value) {
    uint8_t* bytes = (uint8_t*)data;

    switch (size) {
        case 1: {
            int8_t v = (int8_t)value;
            memcpy(bytes + offset, &v, 1);
            break;
        }
        case 2: {
            int16_t v = (int16_t)value;
            memcpy(bytes + offset, &v, 2);
            break;
        }
        case 4: {
            int32_t v = (int32_t)value;
            memcpy(bytes + offset, &v, 4);
            break;
        }
        case 8:
            memcpy(bytes + offset, &value, 8);
            break;
    }
}

/**
 * @brief Read size_t field from structure
 */
static size_t read_size_field(const void* data, size_t offset) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t value = 0;
    memcpy(&value, bytes + offset, sizeof(size_t));
    return value;
}

/*============================================================================
 * Invariant Validators
 *============================================================================*/

/**
 * @brief Validate NULL_PTR invariant (field must not be NULL)
 */
static Seraph_Atlas_Validate_Result validate_null_ptr(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    (void)atlas;  /* May be unused */

    void* ptr = read_ptr_field(data, inv->field_offset);

    if (ptr == NULL) {
        return SERAPH_ATLAS_VALIDATE_NULL_VIOLATION;
    }

    return SERAPH_ATLAS_VALIDATE_OK;
}

/**
 * @brief Validate NULLABLE_PTR invariant (if non-NULL, must be valid)
 */
static Seraph_Atlas_Validate_Result validate_nullable_ptr(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    void* ptr = read_ptr_field(data, inv->field_offset);

    /* NULL is acceptable for nullable pointers */
    if (ptr == NULL) {
        return SERAPH_ATLAS_VALIDATE_OK;
    }

    /* If non-NULL, must be within Atlas */
    if (!seraph_atlas_contains(atlas, ptr)) {
        return SERAPH_ATLAS_VALIDATE_INVALID_PTR;
    }

    return SERAPH_ATLAS_VALIDATE_OK;
}

/**
 * @brief Validate NO_CYCLE invariant using Floyd's algorithm
 *
 * Floyd's Tortoise and Hare algorithm:
 *   - Two pointers: slow (moves 1 step) and fast (moves 2 steps)
 *   - If they meet, there's a cycle
 *   - O(n) time, O(1) space
 */
static Seraph_Atlas_Validate_Result validate_no_cycle(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    size_t next_offset = inv->params.cycle.next_offset;

    /* Get initial pointer */
    void* slow = read_ptr_field(data, inv->field_offset);
    void* fast = slow;

    /* If starting point is NULL, no cycle possible */
    if (slow == NULL) {
        return SERAPH_ATLAS_VALIDATE_OK;
    }

    uint32_t iterations = 0;

    while (fast != NULL && iterations < SERAPH_ATLAS_MAX_CYCLE_DEPTH) {
        /* Move slow one step */
        if (!seraph_atlas_contains(atlas, slow)) {
            return SERAPH_ATLAS_VALIDATE_INVALID_PTR;
        }
        slow = read_ptr_field(slow, next_offset);

        /* Move fast two steps */
        if (!seraph_atlas_contains(atlas, fast)) {
            return SERAPH_ATLAS_VALIDATE_INVALID_PTR;
        }
        fast = read_ptr_field(fast, next_offset);

        if (fast == NULL) {
            break;  /* End of list reached */
        }

        if (!seraph_atlas_contains(atlas, fast)) {
            return SERAPH_ATLAS_VALIDATE_INVALID_PTR;
        }
        fast = read_ptr_field(fast, next_offset);

        /* Check for cycle */
        if (slow == fast && slow != NULL) {
            return SERAPH_ATLAS_VALIDATE_CYCLE_DETECTED;
        }

        iterations++;
    }

    /* Check for maximum depth exceeded (possible infinite loop) */
    if (iterations >= SERAPH_ATLAS_MAX_CYCLE_DEPTH) {
        return SERAPH_ATLAS_VALIDATE_CYCLE_DETECTED;  /* Treat as cycle */
    }

    return SERAPH_ATLAS_VALIDATE_OK;
}

/**
 * @brief Validate ARRAY_BOUNDS invariant
 */
static Seraph_Atlas_Validate_Result validate_array_bounds(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    /* Get array pointer */
    void* array_ptr = read_ptr_field(data, inv->field_offset);

    /* NULL array is OK if count is 0 */
    if (array_ptr == NULL) {
        size_t count = read_size_field(data, inv->params.array.count_offset);
        if (count == 0) {
            return SERAPH_ATLAS_VALIDATE_OK;
        }
        return SERAPH_ATLAS_VALIDATE_NULL_VIOLATION;
    }

    /* Array must be within Atlas */
    if (!seraph_atlas_contains(atlas, array_ptr)) {
        return SERAPH_ATLAS_VALIDATE_INVALID_PTR;
    }

    /* Get count */
    size_t count = read_size_field(data, inv->params.array.count_offset);
    size_t element_size = inv->params.array.element_size;
    size_t max_count = inv->params.array.max_count;

    /* Check max_count limit */
    if (max_count > 0 && count > max_count) {
        return SERAPH_ATLAS_VALIDATE_BOUNDS_EXCEEDED;
    }

    /* Calculate total size and verify it's within Atlas */
    size_t total_size = count * element_size;
    uint8_t* array_end = (uint8_t*)array_ptr + total_size;

    if (!seraph_atlas_contains(atlas, array_end - 1) && count > 0) {
        return SERAPH_ATLAS_VALIDATE_BOUNDS_EXCEEDED;
    }

    return SERAPH_ATLAS_VALIDATE_OK;
}

/**
 * @brief Validate REFCOUNT invariant
 */
static Seraph_Atlas_Validate_Result validate_refcount(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    (void)atlas;  /* Unused */

    int64_t refcount = read_int_field(data, inv->field_offset, inv->field_size);
    int64_t min_count = inv->params.refcount.min_count;

    if (refcount < min_count) {
        return SERAPH_ATLAS_VALIDATE_REFCOUNT_INVALID;
    }

    /* If live_only, skip objects with refcount 0 */
    if (inv->params.refcount.live_only && refcount == 0) {
        return SERAPH_ATLAS_VALIDATE_OK;  /* Dead objects are OK */
    }

    return SERAPH_ATLAS_VALIDATE_OK;
}

/**
 * @brief Validate RANGE invariant
 */
static Seraph_Atlas_Validate_Result validate_range(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    (void)atlas;  /* Unused */

    int64_t value = read_int_field(data, inv->field_offset, inv->field_size);
    int64_t min = inv->params.range.min;
    int64_t max = inv->params.range.max;

    if (value < min || value > max) {
        return SERAPH_ATLAS_VALIDATE_RANGE_EXCEEDED;
    }

    return SERAPH_ATLAS_VALIDATE_OK;
}

/**
 * @brief Validate CUSTOM invariant
 */
static Seraph_Atlas_Validate_Result validate_custom(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    if (inv->params.custom.validator == NULL) {
        return SERAPH_ATLAS_VALIDATE_OK;  /* No validator = always OK */
    }

    return inv->params.custom.validator(
        atlas,
        data,
        inv->field_offset,
        inv->field_size,
        inv->params.custom.user_data
    );
}

/**
 * @brief Master validation dispatcher
 */
static Seraph_Atlas_Validate_Result validate_invariant(
    const Seraph_Atlas* atlas,
    const void* data,
    const Seraph_Atlas_Invariant* inv
) {
    switch (inv->type) {
        case SERAPH_ATLAS_INVARIANT_NULL_PTR:
            return validate_null_ptr(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_NULLABLE_PTR:
            return validate_nullable_ptr(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_NO_CYCLE:
            return validate_no_cycle(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_ARRAY_BOUNDS:
            return validate_array_bounds(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_REFCOUNT:
            return validate_refcount(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_RANGE:
            return validate_range(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_CUSTOM:
            return validate_custom(atlas, data, inv);

        default:
            return SERAPH_ATLAS_VALIDATE_ERROR;
    }
}

/*============================================================================
 * Invariant Recovery Helpers
 *============================================================================*/

/**
 * @brief Recover from NULLABLE_PTR violation (set to NULL)
 */
static bool recover_nullable_ptr(
    Seraph_Atlas* atlas,
    void* data,
    const Seraph_Atlas_Invariant* inv
) {
    (void)atlas;

    /* Set invalid pointer to NULL */
    write_ptr_field(data, inv->field_offset, NULL);
    return true;
}

/**
 * @brief Recover from NO_CYCLE violation (find and break cycle)
 *
 * Uses Floyd's algorithm to find cycle, then breaks it.
 */
static bool recover_no_cycle(
    Seraph_Atlas* atlas,
    void* data,
    const Seraph_Atlas_Invariant* inv
) {
    size_t next_offset = inv->params.cycle.next_offset;

    /* Get initial pointer */
    void* slow = read_ptr_field(data, inv->field_offset);
    void* fast = slow;

    if (slow == NULL) {
        return true;  /* No cycle */
    }

    /* Phase 1: Detect cycle using Floyd's algorithm */
    bool cycle_found = false;
    uint32_t iterations = 0;

    while (fast != NULL && iterations < SERAPH_ATLAS_MAX_CYCLE_DEPTH) {
        if (!seraph_atlas_contains(atlas, slow)) {
            break;
        }
        slow = read_ptr_field(slow, next_offset);

        if (!seraph_atlas_contains(atlas, fast)) {
            break;
        }
        fast = read_ptr_field(fast, next_offset);

        if (fast == NULL) {
            break;
        }

        if (!seraph_atlas_contains(atlas, fast)) {
            break;
        }
        fast = read_ptr_field(fast, next_offset);

        if (slow == fast && slow != NULL) {
            cycle_found = true;
            break;
        }

        iterations++;
    }

    if (!cycle_found) {
        return true;  /* No cycle to fix */
    }

    /* Phase 2: Find cycle start */
    slow = read_ptr_field(data, inv->field_offset);
    void* prev = NULL;

    while (slow != fast) {
        if (!seraph_atlas_contains(atlas, slow)) {
            return false;  /* Cannot recover */
        }
        prev = slow;
        slow = read_ptr_field(slow, next_offset);

        if (!seraph_atlas_contains(atlas, fast)) {
            return false;
        }
        fast = read_ptr_field(fast, next_offset);
    }

    /* Phase 3: Find last node in cycle and break it */
    void* cycle_start = slow;
    prev = cycle_start;
    void* current = read_ptr_field(cycle_start, next_offset);

    iterations = 0;
    while (current != cycle_start && iterations < SERAPH_ATLAS_MAX_CYCLE_DEPTH) {
        if (!seraph_atlas_contains(atlas, current)) {
            break;
        }
        prev = current;
        current = read_ptr_field(current, next_offset);
        iterations++;
    }

    /* Break the cycle by setting the last node's next to NULL */
    if (prev != NULL && seraph_atlas_contains(atlas, prev)) {
        write_ptr_field(prev, next_offset, NULL);
        return true;
    }

    return false;
}

/**
 * @brief Recover from ARRAY_BOUNDS violation (truncate count)
 */
static bool recover_array_bounds(
    Seraph_Atlas* atlas,
    void* data,
    const Seraph_Atlas_Invariant* inv
) {
    void* array_ptr = read_ptr_field(data, inv->field_offset);
    size_t count = read_size_field(data, inv->params.array.count_offset);
    size_t max_count = inv->params.array.max_count;

    /* If array is NULL, set count to 0 */
    if (array_ptr == NULL) {
        size_t zero = 0;
        uint8_t* bytes = (uint8_t*)data;
        memcpy(bytes + inv->params.array.count_offset, &zero, sizeof(size_t));
        return true;
    }

    /* Calculate valid count based on Atlas bounds */
    if (!seraph_atlas_contains(atlas, array_ptr)) {
        /* Array completely outside Atlas - set count to 0 */
        size_t zero = 0;
        uint8_t* bytes = (uint8_t*)data;
        memcpy(bytes + inv->params.array.count_offset, &zero, sizeof(size_t));
        return true;
    }

    /* Truncate to max_count if specified */
    if (max_count > 0 && count > max_count) {
        uint8_t* bytes = (uint8_t*)data;
        memcpy(bytes + inv->params.array.count_offset, &max_count, sizeof(size_t));
        return true;
    }

    /* Calculate how many elements fit within Atlas */
    uint64_t offset = seraph_atlas_ptr_to_offset(atlas, array_ptr);
    size_t available = atlas->size - offset;
    size_t element_size = inv->params.array.element_size;
    size_t max_elements = element_size > 0 ? available / element_size : 0;

    if (count > max_elements) {
        uint8_t* bytes = (uint8_t*)data;
        memcpy(bytes + inv->params.array.count_offset, &max_elements, sizeof(size_t));
        return true;
    }

    return true;
}

/**
 * @brief Recover from REFCOUNT violation (set to minimum)
 */
static bool recover_refcount(
    Seraph_Atlas* atlas,
    void* data,
    const Seraph_Atlas_Invariant* inv
) {
    (void)atlas;

    int64_t min_count = inv->params.refcount.min_count;

    /* Set refcount to minimum valid value */
    write_int_field(data, inv->field_offset, inv->field_size, min_count);
    return true;
}

/**
 * @brief Recover from RANGE violation (clamp to valid range)
 */
static bool recover_range(
    Seraph_Atlas* atlas,
    void* data,
    const Seraph_Atlas_Invariant* inv
) {
    (void)atlas;

    int64_t value = read_int_field(data, inv->field_offset, inv->field_size);
    int64_t min = inv->params.range.min;
    int64_t max = inv->params.range.max;

    /* Clamp to valid range */
    if (value < min) {
        write_int_field(data, inv->field_offset, inv->field_size, min);
    } else if (value > max) {
        write_int_field(data, inv->field_offset, inv->field_size, max);
    }

    return true;
}

/**
 * @brief Recover using custom recovery function
 */
static bool recover_custom(
    Seraph_Atlas* atlas,
    void* data,
    const Seraph_Atlas_Invariant* inv,
    Seraph_Atlas_Validate_Result violation
) {
    if (inv->params.custom.recovery == NULL) {
        return false;  /* No recovery function */
    }

    return inv->params.custom.recovery(
        atlas,
        data,
        inv->field_offset,
        inv->field_size,
        violation,
        inv->params.custom.user_data
    );
}

/**
 * @brief Master recovery dispatcher
 */
static bool recover_invariant(
    Seraph_Atlas* atlas,
    void* data,
    const Seraph_Atlas_Invariant* inv,
    Seraph_Atlas_Validate_Result violation
) {
    if (!inv->auto_recoverable) {
        return false;  /* Not recoverable */
    }

    switch (inv->type) {
        case SERAPH_ATLAS_INVARIANT_NULL_PTR:
            return false;  /* Cannot auto-recover required pointer */

        case SERAPH_ATLAS_INVARIANT_NULLABLE_PTR:
            return recover_nullable_ptr(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_NO_CYCLE:
            return recover_no_cycle(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_ARRAY_BOUNDS:
            return recover_array_bounds(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_REFCOUNT:
            return recover_refcount(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_RANGE:
            return recover_range(atlas, data, inv);

        case SERAPH_ATLAS_INVARIANT_CUSTOM:
            return recover_custom(atlas, data, inv, violation);

        default:
            return false;
    }
}

/*============================================================================
 * Type Registration API Implementation
 *============================================================================*/

uint32_t seraph_atlas_checkpoint_register_type(
    const char* name,
    size_t instance_size
) {
    if (name == NULL || instance_size == 0) {
        return SERAPH_VOID_U32;
    }

    if (atlas_type_count >= SERAPH_ATLAS_MAX_TYPES) {
        return SERAPH_VOID_U32;  /* Registry full */
    }

    /* Check for duplicate name */
    for (uint32_t i = 0; i < atlas_type_count; i++) {
        if (atlas_type_registry[i].registered &&
            atlas_type_registry[i].name != NULL &&
            strcmp(atlas_type_registry[i].name, name) == 0) {
            return SERAPH_VOID_U32;  /* Name already registered */
        }
    }

    /* Register new type */
    uint32_t type_id = atlas_type_count;
    Seraph_Atlas_Type_Info* type = &atlas_type_registry[type_id];

    memset(type, 0, sizeof(Seraph_Atlas_Type_Info));
    type->type_id = type_id;
    type->name = name;  /* Caller responsible for lifetime */
    type->instance_size = instance_size;
    type->invariant_count = 0;
    type->registered = true;
    type->instance_validator = NULL;
    type->instance_recovery = NULL;
    type->user_data = NULL;

    atlas_type_count++;

    return type_id;
}

bool seraph_atlas_checkpoint_add_invariant(
    uint32_t type_id,
    const Seraph_Atlas_Invariant* invariant
) {
    if (type_id >= atlas_type_count || invariant == NULL) {
        return false;
    }

    Seraph_Atlas_Type_Info* type = &atlas_type_registry[type_id];

    if (!type->registered) {
        return false;
    }

    if (type->invariant_count >= SERAPH_ATLAS_MAX_INVARIANTS) {
        return false;  /* Too many invariants */
    }

    /* Copy invariant to type */
    memcpy(&type->invariants[type->invariant_count], invariant,
           sizeof(Seraph_Atlas_Invariant));
    type->invariant_count++;

    return true;
}

bool seraph_atlas_checkpoint_set_type_validator(
    uint32_t type_id,
    Seraph_Atlas_Validator_Fn validator,
    Seraph_Atlas_Recovery_Fn recovery,
    void* user_data
) {
    if (type_id >= atlas_type_count) {
        return false;
    }

    Seraph_Atlas_Type_Info* type = &atlas_type_registry[type_id];

    if (!type->registered) {
        return false;
    }

    type->instance_validator = validator;
    type->instance_recovery = recovery;
    type->user_data = user_data;

    return true;
}

const Seraph_Atlas_Type_Info* seraph_atlas_checkpoint_get_type(uint32_t type_id) {
    if (type_id >= atlas_type_count) {
        return NULL;
    }

    Seraph_Atlas_Type_Info* type = &atlas_type_registry[type_id];

    if (!type->registered) {
        return NULL;
    }

    return type;
}

uint32_t seraph_atlas_checkpoint_find_type(const char* name) {
    if (name == NULL) {
        return SERAPH_VOID_U32;
    }

    for (uint32_t i = 0; i < atlas_type_count; i++) {
        if (atlas_type_registry[i].registered &&
            atlas_type_registry[i].name != NULL &&
            strcmp(atlas_type_registry[i].name, name) == 0) {
            return i;
        }
    }

    return SERAPH_VOID_U32;
}

/*============================================================================
 * Checkpoint API Implementation
 *============================================================================*/

Seraph_Atlas_Checkpoint* seraph_atlas_checkpoint_create(
    Seraph_Atlas* atlas,
    const char* name,
    uint32_t max_entries,
    uint32_t flags
) {
    if (!seraph_atlas_is_valid(atlas)) {
        return NULL;
    }

    /* Use default max entries if not specified */
    if (max_entries == 0) {
        max_entries = SERAPH_ATLAS_MAX_CHECKPOINT_ENTRIES;
    }

    /* Allocate checkpoint structure */
    Seraph_Atlas_Checkpoint* checkpoint = (Seraph_Atlas_Checkpoint*)
        seraph_atlas_calloc(atlas, sizeof(Seraph_Atlas_Checkpoint));

    if (checkpoint == NULL) {
        return NULL;
    }

    /* Allocate entries array */
    size_t entries_size = max_entries * sizeof(Seraph_Atlas_Checkpoint_Entry);
    Seraph_Atlas_Checkpoint_Entry* entries = (Seraph_Atlas_Checkpoint_Entry*)
        seraph_atlas_calloc(atlas, entries_size);

    if (entries == NULL) {
        seraph_atlas_free(atlas, checkpoint, sizeof(Seraph_Atlas_Checkpoint));
        return NULL;
    }

    Seraph_Atlas_Genesis* genesis = seraph_atlas_genesis(atlas);

    /* Initialize checkpoint */
    checkpoint->magic = SERAPH_ATLAS_CHECKPOINT_MAGIC;
    checkpoint->checkpoint_id = atlas_next_checkpoint_id++;
    checkpoint->generation = genesis->generation;
    checkpoint->created_at = 0;  /* Would be seraph_chronon_now() */
    checkpoint->entry_count = 0;
    checkpoint->max_entries = max_entries;
    checkpoint->entries = entries;
    checkpoint->entries_offset = seraph_atlas_ptr_to_offset(atlas, entries);
    checkpoint->validated = false;
    checkpoint->total_errors = 0;
    checkpoint->total_recoveries = 0;
    checkpoint->flags = flags;

    /* Set name */
    if (name != NULL) {
        strncpy(checkpoint->name, name, sizeof(checkpoint->name) - 1);
        checkpoint->name[sizeof(checkpoint->name) - 1] = '\0';
    } else {
        /* Auto-generate name */
        snprintf(checkpoint->name, sizeof(checkpoint->name),
                 "ckpt_%llu", (unsigned long long)checkpoint->checkpoint_id);
    }

    return checkpoint;
}

bool seraph_atlas_checkpoint_add_entry(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint,
    void* ptr,
    uint32_t type_id,
    size_t alloc_size,
    uint32_t flags
) {
    if (!seraph_atlas_is_valid(atlas) || checkpoint == NULL || ptr == NULL) {
        return false;
    }

    if (checkpoint->entry_count >= checkpoint->max_entries) {
        return false;  /* Checkpoint full */
    }

    if (!seraph_atlas_contains(atlas, ptr)) {
        return false;  /* Pointer not in Atlas */
    }

    /* Verify type exists */
    const Seraph_Atlas_Type_Info* type = seraph_atlas_checkpoint_get_type(type_id);
    if (type == NULL) {
        return false;  /* Unknown type */
    }

    /* Use type instance size if alloc_size not specified */
    if (alloc_size == 0) {
        alloc_size = type->instance_size;
    }

    /* Create entry */
    Seraph_Atlas_Checkpoint_Entry* entry =
        &checkpoint->entries[checkpoint->entry_count];

    entry->ptr = ptr;
    entry->offset = seraph_atlas_ptr_to_offset(atlas, ptr);
    entry->type_id = type_id;
    entry->alloc_size = alloc_size;
    entry->checksum = calculate_crc32(ptr, alloc_size);
    entry->flags = flags;
    entry->error_count = 0;
    entry->last_result = SERAPH_ATLAS_VALIDATE_OK;

    checkpoint->entry_count++;

    return true;
}

Seraph_Vbit seraph_atlas_checkpoint_validate(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint,
    Seraph_Atlas_Validation_Report* report
) {
    if (!seraph_atlas_is_valid(atlas) || checkpoint == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (checkpoint->magic != SERAPH_ATLAS_CHECKPOINT_MAGIC) {
        return SERAPH_VBIT_VOID;  /* Invalid checkpoint */
    }

    /* Initialize report if provided */
    if (report != NULL) {
        memset(report, 0, sizeof(Seraph_Atlas_Validation_Report));
        report->checkpoint_id = checkpoint->checkpoint_id;
    }

    uint32_t total_errors = 0;
    uint32_t entries_passed = 0;
    uint32_t entries_failed = 0;
    uint32_t invariants_checked = 0;
    uint32_t invariants_passed = 0;
    uint32_t invariants_failed = 0;

    /* Validate each entry */
    for (uint32_t i = 0; i < checkpoint->entry_count; i++) {
        Seraph_Atlas_Checkpoint_Entry* entry = &checkpoint->entries[i];

        /* Get type */
        const Seraph_Atlas_Type_Info* type =
            seraph_atlas_checkpoint_get_type(entry->type_id);

        if (type == NULL) {
            entry->flags |= SERAPH_ATLAS_ENTRY_INVALID;
            entry->error_count++;
            entry->last_result = SERAPH_ATLAS_VALIDATE_ERROR;
            total_errors++;
            entries_failed++;
            continue;
        }

        /* Resolve pointer (in case entry was restored) */
        void* data = seraph_atlas_offset_to_ptr(atlas, entry->offset);
        if (data == NULL) {
            entry->flags |= SERAPH_ATLAS_ENTRY_INVALID;
            entry->error_count++;
            entry->last_result = SERAPH_ATLAS_VALIDATE_INVALID_PTR;
            total_errors++;
            entries_failed++;
            continue;
        }

        entry->ptr = data;  /* Update pointer */
        bool entry_valid = true;
        entry->error_count = 0;

        /* Validate all invariants for this type */
        for (uint32_t j = 0; j < type->invariant_count; j++) {
            const Seraph_Atlas_Invariant* inv = &type->invariants[j];
            invariants_checked++;

            Seraph_Atlas_Validate_Result result = validate_invariant(atlas, data, inv);

            if (result == SERAPH_ATLAS_VALIDATE_OK) {
                invariants_passed++;
            } else {
                invariants_failed++;
                entry->error_count++;
                entry_valid = false;

                if (entry->last_result == SERAPH_ATLAS_VALIDATE_OK) {
                    entry->last_result = result;  /* Record first error */
                }

                /* Record detail if report provided */
                if (report != NULL && report->details != NULL &&
                    report->detail_count < report->max_details) {
                    Seraph_Atlas_Validation_Detail* detail =
                        &report->details[report->detail_count];
                    detail->entry_index = i;
                    detail->invariant_index = j;
                    detail->result = result;
                    detail->type_id = entry->type_id;
                    detail->field_offset = inv->field_offset;
                    detail->recovery_attempted = false;
                    detail->recovery_succeeded = false;
                    report->detail_count++;
                }
            }
        }

        /* Run type-level validator if present */
        if (type->instance_validator != NULL) {
            invariants_checked++;
            Seraph_Atlas_Validate_Result result = type->instance_validator(
                atlas, data, 0, type->instance_size, type->user_data);

            if (result == SERAPH_ATLAS_VALIDATE_OK) {
                invariants_passed++;
            } else {
                invariants_failed++;
                entry->error_count++;
                entry_valid = false;

                if (entry->last_result == SERAPH_ATLAS_VALIDATE_OK) {
                    entry->last_result = result;
                }
            }
        }

        /* Update entry flags */
        if (entry_valid) {
            entry->flags &= ~SERAPH_ATLAS_ENTRY_INVALID;
            entries_passed++;
        } else {
            entry->flags |= SERAPH_ATLAS_ENTRY_INVALID;
            entries_failed++;
            total_errors += entry->error_count;
        }

        /* Check for modification (compare checksum) */
        uint32_t current_checksum = calculate_crc32(data, entry->alloc_size);
        if (current_checksum != entry->checksum) {
            entry->flags |= SERAPH_ATLAS_ENTRY_MODIFIED;
        }
    }

    /* Update checkpoint state */
    checkpoint->validated = true;
    checkpoint->total_errors = total_errors;

    /* Fill report */
    if (report != NULL) {
        report->entries_validated = checkpoint->entry_count;
        report->entries_passed = entries_passed;
        report->entries_failed = entries_failed;
        report->invariants_checked = invariants_checked;
        report->invariants_passed = invariants_passed;
        report->invariants_failed = invariants_failed;
        report->passed = (entries_failed == 0);
    }

    return (entries_failed == 0) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

Seraph_Vbit seraph_atlas_checkpoint_recover(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint,
    Seraph_Atlas_Validation_Report* report
) {
    if (!seraph_atlas_is_valid(atlas) || checkpoint == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (checkpoint->magic != SERAPH_ATLAS_CHECKPOINT_MAGIC) {
        return SERAPH_VBIT_VOID;
    }

    /* First, validate to identify errors */
    Seraph_Atlas_Validation_Report temp_report = {0};
    seraph_atlas_checkpoint_validate(atlas, checkpoint, &temp_report);

    if (temp_report.entries_failed == 0) {
        /* No errors to recover */
        if (report != NULL) {
            *report = temp_report;
            report->passed = true;
        }
        return SERAPH_VBIT_TRUE;
    }

    uint32_t recoveries_attempted = 0;
    uint32_t recoveries_succeeded = 0;

    /* Attempt recovery for each entry with errors */
    for (uint32_t i = 0; i < checkpoint->entry_count; i++) {
        Seraph_Atlas_Checkpoint_Entry* entry = &checkpoint->entries[i];

        if (!(entry->flags & SERAPH_ATLAS_ENTRY_INVALID)) {
            continue;  /* Entry is valid */
        }

        const Seraph_Atlas_Type_Info* type =
            seraph_atlas_checkpoint_get_type(entry->type_id);

        if (type == NULL) {
            continue;  /* Unknown type */
        }

        void* data = entry->ptr;
        if (data == NULL) {
            data = seraph_atlas_offset_to_ptr(atlas, entry->offset);
            if (data == NULL) {
                continue;  /* Cannot access data */
            }
            entry->ptr = data;
        }

        bool entry_recovered = true;

        /* Try to recover each invariant */
        for (uint32_t j = 0; j < type->invariant_count; j++) {
            const Seraph_Atlas_Invariant* inv = &type->invariants[j];

            Seraph_Atlas_Validate_Result result = validate_invariant(atlas, data, inv);

            if (result == SERAPH_ATLAS_VALIDATE_OK) {
                continue;  /* Invariant OK */
            }

            recoveries_attempted++;

            /* Attempt recovery */
            bool recovered = recover_invariant(atlas, data, inv, result);

            if (recovered) {
                /* Verify recovery worked */
                result = validate_invariant(atlas, data, inv);
                if (result == SERAPH_ATLAS_VALIDATE_OK) {
                    recoveries_succeeded++;
                } else {
                    entry_recovered = false;
                }
            } else {
                entry_recovered = false;
            }
        }

        /* Run type-level recovery if present */
        if (type->instance_recovery != NULL &&
            entry->last_result != SERAPH_ATLAS_VALIDATE_OK) {
            recoveries_attempted++;

            bool recovered = type->instance_recovery(
                atlas, data, 0, type->instance_size,
                entry->last_result, type->user_data);

            if (recovered) {
                recoveries_succeeded++;
            } else {
                entry_recovered = false;
            }
        }

        /* Update entry flags */
        if (entry_recovered) {
            entry->flags &= ~SERAPH_ATLAS_ENTRY_INVALID;
            entry->flags |= SERAPH_ATLAS_ENTRY_RECOVERED;
            entry->error_count = 0;
            entry->last_result = SERAPH_ATLAS_VALIDATE_OK;

            /* Update checksum */
            entry->checksum = calculate_crc32(data, entry->alloc_size);
        }
    }

    checkpoint->total_recoveries = recoveries_succeeded;

    /* Re-validate to check final state */
    seraph_atlas_checkpoint_validate(atlas, checkpoint, report);

    if (report != NULL) {
        report->recoveries_attempted = recoveries_attempted;
        report->recoveries_succeeded = recoveries_succeeded;
    }

    return (checkpoint->total_errors == 0) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

void seraph_atlas_checkpoint_destroy(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint
) {
    if (!seraph_atlas_is_valid(atlas) || checkpoint == NULL) {
        return;
    }

    /* Free entries array */
    if (checkpoint->entries != NULL) {
        size_t entries_size = checkpoint->max_entries *
            sizeof(Seraph_Atlas_Checkpoint_Entry);
        seraph_atlas_free(atlas, checkpoint->entries, entries_size);
    }

    /* Free checkpoint structure */
    seraph_atlas_free(atlas, checkpoint, sizeof(Seraph_Atlas_Checkpoint));
}

void seraph_atlas_validation_report_free(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Validation_Report* report
) {
    if (report == NULL) {
        return;
    }

    if (report->details != NULL && atlas != NULL) {
        size_t details_size = report->max_details *
            sizeof(Seraph_Atlas_Validation_Detail);
        seraph_atlas_free(atlas, report->details, details_size);
    }

    report->details = NULL;
    report->detail_count = 0;
    report->max_details = 0;
}

/*============================================================================
 * Convenience Invariant Builders Implementation
 *============================================================================*/

Seraph_Atlas_Invariant seraph_atlas_invariant_not_null(
    size_t field_offset,
    const char* description
) {
    Seraph_Atlas_Invariant inv = {0};
    inv.type = SERAPH_ATLAS_INVARIANT_NULL_PTR;
    inv.field_offset = field_offset;
    inv.field_size = sizeof(void*);
    inv.description = description;
    inv.auto_recoverable = false;  /* Cannot auto-recover required pointer */
    return inv;
}

Seraph_Atlas_Invariant seraph_atlas_invariant_nullable(
    size_t field_offset,
    const char* description
) {
    Seraph_Atlas_Invariant inv = {0};
    inv.type = SERAPH_ATLAS_INVARIANT_NULLABLE_PTR;
    inv.field_offset = field_offset;
    inv.field_size = sizeof(void*);
    inv.description = description;
    inv.auto_recoverable = true;  /* Can set to NULL on invalid */
    return inv;
}

Seraph_Atlas_Invariant seraph_atlas_invariant_no_cycle(
    size_t next_field_offset,
    const char* description
) {
    Seraph_Atlas_Invariant inv = {0};
    inv.type = SERAPH_ATLAS_INVARIANT_NO_CYCLE;
    inv.field_offset = next_field_offset;  /* Field being checked is the next ptr */
    inv.field_size = sizeof(void*);
    inv.params.cycle.next_offset = next_field_offset;
    inv.description = description;
    inv.auto_recoverable = true;  /* Can break cycle */
    return inv;
}

Seraph_Atlas_Invariant seraph_atlas_invariant_array_bounds(
    size_t array_field_offset,
    size_t count_field_offset,
    size_t element_size,
    size_t max_count,
    const char* description
) {
    Seraph_Atlas_Invariant inv = {0};
    inv.type = SERAPH_ATLAS_INVARIANT_ARRAY_BOUNDS;
    inv.field_offset = array_field_offset;
    inv.field_size = sizeof(void*);
    inv.params.array.count_offset = count_field_offset;
    inv.params.array.element_size = element_size;
    inv.params.array.max_count = max_count;
    inv.description = description;
    inv.auto_recoverable = true;  /* Can truncate count */
    return inv;
}

Seraph_Atlas_Invariant seraph_atlas_invariant_refcount(
    size_t refcount_offset,
    int64_t min_count,
    bool live_only,
    const char* description
) {
    Seraph_Atlas_Invariant inv = {0};
    inv.type = SERAPH_ATLAS_INVARIANT_REFCOUNT;
    inv.field_offset = refcount_offset;
    inv.field_size = sizeof(int64_t);  /* Assume 64-bit refcount */
    inv.params.refcount.min_count = min_count;
    inv.params.refcount.live_only = live_only;
    inv.description = description;
    inv.auto_recoverable = true;  /* Can reset to minimum */
    return inv;
}

Seraph_Atlas_Invariant seraph_atlas_invariant_range(
    size_t field_offset,
    size_t field_size,
    int64_t min,
    int64_t max,
    const char* description
) {
    Seraph_Atlas_Invariant inv = {0};
    inv.type = SERAPH_ATLAS_INVARIANT_RANGE;
    inv.field_offset = field_offset;
    inv.field_size = field_size;
    inv.params.range.min = min;
    inv.params.range.max = max;
    inv.description = description;
    inv.auto_recoverable = true;  /* Can clamp to range */
    return inv;
}

Seraph_Atlas_Invariant seraph_atlas_invariant_custom(
    size_t field_offset,
    size_t field_size,
    Seraph_Atlas_Validator_Fn validator,
    Seraph_Atlas_Recovery_Fn recovery,
    void* user_data,
    const char* description
) {
    Seraph_Atlas_Invariant inv = {0};
    inv.type = SERAPH_ATLAS_INVARIANT_CUSTOM;
    inv.field_offset = field_offset;
    inv.field_size = field_size;
    inv.params.custom.validator = validator;
    inv.params.custom.recovery = recovery;
    inv.params.custom.user_data = user_data;
    inv.description = description;
    inv.auto_recoverable = (recovery != NULL);
    return inv;
}
