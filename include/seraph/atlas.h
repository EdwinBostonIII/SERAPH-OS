/**
 * @file atlas.h
 * @brief MC27: Atlas - The Single-Level Store
 *
 * "There is no disk. There is no file system. There is only memory that remembers."
 *
 * Atlas implements the Single-Level Store paradigm: the entire NVMe storage
 * device is mapped directly into the process address space. There is no
 * open(), read(), write(), close(). There is no serialization. There is no
 * deserialization. There are only POINTERS.
 *
 * KEY INNOVATIONS:
 * 1. GENESIS POINTER: One root pointer to all persistent data - no file names
 * 2. COPY-ON-WRITE: Mutations create new versions; crashes never corrupt
 * 3. INSTANT RECOVERY: O(1) recovery regardless of data size
 * 4. CAPABILITY PERSISTENCE: Generations survive reboots; revoked = revoked forever
 * 5. TRANSPARENT ACCESS: Code just uses pointers
 *
 * ADDRESS SPACE LAYOUT:
 * +-----------------------------------------+
 * | 0x0000_0000_0000_0000 - VOLATILE (RAM)  |
 * | 0x0000_7FFF_FFFF_FFFF                   |
 * +-----------------------------------------+
 * | 0x0000_8000_0000_0000 - ATLAS (NVMe)    |
 * | 0x0000_BFFF_FFFF_FFFF                   |
 * +-----------------------------------------+
 * | 0x0000_C000_0000_0000 - AETHER (Net)    |
 * | 0x0000_FFFF_FFFF_FFFF                   |
 * +-----------------------------------------+
 */

#ifndef SERAPH_ATLAS_H
#define SERAPH_ATLAS_H

#include "void.h"
#include "vbit.h"
#include "chronon.h"
#include "capability.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Address Space Constants
 *============================================================================*/

/** Base address of volatile (RAM) region */
#define SERAPH_VOLATILE_BASE  0x0000000000000000ULL

/** Base address of Atlas (persistent NVMe) region */
#ifndef SERAPH_ATLAS_BASE
#define SERAPH_ATLAS_BASE     0x0000800000000000ULL
#endif

/** Base address of Aether (network) region */
#ifndef SERAPH_AETHER_BASE
#define SERAPH_AETHER_BASE    0x0000C00000000000ULL
#endif

/** Standard page size (4KB) */
#define SERAPH_PAGE_SIZE      4096

/** Page size mask for alignment */
#define SERAPH_PAGE_MASK      (SERAPH_PAGE_SIZE - 1)

/*============================================================================
 * Atlas Configuration
 *============================================================================*/

/** Default simulated Atlas size (64MB) */
#define SERAPH_ATLAS_DEFAULT_SIZE   (64ULL * 1024 * 1024)

/** Maximum simulated Atlas size (4GB) */
#define SERAPH_ATLAS_MAX_SIZE       (4ULL * 1024 * 1024 * 1024)

/** Atlas magic number ("SERAPHAT" in hex) */
#define SERAPH_ATLAS_MAGIC          0x5345524150484154ULL

/** Atlas format version */
#define SERAPH_ATLAS_VERSION        1

/** Maximum path length for backing file */
#define SERAPH_ATLAS_MAX_PATH       256

/** Maximum transactions */
#define SERAPH_ATLAS_MAX_TRANSACTIONS 16

/** Maximum dirty pages per transaction */
#define SERAPH_ATLAS_MAX_DIRTY_PAGES  256

/** Generation table size (max allocations tracked) */
#define SERAPH_ATLAS_GEN_TABLE_SIZE   4096

/*============================================================================
 * Semantic Checkpoint Configuration
 *============================================================================*/

/** Maximum registered types for semantic checkpointing */
#define SERAPH_ATLAS_MAX_TYPES              64

/** Maximum invariants per type */
#define SERAPH_ATLAS_MAX_INVARIANTS         32

/** Maximum entries per checkpoint */
#define SERAPH_ATLAS_MAX_CHECKPOINT_ENTRIES 256

/** Maximum cycle detection depth (prevents infinite loops) */
#define SERAPH_ATLAS_MAX_CYCLE_DEPTH        65536

/** Checkpoint magic number ("SERAPCHK" in hex) */
#define SERAPH_ATLAS_CHECKPOINT_MAGIC       0x5345524150434B48ULL

/*============================================================================
 * Causal Snapshot Configuration
 *
 * Causal snapshots capture a consistent point-in-time view of Atlas state
 * that respects causality ordering. Uses vector clocks from chronon.h to
 * track distributed causality and copy-on-write for snapshot isolation.
 *============================================================================*/

/** Maximum number of concurrent snapshots */
#define SERAPH_ATLAS_MAX_SNAPSHOTS          8

/** Maximum pages tracked per snapshot for COW */
#define SERAPH_ATLAS_SNAPSHOT_MAX_PAGES     1024

/** Maximum vector clock dimension (nodes in distributed system) */
#define SERAPH_ATLAS_VCLOCK_MAX_NODES       64

/** Snapshot magic number ("SERAPSNP" in hex) */
#define SERAPH_ATLAS_SNAPSHOT_MAGIC         0x5345524150534E50ULL

/** Snapshot version for forward compatibility */
#define SERAPH_ATLAS_SNAPSHOT_VERSION       1

/*============================================================================
 * Genesis Structure
 *============================================================================*/

/**
 * @brief The Genesis structure - ONE pointer to ALL persistent data
 *
 * At offset 0 of the Atlas region sits Genesis. Genesis points to the root
 * of ALL persistent data. Everything reachable from Genesis persists.
 * Everything else doesn't.
 *
 * This is SIMPLER than a file system:
 *   - No file names to manage
 *   - No directories to navigate
 *   - No path resolution
 *   - No permissions per file (capabilities cover everything)
 *
 * This is MORE POWERFUL than a file system:
 *   - Arbitrary graph structures (not just tree)
 *   - Pointer consistency guaranteed
 *   - Atomic updates to entire data structure
 *   - O(1) "find" for any data (if you have a capability)
 */
typedef struct __attribute__((aligned(256))) {
    /** Magic number for validation (SERAPH_ATLAS_MAGIC) */
    uint64_t magic;

    /** Format version */
    uint64_t version;

    /** Root generation for revocation */
    uint64_t generation;

    /** Offset to application root data (0 if none) */
    uint64_t root_offset;

    /** Offset to free page list head */
    uint64_t free_list_offset;

    /** Offset to generation table */
    uint64_t gen_table_offset;

    /** Next allocation offset (bump allocator) */
    uint64_t next_alloc_offset;

    /** Total allocated bytes */
    uint64_t total_allocated;

    /** Total freed bytes */
    uint64_t total_freed;

    /** When Atlas was created */
    Seraph_Chronon created_at;

    /** Last modification time */
    Seraph_Chronon modified_at;

    /** Last commit time */
    Seraph_Chronon last_commit_at;

    /** Number of commits */
    uint64_t commit_count;

    /** Number of aborted transactions */
    uint64_t abort_count;

    /** Reserved for future use */
    uint8_t _reserved[128];
} Seraph_Atlas_Genesis;

/* Static assertion for genesis size */
_Static_assert(sizeof(Seraph_Atlas_Genesis) == 256,
    "Genesis must be exactly 256 bytes");

/*============================================================================
 * Generation Table
 *============================================================================*/

/**
 * @brief Persistent generation table for capability revocation
 *
 * Generation counters survive reboots. If a capability is revoked
 * (generation incremented), it stays revoked even after power loss.
 */
typedef struct {
    /** Magic for validation */
    uint64_t magic;

    /** Number of entries in use */
    uint64_t entry_count;

    /** Next generation to assign */
    uint64_t next_generation;

    /** Generation counters for each allocation */
    uint64_t generations[SERAPH_ATLAS_GEN_TABLE_SIZE];
} Seraph_Atlas_Gen_Table;

/*============================================================================
 * Free List
 *============================================================================*/

/**
 * @brief Free page entry for memory reclamation
 */
typedef struct Seraph_Atlas_Free_Entry {
    /** Offset of next free entry (0 = end of list) */
    uint64_t next_offset;

    /** Size of this free block */
    uint64_t size;

    /** Generation when freed (for debugging) */
    uint64_t freed_generation;
} Seraph_Atlas_Free_Entry;

/*============================================================================
 * Transaction
 *============================================================================*/

/**
 * @brief Transaction state enumeration
 */
typedef enum {
    SERAPH_ATLAS_TX_VOID = 0,      /**< Invalid/uninitialized */
    SERAPH_ATLAS_TX_ACTIVE = 1,    /**< In progress */
    SERAPH_ATLAS_TX_COMMITTED = 2, /**< Successfully committed */
    SERAPH_ATLAS_TX_ABORTED = 3    /**< Aborted/rolled back */
} Seraph_Atlas_Tx_State;

/**
 * @brief Dirty page tracking for transactions
 */
typedef struct {
    uint64_t offset;    /**< Offset in Atlas */
    uint64_t size;      /**< Size of dirty region */
    void* original;     /**< Copy of original data (for rollback) */
} Seraph_Atlas_Dirty_Page;

/**
 * @brief Atlas transaction context
 *
 * Atlas provides ACID transactions without a transaction log:
 *   - Atomicity: Commit is a single pointer swap
 *   - Consistency: Invariants checked before commit
 *   - Isolation: Copy-on-write provides snapshot isolation
 *   - Durability: Committed data is on NVMe
 */
typedef struct {
    /** Transaction ID */
    uint64_t tx_id;

    /** Epoch when transaction started */
    uint64_t epoch;

    /** Genesis generation at transaction start */
    uint64_t start_generation;

    /** When transaction began */
    Seraph_Chronon start_chronon;

    /** Current state */
    Seraph_Atlas_Tx_State state;

    /** Dirty pages modified in this transaction */
    Seraph_Atlas_Dirty_Page dirty_pages[SERAPH_ATLAS_MAX_DIRTY_PAGES];

    /** Number of dirty pages */
    uint32_t dirty_count;
} Seraph_Atlas_Transaction;

/*============================================================================
 * Atlas Subsystem State
 *============================================================================*/

/* Forward declaration for snapshot */
typedef struct Seraph_Atlas_Snapshot Seraph_Atlas_Snapshot;

/**
 * @brief Atlas subsystem state
 *
 * This is the main state structure for the Atlas subsystem.
 * For userspace simulation, we use mmap with file backing.
 */
typedef struct {
    /** Base pointer of the mmap'd region */
    void* base;

    /** Size of the Atlas region */
    size_t size;

    /** Path to the backing file */
    char path[SERAPH_ATLAS_MAX_PATH];

    /** File handle (platform-specific) */
#ifdef _WIN32
    void* file_handle;    /**< HANDLE on Windows */
    void* mapping_handle; /**< File mapping handle */
#else
    int fd;               /**< File descriptor on POSIX */
#endif

    /** Is Atlas initialized? */
    bool initialized;

    /** Is Atlas read-only? */
    bool read_only;

    /** Current epoch (incremented each commit) */
    uint64_t current_epoch;

    /** Active transactions */
    Seraph_Atlas_Transaction transactions[SERAPH_ATLAS_MAX_TRANSACTIONS];

    /** Next transaction ID */
    uint64_t next_tx_id;

    /*--- Causal Snapshot State ---*/

    /** Active/committed snapshots */
    Seraph_Atlas_Snapshot* snapshots[SERAPH_ATLAS_MAX_SNAPSHOTS];

    /** Next snapshot ID */
    uint64_t next_snapshot_id;

    /** Local node ID for vector clocks */
    uint32_t local_node_id;

    /** Number of nodes in distributed system */
    uint32_t node_count;

    /** Current vector clock (for causality tracking) */
    Seraph_Chronon current_vclock[SERAPH_ATLAS_VCLOCK_MAX_NODES];
} Seraph_Atlas;

/*============================================================================
 * VOID Constants
 *============================================================================*/

/** VOID Atlas - invalid/failed initialization */
#define SERAPH_ATLAS_VOID ((Seraph_Atlas){ .initialized = false, .base = NULL })

/** VOID Transaction */
#define SERAPH_ATLAS_TX_VOID_VALUE ((Seraph_Atlas_Transaction){ \
    .tx_id = SERAPH_VOID_U64, \
    .state = SERAPH_ATLAS_TX_VOID \
})

/*============================================================================
 * Initialization and Cleanup
 *============================================================================*/

/**
 * @brief Initialize the Atlas subsystem
 *
 * Opens or creates the backing file and maps it into memory.
 * If the file doesn't exist, formats a new Atlas.
 * If the file exists, validates and recovers.
 *
 * @param atlas Atlas structure to initialize
 * @param path Path to backing file (persistent storage)
 * @param size Size of Atlas region (0 = use existing file size or default)
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_atlas_init(
    Seraph_Atlas* atlas,
    const char* path,
    size_t size
);

/**
 * @brief Initialize Atlas with default settings
 *
 * Uses a default path in the current directory.
 *
 * @param atlas Atlas structure to initialize
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_atlas_init_default(Seraph_Atlas* atlas);

/**
 * @brief Destroy the Atlas subsystem
 *
 * Syncs all data, unmaps memory, and closes the backing file.
 *
 * @param atlas Atlas to destroy
 */
void seraph_atlas_destroy(Seraph_Atlas* atlas);

/**
 * @brief Check if Atlas is valid
 */
static inline bool seraph_atlas_is_valid(const Seraph_Atlas* atlas) {
    return atlas != NULL && atlas->initialized && atlas->base != NULL;
}

/*============================================================================
 * Genesis Access
 *============================================================================*/

/**
 * @brief Get the Genesis structure
 *
 * @param atlas The Atlas instance
 * @return Pointer to Genesis, or NULL if Atlas invalid
 */
Seraph_Atlas_Genesis* seraph_atlas_genesis(Seraph_Atlas* atlas);

/**
 * @brief Get the application root pointer
 *
 * @param atlas The Atlas instance
 * @return Pointer to application root data, or NULL if none set
 */
void* seraph_atlas_get_root(Seraph_Atlas* atlas);

/**
 * @brief Set the application root pointer
 *
 * @param atlas The Atlas instance
 * @param root Pointer to the new root (must be within Atlas region)
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_atlas_set_root(Seraph_Atlas* atlas, void* root);

/*============================================================================
 * Allocation
 *============================================================================*/

/**
 * @brief Allocate memory from Atlas (persistent)
 *
 * Allocated memory persists across program restarts.
 *
 * @param atlas The Atlas instance
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void* seraph_atlas_alloc(Seraph_Atlas* atlas, size_t size);

/**
 * @brief Allocate zeroed memory from Atlas
 *
 * @param atlas The Atlas instance
 * @param size Number of bytes to allocate
 * @return Pointer to zeroed memory, or NULL on failure
 */
void* seraph_atlas_calloc(Seraph_Atlas* atlas, size_t size);

/**
 * @brief Allocate page-aligned memory from Atlas
 *
 * @param atlas The Atlas instance
 * @param size Number of bytes to allocate (will be rounded up to page size)
 * @return Pointer to page-aligned memory, or NULL on failure
 */
void* seraph_atlas_alloc_pages(Seraph_Atlas* atlas, size_t size);

/**
 * @brief Free memory back to Atlas
 *
 * Memory is added to the free list for reuse.
 *
 * @param atlas The Atlas instance
 * @param ptr Pointer to free
 * @param size Size of the allocation
 */
void seraph_atlas_free(Seraph_Atlas* atlas, void* ptr, size_t size);

/**
 * @brief Get remaining free space in Atlas
 *
 * @param atlas The Atlas instance
 * @return Number of bytes available
 */
size_t seraph_atlas_available(const Seraph_Atlas* atlas);

/*============================================================================
 * Pointer Utilities
 *============================================================================*/

/**
 * @brief Check if a pointer is within the Atlas region
 *
 * @param atlas The Atlas instance
 * @param ptr Pointer to check
 * @return TRUE if in Atlas, FALSE otherwise
 */
bool seraph_atlas_contains(const Seraph_Atlas* atlas, const void* ptr);

/**
 * @brief Convert Atlas pointer to offset
 *
 * @param atlas The Atlas instance
 * @param ptr Pointer within Atlas
 * @return Offset from Atlas base, or SERAPH_VOID_U64 if invalid
 */
uint64_t seraph_atlas_ptr_to_offset(const Seraph_Atlas* atlas, const void* ptr);

/**
 * @brief Convert offset to Atlas pointer
 *
 * @param atlas The Atlas instance
 * @param offset Offset from Atlas base
 * @return Pointer within Atlas, or NULL if invalid
 */
void* seraph_atlas_offset_to_ptr(const Seraph_Atlas* atlas, uint64_t offset);

/*============================================================================
 * Transactions
 *============================================================================*/

/**
 * @brief Begin a new transaction
 *
 * @param atlas The Atlas instance
 * @return Transaction pointer, or NULL on failure
 */
Seraph_Atlas_Transaction* seraph_atlas_begin(Seraph_Atlas* atlas);

/**
 * @brief Commit a transaction
 *
 * Flushes all dirty pages and atomically updates Genesis.
 *
 * @param atlas The Atlas instance
 * @param tx Transaction to commit
 * @return TRUE on success, FALSE on conflict, VOID on error
 */
Seraph_Vbit seraph_atlas_commit(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Transaction* tx
);

/**
 * @brief Abort a transaction
 *
 * Discards all changes. Copy-on-write pages become garbage.
 *
 * @param atlas The Atlas instance
 * @param tx Transaction to abort
 */
void seraph_atlas_abort(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Transaction* tx
);

/**
 * @brief Mark a region as dirty within a transaction
 *
 * @param tx The transaction
 * @param ptr Pointer to modified data
 * @param size Size of modified region
 * @return TRUE on success, FALSE if too many dirty pages
 */
Seraph_Vbit seraph_atlas_tx_mark_dirty(
    Seraph_Atlas_Transaction* tx,
    void* ptr,
    size_t size
);

/*============================================================================
 * Persistence Operations
 *============================================================================*/

/**
 * @brief Synchronize all changes to disk
 *
 * Forces all modified data to be written to the backing file.
 *
 * @param atlas The Atlas instance
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_atlas_sync(Seraph_Atlas* atlas);

/**
 * @brief Synchronize a specific region to disk
 *
 * @param atlas The Atlas instance
 * @param ptr Start of region
 * @param size Size of region
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_atlas_sync_range(
    Seraph_Atlas* atlas,
    void* ptr,
    size_t size
);

/*============================================================================
 * Generation Table (Capability Persistence)
 *============================================================================*/

/**
 * @brief Get the generation table
 *
 * @param atlas The Atlas instance
 * @return Pointer to generation table, or NULL if not initialized
 */
Seraph_Atlas_Gen_Table* seraph_atlas_get_gen_table(Seraph_Atlas* atlas);

/**
 * @brief Allocate a new generation ID
 *
 * @param atlas The Atlas instance
 * @return New generation ID, or SERAPH_VOID_U64 if table full
 */
uint64_t seraph_atlas_alloc_generation(Seraph_Atlas* atlas);

/**
 * @brief Increment generation (revoke all capabilities to an allocation)
 *
 * @param atlas The Atlas instance
 * @param alloc_id Allocation ID to revoke
 * @return New generation, or SERAPH_VOID_U64 if invalid
 */
uint64_t seraph_atlas_revoke(Seraph_Atlas* atlas, uint64_t alloc_id);

/**
 * @brief Check if a generation is current (capability still valid)
 *
 * @param atlas The Atlas instance
 * @param alloc_id Allocation ID
 * @param generation Generation to check
 * @return TRUE if valid, FALSE if revoked, VOID if error
 */
Seraph_Vbit seraph_atlas_check_generation(
    Seraph_Atlas* atlas,
    uint64_t alloc_id,
    uint64_t generation
);

/*============================================================================
 * Statistics
 *============================================================================*/

/**
 * @brief Atlas statistics
 */
typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t commit_count;
    uint64_t abort_count;
    bool initialized;
} Seraph_Atlas_Stats;

/**
 * @brief Get Atlas statistics
 *
 * @param atlas The Atlas instance
 * @return Statistics structure
 */
Seraph_Atlas_Stats seraph_atlas_get_stats(const Seraph_Atlas* atlas);

/*============================================================================
 * Causal Snapshot Structures
 *
 * CAUSAL SNAPSHOTS provide consistent point-in-time captures of Atlas state
 * that respect causality ordering. This means:
 *
 *   1. If event A causally preceded event B (A -> B), and the snapshot
 *      includes B's effects, it MUST include A's effects.
 *
 *   2. Concurrent events (A || B) may or may not be included, but the
 *      snapshot will be consistent (no partial states).
 *
 *   3. Copy-on-write ensures the snapshot sees a frozen view while
 *      the live Atlas can continue to be modified.
 *
 * USE CASES:
 *   - Consistent backup without pausing the system
 *   - Time-travel debugging (restore to any snapshot)
 *   - Replication to other nodes with causality preserved
 *   - Undo/redo with proper causality semantics
 *
 * IMPLEMENTATION:
 *   - Vector clock captures causality context at snapshot time
 *   - Dirty page tracking identifies modified pages
 *   - COW pages store original data for snapshot readers
 *   - Generation numbers prevent stale capability use after restore
 *============================================================================*/

/**
 * @brief Snapshot state enumeration
 *
 * Tracks the lifecycle of a causal snapshot.
 */
typedef enum {
    SERAPH_ATLAS_SNAP_VOID       = 0,   /**< Invalid/uninitialized */
    SERAPH_ATLAS_SNAP_PREPARING  = 1,   /**< Being prepared (pages being added) */
    SERAPH_ATLAS_SNAP_ACTIVE     = 2,   /**< Active - COW in effect */
    SERAPH_ATLAS_SNAP_COMMITTED  = 3,   /**< Finalized and persisted */
    SERAPH_ATLAS_SNAP_RESTORING  = 4,   /**< Being restored */
    SERAPH_ATLAS_SNAP_FAILED     = 5    /**< Failed (error during operation) */
} Seraph_Atlas_Snap_State;

/**
 * @brief Copy-on-write page entry
 *
 * Stores original page data when a page is modified during an active snapshot.
 * The snapshot sees the original data; live Atlas sees the new data.
 */
typedef struct {
    uint64_t page_offset;               /**< Offset of page in Atlas (page-aligned) */
    uint64_t copy_offset;               /**< Offset where COW copy is stored */
    uint64_t modification_time;         /**< Chronon when page was modified */
    uint32_t page_count;                /**< Number of contiguous pages */
    uint32_t flags;                     /**< COW flags (see below) */
} Seraph_Atlas_COW_Page;

/** COW page flags */
#define SERAPH_ATLAS_COW_VALID       0x0001  /**< COW entry is valid */
#define SERAPH_ATLAS_COW_DIRTY       0x0002  /**< Page was dirty at snapshot */
#define SERAPH_ATLAS_COW_COMPRESSED  0x0004  /**< COW data is compressed */
#define SERAPH_ATLAS_COW_GENESIS     0x0008  /**< Contains Genesis metadata */

/**
 * @brief Causal snapshot structure
 *
 * A causal snapshot captures a consistent view of Atlas state at a specific
 * point in logical time, as defined by a vector clock. The snapshot respects
 * causality: if A -> B and B is in the snapshot, then A is also in the snapshot.
 *
 * STRUCTURE LAYOUT:
 * +------------------+
 * | Header           | - Magic, version, ID, state
 * +------------------+
 * | Vector Clock     | - Causality context (node_count timestamps)
 * +------------------+
 * | Page Tracking    | - Which pages are included
 * +------------------+
 * | COW Pages        | - Original page data for modified pages
 * +------------------+
 * | Genesis Copy     | - Snapshot of Genesis at capture time
 * +------------------+
 */
struct Seraph_Atlas_Snapshot {
    /*--- Header ---*/
    uint64_t magic;                     /**< SERAPH_ATLAS_SNAPSHOT_MAGIC */
    uint32_t version;                   /**< Snapshot format version */
    Seraph_Atlas_Snap_State state;      /**< Current snapshot state */
    uint64_t snapshot_id;               /**< Unique snapshot identifier */

    /*--- Temporal Context ---*/
    Seraph_Chronon timestamp;           /**< Logical timestamp at snapshot */
    Seraph_Chronon wall_clock;          /**< Wall clock time (for debugging) */
    uint64_t generation;                /**< Atlas generation at snapshot */
    uint64_t epoch;                     /**< Epoch counter at snapshot */

    /*--- Vector Clock (Causality Tracking) ---*/
    uint32_t vclock_node_count;         /**< Number of nodes in vector clock */
    uint32_t vclock_self_id;            /**< This node's ID */
    Seraph_Chronon vclock[SERAPH_ATLAS_VCLOCK_MAX_NODES]; /**< Vector clock state */

    /*--- Page Tracking ---*/
    uint64_t included_pages;            /**< Bitmap of explicitly included pages */
    uint32_t total_page_count;          /**< Total pages in Atlas at snapshot */
    uint32_t included_page_count;       /**< Number of pages in snapshot */

    /*--- Copy-on-Write State ---*/
    Seraph_Atlas_COW_Page cow_pages[SERAPH_ATLAS_SNAPSHOT_MAX_PAGES];
    uint32_t cow_page_count;            /**< Number of COW pages */
    uint64_t cow_storage_offset;        /**< Where COW data is stored in Atlas */
    uint64_t cow_storage_size;          /**< Total size of COW storage used */

    /*--- Genesis Snapshot ---*/
    Seraph_Atlas_Genesis genesis_copy;  /**< Copy of Genesis at snapshot time */

    /*--- Metadata ---*/
    uint64_t creation_time;             /**< When snapshot was created */
    uint64_t commit_time;               /**< When snapshot was committed (0 if not) */
    char description[64];               /**< Optional description */

};

/** VOID Snapshot constant */
#define SERAPH_ATLAS_SNAPSHOT_VOID ((Seraph_Atlas_Snapshot){ \
    .magic = 0, \
    .state = SERAPH_ATLAS_SNAP_VOID, \
    .snapshot_id = SERAPH_VOID_U64 \
})

/*============================================================================
 * Causal Snapshot API
 *
 * The snapshot API provides four main operations:
 *
 *   1. BEGIN   - Start a new snapshot, capture vector clock
 *   2. INCLUDE - Add specific pages/regions to the snapshot
 *   3. COMMIT  - Finalize the snapshot, make it persistent
 *   4. RESTORE - Roll back Atlas to a previous snapshot
 *
 * USAGE PATTERN:
 *
 *   // Create a snapshot
 *   Seraph_Atlas_Snapshot* snap = seraph_atlas_snapshot_begin(atlas, vclock);
 *   seraph_atlas_snapshot_include(snap, data_ptr, data_size);
 *   seraph_atlas_snapshot_commit(atlas, snap);
 *
 *   // ... later, restore to that snapshot
 *   seraph_atlas_snapshot_restore(atlas, snap);
 *
 * CAUSALITY GUARANTEES:
 *
 *   The vector clock parameter to snapshot_begin() defines the causality
 *   context. Only changes that happened-before the vector clock are
 *   guaranteed to be in the snapshot. Concurrent changes may or may not
 *   be included, but the snapshot will always be consistent.
 *
 * COPY-ON-WRITE:
 *
 *   While a snapshot is ACTIVE, any writes to included pages trigger COW:
 *   the original page data is copied before the write proceeds. This
 *   allows the live Atlas to continue operating while the snapshot
 *   preserves a frozen view.
 *============================================================================*/

/**
 * @brief Begin a new causal snapshot
 *
 * Starts the snapshot creation process by capturing the current causality
 * context (vector clock) and preparing for page inclusion. The snapshot
 * starts in PREPARING state.
 *
 * CAUSALITY SEMANTICS:
 *   The snapshot will include all changes that causally precede the
 *   provided vector clock. If vclock is NULL, the current system time
 *   is used (suitable for single-node operation).
 *
 * @param atlas     The Atlas instance
 * @param vclock    Vector clock defining causality context (NULL for current)
 * @return Pointer to new snapshot, or NULL on failure
 *
 * @note The returned snapshot must be either committed or aborted.
 *       Failing to do so will leak resources.
 */
Seraph_Atlas_Snapshot* seraph_atlas_snapshot_begin(
    Seraph_Atlas* atlas,
    const Seraph_VectorClock* vclock
);

/**
 * @brief Include pages in the snapshot
 *
 * Adds the specified memory region to the snapshot. The pages covering
 * this region will be tracked for copy-on-write. Any modifications to
 * these pages after this call will preserve the original data in the
 * snapshot.
 *
 * PAGE ALIGNMENT:
 *   The region is automatically expanded to page boundaries. For example,
 *   including bytes 100-200 on a 4KB page system will include the entire
 *   first page (bytes 0-4095).
 *
 * MULTIPLE CALLS:
 *   This function can be called multiple times to include non-contiguous
 *   regions. Previously included pages are not affected.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot being prepared
 * @param ptr       Start of region to include (must be in Atlas)
 * @param size      Size of region to include
 * @return TRUE on success, FALSE if max pages exceeded, VOID on error
 *
 * @note Snapshot must be in PREPARING state.
 */
Seraph_Vbit seraph_atlas_snapshot_include(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot,
    const void* ptr,
    size_t size
);

/**
 * @brief Include all pages in the snapshot
 *
 * Convenience function to include the entire Atlas region in the snapshot.
 * Equivalent to calling snapshot_include with the full Atlas range.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot being prepared
 * @return TRUE on success, VOID on error
 */
Seraph_Vbit seraph_atlas_snapshot_include_all(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
);

/**
 * @brief Activate the snapshot for copy-on-write
 *
 * Transitions the snapshot from PREPARING to ACTIVE state. Once active,
 * any writes to included pages will trigger copy-on-write: the original
 * page data is preserved in the snapshot before the write proceeds.
 *
 * IMPORTANT:
 *   After activation, no more pages can be added via snapshot_include.
 *   Make sure all desired regions are included before activation.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot to activate
 * @return TRUE on success, FALSE if already active, VOID on error
 */
Seraph_Vbit seraph_atlas_snapshot_activate(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
);

/**
 * @brief Commit the snapshot to persistent storage
 *
 * Finalizes the snapshot and writes it to the Atlas backing store.
 * After commit, the snapshot can be used for restore operations even
 * after system restart.
 *
 * ATOMICITY:
 *   The commit operation is atomic - either the entire snapshot is
 *   persisted or none of it is. Partial commits are not possible.
 *
 * VECTOR CLOCK UPDATE:
 *   Committing a snapshot increments the local component of the
 *   vector clock (if provided), establishing a happens-before
 *   relationship with future operations.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot to commit
 * @return TRUE on success, FALSE if already committed, VOID on error
 *
 * @note After commit, the snapshot transitions to COMMITTED state.
 */
Seraph_Vbit seraph_atlas_snapshot_commit(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
);

/**
 * @brief Abort a snapshot
 *
 * Discards a snapshot that is being prepared or is active. All COW
 * pages are freed and the snapshot slot becomes available for reuse.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot to abort
 *
 * @note This is safe to call on any non-committed snapshot.
 */
void seraph_atlas_snapshot_abort(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
);

/**
 * @brief Restore Atlas to a snapshot state
 *
 * Rolls back the Atlas to the state captured in the snapshot. This
 * operation:
 *
 *   1. Restores Genesis to snapshot's Genesis copy
 *   2. Restores all COW pages to their original data
 *   3. Increments generation to invalidate stale capabilities
 *   4. Updates vector clock to reflect the restore operation
 *
 * CAUSALITY IMPLICATIONS:
 *   Restoring a snapshot creates a new causal branch. The vector
 *   clock is updated to reflect that this is a new timeline that
 *   happens-after both the original snapshot and the current state.
 *
 * WARNING:
 *   This is a destructive operation - all changes made after the
 *   snapshot was taken will be lost. Active transactions will be
 *   aborted.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot to restore (must be COMMITTED)
 * @return TRUE on success, FALSE if cannot restore, VOID on error
 */
Seraph_Vbit seraph_atlas_snapshot_restore(
    Seraph_Atlas* atlas,
    const Seraph_Atlas_Snapshot* snapshot
);

/**
 * @brief Check if a snapshot causally precedes another
 *
 * Uses vector clock comparison to determine if snapshot A's state
 * causally precedes snapshot B's state.
 *
 * CAUSAL ORDERING:
 *   - BEFORE:     A -> B (A happened before B)
 *   - AFTER:      B -> A (B happened before A)
 *   - CONCURRENT: A || B (neither ordered - parallel timelines)
 *   - EQUAL:      A == B (same point in time)
 *   - VOID:       Cannot determine (invalid input)
 *
 * @param a First snapshot
 * @param b Second snapshot
 * @return Causal ordering relationship
 */
Seraph_CausalOrder seraph_atlas_snapshot_compare(
    const Seraph_Atlas_Snapshot* a,
    const Seraph_Atlas_Snapshot* b
);

/**
 * @brief Get snapshot by ID
 *
 * Retrieves a snapshot by its unique identifier. Returns NULL if
 * no snapshot with that ID exists.
 *
 * @param atlas       The Atlas instance
 * @param snapshot_id The snapshot ID to find
 * @return Pointer to snapshot, or NULL if not found
 */
Seraph_Atlas_Snapshot* seraph_atlas_snapshot_get(
    Seraph_Atlas* atlas,
    uint64_t snapshot_id
);

/**
 * @brief List all committed snapshots
 *
 * Fills the provided array with IDs of all committed snapshots,
 * ordered by snapshot time (oldest first).
 *
 * @param atlas   The Atlas instance
 * @param ids     Array to fill with snapshot IDs
 * @param max_ids Maximum number of IDs to return
 * @return Number of snapshot IDs written, or 0 if none
 */
uint32_t seraph_atlas_snapshot_list(
    Seraph_Atlas* atlas,
    uint64_t* ids,
    uint32_t max_ids
);

/**
 * @brief Delete a committed snapshot
 *
 * Removes a snapshot and frees its COW storage. The snapshot must
 * be in COMMITTED state. Active snapshots cannot be deleted (abort
 * them instead).
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot to delete
 * @return TRUE on success, FALSE if in use, VOID on error
 */
Seraph_Vbit seraph_atlas_snapshot_delete(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot
);

/**
 * @brief Check if a snapshot is valid
 *
 * Validates snapshot structure integrity including magic number,
 * version, and internal consistency checks.
 *
 * @param snapshot The snapshot to validate
 * @return TRUE if valid, FALSE if invalid, VOID if NULL
 */
Seraph_Vbit seraph_atlas_snapshot_is_valid(
    const Seraph_Atlas_Snapshot* snapshot
);

/**
 * @brief Trigger copy-on-write for a page
 *
 * Internal function called when a write occurs to a page that is
 * included in an active snapshot. Copies the original page data
 * to COW storage before allowing the write to proceed.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The active snapshot
 * @param page_ptr  Pointer to the page being written
 * @return TRUE if COW succeeded, FALSE if already copied, VOID on error
 *
 * @note This is typically called automatically by the write path.
 */
Seraph_Vbit seraph_atlas_snapshot_cow_page(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Snapshot* snapshot,
    void* page_ptr
);

/**
 * @brief Get the original page data from a snapshot
 *
 * Retrieves the page data as it was at snapshot time. If the page
 * was modified after the snapshot, returns the COW copy. If the
 * page was not modified, returns the current data.
 *
 * @param atlas     The Atlas instance
 * @param snapshot  The snapshot to read from
 * @param page_ptr  Pointer to the page (in current Atlas)
 * @return Pointer to original page data, or NULL if not in snapshot
 */
const void* seraph_atlas_snapshot_read_page(
    const Seraph_Atlas* atlas,
    const Seraph_Atlas_Snapshot* snapshot,
    const void* page_ptr
);

/*============================================================================
 * Semantic Checkpointing - Invariant Types
 *
 * SEMANTIC CHECKPOINTS go beyond raw byte snapshots to understand the
 * semantic meaning of data structures. They can:
 *
 *   1. Validate data structure invariants on restore
 *   2. Detect corruption that byte-level checks would miss
 *   3. Automatically repair certain types of corruption
 *   4. Provide detailed validation reports
 *
 * INVARIANT TYPES:
 *   - NULL_PTR:      Required pointer must not be NULL
 *   - NULLABLE_PTR:  Optional pointer, valid if non-NULL
 *   - NO_CYCLE:      No cycles in linked structures
 *   - ARRAY_BOUNDS:  Array indices stay within bounds
 *   - REFCOUNT:      Reference counts are valid
 *   - RANGE:         Numeric values within specified range
 *   - CUSTOM:        User-defined validation logic
 *
 * RECOVERY:
 *   Each invariant type has associated recovery logic that can
 *   automatically repair certain violations. For example:
 *   - NULLABLE_PTR violation: Set to NULL
 *   - RANGE violation: Clamp to valid range
 *   - CYCLE detected: Break cycle at last link
 *============================================================================*/

/**
 * @brief Types of semantic invariants for data structure validation
 *
 * These invariants go beyond raw byte snapshots to understand the semantic
 * meaning of data structures. Each invariant type has specific validation
 * logic that can detect corruption and (in some cases) perform automatic repair.
 */
typedef enum {
    /**
     * @brief Field must not be NULL
     *
     * Use for required pointers that must always point to valid data.
     * Recovery: Can orphan the structure or set to default value.
     */
    SERAPH_ATLAS_INVARIANT_NULL_PTR = 0,

    /**
     * @brief Field may be NULL but must be valid if non-NULL
     *
     * Use for optional pointers. If non-NULL, must point within Atlas.
     * Recovery: Set to NULL if pointing outside Atlas.
     */
    SERAPH_ATLAS_INVARIANT_NULLABLE_PTR = 1,

    /**
     * @brief No cycles allowed in linked structure via this field
     *
     * Uses Floyd's Tortoise and Hare algorithm for O(n) detection.
     * Recovery: Break cycle at the last link detected.
     */
    SERAPH_ATLAS_INVARIANT_NO_CYCLE = 2,

    /**
     * @brief Array bounds must not exceed allocation
     *
     * Validates that array access doesn't go beyond allocated memory.
     * Requires specifying count field offset.
     * Recovery: Truncate count to valid range.
     */
    SERAPH_ATLAS_INVARIANT_ARRAY_BOUNDS = 3,

    /**
     * @brief Reference count must be valid (>= 0 or >= 1 for live)
     *
     * Validates reference counting invariants.
     * Recovery: Reset to 1 or recalculate from incoming references.
     */
    SERAPH_ATLAS_INVARIANT_REFCOUNT = 4,

    /**
     * @brief Numeric value must be within specified range [min, max]
     *
     * General-purpose range validation for integers.
     * Recovery: Clamp to nearest valid value.
     */
    SERAPH_ATLAS_INVARIANT_RANGE = 5,

    /**
     * @brief Custom user-defined validation function
     *
     * Allows application-specific invariant checking.
     * Recovery: Determined by custom recovery function.
     */
    SERAPH_ATLAS_INVARIANT_CUSTOM = 6
} Seraph_Atlas_Invariant_Type;

/**
 * @brief Validation result codes
 */
typedef enum {
    SERAPH_ATLAS_VALIDATE_OK = 0,           /**< Invariant satisfied */
    SERAPH_ATLAS_VALIDATE_NULL_VIOLATION,   /**< NULL where not allowed */
    SERAPH_ATLAS_VALIDATE_INVALID_PTR,      /**< Pointer outside Atlas */
    SERAPH_ATLAS_VALIDATE_CYCLE_DETECTED,   /**< Cycle found in structure */
    SERAPH_ATLAS_VALIDATE_BOUNDS_EXCEEDED,  /**< Array bounds violation */
    SERAPH_ATLAS_VALIDATE_REFCOUNT_INVALID, /**< Invalid reference count */
    SERAPH_ATLAS_VALIDATE_RANGE_EXCEEDED,   /**< Value out of range */
    SERAPH_ATLAS_VALIDATE_CUSTOM_FAILED,    /**< Custom validator failed */
    SERAPH_ATLAS_VALIDATE_ERROR             /**< General validation error */
} Seraph_Atlas_Validate_Result;

/*============================================================================
 * Semantic Checkpointing - Invariant Definition
 *============================================================================*/

/**
 * @brief Custom validation function signature
 *
 * @param atlas The Atlas instance (for pointer validation)
 * @param data Pointer to the structure being validated
 * @param field_offset Offset of field within structure
 * @param field_size Size of the field
 * @param user_data Optional user-provided context
 * @return SERAPH_ATLAS_VALIDATE_OK on success, error code otherwise
 */
typedef Seraph_Atlas_Validate_Result (*Seraph_Atlas_Validator_Fn)(
    const Seraph_Atlas* atlas,
    const void* data,
    size_t field_offset,
    size_t field_size,
    void* user_data
);

/**
 * @brief Custom recovery function signature
 *
 * @param atlas The Atlas instance
 * @param data Pointer to the structure to repair
 * @param field_offset Offset of field within structure
 * @param field_size Size of the field
 * @param violation_type The type of violation detected
 * @param user_data Optional user-provided context
 * @return true if recovery succeeded, false if unrecoverable
 */
typedef bool (*Seraph_Atlas_Recovery_Fn)(
    Seraph_Atlas* atlas,
    void* data,
    size_t field_offset,
    size_t field_size,
    Seraph_Atlas_Validate_Result violation_type,
    void* user_data
);

/**
 * @brief Single invariant definition for a data structure field
 *
 * An invariant defines a semantic constraint on a field within a structure.
 * Multiple invariants can be applied to the same structure type.
 */
typedef struct {
    /** Type of invariant (determines validation logic) */
    Seraph_Atlas_Invariant_Type type;

    /** Offset of the field within the structure (from struct base) */
    size_t field_offset;

    /** Size of the field in bytes */
    size_t field_size;

    /**
     * @brief Type-specific parameters
     *
     * Different invariant types use different parameters:
     * - RANGE: min/max values
     * - ARRAY_BOUNDS: offset of count field, element size
     * - NO_CYCLE: offset of next pointer field
     * - CUSTOM: validator and recovery functions
     */
    union {
        /** Parameters for RANGE invariant */
        struct {
            int64_t min;            /**< Minimum allowed value (inclusive) */
            int64_t max;            /**< Maximum allowed value (inclusive) */
        } range;

        /** Parameters for ARRAY_BOUNDS invariant */
        struct {
            size_t count_offset;    /**< Offset of count field in structure */
            size_t element_size;    /**< Size of each array element */
            size_t max_count;       /**< Maximum allowed count (0 = no limit) */
        } array;

        /** Parameters for NO_CYCLE invariant (linked structure) */
        struct {
            size_t next_offset;     /**< Offset of 'next' pointer in structure */
        } cycle;

        /** Parameters for REFCOUNT invariant */
        struct {
            int64_t min_count;      /**< Minimum valid refcount (usually 0 or 1) */
            bool live_only;         /**< If true, only validate live objects (refcount >= 1) */
        } refcount;

        /** Parameters for CUSTOM invariant */
        struct {
            Seraph_Atlas_Validator_Fn validator;    /**< Custom validation function */
            Seraph_Atlas_Recovery_Fn recovery;      /**< Custom recovery function */
            void* user_data;                        /**< User context passed to functions */
        } custom;
    } params;

    /** Human-readable description of this invariant */
    const char* description;

    /** Can this invariant be automatically repaired? */
    bool auto_recoverable;
} Seraph_Atlas_Invariant;

/*============================================================================
 * Semantic Checkpointing - Type Information
 *============================================================================*/

/**
 * @brief Type information for semantic checkpointing
 *
 * Describes a data structure type with its invariants. Types are registered
 * globally and can be referenced by multiple checkpoints.
 */
typedef struct {
    /** Unique type identifier (assigned during registration) */
    uint32_t type_id;

    /** Human-readable type name (e.g., "LinkedList", "BTree") */
    const char* name;

    /** Size of a single instance of this type in bytes */
    size_t instance_size;

    /** Number of invariants defined for this type */
    uint32_t invariant_count;

    /** Array of invariants for this type */
    Seraph_Atlas_Invariant invariants[SERAPH_ATLAS_MAX_INVARIANTS];

    /** Is this type registered and active? */
    bool registered;

    /** Optional type-level validation (validates entire instance, not just fields) */
    Seraph_Atlas_Validator_Fn instance_validator;

    /** Optional type-level recovery function */
    Seraph_Atlas_Recovery_Fn instance_recovery;

    /** User context for type-level functions */
    void* user_data;
} Seraph_Atlas_Type_Info;

/*============================================================================
 * Semantic Checkpointing - Checkpoint Entry
 *============================================================================*/

/**
 * @brief Single entry in a checkpoint (links pointer to type)
 *
 * Each entry tracks one data structure instance in the checkpoint.
 */
typedef struct {
    /** Pointer to the data structure instance */
    void* ptr;

    /** Atlas offset of this pointer (for persistence) */
    uint64_t offset;

    /** Type ID of this entry (references registered type) */
    uint32_t type_id;

    /** Size of this allocation (may differ from type instance_size for arrays) */
    size_t alloc_size;

    /** CRC32 checksum of data at checkpoint creation time */
    uint32_t checksum;

    /** Entry flags */
    uint32_t flags;

    /** Number of validation errors found for this entry */
    uint32_t error_count;

    /** Last validation result (for first failed invariant) */
    Seraph_Atlas_Validate_Result last_result;
} Seraph_Atlas_Checkpoint_Entry;

/** Entry flag: Entry has been modified since checkpoint creation */
#define SERAPH_ATLAS_ENTRY_MODIFIED     0x0001

/** Entry flag: Entry failed validation */
#define SERAPH_ATLAS_ENTRY_INVALID      0x0002

/** Entry flag: Entry was recovered/repaired */
#define SERAPH_ATLAS_ENTRY_RECOVERED    0x0004

/** Entry flag: Entry is the root of a structure graph */
#define SERAPH_ATLAS_ENTRY_ROOT         0x0008

/*============================================================================
 * Semantic Checkpointing - Checkpoint Structure
 *============================================================================*/

/**
 * @brief Semantic checkpoint structure
 *
 * A checkpoint captures not just the raw bytes of data but also the semantic
 * type information needed to validate correctness on restore.
 */
typedef struct {
    /** Magic number for validation */
    uint64_t magic;

    /** Unique checkpoint ID */
    uint64_t checkpoint_id;

    /** Atlas generation when checkpoint was created */
    uint64_t generation;

    /** Chronon timestamp of creation */
    Seraph_Chronon created_at;

    /** Number of entries in this checkpoint */
    uint32_t entry_count;

    /** Maximum capacity of entries array */
    uint32_t max_entries;

    /** Pointer to entries array (allocated within Atlas) */
    Seraph_Atlas_Checkpoint_Entry* entries;

    /** Offset of entries array in Atlas (for persistence) */
    uint64_t entries_offset;

    /** Has this checkpoint been validated since creation/restore? */
    bool validated;

    /** Total errors found during last validation */
    uint32_t total_errors;

    /** Total recoveries performed during last recover operation */
    uint32_t total_recoveries;

    /** Checkpoint flags */
    uint32_t flags;

    /** User-provided checkpoint name/description */
    char name[64];
} Seraph_Atlas_Checkpoint;

/** Checkpoint flag: Checkpoint is persistent (stored in Atlas) */
#define SERAPH_ATLAS_CKPT_PERSISTENT    0x0001

/** Checkpoint flag: Auto-validate on restore */
#define SERAPH_ATLAS_CKPT_AUTO_VALIDATE 0x0002

/** Checkpoint flag: Auto-recover on validation failure */
#define SERAPH_ATLAS_CKPT_AUTO_RECOVER  0x0004

/*============================================================================
 * Semantic Checkpointing - Validation Report
 *============================================================================*/

/**
 * @brief Detailed validation result for a single invariant check
 */
typedef struct {
    /** Entry index in checkpoint */
    uint32_t entry_index;

    /** Invariant index within type */
    uint32_t invariant_index;

    /** Validation result code */
    Seraph_Atlas_Validate_Result result;

    /** Type ID of the entry */
    uint32_t type_id;

    /** Offset of field that failed validation */
    size_t field_offset;

    /** Was recovery attempted? */
    bool recovery_attempted;

    /** Did recovery succeed? */
    bool recovery_succeeded;
} Seraph_Atlas_Validation_Detail;

/**
 * @brief Complete validation report for a checkpoint
 */
typedef struct {
    /** Checkpoint that was validated */
    uint64_t checkpoint_id;

    /** Total entries validated */
    uint32_t entries_validated;

    /** Total entries that passed all invariants */
    uint32_t entries_passed;

    /** Total entries that failed at least one invariant */
    uint32_t entries_failed;

    /** Total individual invariant checks performed */
    uint32_t invariants_checked;

    /** Total invariant checks that passed */
    uint32_t invariants_passed;

    /** Total invariant checks that failed */
    uint32_t invariants_failed;

    /** Total recoveries attempted */
    uint32_t recoveries_attempted;

    /** Total recoveries that succeeded */
    uint32_t recoveries_succeeded;

    /** Array of detailed results (NULL if details not requested) */
    Seraph_Atlas_Validation_Detail* details;

    /** Number of detail entries */
    uint32_t detail_count;

    /** Maximum detail entries allocated */
    uint32_t max_details;

    /** Overall validation passed (no failures or all recovered) */
    bool passed;
} Seraph_Atlas_Validation_Report;

/*============================================================================
 * Semantic Checkpointing - Type Registration API
 *============================================================================*/

/**
 * @brief Register a new data structure type for semantic checkpointing
 *
 * Types must be registered before they can be used in checkpoints.
 * Each type defines its size and a set of semantic invariants that
 * must hold for all instances of that type.
 *
 * @param name Human-readable type name (copied internally)
 * @param instance_size Size of a single instance in bytes
 * @return Type ID on success, SERAPH_VOID_U32 on failure
 *
 * @example
 * uint32_t list_type = seraph_atlas_checkpoint_register_type(
 *     "LinkedListNode", sizeof(LinkedListNode));
 */
uint32_t seraph_atlas_checkpoint_register_type(
    const char* name,
    size_t instance_size
);

/**
 * @brief Add an invariant to a registered type
 *
 * @param type_id Type ID returned from register_type
 * @param invariant Pointer to invariant definition (copied internally)
 * @return true on success, false if type not found or invariant limit reached
 */
bool seraph_atlas_checkpoint_add_invariant(
    uint32_t type_id,
    const Seraph_Atlas_Invariant* invariant
);

/**
 * @brief Set type-level validator and recovery functions
 *
 * These are called after all field-level invariants for comprehensive validation.
 *
 * @param type_id Type ID
 * @param validator Optional type-level validation function
 * @param recovery Optional type-level recovery function
 * @param user_data User context passed to functions
 * @return true on success, false if type not found
 */
bool seraph_atlas_checkpoint_set_type_validator(
    uint32_t type_id,
    Seraph_Atlas_Validator_Fn validator,
    Seraph_Atlas_Recovery_Fn recovery,
    void* user_data
);

/**
 * @brief Get type information by ID
 *
 * @param type_id Type ID
 * @return Pointer to type info, or NULL if not found
 */
const Seraph_Atlas_Type_Info* seraph_atlas_checkpoint_get_type(uint32_t type_id);

/**
 * @brief Find type by name
 *
 * @param name Type name to search for
 * @return Type ID, or SERAPH_VOID_U32 if not found
 */
uint32_t seraph_atlas_checkpoint_find_type(const char* name);

/*============================================================================
 * Semantic Checkpointing - Checkpoint API
 *============================================================================*/

/**
 * @brief Create a new semantic checkpoint
 *
 * Creates a checkpoint structure that can track data structure instances
 * with their associated types and invariants.
 *
 * @param atlas The Atlas instance
 * @param name Optional checkpoint name (NULL for auto-generated)
 * @param max_entries Maximum entries to track (0 = use default)
 * @param flags Checkpoint flags (SERAPH_ATLAS_CKPT_*)
 * @return Pointer to checkpoint, or NULL on failure
 */
Seraph_Atlas_Checkpoint* seraph_atlas_checkpoint_create(
    Seraph_Atlas* atlas,
    const char* name,
    uint32_t max_entries,
    uint32_t flags
);

/**
 * @brief Add an entry to a checkpoint
 *
 * Registers a data structure instance for tracking. The instance's
 * type must have been previously registered.
 *
 * @param atlas The Atlas instance
 * @param checkpoint The checkpoint to add to
 * @param ptr Pointer to data structure instance (must be within Atlas)
 * @param type_id Type ID of this instance
 * @param alloc_size Allocation size (0 = use type instance_size)
 * @param flags Entry flags (SERAPH_ATLAS_ENTRY_*)
 * @return true on success, false on failure
 */
bool seraph_atlas_checkpoint_add_entry(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint,
    void* ptr,
    uint32_t type_id,
    size_t alloc_size,
    uint32_t flags
);

/**
 * @brief Validate all entries in a checkpoint against their invariants
 *
 * Performs semantic validation of all tracked data structures.
 * This checks that all registered invariants hold for each entry.
 *
 * @param atlas The Atlas instance
 * @param checkpoint The checkpoint to validate
 * @param report Optional report structure to fill with details (may be NULL)
 * @return SERAPH_VBIT_TRUE if all valid, SERAPH_VBIT_FALSE if errors found,
 *         SERAPH_VBIT_VOID on error
 */
Seraph_Vbit seraph_atlas_checkpoint_validate(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint,
    Seraph_Atlas_Validation_Report* report
);

/**
 * @brief Recover/repair checkpoint entries with automatic repair
 *
 * Attempts to automatically repair validation failures using
 * type-specific recovery strategies. Only invariants marked as
 * auto_recoverable will be repaired.
 *
 * @param atlas The Atlas instance
 * @param checkpoint The checkpoint to recover
 * @param report Optional report structure to fill with details (may be NULL)
 * @return SERAPH_VBIT_TRUE if fully recovered, SERAPH_VBIT_FALSE if partial,
 *         SERAPH_VBIT_VOID on error
 */
Seraph_Vbit seraph_atlas_checkpoint_recover(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint,
    Seraph_Atlas_Validation_Report* report
);

/**
 * @brief Destroy a checkpoint and free its resources
 *
 * @param atlas The Atlas instance
 * @param checkpoint The checkpoint to destroy
 */
void seraph_atlas_checkpoint_destroy(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Checkpoint* checkpoint
);

/**
 * @brief Free a validation report's detail array
 *
 * @param atlas The Atlas instance (for memory deallocation)
 * @param report The report to free
 */
void seraph_atlas_validation_report_free(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Validation_Report* report
);

/*============================================================================
 * Semantic Checkpointing - Convenience Invariant Builders
 *============================================================================*/

/**
 * @brief Create a non-NULL pointer invariant
 *
 * @param field_offset Offset of pointer field in structure
 * @param description Human-readable description
 * @return Configured invariant structure
 */
Seraph_Atlas_Invariant seraph_atlas_invariant_not_null(
    size_t field_offset,
    const char* description
);

/**
 * @brief Create a nullable pointer invariant (valid if non-NULL)
 *
 * @param field_offset Offset of pointer field in structure
 * @param description Human-readable description
 * @return Configured invariant structure
 */
Seraph_Atlas_Invariant seraph_atlas_invariant_nullable(
    size_t field_offset,
    const char* description
);

/**
 * @brief Create a no-cycle invariant for linked structures
 *
 * @param next_field_offset Offset of 'next' pointer in structure
 * @param description Human-readable description
 * @return Configured invariant structure
 */
Seraph_Atlas_Invariant seraph_atlas_invariant_no_cycle(
    size_t next_field_offset,
    const char* description
);

/**
 * @brief Create an array bounds invariant
 *
 * @param array_field_offset Offset of array pointer field
 * @param count_field_offset Offset of count field
 * @param element_size Size of each array element
 * @param max_count Maximum allowed count (0 = no limit)
 * @param description Human-readable description
 * @return Configured invariant structure
 */
Seraph_Atlas_Invariant seraph_atlas_invariant_array_bounds(
    size_t array_field_offset,
    size_t count_field_offset,
    size_t element_size,
    size_t max_count,
    const char* description
);

/**
 * @brief Create a reference count invariant
 *
 * @param refcount_offset Offset of refcount field in structure
 * @param min_count Minimum valid refcount (usually 0 or 1)
 * @param live_only If true, only validate objects with refcount >= 1
 * @param description Human-readable description
 * @return Configured invariant structure
 */
Seraph_Atlas_Invariant seraph_atlas_invariant_refcount(
    size_t refcount_offset,
    int64_t min_count,
    bool live_only,
    const char* description
);

/**
 * @brief Create a numeric range invariant
 *
 * @param field_offset Offset of numeric field in structure
 * @param field_size Size of the field (1, 2, 4, or 8 bytes)
 * @param min Minimum allowed value (inclusive)
 * @param max Maximum allowed value (inclusive)
 * @param description Human-readable description
 * @return Configured invariant structure
 */
Seraph_Atlas_Invariant seraph_atlas_invariant_range(
    size_t field_offset,
    size_t field_size,
    int64_t min,
    int64_t max,
    const char* description
);

/**
 * @brief Create a custom invariant with user-defined validation
 *
 * @param field_offset Offset of field to validate
 * @param field_size Size of field
 * @param validator Custom validation function
 * @param recovery Custom recovery function (NULL if not recoverable)
 * @param user_data User context passed to functions
 * @param description Human-readable description
 * @return Configured invariant structure
 */
Seraph_Atlas_Invariant seraph_atlas_invariant_custom(
    size_t field_offset,
    size_t field_size,
    Seraph_Atlas_Validator_Fn validator,
    Seraph_Atlas_Recovery_Fn recovery,
    void* user_data,
    const char* description
);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_ATLAS_H */
