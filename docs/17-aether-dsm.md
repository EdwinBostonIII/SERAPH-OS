# MC28: Aether - Distributed Shared Memory

## Overview

Aether is SERAPH's revolutionary distributed shared memory system that makes the network transparent. A pointer can reference memory on another machine, and accessing it works exactly like accessing local memory—just slower. Network failures don't throw exceptions; they return VOID.

**"There is no network. There is only memory that happens to be far away."**

## Plain English Explanation

Traditional distributed systems require you to:
1. Serialize your data (convert to JSON, protobuf, etc.)
2. Make an explicit network call (REST, gRPC, etc.)
3. Handle timeouts, retries, circuit breakers
4. Deserialize the response
5. Handle all possible error cases differently

**Aether Approach:**
1. Just read from a pointer
2. If it works, you get the data
3. If it fails, you get VOID
4. Same code works for local and remote data

This is not just simpler—it's fundamentally different. The network becomes an implementation detail that your application code doesn't need to know about.

## Architecture

### Address Space Layout

```
VIRTUAL ADDRESS SPACE
─────────────────────

0x0000_0000_0000_0000  ┌────────────────┐
        ↓              │    VOLATILE    │  ← Local RAM (fast)
0x0000_7FFF_FFFF_FFFF  │   SUBSTRATE    │
────────────────────── └────────────────┘

0x0000_8000_0000_0000  ┌────────────────┐
        ↓              │     ATLAS      │  ← Persistent (NVMe)
0x0000_BFFF_FFFF_FFFF  │   SUBSTRATE    │
────────────────────── └────────────────┘

0x0000_C000_0000_0000  ┌────────────────┐  ┌─────────────────┐
        ↓              │    AETHER      │◀─▶│  Remote Nodes   │
0x0000_FFFF_FFFF_FFFF  │   SUBSTRATE    │  │  (RDMA/TCP)     │
                       └────────────────┘  └─────────────────┘
```

### Address Encoding

Aether addresses encode the node ID directly in the pointer:

```
Address layout (within Aether range):
┌──────────────────┬─────────────────────────────────┐
│  Node ID (14)    │  Local Offset (32 bits)         │
└──────────────────┴─────────────────────────────────┘
bits 45-32          bits 31-0

This gives us:
  - 16,384 nodes in the cluster (2^14)
  - 4 GB addressable per node (2^32)
```

## Key Innovations

### 1. VOID Over Network

All network failures collapse to VOID:
- Timeout? → VOID
- Network partition? → VOID
- Node crashed? → VOID
- Permission denied? → VOID
- Capability revoked? → VOID

```c
/* Traditional approach */
try {
    data = await fetch_from_remote(key);
} catch (TimeoutException e) {
    // Handle timeout
} catch (NetworkException e) {
    // Handle network error
} catch (NotFoundError e) {
    // Handle missing data
}

/* Aether approach */
uint64_t data = 0;
Seraph_Vbit result = seraph_aether_read_vbit(&aether, addr, &data, sizeof(data));
if (seraph_vbit_is_void(result)) {
    data = default_value;  // One error path
}
```

### 2. Global Generations

Capability revocation works cluster-wide. A global generation is packed as:
- Bits 63-48: Node ID (16 bits)
- Bits 47-0: Local generation (48 bits)

When you revoke a capability on one node, the generation increments, and all cached copies become stale everywhere in the cluster.

### 3. Coherent Caching

Modified pages automatically invalidate remote caches:

1. **Read Request**: Requester is added to sharers list
2. **Write Request**: All sharers are invalidated first
3. **After Invalidation**: Writer has exclusive access

This is a directory-based write-invalidate protocol.

### 4. Transparent Access

From the application's perspective:

```c
/* Local access */
uint64_t* local_ptr = volatile_alloc(sizeof(uint64_t));
*local_ptr = 42;

/* Remote access - SAME SYNTAX */
uint64_t remote_addr = seraph_aether_alloc_on_node(&aether, remote_node, sizeof(uint64_t));
uint64_t value = 42;
seraph_aether_write(&aether, remote_addr, &value, sizeof(value));
```

The only difference is the address range and that network access may return VOID.

## API Reference

### Initialization

```c
/* Initialize Aether with node configuration */
Seraph_Vbit seraph_aether_init(
    Seraph_Aether* aether,
    uint16_t node_id,      /* This node's ID (0 to node_count-1) */
    uint16_t node_count    /* Total nodes in cluster */
);

/* Initialize with defaults (single node) */
Seraph_Vbit seraph_aether_init_default(Seraph_Aether* aether);

/* Clean shutdown */
void seraph_aether_destroy(Seraph_Aether* aether);
```

### Address Manipulation

```c
/* Extract node ID from address */
uint16_t seraph_aether_get_node(uint64_t addr);

/* Extract local offset from address */
uint64_t seraph_aether_get_offset(uint64_t addr);

/* Construct Aether address */
uint64_t seraph_aether_make_addr(uint16_t node_id, uint64_t offset);

/* Check if address is in Aether range */
bool seraph_aether_is_aether_addr(uint64_t addr);

/* Check if address is on local node */
bool seraph_aether_is_local(const Seraph_Aether* aether, uint64_t addr);
```

### Memory Operations

```c
/* Allocate on local node */
uint64_t seraph_aether_alloc(Seraph_Aether* aether, size_t size);

/* Allocate on specific node */
uint64_t seraph_aether_alloc_on_node(
    Seraph_Aether* aether,
    uint16_t node_id,
    size_t size
);

/* Read (may trigger remote fetch) */
Seraph_Aether_Fetch_Result seraph_aether_read(
    Seraph_Aether* aether,
    uint64_t addr,
    void* dest,
    size_t size
);

/* Write (may invalidate remote caches) */
Seraph_Aether_Fetch_Result seraph_aether_write(
    Seraph_Aether* aether,
    uint64_t addr,
    const void* src,
    size_t size
);

/* VOID-returning variants */
Seraph_Vbit seraph_aether_read_vbit(...);
Seraph_Vbit seraph_aether_write_vbit(...);
```

### Cache Operations

```c
/* Look up in cache */
Seraph_Aether_Cache_Entry* seraph_aether_cache_lookup(
    Seraph_Aether* aether,
    uint64_t addr
);

/* Invalidate cache entry */
void seraph_aether_cache_invalidate(Seraph_Aether* aether, uint64_t addr);

/* Flush dirty pages */
uint32_t seraph_aether_cache_flush(Seraph_Aether* aether);

/* Clear entire cache */
void seraph_aether_cache_clear(Seraph_Aether* aether);
```

### Generation and Revocation

```c
/* Get current generation */
uint64_t seraph_aether_get_generation(Seraph_Aether* aether, uint64_t addr);

/* Check if generation is current */
Seraph_Vbit seraph_aether_check_generation(
    Seraph_Aether* aether,
    uint64_t addr,
    uint64_t expected_gen
);

/* Revoke capability (cluster-wide) */
Seraph_Vbit seraph_aether_revoke(Seraph_Aether* aether, uint64_t addr);

/* Pack/unpack global generation */
uint64_t seraph_aether_pack_global_gen(uint16_t node_id, uint64_t local_gen);
Seraph_Aether_Global_Gen seraph_aether_unpack_global_gen(uint64_t packed);
```

### Failure Injection (Testing)

```c
/* Inject failure for testing */
void seraph_aether_inject_failure(
    Seraph_Aether* aether,
    uint16_t node_id,
    Seraph_Aether_Void_Reason reason
);

/* Clear injected failure */
void seraph_aether_clear_failure(
    Seraph_Aether* aether,
    uint16_t node_id
);

/* Get last VOID reason */
Seraph_Aether_Void_Reason seraph_aether_get_void_reason(void);
```

## Usage Example

```c
/* Initialize Aether cluster with 3 nodes */
Seraph_Aether aether;
seraph_aether_init(&aether, 0, 3);  /* We are node 0 */

/* Add simulated nodes (for userspace testing) */
seraph_aether_add_sim_node(&aether, 0, 1024 * 1024);  /* 1MB */
seraph_aether_add_sim_node(&aether, 1, 1024 * 1024);
seraph_aether_add_sim_node(&aether, 2, 1024 * 1024);

/* Allocate data structure on node 1 */
typedef struct {
    uint64_t counter;
    char name[64];
} SharedData;

uint64_t remote_addr = seraph_aether_alloc_on_node(&aether, 1, sizeof(SharedData));

/* Write to remote node */
SharedData data = { .counter = 0 };
strcpy(data.name, "shared counter");
seraph_aether_write(&aether, remote_addr, &data, sizeof(data));

/* Read from remote node (transparently fetches and caches) */
SharedData read_data;
Seraph_Vbit result = seraph_aether_read_vbit(
    &aether, remote_addr, &read_data, sizeof(read_data));

if (seraph_vbit_is_void(result)) {
    printf("Remote access failed: %d\n", seraph_aether_get_void_reason());
} else {
    printf("Counter: %llu, Name: %s\n", read_data.counter, read_data.name);
}

/* Simulate node failure */
seraph_aether_set_node_online(&aether, 1, false);

/* Now reads will return VOID */
result = seraph_aether_read_vbit(&aether, remote_addr, &read_data, sizeof(read_data));
/* result is VOID - node is offline */

seraph_aether_destroy(&aether);
```

## VOID Semantics

Aether fully embraces VOID:
- Invalid address → VOID
- Node unreachable → VOID
- Timeout → VOID
- Permission denied → VOID
- Generation mismatch → VOID
- Corruption detected → VOID

The VOID reason can be queried for debugging:

```c
typedef enum Seraph_Aether_Void_Reason {
    SERAPH_AETHER_VOID_NONE,
    SERAPH_AETHER_VOID_TIMEOUT,
    SERAPH_AETHER_VOID_UNREACHABLE,
    SERAPH_AETHER_VOID_PARTITION,
    SERAPH_AETHER_VOID_NODE_CRASHED,
    SERAPH_AETHER_VOID_PERMISSION,
    SERAPH_AETHER_VOID_GENERATION,
    SERAPH_AETHER_VOID_CORRUPTION,
} Seraph_Aether_Void_Reason;
```

## Performance Characteristics

```
ACCESS LATENCY:
───────────────
┌─────────────────────────┬────────────────────────────────────┐
│  Scenario               │  Latency                           │
├─────────────────────────┼────────────────────────────────────┤
│  Local cache hit        │  ~100ns (same as volatile)         │
│  RDMA fetch (simulated) │  ~1-2μs (real InfiniBand)          │
│  TCP fetch (LAN)        │  ~50-100μs                         │
│  Cache invalidation     │  ~10μs (async)                     │
└─────────────────────────┴────────────────────────────────────┘

COMPARED TO TRADITIONAL RPC:
────────────────────────────
┌─────────────────────┬─────────────────┬─────────────────┐
│  Operation          │  gRPC           │  Aether         │
├─────────────────────┼─────────────────┼─────────────────┤
│  Small read (64B)   │  ~100μs         │  ~2-60μs        │
│  Large read (4KB)   │  ~150μs         │  ~3-70μs        │
│  Serialization      │  Required       │  None           │
│  Error handling     │  Multiple paths │  VOID only      │
└─────────────────────┴─────────────────┴─────────────────┘

SCALABILITY:
────────────
• Up to 16,384 nodes (14-bit node ID)
• Up to 4 GB per node (32-bit offset)
• Total addressable: ~64 TB across cluster
• Directory overhead: ~64 bytes per cached page
```

## Test Coverage

40 comprehensive tests covering:

**Address Encoding (4 tests):**
- Basic encoding/decoding
- Boundary values
- Range checking
- Page alignment

**Global Generations (2 tests):**
- Pack/unpack round-trip
- Boundary values

**Initialization (4 tests):**
- Basic init
- Default init
- NULL handling
- Cleanup verification

**Simulated Nodes (3 tests):**
- Adding nodes
- Online/offline status
- Local node detection

**Memory Allocation (5 tests):**
- Basic allocation
- Multiple allocations
- Node-specific allocation
- Out of memory
- Zero size handling

**Read/Write Operations (5 tests):**
- Local read/write
- VBIT interface
- Remote read/write
- Invalid address handling
- Array operations

**Cache Operations (3 tests):**
- Hit/miss tracking
- Invalidation
- Cache clearing

**Generation and Revocation (3 tests):**
- Generation tracking
- Revocation
- Generation checking

**VOID Over Network (4 tests):**
- Timeout injection
- Partition injection
- Node offline simulation
- VOID context tracking

**Coherence Protocol (3 tests):**
- Read requests
- Write requests
- Directory management

**Statistics (2 tests):**
- Tracking
- Reset

**Edge Cases (2 tests):**
- NULL parameters
- Maximum node count

## Integration with Other Components

- **Capability (MC6)**: Global generations for cluster-wide revocation
- **Chronon (MC7)**: Causality ordering across distributed operations
- **Atlas (MC27)**: Shares address space partitioning scheme
- **VOID (MC0)**: Network failures return VOID, not exceptions

## Summary

Aether transforms distributed programming from explicit RPC to transparent memory access:

1. **No serialization** - Data is accessed directly, not converted
2. **No explicit networking** - Pointers just work across machines
3. **Uniform error handling** - VOID replaces exception taxonomies
4. **Cache coherent** - Writes automatically invalidate remote caches
5. **Capability secured** - Same security model as local memory
6. **Cluster-wide revocation** - Security survives network topology

This is not an optimization. This is a fundamental paradigm shift in how distributed systems work.

## Source Files

| File | Description |
|------|-------------|
| `src/aether.c` | Distributed shared memory core, remote page faults |
| `src/aether_nic.c` | Network interface driver, packet TX/RX |
| `src/aether_nvme.c` | Remote NVMe access over network |
| `src/aether_security.c` | Cryptographic verification, capability attestation |
| `include/seraph/aether.h` | Aether API, remote pointer types |
| `include/seraph/aether_security.h` | Security primitives, attestation |
