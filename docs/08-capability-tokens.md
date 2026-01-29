# MC6: Capability Tokens

## What is a Capability?

**Plain English**: A capability is like a VIP pass that proves you're allowed to access a specific area of memory. Unlike a regular pointer (which is just an address you can make up), a capability is unforgeable - you can only get one from the system, and you can only use it for what it was designed for.

**Technical**: A capability is a fat pointer that bundles together:
1. **Base address** - Where the accessible memory region starts
2. **Length** - How many bytes you can access
3. **Generation** - Which allocation epoch this belongs to (for use-after-free detection)
4. **Permissions** - What you can do (read, write, execute)

## Why Capabilities?

Traditional C/C++ has three massive security problems:

1. **Buffer Overflows**: Nothing stops you from accessing memory beyond an array's bounds
2. **Use-After-Free**: After `free(ptr)`, the pointer still works - accessing dangling memory
3. **Wild Pointers**: Any integer can be cast to a pointer

Capabilities solve all three:

| Problem | How Capabilities Solve It |
|---------|---------------------------|
| Buffer overflow | Bounds checking: offset must be < length |
| Use-after-free | Generation checking: capability's gen must match current allocation |
| Wild pointers | Unforgeability: can't create capabilities from integers |

## Capability Structure

### Full Capability (256 bits)

```c
typedef struct {
    void*    base;        // 64 bits: Start address
    uint64_t length;      // 64 bits: Size in bytes
    uint32_t generation;  // 32 bits: Allocation epoch
    uint8_t  permissions; // 8 bits: Access flags
    uint8_t  type;        // 8 bits: Seal type (0 = unsealed)
    uint16_t reserved;    // 16 bits: Future use
} Seraph_Capability;      // Total: 256 bits = 32 bytes
```

### Compact Capability (64 bits)

For hot paths where passing 32 bytes is too expensive:

```c
typedef struct {
    uint32_t cdt_index;   // Index into Capability Descriptor Table
    uint32_t offset : 24; // Offset from base (max 16MB)
    uint32_t perms  : 8;  // Cached permissions
} Seraph_CapCompact;      // Total: 64 bits = 8 bytes
```

The compact form references an entry in a Capability Descriptor Table (CDT), trading off a table lookup for smaller size.

## Permission Flags

```c
SERAPH_CAP_READ    = 0x01  // Can read memory
SERAPH_CAP_WRITE   = 0x02  // Can write memory
SERAPH_CAP_EXEC    = 0x04  // Can execute (for code)
SERAPH_CAP_DERIVE  = 0x08  // Can create sub-capabilities
SERAPH_CAP_SEAL    = 0x10  // Can seal (make opaque)
SERAPH_CAP_UNSEAL  = 0x20  // Can unseal
SERAPH_CAP_GLOBAL  = 0x40  // Survives context switch
SERAPH_CAP_LOCAL   = 0x80  // Valid only in current context
```

## Monotonic Restriction

**Key Principle**: Capabilities can only become MORE restricted, never less.

```
Original: [base=0x1000, length=1024, perms=RWX]
            ↓
Derived:  [base=0x1100, length=512, perms=RW]  ← Valid
            ↓
Further:  [base=0x1100, length=256, perms=R]   ← Valid

INVALID:  [base=0x0F00, length=2000, perms=RWX] ← Cannot expand!
```

You can:
- Shrink the bounds (increase base, decrease length)
- Remove permissions (R → none, RW → R)
- Derive sub-capabilities

You cannot:
- Expand bounds beyond parent
- Add permissions parent didn't have
- Create capabilities from thin air

## Derivation

```c
// Parent capability from allocator
Seraph_Capability parent = seraph_cap_create(buffer, 1024, gen,
                                              SERAPH_CAP_RW | SERAPH_CAP_DERIVE);

// Derive a read-only view of bytes 100-300
Seraph_Capability child = seraph_cap_derive(parent,  // source
                                             100,      // offset
                                             200,      // length
                                             SERAPH_CAP_READ);  // reduced perms

// child.base = buffer + 100
// child.length = 200
// child.permissions = READ only
```

## Temporal Safety (Use-After-Free Protection)

Every memory allocation has a **generation number**. When memory is freed and reallocated, the generation increments.

```
Time 0: allocate(100) → gen=1, ptr=0x1000
Time 1: create capability {base=0x1000, gen=1}
Time 2: free(ptr)      → gen becomes invalid
Time 3: allocate(100) → gen=2, ptr=0x1000 (same address, new gen)
Time 4: use old capability → gen=1 ≠ current gen=2 → ACCESS DENIED
```

This prevents use-after-free: even if you hold a capability to a freed address, it won't work because the generation doesn't match.

## Sealing (Opaque Types)

Sealing converts a capability into an opaque token that can't be dereferenced:

```c
// Create a capability to sensitive data
Seraph_Capability cap = seraph_cap_create(secret_key, 32, gen,
                                           SERAPH_CAP_RW | SERAPH_CAP_SEAL | SERAPH_CAP_UNSEAL);

// Seal it with type 42 (representing "EncryptionKey" type)
Seraph_Capability sealed = seraph_cap_seal(cap, 42);

// Cannot read through sealed capability
uint8_t byte = seraph_cap_read_u8(sealed, 0);  // Returns VOID

// Only code that knows type 42 can unseal
Seraph_Capability unsealed = seraph_cap_unseal(sealed, 42);  // Works
Seraph_Capability wrong = seraph_cap_unseal(sealed, 99);      // VOID
```

Use cases:
- Implementing abstract data types
- Protecting secrets that should only be accessed by specific modules
- Enforcing type safety at runtime

## Capability Descriptor Table (CDT)

For compact capabilities, the CDT stores full capability information:

```c
Seraph_CDT cdt;
seraph_cdt_init(&cdt, 1000);  // Up to 1000 entries

// Store a capability
Seraph_CapCompact compact = seraph_cdt_alloc(&cdt, full_cap);

// Retrieve when needed
Seraph_Capability full = seraph_cdt_lookup(&cdt, compact);

// Reference counting
seraph_cdt_addref(&cdt, compact);   // Increment refcount
seraph_cdt_release(&cdt, compact);  // Decrement, free if zero
```

Benefits:
- 8 bytes vs 32 bytes per capability reference
- Centralized revocation (invalidate CDT entry = revoke all compact caps)
- Efficient for hot paths

## Access Operations

All access goes through capability-checked functions:

```c
// Reading
uint8_t  byte = seraph_cap_read_u8(cap, offset);
uint64_t quad = seraph_cap_read_u64(cap, offset);

// Writing
Seraph_Vbit ok = seraph_cap_write_u8(cap, offset, value);
// Returns TRUE if succeeded, FALSE if failed, VOID if cap invalid

// Bulk copy
Seraph_Vbit ok = seraph_cap_copy(dst_cap, dst_off, src_cap, src_off, length);
```

Each operation checks:
1. Is the capability VOID?
2. Does it have the required permission (read/write)?
3. Is the offset within bounds?
4. Is the capability unsealed?

## VOID Propagation

Like all SERAPH types, capabilities use VOID for invalid states:

```c
// VOID capability
Seraph_Capability void_cap = SERAPH_CAP_VOID;
seraph_cap_is_void(void_cap);  // true

// Invalid operations return VOID
Seraph_Capability bad = seraph_cap_derive(parent, 99999, 100, perms);
// bad is VOID (offset out of bounds)

// Reading through VOID returns VOID values
uint64_t val = seraph_cap_read_u64(void_cap, 0);
// val is SERAPH_VOID_U64
```

## Comparison with Other Systems

| Feature | Raw Pointers | CHERI | SERAPH Capabilities |
|---------|--------------|-------|---------------------|
| Bounds checking | No | Yes (hardware) | Yes (software) |
| Temporal safety | No | Partial | Yes (generation) |
| Permission bits | No | Yes | Yes |
| Sealing | No | Yes | Yes |
| Monotonic restriction | N/A | Yes | Yes |
| Hardware support | N/A | Required | Optional |
| VOID semantics | N/A | No | Yes |

SERAPH capabilities are designed to work on any hardware, with optional hardware acceleration.

## Implementation Notes

### Performance

The capability check overhead is:
- ~5-10 cycles for bounds check (branchless)
- ~10-20 cycles for full validation
- CDT lookup adds ~20-30 cycles

For hot loops, use the `_unchecked` variants (internal use only) or batch operations.

### Integration with Arena

When using the Spectral Arena allocator (MC8), capabilities are automatically created with correct generations:

```c
// Arena tracks generations automatically
void* ptr = seraph_arena_alloc(arena, 256);
Seraph_Capability cap = seraph_arena_get_capability(arena, ptr);
// cap.generation is set correctly

// After arena reset, old capabilities become invalid
seraph_arena_reset(arena);
// cap.generation no longer matches → access will fail
```

## API Summary

### Creation & Derivation
- `seraph_cap_create()` - Create new capability (trusted code only)
- `seraph_cap_derive()` - Derive restricted sub-capability
- `seraph_cap_shrink()` - Shrink bounds
- `seraph_cap_restrict()` - Remove permissions

### Checking
- `seraph_cap_is_void()` - Check if VOID
- `seraph_cap_is_null()` - Check if null (valid but empty)
- `seraph_cap_in_bounds()` - Check offset validity
- `seraph_cap_can_read/write/exec()` - Check permissions

### Access
- `seraph_cap_read_u8/u16/u32/u64()` - Read values
- `seraph_cap_write_u8/u16/u32/u64()` - Write values
- `seraph_cap_copy()` - Bulk memory copy
- `seraph_cap_get_ptr()` - Get raw pointer (checked)

### Sealing
- `seraph_cap_seal()` - Make opaque
- `seraph_cap_unseal()` - Restore access
- `seraph_cap_is_sealed()` - Check seal status

### CDT Operations
- `seraph_cdt_init/destroy()` - Manage CDT lifecycle
- `seraph_cdt_alloc()` - Store capability
- `seraph_cdt_lookup()` - Retrieve capability
- `seraph_cdt_addref/release()` - Reference counting

## Source Files

| File | Description |
|------|-------------|
| `src/capability.c` | Capability creation, derivation, bounds checking |
| `src/capability_cdt.c` | Capability Descriptor Table, reference counting |
| `include/seraph/capability.h` | Capability structure, permission flags, API |
