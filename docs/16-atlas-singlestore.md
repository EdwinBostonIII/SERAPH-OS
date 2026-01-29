# MC27: Atlas - The Single-Level Store

## Overview

Atlas is SERAPH's revolutionary storage system that eliminates the distinction between RAM and disk. There is no file system. There are no file operations. There are only **pointers that persist**.

**"There is no disk. There is no file system. There is only memory that remembers."**

## Plain English Explanation

Imagine you want to save your application's state:

**Traditional Approach (Files):**
1. Create a JSON/XML/binary serializer
2. Walk your entire data structure
3. Convert pointers to IDs
4. Write to a file
5. On load: parse the file, recreate objects, convert IDs back to pointers
6. Pray nothing changed in between

**Atlas Approach:**
1. Your data is already persistent
2. On restart: read the Genesis pointer
3. Your entire data graph is exactly where you left it
4. No serialization. No parsing. Just pointers.

The key insight: **Don't serialize data. Persist memory itself.**

## Architecture

### Address Space Layout

```
VIRTUAL ADDRESS SPACE                    PHYSICAL STORAGE
─────────────────────                    ────────────────────

0x0000_0000_0000_0000  ┌────────────────┐
        ↓              │    VOLATILE    │  ← RAM (fast, temporary)
0x0000_7FFF_FFFF_FFFF  │   SUBSTRATE    │
────────────────────── └────────────────┘

0x0000_8000_0000_0000  ┌────────────────┐  ┌─────────────────┐
        ↓              │     ATLAS      │◀─▶│   NVMe Drive    │
0x0000_BFFF_FFFF_FFFF  │   SUBSTRATE    │  │  (persistent)   │
────────────────────── └────────────────┘  └─────────────────┘

0x0000_C000_0000_0000  ┌────────────────┐
        ↓              │    AETHER      │  ← Network (see MC28)
0x0000_FFFF_FFFF_FFFF  │   SUBSTRATE    │
                       └────────────────┘
```

### The Genesis Pointer

At the base of Atlas sits ONE pointer: **Genesis**. Genesis points to the root of ALL persistent data. Everything reachable from Genesis persists. Everything else doesn't.

```c
/* The Genesis structure */
typedef struct Seraph_Atlas_Genesis {
    uint64_t magic;           /* SERAPH_ATLAS_MAGIC */
    uint64_t version;         /* Format version */
    uint64_t generation;      /* For capability revocation */
    uint64_t root_offset;     /* Offset to application root */
    uint64_t free_list_offset;
    uint64_t gen_table_offset;
    uint64_t next_alloc_offset;
    /* Statistics and timestamps... */
} Seraph_Atlas_Genesis;
```

This is **simpler** than a file system:
- No file names to manage
- No directories to navigate
- No path resolution
- No permissions per file (capabilities cover everything)

This is **more powerful** than a file system:
- Arbitrary graph structures (not just trees)
- Pointer consistency guaranteed
- Atomic updates to entire data structure
- O(1) "find" for any data (if you have a capability)

## Copy-on-Write Transactions

Atlas provides ACID transactions without a transaction log:

- **Atomicity**: Commit is a single pointer swap
- **Consistency**: Invariants checked before commit
- **Isolation**: Copy-on-write provides snapshot isolation
- **Durability**: Committed data is on NVMe

```c
/* Begin a transaction */
Seraph_Atlas_Transaction* tx = seraph_atlas_begin(&atlas);

/* Modify data... */
void* data = seraph_atlas_alloc(&atlas, 1024);
/* write to data... */

/* Commit atomically */
Seraph_Vbit result = seraph_atlas_commit(&atlas, tx);
if (seraph_vbit_is_true(result)) {
    /* Data is now persistent */
}
```

### Why Copy-on-Write Guarantees Crash Safety

```
SCENARIO: Application crashes mid-transaction

TIME        GENESIS     OLD DATA       NEW DATA        STATE
────        ───────     ────────       ────────        ─────

T₀          → [A]       A: "hello"     (none)          Committed: "hello"

T₁ BEGIN    → [A]       A: "hello"     (none)          Tx starts

T₂ WRITE    → [A]       A: "hello"     A': "world"     Tx writes to COPY
            (unchanged) (unchanged)    (new page)      Old data intact

T₃ ⚡CRASH   → [A]       A: "hello"     A': "world"     CRASH!
                                       (orphaned)

T₄ REBOOT   → [A]       A: "hello"     (GC'd)          Recovered: "hello"
            (still valid!)              O(1) recovery
```

**The Guarantee:** Genesis ALWAYS points to a consistent, committed state.

## Capability Persistence

Atlas capabilities survive reboots. If a capability is revoked, it stays revoked even after power loss.

```c
/* Allocate a generation ID */
uint64_t alloc_id = seraph_atlas_alloc_generation(&atlas);

/* Create capability with this generation... */

/* Later: Revoke all capabilities to this allocation */
uint64_t new_gen = seraph_atlas_revoke(&atlas, alloc_id);

/* After reboot, old capabilities still fail:
 * they have the old generation, table has new generation */
```

This solves a fundamental security problem: revocation must be permanent, not just until the next restart.

## API Reference

### Initialization

```c
/* Initialize Atlas with backing file */
Seraph_Vbit seraph_atlas_init(
    Seraph_Atlas* atlas,
    const char* path,    /* Path to persistent file */
    size_t size          /* Size (0 = auto) */
);

/* Initialize with defaults */
Seraph_Vbit seraph_atlas_init_default(Seraph_Atlas* atlas);

/* Clean shutdown */
void seraph_atlas_destroy(Seraph_Atlas* atlas);
```

### Genesis Access

```c
/* Get Genesis structure */
Seraph_Atlas_Genesis* seraph_atlas_genesis(Seraph_Atlas* atlas);

/* Get/set application root */
void* seraph_atlas_get_root(Seraph_Atlas* atlas);
Seraph_Vbit seraph_atlas_set_root(Seraph_Atlas* atlas, void* root);
```

### Allocation

```c
/* Allocate persistent memory */
void* seraph_atlas_alloc(Seraph_Atlas* atlas, size_t size);

/* Allocate zeroed memory */
void* seraph_atlas_calloc(Seraph_Atlas* atlas, size_t size);

/* Allocate page-aligned memory */
void* seraph_atlas_alloc_pages(Seraph_Atlas* atlas, size_t size);

/* Free (adds to free list) */
void seraph_atlas_free(Seraph_Atlas* atlas, void* ptr, size_t size);

/* Check available space */
size_t seraph_atlas_available(const Seraph_Atlas* atlas);
```

### Pointer Utilities

```c
/* Check if pointer is in Atlas */
bool seraph_atlas_contains(const Seraph_Atlas* atlas, const void* ptr);

/* Convert pointer ↔ offset */
uint64_t seraph_atlas_ptr_to_offset(const Seraph_Atlas* atlas, const void* ptr);
void* seraph_atlas_offset_to_ptr(const Seraph_Atlas* atlas, uint64_t offset);
```

### Transactions

```c
/* Begin transaction */
Seraph_Atlas_Transaction* seraph_atlas_begin(Seraph_Atlas* atlas);

/* Commit (atomic) */
Seraph_Vbit seraph_atlas_commit(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Transaction* tx
);

/* Abort (discard changes) */
void seraph_atlas_abort(
    Seraph_Atlas* atlas,
    Seraph_Atlas_Transaction* tx
);
```

### Persistence Operations

```c
/* Sync all to disk */
Seraph_Vbit seraph_atlas_sync(Seraph_Atlas* atlas);

/* Sync specific region */
Seraph_Vbit seraph_atlas_sync_range(
    Seraph_Atlas* atlas,
    void* ptr,
    size_t size
);
```

### Generation Table

```c
/* Allocate generation ID */
uint64_t seraph_atlas_alloc_generation(Seraph_Atlas* atlas);

/* Revoke (increment generation) */
uint64_t seraph_atlas_revoke(Seraph_Atlas* atlas, uint64_t alloc_id);

/* Check if generation is current */
Seraph_Vbit seraph_atlas_check_generation(
    Seraph_Atlas* atlas,
    uint64_t alloc_id,
    uint64_t generation
);
```

## Usage Example

```c
/* Application root structure */
typedef struct AppRoot {
    uint64_t version;
    char* username;
    int64_t score;
} AppRoot;

/* First run: create persistent data */
void first_run(void) {
    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, "game.dat", 0);

    /* Allocate root */
    AppRoot* root = seraph_atlas_alloc(&atlas, sizeof(AppRoot));
    root->version = 1;
    root->username = seraph_atlas_alloc(&atlas, 64);
    strcpy(root->username, "Player1");
    root->score = 0;

    /* Set as application root */
    seraph_atlas_set_root(&atlas, root);

    /* Commit and sync */
    Seraph_Atlas_Transaction* tx = seraph_atlas_begin(&atlas);
    seraph_atlas_commit(&atlas, tx);

    seraph_atlas_destroy(&atlas);
}

/* Subsequent runs: data is already there */
void load_game(void) {
    Seraph_Atlas atlas;
    seraph_atlas_init(&atlas, "game.dat", 0);

    /* Root is exactly where we left it */
    AppRoot* root = seraph_atlas_get_root(&atlas);

    printf("Welcome back, %s! Score: %lld\n",
           root->username, root->score);

    /* Modify and commit */
    root->score += 100;

    Seraph_Atlas_Transaction* tx = seraph_atlas_begin(&atlas);
    seraph_atlas_commit(&atlas, tx);

    seraph_atlas_destroy(&atlas);
}
```

**Total serialization code: 0 lines.** The data just persists.

## VOID Semantics

Atlas fully embraces VOID:
- Invalid Atlas → VOID on all operations
- Allocation failure → NULL (VOID pointer)
- Invalid generation check → VOID
- Sync failure → VOID
- Commit conflict → FALSE (with VOID on error)

## Performance Characteristics

```
ACCESS LATENCY:
───────────────
┌─────────────────────────┬────────────────────────────────────┐
│  Scenario               │  Latency                           │
├─────────────────────────┼────────────────────────────────────┤
│  Hot data (in RAM)      │  ~100ns (same as volatile)         │
│  Warm data (in cache)   │  ~1μs (NVMe controller cache)      │
│  Cold data (on NVMe)    │  ~10μs (NVMe read latency)         │
│  Sequential access      │  ~7GB/s throughput                  │
└─────────────────────────┴────────────────────────────────────┘

COMPARED TO FILE I/O:
─────────────────────
┌─────────────────────┬─────────────────┬─────────────────┐
│  Operation          │  File System    │  Atlas          │
├─────────────────────┼─────────────────┼─────────────────┤
│  Open 1000 objects  │  1000 opens     │  1 pointer      │
│  Save state         │  Serialize all  │  Already saved  │
│  Load state         │  Deserialize    │  Already loaded │
│  Recovery           │  O(log_size)    │  O(1)           │
└─────────────────────┴─────────────────┴─────────────────┘
```

## Test Coverage

34 comprehensive tests covering:

**Initialization (5 tests):**
- New Atlas creation
- Default initialization
- NULL parameter handling
- Existing file reopening
- Safe destruction

**Genesis (4 tests):**
- Magic number validation
- Version check
- NULL handling
- Root pointer get/set

**Allocation (8 tests):**
- Basic allocation
- Multiple allocations
- Zero-size handling
- Zeroed allocation (calloc)
- Page-aligned allocation
- Allocation until full
- Free operation
- Available space tracking

**Pointer Utilities (2 tests):**
- Contains check
- Pointer/offset conversion

**Transactions (4 tests):**
- Begin transaction
- Commit
- Abort
- Multiple concurrent

**Persistence (2 tests):**
- Data survives reopen
- Genesis survives reopen

**Generation Table (5 tests):**
- Table initialization
- Generation allocation
- Revocation
- Generation checking
- Persistence across reopen

**Statistics & VOID (3 tests):**
- Statistics collection
- VOID operations on invalid Atlas
- Sync operations

## Integration with Other Components

- **Capability (MC6)**: Generation table for persistent revocation
- **Chronon (MC7)**: Timestamps in Genesis
- **Strand (MC13)**: Thread-safe access patterns
- **VOID (MC0)**: Error propagation throughout

## Summary

Atlas transforms storage from "file operations" to "persistent memory":

1. **No serialization** - Pointers are pointers, forever
2. **Instant recovery** - O(1) regardless of data size
3. **ACID transactions** - Without transaction logs
4. **Persistent revocation** - Security survives reboots
5. **Zero impedance** - Your data model IS your storage model

This is not an optimization. This is a fundamental paradigm shift in how data persists.

## Source Files

| File | Description |
|------|-------------|
| `src/atlas.c` | Single-level store implementation, NVMe memory mapping |
| `src/atlas_nvme.c` | NVMe command submission, completion handling |
| `include/seraph/atlas.h` | Atlas API, persistent pointer types |
