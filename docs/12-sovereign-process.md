# MC10: The Sovereign - Capability-Based Process Isolation

## Overview

A **Sovereign** is SERAPH's fundamental unit of isolation and authority - the equivalent of a "process" in traditional operating systems, but with far stronger guarantees. Unlike Unix processes that can access anything they have permission for, a Sovereign can **ONLY** access what its capability tokens explicitly allow.

Think of a Sovereign as a kingdom: it has defined borders (memory regions), a ruler (the executing code), citizens (Strands/threads), and relationships with other kingdoms (parent/child hierarchy, capability grants). No foreign entity can enter without an explicit invitation (capability grant).

## Core Principles

### 1. Capability-Based Identity

Unlike PIDs (process IDs), Sovereign identifiers are:

- **256-bit cryptographically random tokens**: Cannot be guessed or forged
- **Never reused**: A Sovereign ID is used exactly once in the system's lifetime
- **Self-validating**: Contains an embedded checksum for integrity verification

The 256-bit ID structure:
```
┌────────────────────────────────────────────────────────────────┐
│ quads[0]: random_id       (64 bits) - Cryptographic randomness │
│ quads[1]: generation      (64 bits) - Monotonic counter        │
│ quads[2]: authority_mask  (64 bits) - Authority at creation    │
│ quads[3]: nonce_checksum  (64 bits) - Nonce + XOR checksum     │
└────────────────────────────────────────────────────────────────┘
```

### 2. Law of Diminishing Sovereignty

A child Sovereign can **NEVER** have more authority than its parent. Authority only decreases down the tree.

```
     THE PRIMORDIAL (~0ULL = all authorities)
              │
     ┌────────┴────────┐
     ▼                 ▼
  System           System
   Sov              Sov
 (auth A)        (auth B)
     │               │
     ▼               ▼
   App             App
   Sov            Sov
(auth A')       (auth B')  ← Always: A' ⊆ A, B' ⊆ B
```

### 3. VOID Propagation

When a Sovereign dies, all capabilities pointing to it become VOID. References to dead Sovereigns naturally fail without explicit null checks.

### 4. State Machine

Sovereigns transition through well-defined states:

```
  CONCEIVING → NASCENT → RUNNING ↔ WAITING ↔ SUSPENDED → TERMINAL → VOID
      │           │          │         │          │           │
      │           │          │         │          │           └─ Final state
      │           │          │         │          └─ Frozen by parent
      │           │          │         └─ Blocked on I/O
      │           │          └─ Actively executing
      │           └─ Setup complete, awaiting vivify
      └─ Being configured
```

## THE PRIMORDIAL

THE PRIMORDIAL is the root Sovereign:

- **Created at boot** with all authorities (`SERAPH_AUTH_PRIMORDIAL = ~0ULL`)
- **Has no parent** (parent_id is VOID)
- **Cannot be killed** - if THE PRIMORDIAL exits, the system halts
- **Is the ancestor** of all other Sovereigns

### PRIMORDIAL Authority Semantics

**Critical Implementation Note**: `SERAPH_AUTH_PRIMORDIAL` equals `~0ULL`, which is the same bit pattern as `SERAPH_VOID_U64`. However:

- In **data context**: `~0ULL` = VOID (absence/error)
- In **authority context**: `~0ULL` = all authorities (full power)

The code correctly distinguishes these contexts. Authority validation and capability checks treat `~0ULL` as "all authority", not as VOID.

## Sovereign Anatomy

### Memory Structure

```c
typedef struct Seraph_Sovereign {
    /* Identity */
    Seraph_Sovereign_ID id;           /* 256-bit unforgeable ID */
    Seraph_Sovereign_ID parent_id;    /* VOID for THE PRIMORDIAL */

    /* Authority */
    uint64_t authority;               /* Bitmask of allowed operations */

    /* Lifecycle */
    Seraph_Sovereign_State state;     /* Current state */
    Seraph_Chronon birth_chronon;     /* When created */
    Seraph_Chronon last_active;       /* Last execution time */
    uint32_t exit_code;               /* If terminated */

    /* Capability Table */
    Seraph_Cap_Entry capabilities[MAX];  /* All held capabilities */
    uint32_t cap_count;
    uint32_t cap_generation;

    /* Children */
    Seraph_Child_Entry children[MAX];    /* Child Sovereigns */
    uint32_t child_count;

    /* Memory Arenas */
    Seraph_Arena* primary_arena;      /* Main heap */
    Seraph_Arena* code_arena;         /* Executable code */
    Seraph_Arena* scratch_arena;      /* Temporary allocations */
    uint64_t memory_limit;
    uint64_t memory_used;

    /* Strands (green threads) */
    struct Seraph_Strand* strands[MAX];
    uint32_t strand_count;
} Seraph_Sovereign;
```

## Authority Flags

Authorities are a 64-bit bitmask defining what operations a Sovereign may perform.

### Core Lifecycle Authorities

| Flag | Bit | Description |
|------|-----|-------------|
| `SERAPH_AUTH_SPAWN` | 0 | Can create child Sovereigns |
| `SERAPH_AUTH_KILL` | 1 | Can terminate child Sovereigns |
| `SERAPH_AUTH_SUSPEND` | 2 | Can suspend/resume children |

### Capability Management

| Flag | Bit | Description |
|------|-----|-------------|
| `SERAPH_AUTH_GRANT` | 8 | Can permanently transfer capabilities |
| `SERAPH_AUTH_LEND` | 9 | Can temporarily lend capabilities |
| `SERAPH_AUTH_REVOKE` | 10 | Can revoke granted/lent capabilities |
| `SERAPH_AUTH_DERIVE` | 11 | Can create narrowed capability copies |

### Memory Authorities

| Flag | Bit | Description |
|------|-----|-------------|
| `SERAPH_AUTH_ARENA_CREATE` | 16 | Can create new Spectral Arenas |
| `SERAPH_AUTH_ARENA_DESTROY` | 17 | Can destroy owned Arenas |
| `SERAPH_AUTH_ARENA_RESIZE` | 18 | Can grow/shrink Arena bounds |
| `SERAPH_AUTH_MEMORY_EXECUTE` | 19 | Can mark memory executable |

### Temporal Authorities

| Flag | Bit | Description |
|------|-----|-------------|
| `SERAPH_AUTH_CHRONON_READ` | 24 | Can read current Chronon |
| `SERAPH_AUTH_CHRONON_WAIT` | 25 | Can wait for Chronon threshold |
| `SERAPH_AUTH_CHRONON_INJECT` | 26 | Can inject events into Chronon stream |

### Threading Authorities

| Flag | Bit | Description |
|------|-----|-------------|
| `SERAPH_AUTH_STRAND_CREATE` | 32 | Can create new Strands |
| `SERAPH_AUTH_STRAND_JOIN` | 33 | Can wait for Strand completion |
| `SERAPH_AUTH_STRAND_KILL` | 34 | Can terminate Strands |

### I/O Authorities

| Flag | Bit | Description |
|------|-----|-------------|
| `SERAPH_AUTH_CONDUIT_OPEN` | 40 | Can open new Conduits |
| `SERAPH_AUTH_CONDUIT_READ` | 41 | Can read from Conduits |
| `SERAPH_AUTH_CONDUIT_WRITE` | 42 | Can write to Conduits |

### Input/Display Authorities

| Flag | Bit | Description |
|------|-----|-------------|
| `SERAPH_AUTH_SENSE_ATTACH` | 48 | Can attach to input devices |
| `SERAPH_AUTH_SENSE_GRAB` | 49 | Can grab exclusive input focus |
| `SERAPH_AUTH_GLYPH_CREATE` | 56 | Can create Glyphs |
| `SERAPH_AUTH_GLYPH_RENDER` | 57 | Can submit Glyphs for rendering |
| `SERAPH_AUTH_FRAMEBUFFER` | 58 | Can access raw framebuffer |

### Composite Masks

| Mask | Description |
|------|-------------|
| `SERAPH_AUTH_NONE` | No authorities (0) |
| `SERAPH_AUTH_MINIMAL` | Just `CHRONON_READ` |
| `SERAPH_AUTH_WORKER` | Read/wait chronon, read/write conduits |
| `SERAPH_AUTH_APPLICATION` | Worker + strands, arenas, glyphs, input |
| `SERAPH_AUTH_SYSTEM` | Application + spawn, kill, grant/lend/revoke |
| `SERAPH_AUTH_PRIMORDIAL` | All authorities (~0ULL) |

## Sovereign Lifecycle

### 1. Conception (conceive)

A parent creates a new Sovereign in NASCENT state:

```c
Seraph_Spawn_Config config = {
    .authority = SERAPH_AUTH_APPLICATION,
    .memory_limit = 16 * 1024 * 1024,  /* 16 MB */
};

Seraph_Capability child = seraph_sovereign_conceive(self, config);
```

The new Sovereign:
- Has a unique 256-bit ID
- Has authority that is a subset of parent's
- Is in NASCENT state (not yet running)
- Has its arenas allocated

### 2. Capability Grants (grant_cap)

Before vivification, the parent grants capabilities to the child:

```c
Seraph_Capability resource = /* ... */;
seraph_sovereign_grant_cap(child, resource, SERAPH_GRANT_COPY);
```

Grant flags:
- `SERAPH_GRANT_COPY`: Child gets a copy, parent retains original
- `SERAPH_GRANT_TRANSFER`: Parent loses the capability
- `SERAPH_GRANT_NARROW`: Apply additional permission restrictions

### 3. Code Loading (load_code)

Load executable code into the child's code arena:

```c
seraph_sovereign_load_code(child, code_ptr, code_size, load_addr);
```

### 4. Vivification (vivify)

Transition the child from NASCENT to RUNNING:

```c
seraph_sovereign_vivify(child);  /* Child begins execution */
```

### 5. Termination

Children can terminate in several ways:

**Voluntary exit**:
```c
seraph_sovereign_exit(exit_code);  /* From within the Sovereign */
```

**Parent kill**:
```c
seraph_sovereign_kill(child);  /* From parent */
```

**VOID violation**: If a Sovereign attempts an unauthorized operation, it is VOIDED (forcibly terminated with cascading termination of children).

### 6. Waiting

Parents can wait for child termination:

```c
uint32_t exit_code;
Seraph_Vbit result = seraph_sovereign_wait(child, timeout, &exit_code);
```

## Suspension and Resume

Parents can freeze and thaw children:

```c
seraph_sovereign_suspend(child);  /* Freeze all execution */
seraph_sovereign_resume(child);   /* Resume execution */
```

Suspended Sovereigns:
- Cannot execute any Strands
- Have their Chronon stream frozen
- Retain all capabilities and state
- Can be resumed at any time

## State Queries

### Check Current State

```c
Seraph_Sovereign_State state = seraph_sovereign_get_state(cap);

if (seraph_sovereign_state_is_alive(state)) {
    /* Can execute (NASCENT, RUNNING, WAITING, SUSPENDED) */
}

if (seraph_sovereign_state_is_terminal(state)) {
    /* Dying or dead (EXITING, KILLED, VOIDED, VOID) */
}
```

### Get Identity

```c
Seraph_Sovereign_ID id = seraph_sovereign_get_id(cap);
ASSERT(!seraph_sovereign_id_is_void(id));
ASSERT_VBIT_TRUE(seraph_sovereign_id_validate(id));
```

### Current Sovereign Context

```c
Seraph_Sovereign* current = seraph_sovereign_current();
Seraph_Capability self = seraph_sovereign_self();
uint64_t my_auth = seraph_sovereign_get_authority();
```

## Security Properties

### 1. No Ambient Authority

A Sovereign has NO default access to anything. Every resource access requires an explicit capability. There is no equivalent of Unix's "root can do anything."

### 2. Capability Unforgability

Capabilities cannot be:
- Guessed (random 256-bit ID)
- Forged (checksum validation)
- Reused (generation tracking)

### 3. Temporal Safety

The generation counter in capabilities prevents use-after-free attacks. When a Sovereign dies, all capabilities referencing it become invalid.

### 4. Authority Attenuation

Authority can only decrease:
```
Parent.authority ⊇ Child.authority
```

No child can ever gain authority its parent doesn't have.

### 5. VOID Propagation

Errors and invalid states propagate naturally:
- VOID capabilities cannot be dereferenced
- VOID IDs fail validation
- Operations on VOID return VOID

## API Reference

### ID Operations

```c
/* Generate new ID with embedded authority */
Seraph_Sovereign_ID seraph_sovereign_id_generate(uint64_t authority);

/* Validate ID checksum */
Seraph_Vbit seraph_sovereign_id_validate(Seraph_Sovereign_ID id);

/* Check if ID is VOID */
bool seraph_sovereign_id_is_void(Seraph_Sovereign_ID id);

/* Compare IDs (returns VOID if either is VOID) */
Seraph_Vbit seraph_sovereign_id_equal(Seraph_Sovereign_ID a, Seraph_Sovereign_ID b);
```

### Lifecycle Operations

```c
/* Initialize the subsystem (creates THE PRIMORDIAL) */
void seraph_sovereign_subsystem_init(void);

/* Shut down (terminates all Sovereigns) */
void seraph_sovereign_subsystem_shutdown(void);

/* Create child Sovereign (returns cap to child) */
Seraph_Capability seraph_sovereign_conceive(
    Seraph_Capability parent_cap,
    Seraph_Spawn_Config config
);

/* Grant capability to nascent child */
Seraph_Vbit seraph_sovereign_grant_cap(
    Seraph_Capability child_cap,
    Seraph_Capability cap_to_grant,
    Seraph_Grant_Flags flags
);

/* Load code into nascent child */
Seraph_Vbit seraph_sovereign_load_code(
    Seraph_Capability child_cap,
    const void* code,
    uint64_t code_size,
    uint64_t load_addr
);

/* Start child execution */
Seraph_Vbit seraph_sovereign_vivify(Seraph_Capability child_cap);
```

### Termination Operations

```c
/* Exit current Sovereign (voluntary) */
void seraph_sovereign_exit(uint32_t exit_code);

/* Kill child Sovereign (requires KILL authority) */
Seraph_Vbit seraph_sovereign_kill(Seraph_Capability child_cap);

/* Wait for child termination */
Seraph_Vbit seraph_sovereign_wait(
    Seraph_Capability child_cap,
    Seraph_Chronon timeout,
    uint32_t* exit_code
);
```

### Suspension Operations

```c
/* Suspend child (requires SUSPEND authority) */
Seraph_Vbit seraph_sovereign_suspend(Seraph_Capability child_cap);

/* Resume suspended child */
Seraph_Vbit seraph_sovereign_resume(Seraph_Capability child_cap);
```

### Query Operations

```c
/* Get current Sovereign pointer */
Seraph_Sovereign* seraph_sovereign_current(void);

/* Get capability to self */
Seraph_Capability seraph_sovereign_self(void);

/* Get capability to parent (VOID for PRIMORDIAL) */
Seraph_Capability seraph_sovereign_parent(void);

/* Get current Sovereign's authority */
uint64_t seraph_sovereign_get_authority(void);

/* Get state from capability */
Seraph_Sovereign_State seraph_sovereign_get_state(Seraph_Capability cap);

/* Get ID from capability */
Seraph_Sovereign_ID seraph_sovereign_get_id(Seraph_Capability cap);
```

### Authority Operations

```c
/* Check if parent authority covers child authority */
Seraph_Vbit seraph_authority_valid(uint64_t parent, uint64_t child);

/* Check if authority has specific flags */
bool seraph_authority_has(uint64_t authority, uint64_t required);
```

## Implementation Files

- **Header**: `include/seraph/sovereign.h` (578 lines)
- **Implementation**: `src/sovereign.c` (842 lines)
- **Tests**: `tests/test_sovereign.c` (914 lines)

## Test Coverage

The implementation includes 53 comprehensive tests covering:

- State enumeration (is_void, is_alive, is_terminal)
- Authority validation (subset, superset, equal, none, primordial)
- ID operations (generation, validation, equality, corruption detection)
- Subsystem initialization (primordial creation)
- Context operations (current, self, parent, authority, state, ID)
- Creation lifecycle (conceive, grant, load, vivify)
- Termination (kill, wait, exit)
- Suspension (suspend, resume)
- VOID propagation throughout

## Future Enhancements

The current implementation is foundational. Future work includes:

1. **Full Strand Integration**: Green thread creation and scheduling within Sovereigns
2. **Whisper IPC**: Zero-copy inter-Sovereign communication
3. **Capability Revocation**: Time-limited and revocable capability grants
4. **Hierarchical Termination**: Proper cascading termination of child trees
5. **Memory Quotas**: Enforcement of memory limits
6. **Cryptographic ID Generation**: Replace RNG with true CSPRNG
7. **Persistent Sovereigns**: Atlas integration for persistent state

## Relationship to Other Components

```
┌─────────────────────────────────────────────────────────────────┐
│                         SOVEREIGN                               │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Identity: Sovereign_ID (256-bit)                        │   │
│  │  Authority: uint64_t bitmask                             │   │
│  │                                                          │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │   │
│  │  │   ARENA     │  │   ARENA     │  │   ARENA     │       │   │
│  │  │  (primary)  │  │   (code)    │  │  (scratch)  │       │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘       │   │
│  │                                                          │   │
│  │  ┌───────────────────────────────────────────────────┐   │   │
│  │  │           CAPABILITY TABLE                        │   │   │
│  │  │  [slot 0] [slot 1] [slot 2] ... [slot N]          │   │   │
│  │  └───────────────────────────────────────────────────┘   │   │
│  │                                                          │   │
│  │  ┌───────────────────────────────────────────────────┐   │   │
│  │  │           STRAND POOL                             │   │   │
│  │  │  [strand 0] [strand 1] [strand 2] ...             │   │   │
│  │  └───────────────────────────────────────────────────┘   │   │
│  │                                                          │   │
│  │  ┌───────────────────────────────────────────────────┐   │   │
│  │  │           CHILD REGISTRY                          │   │   │
│  │  │  [child 0] [child 1] [child 2] ...                │   │   │
│  │  └───────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
    ┌─────────┐         ┌─────────┐         ┌─────────┐
    │ CHRONON │         │CAPABILITY│        │ WHISPER │
    │ (time)  │         │ (access) │        │  (IPC)  │
    └─────────┘         └─────────┘         └─────────┘
```

The Sovereign is the central organizing principle. It owns:
- **Arenas**: All memory belongs to a Sovereign (via Arena)
- **Capabilities**: All access tokens belong to a Sovereign
- **Strands**: All threads belong to a Sovereign
- **Children**: All spawned processes belong to their parent Sovereign

And it coordinates with:
- **Chronon**: For temporal tracking and scheduling
- **Whisper**: For inter-Sovereign communication
- **Glyph**: For rendering (via capability)

## Source Files

| File | Description |
|------|-------------|
| `src/sovereign.c` | Process isolation, capability table management |
| `src/capability.c` | Capability creation, validation, revocation |
| `src/foreign_substrate.c` | VMX-based process isolation (hardware virtualization) |
| `src/vmx.c` | Intel VT-x hypervisor support |
| `include/seraph/sovereign.h` | Sovereign structure, API |
| `include/seraph/capability.h` | Capability types, operations |
| `include/seraph/vmx.h` | VMX structures, VMCS fields |
