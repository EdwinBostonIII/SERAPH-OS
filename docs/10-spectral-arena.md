# MC8: Spectral Arena - Auto-SoA Memory Allocator

## What is the Spectral Arena?

**Plain English**: The Spectral Arena is SERAPH's memory allocator - but it's very different from traditional allocators like `malloc`. Instead of managing individual objects, it manages *regions* of memory that are all freed together. Even more importantly, it automatically reorganizes your data for maximum performance.

**The Problem**: Traditional memory management has three expensive costs:
1. **Allocation overhead**: Finding free space, managing free lists, splitting/coalescing blocks
2. **Deallocation overhead**: Returning memory to free lists, potentially coalescing
3. **Cache inefficiency**: When you store objects together (Array-of-Structures), iterating over one field touches all fields, wasting cache

**The Solution**: The Spectral Arena solves all three:
1. **Bump allocation** - O(1): Just increment a pointer. No bookkeeping.
2. **Generation-based deallocation** - O(1): Increment a counter to invalidate everything
3. **Automatic SoA transformation**: Fields are stored contiguously for cache efficiency

**Analogy**: Imagine a whiteboard where you just write from left to right. No erasing individual things - when you're done with the whole board, you wipe it clean and start over. The "generation number" is like dating your notes - old notes from yesterday are obviously obsolete.

## Why Array-of-Structures is Slow

### The Cache Problem

Modern CPUs don't read single bytes - they read *cache lines* (typically 64 bytes). When you access memory, the CPU loads a whole cache line hoping you'll use nearby data.

Consider a 3D point:
```c
typedef struct {
    float x;  // 4 bytes
    float y;  // 4 bytes
    float z;  // 4 bytes
} Point3D;   // 12 bytes total
```

**Array-of-Structures (AoS) layout:**
```
Memory: [x0,y0,z0] [x1,y1,z1] [x2,y2,z2] [x3,y3,z3] ...
         ↑ Cache line 1 ↑     ↑ Cache line 2 ↑
```

If you want to sum all x-values:
```c
float sum = 0;
for (int i = 0; i < count; i++) {
    sum += points[i].x;  // Loads x,y,z but only uses x (66% waste!)
}
```

Each cache line contains multiple points, but you only use the x-values. The y and z values are loaded into cache but never read - wasted bandwidth.

### Structure-of-Arrays (SoA) Solution

**SoA layout:**
```
Memory: [x0,x1,x2,x3,...] [y0,y1,y2,y3,...] [z0,z1,z2,z3,...]
         ↑ All x values ↑  ↑ All y values ↑  ↑ All z values ↑
```

Now summing all x-values:
```c
float sum = 0;
for (int i = 0; i < count; i++) {
    sum += x_array[i];  // Every byte loaded is used! 100% utilization
}
```

Every byte in the cache line is used. This can provide 2-4x speedup for field-iteration patterns.

## The Spectral Arena Design

### Core Concepts

1. **Arena**: A contiguous region of memory with a bump pointer
2. **Generation**: A counter that increments on reset, invalidating all allocations
3. **Schema**: Description of struct layout for SoA transformation
4. **SoA Array**: Collection of field arrays (one per field)
5. **Prism**: A view into one field across all elements

### Memory Layout

```
+------------------------------------------------------------------+
|                        ARENA MEMORY                               |
|  +-----------------------------------------------------------+   |
|  | [Field Array 0: x0,x1,x2,x3,...]                          |   |
|  +-----------------------------------------------------------+   |
|  | [Field Array 1: y0,y1,y2,y3,...]                          |   |
|  +-----------------------------------------------------------+   |
|  | [Field Array 2: z0,z1,z2,z3,...]                          |   |
|  +-----------------------------------------------------------+   |
|  | ... more allocations ...                                  |   |
+------------------------------------------------------------------+
     ↑                                                          ↑
     |                                                          |
   used pointer                                             capacity
   (bump allocation)
```

### Generation-Based Temporal Safety

Every allocation is tagged with the arena's current generation:

```
Time 0: arena.generation = 1
        alloc() → capability with gen=1

Time 1: alloc() → capability with gen=1
        alloc() → capability with gen=1

Time 2: arena_reset()
        arena.generation = 2
        arena.used = 0

Time 3: Old capabilities (gen=1) are INVALID
        - gen=1 ≠ current gen=2
        - Temporal safety check fails

Time 4: alloc() → capability with gen=2 (valid)
```

This provides use-after-free protection without tracking individual allocations!

## Using the Spectral Arena

### Basic Allocation

```c
#include "seraph/arena.h"

// Create arena with 1MB capacity
Seraph_Arena arena;
seraph_arena_create(&arena, 1024 * 1024, 0, SERAPH_ARENA_FLAG_NONE);

// Allocate memory (O(1) bump allocation)
void* buffer1 = seraph_arena_alloc(&arena, 256, 0);
void* buffer2 = seraph_arena_alloc(&arena, 512, 64);  // 64-byte aligned

// Check remaining space
size_t remaining = seraph_arena_remaining(&arena);

// Reset arena (O(1) - invalidates all allocations)
seraph_arena_reset(&arena);

// Destroy when done
seraph_arena_destroy(&arena);
```

### Capability Integration

```c
// Get capability for an allocation
void* data = seraph_arena_alloc(&arena, 100, 0);
Seraph_Capability cap = seraph_arena_get_capability(
    &arena, data, 100, SERAPH_CAP_RW);

// Capability is valid now
seraph_arena_check_capability(&arena, cap);  // Returns TRUE

// Reset arena
seraph_arena_reset(&arena);

// Old capability is now invalid (generation mismatch)
seraph_arena_check_capability(&arena, cap);  // Returns FALSE
```

### SoA Transformation

```c
// Define your struct
typedef struct {
    float x, y, z;
    uint32_t id;
    uint8_t flags;
} Particle;

// Create schema describing the struct layout
Seraph_FieldDesc fields[] = {
    SERAPH_FIELD(Particle, x),
    SERAPH_FIELD(Particle, y),
    SERAPH_FIELD(Particle, z),
    SERAPH_FIELD(Particle, id),
    SERAPH_FIELD(Particle, flags)
};

Seraph_SoA_Schema schema;
seraph_soa_schema_create(&schema, sizeof(Particle), _Alignof(Particle),
                         fields, 5);

// Create SoA array (automatically transforms to SoA layout)
Seraph_SoA_Array particles;
seraph_soa_array_create(&particles, &arena, &schema, 10000);

// Push elements (scatter to field arrays)
Particle p = {1.0f, 2.0f, 3.0f, 42, 0xFF};
size_t idx = seraph_soa_array_push(&particles, &p);

// Get elements (gather from field arrays)
Particle out;
seraph_soa_array_get(&particles, idx, &out);

// Clean up
seraph_soa_schema_destroy(&schema);
seraph_arena_destroy(&arena);
```

### Prism Access (Cache-Efficient Field Iteration)

```c
// Get prism for x field (field 0)
Seraph_Prism x_prism = seraph_soa_get_prism(&particles, 0);

// Iterate over all x values (cache-friendly!)
for (size_t i = 0; i < x_prism.count; i++) {
    float* x = (float*)seraph_prism_get_ptr(x_prism, i);
    *x *= 2.0f;  // Double all x values
}

// Or use typed read/write
for (size_t i = 0; i < x_prism.count; i++) {
    uint32_t id = seraph_prism_read_u32(id_prism, i);
    seraph_prism_write_u32(id_prism, i, id + 1);
}
```

## API Reference

### Types

| Type | Description |
|------|-------------|
| `Seraph_Arena` | The arena allocator |
| `Seraph_FieldDesc` | Description of a single field |
| `Seraph_SoA_Schema` | Layout description for SoA transformation |
| `Seraph_SoA_Array` | Array stored in SoA layout |
| `Seraph_Prism` | View into one field across all elements |

### Arena Flags

| Flag | Description |
|------|-------------|
| `SERAPH_ARENA_FLAG_NONE` | Default behavior |
| `SERAPH_ARENA_FLAG_ZERO_ON_ALLOC` | Zero-initialize all allocations |
| `SERAPH_ARENA_FLAG_ZERO_ON_RESET` | Zero memory on arena reset |

### Arena Functions

```c
// Create arena
Seraph_Vbit seraph_arena_create(Seraph_Arena* arena, size_t capacity,
                                 size_t alignment, uint32_t flags);

// Destroy arena
void seraph_arena_destroy(Seraph_Arena* arena);

// Reset arena (O(1) mass deallocation)
uint32_t seraph_arena_reset(Seraph_Arena* arena);

// Allocate from arena (O(1))
void* seraph_arena_alloc(Seraph_Arena* arena, size_t size, size_t align);
void* seraph_arena_alloc_array(Seraph_Arena* arena, size_t elem_size,
                                size_t count, size_t align);
void* seraph_arena_calloc(Seraph_Arena* arena, size_t size, size_t align);

// Information
size_t seraph_arena_remaining(const Seraph_Arena* arena);
size_t seraph_arena_used(const Seraph_Arena* arena);
uint32_t seraph_arena_generation(const Seraph_Arena* arena);
bool seraph_arena_is_valid(const Seraph_Arena* arena);

// Capability integration
Seraph_Capability seraph_arena_get_capability(const Seraph_Arena* arena,
                                               void* ptr, size_t size,
                                               uint8_t perms);
Seraph_Vbit seraph_arena_check_capability(const Seraph_Arena* arena,
                                           Seraph_Capability cap);
```

### Schema Functions

```c
// Create schema
Seraph_Vbit seraph_soa_schema_create(Seraph_SoA_Schema* schema,
                                      size_t struct_size, size_t struct_align,
                                      const Seraph_FieldDesc* fields,
                                      uint32_t field_count);

// Destroy schema
void seraph_soa_schema_destroy(Seraph_SoA_Schema* schema);

// Helper macro for field descriptors
SERAPH_FIELD(type, field)  // Generates {offset, size, align}
```

### SoA Array Functions

```c
// Create SoA array
Seraph_Vbit seraph_soa_array_create(Seraph_SoA_Array* array,
                                     Seraph_Arena* arena,
                                     Seraph_SoA_Schema* schema,
                                     size_t capacity);

// Push element (scatter to field arrays)
size_t seraph_soa_array_push(Seraph_SoA_Array* array, const void* element);

// Get element (gather from field arrays)
Seraph_Vbit seraph_soa_array_get(const Seraph_SoA_Array* array,
                                  size_t index, void* element);

// Set element (scatter to field arrays)
Seraph_Vbit seraph_soa_array_set(Seraph_SoA_Array* array,
                                  size_t index, const void* element);

// Information
size_t seraph_soa_array_count(const Seraph_SoA_Array* array);
bool seraph_soa_array_is_valid(const Seraph_SoA_Array* array);
```

### Prism Functions

```c
// Get prism for a field
Seraph_Prism seraph_soa_get_prism(const Seraph_SoA_Array* array,
                                   uint32_t field_index);

// Validity checks
bool seraph_prism_is_valid(Seraph_Prism prism);
bool seraph_prism_in_bounds(Seraph_Prism prism, size_t index);
void* seraph_prism_get_ptr(Seraph_Prism prism, size_t index);

// Typed read operations
uint8_t  seraph_prism_read_u8(Seraph_Prism prism, size_t index);
uint16_t seraph_prism_read_u16(Seraph_Prism prism, size_t index);
uint32_t seraph_prism_read_u32(Seraph_Prism prism, size_t index);
uint64_t seraph_prism_read_u64(Seraph_Prism prism, size_t index);

// Typed write operations
Seraph_Vbit seraph_prism_write_u8(Seraph_Prism prism, size_t index, uint8_t value);
Seraph_Vbit seraph_prism_write_u16(Seraph_Prism prism, size_t index, uint16_t value);
Seraph_Vbit seraph_prism_write_u32(Seraph_Prism prism, size_t index, uint32_t value);
Seraph_Vbit seraph_prism_write_u64(Seraph_Prism prism, size_t index, uint64_t value);

// Bulk operations
Seraph_Vbit seraph_prism_fill(Seraph_Prism prism, const void* value);
Seraph_Vbit seraph_prism_copy(Seraph_Prism dst, Seraph_Prism src);
```

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|-----------------|-------|
| `arena_alloc` | O(1) | Bump pointer increment |
| `arena_reset` | O(1) or O(n) | O(1) normally, O(n) if zeroing |
| `soa_array_push` | O(k) | k = number of fields |
| `soa_array_get` | O(k) | k = number of fields |
| `prism iteration` | O(n) | Cache-friendly linear access |

### When to Use Arena vs Traditional Allocation

**Use Arena when:**
- Objects have similar lifetimes (e.g., per-frame game objects)
- You can batch deallocations (e.g., end of frame, end of request)
- You want to avoid fragmentation
- You're iterating over fields (SoA benefit)

**Use Traditional when:**
- Objects have widely varying lifetimes
- Individual deallocation is required
- Memory must persist across arena resets

## Integration Examples

### Game Engine Per-Frame Allocation

```c
// Frame arena - reset every frame
Seraph_Arena frame_arena;
seraph_arena_create(&frame_arena, 16 * 1024 * 1024, 0, 0);  // 16MB

void render_frame() {
    // Reset arena (O(1) - invalidates all previous frame's allocations)
    seraph_arena_reset(&frame_arena);

    // All allocations this frame use the arena
    RenderCommand* commands = seraph_arena_alloc_array(
        &frame_arena, sizeof(RenderCommand), 10000, 0);

    // ... fill commands, render ...

    // Frame ends - arena will be reset next frame
    // No individual frees needed!
}
```

### Entity Component System (ECS)

```c
// Define components
typedef struct { float x, y, z; } Position;
typedef struct { float vx, vy, vz; } Velocity;
typedef struct { float r, g, b, a; } Color;

// Create schemas
Seraph_FieldDesc pos_fields[] = {
    SERAPH_FIELD(Position, x),
    SERAPH_FIELD(Position, y),
    SERAPH_FIELD(Position, z)
};
Seraph_SoA_Schema pos_schema;
seraph_soa_schema_create(&pos_schema, sizeof(Position), _Alignof(Position),
                         pos_fields, 3);

// Create component arrays
Seraph_SoA_Array positions, velocities;
seraph_soa_array_create(&positions, &ecs_arena, &pos_schema, 100000);
seraph_soa_array_create(&velocities, &ecs_arena, &vel_schema, 100000);

// Update system - iterate one component at a time (cache-friendly!)
void physics_system(float dt) {
    Seraph_Prism pos_x = seraph_soa_get_prism(&positions, 0);
    Seraph_Prism vel_x = seraph_soa_get_prism(&velocities, 0);

    // Update all x positions (contiguous memory access)
    for (size_t i = 0; i < pos_x.count; i++) {
        float* px = seraph_prism_get_ptr(pos_x, i);
        float* vx = seraph_prism_get_ptr(vel_x, i);
        *px += *vx * dt;
    }

    // Same for y and z...
}
```

### Request-Scoped Allocation (Web Server)

```c
void handle_request(Request* req) {
    // Per-request arena
    Seraph_Arena req_arena;
    seraph_arena_create(&req_arena, 64 * 1024, 0, 0);  // 64KB

    // All allocations for this request use the arena
    char* response_buffer = seraph_arena_alloc(&req_arena, 4096, 0);
    Headers* headers = seraph_arena_alloc(&req_arena, sizeof(Headers), 0);

    // Process request...

    // Done - destroy arena (frees everything at once)
    seraph_arena_destroy(&req_arena);
}
```

## Common Pitfalls

### 1. Forgetting Arena Lifetime

```c
// WRONG: Returning pointer to arena memory after reset
Point3D* get_point(Seraph_Arena* arena) {
    Point3D* p = seraph_arena_alloc(arena, sizeof(Point3D), 0);
    return p;  // Will become invalid when arena is reset!
}

// RIGHT: Copy out if needed beyond arena lifetime
Point3D get_point_copy(Seraph_Arena* arena) {
    Point3D* p = seraph_arena_alloc(arena, sizeof(Point3D), 0);
    p->x = 1.0f; p->y = 2.0f; p->z = 3.0f;
    return *p;  // Copy, not pointer
}
```

### 2. Using Old Capabilities After Reset

```c
// WRONG: Using capability after arena reset
Seraph_Capability cap = seraph_arena_get_capability(&arena, ptr, 100, SERAPH_CAP_RW);
seraph_arena_reset(&arena);
seraph_cap_read_u64(cap, 0);  // Will return VOID! (generation mismatch)

// RIGHT: Get new capability after reset
seraph_arena_reset(&arena);
void* new_ptr = seraph_arena_alloc(&arena, 100, 0);
Seraph_Capability new_cap = seraph_arena_get_capability(&arena, new_ptr, 100, SERAPH_CAP_RW);
```

### 3. Ignoring Schema Field Order

```c
// Schema must match struct field order!
typedef struct {
    float x;    // offset 0
    float y;    // offset 4
    float z;    // offset 8
} Point3D;

// WRONG: Fields in wrong order
Seraph_FieldDesc fields[] = {
    SERAPH_FIELD(Point3D, z),  // offset 8, not 0!
    SERAPH_FIELD(Point3D, x),
    SERAPH_FIELD(Point3D, y)
};
// push/get will put data in wrong fields!

// RIGHT: Fields in struct order
Seraph_FieldDesc fields[] = {
    SERAPH_FIELD(Point3D, x),
    SERAPH_FIELD(Point3D, y),
    SERAPH_FIELD(Point3D, z)
};
```

### 4. Prism Index vs Array Index

```c
// Prism count is current element count, not capacity!
Seraph_Prism prism = seraph_soa_get_prism(&array, 0);

// WRONG: Iterating to capacity
for (size_t i = 0; i < array.capacity; i++) {  // May access uninitialized!
    float* p = seraph_prism_get_ptr(prism, i);
}

// RIGHT: Iterate to count
for (size_t i = 0; i < prism.count; i++) {
    float* p = seraph_prism_get_ptr(prism, i);
}
```

## Comparison with Other Allocators

| Feature | malloc/free | Pool Allocator | Spectral Arena |
|---------|-------------|----------------|----------------|
| Alloc time | O(log n) | O(1) | O(1) |
| Free time | O(log n) | O(1) | N/A |
| Mass dealloc | O(n) | O(n) or O(1) | O(1) |
| Fragmentation | Yes | No | No |
| Use-after-free | Undefined | Undefined | Detected (generation) |
| Auto SoA | No | No | Yes |
| Cache optimization | No | No | Yes (prisms) |

## Source Files

| File | Description |
|------|-------------|
| `src/arena.c` | Arena allocation, prism management, generation tracking |
| `src/early_mem.c` | Bootstrap allocator for pre-arena kernel init |
| `src/pmm.c` | Physical memory manager, page frame allocation |
| `src/kmalloc.c` | Kernel malloc interface using arenas |
| `include/seraph/arena.h` | Arena API, prism structures |
| `include/seraph/early_mem.h` | Early memory allocator interface |
| `include/seraph/pmm.h` | Physical memory manager interface |
