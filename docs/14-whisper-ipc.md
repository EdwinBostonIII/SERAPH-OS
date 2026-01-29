# MC12: Whisper - Capability-Based Zero-Copy IPC

## Overview

Whisper is SERAPH's revolutionary inter-process communication system. Unlike traditional IPC that copies data between processes, Whisper transfers **capabilities**. The data itself never moves - only the **authority** to access it moves.

**"A message is not 'data in transit.' A message is the DELEGATION OF AUTHORITY."**

## Plain English Explanation

Imagine you want to share a 1GB video file with another program:

**Traditional IPC (Unix pipes, shared memory, sockets):**
1. Your process copies 1GB to a kernel buffer
2. The kernel copies 1GB to the other process's buffer
3. Total: 2GB of memory bandwidth, 2GB of CPU time for copying

**SERAPH Whisper:**
1. Your process sends a 256-byte message containing a capability to the 1GB buffer
2. The other process uses that capability to access the buffer directly
3. Total: 256 bytes transferred. The 1GB never moves.

The key insight: **Don't send data. Send the AUTHORITY to access data.**

## Architecture

### The Whisper Message (256 bytes)

Every Whisper message is exactly 256 bytes (4 cache lines):

```
┌─────────────────────────────────────────────────────────────┐
│ HEADER (40 bytes)                                           │
├─────────────────────────────────────────────────────────────┤
│ message_id     : 8 bytes - Unique identifier                │
│ sender_id      : 8 bytes - Who sent this                    │
│ send_chronon   : 8 bytes - When it was sent                 │
│ type           : 4 bytes - Message type (REQUEST, GRANT...) │
│ cap_count      : 1 byte  - Number of capabilities (0-7)     │
│ flags          : 2 bytes - URGENT, REPLY_REQ, etc.          │
│ lend_timeout   : 4 bytes - Timeout for LEND messages        │
│ (padding)      : 5 bytes - Alignment                        │
├─────────────────────────────────────────────────────────────┤
│ CAPABILITIES (168 bytes = 7 x 24 bytes)                     │
│ Up to 7 Seraph_Capability tokens                            │
├─────────────────────────────────────────────────────────────┤
│ RESERVED (48 bytes)                                         │
│ Future extensions                                           │
└─────────────────────────────────────────────────────────────┘
```

### Message Types

```c
typedef enum {
    SERAPH_WHISPER_REQUEST,      /* Expects a response */
    SERAPH_WHISPER_RESPONSE,     /* Reply to a request */
    SERAPH_WHISPER_NOTIFICATION, /* One-way, no response */
    SERAPH_WHISPER_GRANT,        /* Permanent capability transfer */
    SERAPH_WHISPER_LEND,         /* Temporary capability loan */
    SERAPH_WHISPER_RETURN,       /* Return a borrowed capability */
    SERAPH_WHISPER_DERIVE,       /* Send restricted version */
    SERAPH_WHISPER_COPY,         /* Share read-only capability */
    SERAPH_WHISPER_VOID          /* Invalid/closed channel */
} Seraph_Whisper_Type;
```

## Transfer Modes

### GRANT - Permanent Transfer

"I give you this forever."

```
BEFORE                           AFTER
┌─────────────┐                  ┌─────────────┐
│ SOVEREIGN A │                  │ SOVEREIGN A │
│ cap: [DATA] │  whisper_grant   │ cap: [VOID] │
└─────────────┘ ───────────────► └─────────────┘

┌─────────────┐                  ┌─────────────┐
│ SOVEREIGN B │                  │ SOVEREIGN B │
│ cap: [NONE] │                  │ cap: [DATA] │
└─────────────┘                  └─────────────┘
```

The sender loses the capability. The receiver gains it.

### LEND - Temporary Transfer with Full Tracking

"Borrow this, but give it back."

```
BEFORE              DURING LEND           AFTER TIMEOUT
┌─────────────┐     ┌─────────────┐       ┌─────────────┐
│ SOVEREIGN A │     │ SOVEREIGN A │       │ SOVEREIGN A │
│ cap: [OWNED]│     │ cap: [LENT] │       │ cap: [OWNED]│
└─────────────┘     └─────────────┘       └─────────────┘

┌─────────────┐     ┌─────────────┐       ┌─────────────┐
│ SOVEREIGN B │     │ SOVEREIGN B │       │ SOVEREIGN B │
│ cap: [NONE] │     │ cap:[BORROW]│       │ cap: [VOID] │
└─────────────┘     └─────────────┘       └─────────────┘
```

The capability automatically returns when:
- Timeout expires (automatic revocation)
- Borrower explicitly returns it (early return)
- Lender manually revokes it (forced revocation)
- Borrower terminates

#### LEND Registry

Every endpoint maintains a **Lend Registry** that tracks all capabilities it has lent out:

```c
typedef struct {
    Seraph_Capability original_cap;    /* The cap that was lent */
    Seraph_Capability borrowed_cap;    /* Cap given to borrower */
    uint64_t lend_message_id;          /* Message ID for matching RETURN */
    Seraph_Chronon lend_chronon;       /* When lend started */
    Seraph_Chronon expiry_chronon;     /* When it auto-expires (0=never) */
    uint64_t borrower_endpoint_id;     /* Who borrowed it */
    Seraph_Whisper_Lend_Status status; /* ACTIVE, RETURNED, EXPIRED, REVOKED */
} Seraph_Whisper_Lend_Record;
```

**Status Lifecycle:**
```
    ┌──────────────────────────────────────────────────────────────┐
    │                         LEND LIFECYCLE                        │
    └──────────────────────────────────────────────────────────────┘

    ┌──────────┐
    │   VOID   │  (empty slot in registry)
    └────┬─────┘
         │ seraph_whisper_lend()
         ▼
    ┌──────────┐
    │  ACTIVE  │  (capability currently lent out)
    └────┬─────┘
         │
         ├─────── process_lends() timeout ─────────► ┌──────────┐
         │                                           │ EXPIRED  │
         │                                           └──────────┘
         │
         ├─────── handle_return() ─────────────────► ┌──────────┐
         │                                           │ RETURNED │
         │                                           └──────────┘
         │
         └─────── revoke_lend() ───────────────────► ┌──────────┐
                                                     │ REVOKED  │
                                                     └──────────┘
```

#### Lend Management Functions

```c
/* Process expired lends - call periodically or before accessing caps */
uint32_t seraph_whisper_process_lends(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Chronon current_chronon
);

/* Manually revoke a lend before timeout */
Seraph_Vbit seraph_whisper_revoke_lend(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
);

/* Check if a lend is still active */
Seraph_Vbit seraph_whisper_lend_is_active(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
);

/* Get the lend record for inspection */
Seraph_Whisper_Lend_Record* seraph_whisper_get_lend_record(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t lend_message_id
);

/* Get count of active lends */
uint32_t seraph_whisper_active_lend_count(Seraph_Whisper_Endpoint* endpoint);
```

#### Why Lend Tracking Prevents Memory Leaks

Without tracking:
1. A lends cap to B with timeout 1000
2. B crashes at time 500 without returning
3. **A never knows B is gone - cap is stuck in limbo forever**

With tracking:
1. A lends cap to B, creating a Lend_Record with expiry_chronon = 1000
2. B crashes at time 500
3. At time 1000, A calls `seraph_whisper_process_lends(endpoint, 1000)`
4. The Lend_Record is marked EXPIRED
5. A can now access the capability again
6. **No memory leak - the cap is reclaimed automatically**

### COPY - Shared Read-Only

"We both can read this."

Both processes get read-only access to the same resource. Safe because reads don't conflict.

### DERIVE - Restricted Copy

"You get a limited version."

The sender keeps full access. The receiver gets a restricted capability (e.g., read-only, smaller bounds).

## Whisper Channels

A Whisper Channel is a bidirectional communication endpoint between two Sovereigns.

```c
typedef struct {
    Seraph_Whisper_Endpoint parent_end;  /* Parent's endpoint */
    Seraph_Whisper_Endpoint child_end;   /* Child's endpoint */
    uint64_t channel_id;                 /* Unique identifier */
    Seraph_Vbit active;                  /* Is channel active? */
    uint64_t generation;                 /* For capability validation */
} Seraph_Whisper_Channel;
```

Each endpoint has:
- **Send queue**: Outgoing messages (ring buffer of 64 messages)
- **Recv queue**: Incoming messages (ring buffer of 64 messages)
- **Atomic indices**: Lock-free operation
- **Statistics**: Sent, received, dropped counts

## API Reference

### Channel Operations

```c
/* Create a new bidirectional channel */
Seraph_Whisper_Channel seraph_whisper_channel_create(void);

/* Initialize channel in-place */
Seraph_Vbit seraph_whisper_channel_init(Seraph_Whisper_Channel* channel);

/* Close channel (marks inactive) */
Seraph_Vbit seraph_whisper_channel_close(Seraph_Whisper_Channel* channel);

/* Destroy channel (invalidates capabilities) */
void seraph_whisper_channel_destroy(Seraph_Whisper_Channel* channel);

/* Get capability to one end */
Seraph_Capability seraph_whisper_channel_get_cap(
    Seraph_Whisper_Channel* channel,
    bool is_child_end
);
```

### Message Construction

```c
/* Create a new message */
Seraph_Whisper_Message seraph_whisper_message_new(Seraph_Whisper_Type type);

/* Add a capability to message (max 7) */
Seraph_Vbit seraph_whisper_message_add_cap(
    Seraph_Whisper_Message* msg,
    Seraph_Capability cap
);

/* Get capability from message */
Seraph_Capability seraph_whisper_message_get_cap(
    Seraph_Whisper_Message* msg,
    uint8_t index
);
```

### Send Operations

```c
/* Send a message */
Seraph_Vbit seraph_whisper_send(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Whisper_Message message
);

/* Grant a capability permanently */
Seraph_Vbit seraph_whisper_grant(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap
);

/* Lend a capability temporarily */
Seraph_Vbit seraph_whisper_lend(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    Seraph_Chronon timeout
);

/* Send a request (expects response) */
uint64_t seraph_whisper_request(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability* caps,
    uint8_t cap_count,
    uint16_t flags
);

/* Send a response */
Seraph_Vbit seraph_whisper_respond(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t request_id,
    Seraph_Capability* caps,
    uint8_t cap_count
);

/* Send a notification (no response expected) */
Seraph_Vbit seraph_whisper_notify(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability* caps,
    uint8_t cap_count
);
```

### Receive Operations

```c
/* Receive a message */
Seraph_Whisper_Message seraph_whisper_recv(
    Seraph_Whisper_Endpoint* endpoint,
    bool blocking
);

/* Peek without removing */
Seraph_Whisper_Message seraph_whisper_peek(Seraph_Whisper_Endpoint* endpoint);

/* Check if messages available */
Seraph_Vbit seraph_whisper_available(Seraph_Whisper_Endpoint* endpoint);

/* Get pending message count */
uint32_t seraph_whisper_pending_count(Seraph_Whisper_Endpoint* endpoint);

/* Wait for specific response */
Seraph_Whisper_Message seraph_whisper_await_response(
    Seraph_Whisper_Endpoint* endpoint,
    uint64_t request_id,
    uint32_t max_wait
);
```

### Return Operations

```c
/* Return a borrowed capability early (matches by capability base) */
Seraph_Vbit seraph_whisper_return_cap(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap
);

/* Return with explicit lend message ID (more precise matching) */
Seraph_Vbit seraph_whisper_return_cap_by_id(
    Seraph_Whisper_Endpoint* endpoint,
    Seraph_Capability cap,
    uint64_t lend_message_id
);
```

The `_by_id` variant is preferred when you know the original LEND message ID, as it provides exact matching. The basic `return_cap` matches by the capability's base address.

## RPC Pattern Example

```c
/* Client side */
void client_read_file(Seraph_Whisper_Endpoint* ep) {
    /* Create request with path and output buffer capabilities */
    Seraph_Capability caps[2] = {
        path_cap,      /* Where to read filename from */
        output_cap     /* Where to write file contents */
    };

    /* Send request */
    uint64_t req_id = seraph_whisper_request(ep, caps, 2,
        SERAPH_WHISPER_FLAG_REPLY_REQ);

    /* Wait for response */
    Seraph_Whisper_Message resp = seraph_whisper_await_response(
        ep, req_id, 0
    );

    /* output_cap now contains the file data! */
}

/* Server side */
void server_handle_request(Seraph_Whisper_Endpoint* ep) {
    Seraph_Whisper_Message req = seraph_whisper_recv(ep, true);

    Seraph_Capability path_cap = seraph_whisper_message_get_cap(&req, 0);
    Seraph_Capability output_cap = seraph_whisper_message_get_cap(&req, 1);

    /* Read file into client's buffer via capability */
    const char* path = seraph_cap_get_ptr(path_cap);
    void* output = seraph_cap_get_ptr(output_cap);
    size_t bytes = read_file(path, output, seraph_cap_size(output_cap));

    /* Send response with status */
    Seraph_Capability status_cap = /* ... */;
    seraph_whisper_respond(ep, req.message_id, &status_cap, 1);
}
```

**Total data copies: 0.** The file was read directly into the client's buffer.

## VOID Semantics

Whisper fully embraces VOID:
- Closed channels return VOID messages
- Dead endpoints cause send to return VOID
- Invalid operations propagate VOID
- Message with VOID type indicates channel death

## Test Coverage

43 comprehensive tests covering:
- Message creation and manipulation
- Unique ID generation
- Capability packing (max 7 per message)
- Channel lifecycle (create, init, close, destroy)
- Bidirectional communication
- Grant/Lend/Return semantics
- Request/Response patterns
- Queue management (peek, pending, available)
- Statistics tracking
- Error handling (closed channels, null endpoints)
- **Lend Tracking Tests (11 new):**
  - Lend creates registry entry
  - Registry tracks message ID
  - Timeout expiration processing
  - Manual revocation
  - Early return (by cap match)
  - Early return (by message ID)
  - Multiple concurrent lends
  - Revoke non-existent lend (error handling)
  - VOID input handling
  - Lend record inspection
  - Zero timeout (never expires)

## Integration with Other Components

- **Capability (MC6)**: Messages ARE capability transfers
- **Chronon (MC7)**: Message timestamps, lend timeouts
- **Sovereign (MC10)**: Channels connect Sovereigns
- **VOID (MC0)**: Error propagation throughout

## Performance Characteristics

- **Zero-copy**: Data never moves, only capabilities
- **Lock-free**: Atomic queue operations
- **Fixed-size**: 256-byte messages (4 cache lines)
- **Bounded queues**: No unbounded memory growth
- **Generation-tracked**: Temporal safety

## Summary

Whisper transforms IPC from "moving data" to "delegating authority":

1. **Zero copies** - Capability transfer only
2. **Clear ownership** - Whoever holds the cap owns the data
3. **No races** - Only one holder can access at a time
4. **Deterministic cleanup** - Memory freed when last cap dropped
5. **Type-safe** - GRANT, LEND, COPY, DERIVE semantics

This is not just an optimization - it's a fundamental paradigm shift in how processes communicate.

## Source Files

| File | Description |
|------|-------------|
| `src/whisper.c` | Zero-copy IPC, capability transfer, message queues |
| `include/seraph/whisper.h` | Whisper API, message types, channel structures |
