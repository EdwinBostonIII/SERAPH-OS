# MC7: Chronon - Causal Time

## What is Chronon?

**Plain English**: Chronon is SERAPH's time system that tracks *causality* instead of wall-clock time. Rather than asking "what time did this happen?" Chronon asks "what happened before this?" This sounds strange, but it solves critical problems in both local and distributed systems.

**The Problem**: Wall-clock time is unreliable. Clocks drift. NTP corrections can make time jump forward or backward. In a distributed system, machines have different clocks. You can't trust "3:00pm on Machine A" means the same thing as "3:00pm on Machine B."

**The Solution**: Chronon uses logical timestamps that only increase, never decrease. Every event gets a number that's guaranteed to be higher than any event that causally preceded it. No clock synchronization needed.

**Analogy**: Think of a deli counter with numbered tickets. You don't care what time it is - you only care that ticket #47 will be served before ticket #48. If two people got their tickets independently (one at each end of the counter), you can't say who was "first" - they're concurrent.

## Why Causality Matters

### The Distributed Systems Problem

Consider two bank accounts being debited simultaneously from different ATMs:

```
ATM 1 (New York): Debit $100 from Account A at 3:00:00.000
ATM 2 (London):   Debit $100 from Account A at 3:00:00.001

Which happened first?
- If New York's clock is 50ms fast, London was actually first
- If London's clock is 100ms fast, New York was actually first
- You literally cannot know from wall-clock time alone
```

With Chronon, we don't ask "which was first in absolute time." We ask "did either operation *know about* the other?" If not, they're concurrent, and we need conflict resolution.

### What "Happens-Before" Means

Event A "happens-before" event B (written A → B) if:

1. **Same process, sequential order**: A happened before B in the same execution thread
2. **Message passing**: A sent a message that B received
3. **Transitivity**: If A → C and C → B, then A → B

If neither A → B nor B → A, then A and B are **concurrent** (A || B). This isn't about timing - it's about whether one event could have *influenced* the other.

## Local Timestamps (Lamport Clocks)

### The Simplest Logical Clock

A Lamport clock is just a counter that follows three rules:

1. **Start at zero**: `clock = 0`
2. **Tick before each event**: `clock = clock + 1`
3. **Merge on receive**: `clock = max(clock, received) + 1`

```c
Seraph_LocalClock clock;
seraph_localclock_init(&clock, node_id);

// Local event
seraph_localclock_tick(&clock);  // Returns 1

// Another local event
seraph_localclock_tick(&clock);  // Returns 2

// Receive message with timestamp 10
seraph_localclock_merge(&clock, 10);  // Returns 11 (max(2, 10) + 1)
```

### Limitation: Can't Detect Concurrency

Lamport clocks guarantee that if A → B, then `timestamp(A) < timestamp(B)`. But the reverse isn't true! If `timestamp(A) < timestamp(B)`, we can't conclude A → B - they might be concurrent.

This is why SERAPH also provides vector clocks.

## Vector Clocks

### Tracking Causality Precisely

A vector clock is an array of timestamps, one per node in the system. Each node maintains:
- Its own component (which it increments)
- Its view of every other node's progress (updated via messages)

```c
// 3-node system, we are node 0
Seraph_VectorClock vclock;
seraph_vclock_init(&vclock, 3, 0);  // [0, 0, 0]

// Do local work
seraph_vclock_tick(&vclock);  // [1, 0, 0]

// Receive from node 1 with their clock [0, 5, 2]
Seraph_Chronon received[] = {0, 5, 2};
seraph_vclock_receive(&vclock, received, 3);  // [2, 5, 2]
// Component-wise max: [max(1,0), max(0,5), max(0,2)] = [1, 5, 2]
// Then tick our component: [2, 5, 2]
```

### The Comparison Algorithm

To compare vector clocks A and B:

```
If A[i] <= B[i] for all i, AND A[j] < B[j] for some j: A → B (A happens-before B)
If B[i] <= A[i] for all i, AND B[j] < A[j] for some j: B → A (B happens-before A)
If A[i] == B[i] for all i: A == B (same event)
Otherwise: A || B (concurrent)
```

Example:
```
A = [2, 3, 1]
B = [2, 4, 1]

A[0] <= B[0]: 2 <= 2 ✓
A[1] <= B[1]: 3 <= 4 ✓ (and strictly less)
A[2] <= B[2]: 1 <= 1 ✓

Result: A → B
```

Another example:
```
A = [2, 3, 1]
B = [1, 4, 1]

A[0] <= B[0]: 2 <= 1 ✗ (A[0] > B[0])
A[1] <= B[1]: 3 <= 4 ✓ (A[1] < B[1])

Since we have both A[i] > B[i] and A[j] < B[j], these are concurrent: A || B
```

### SERAPH Implementation

```c
Seraph_VectorClock a, b;
seraph_vclock_init(&a, 3, 0);
seraph_vclock_init(&b, 3, 1);

// ... operations ...

Seraph_CausalOrder order = seraph_vclock_compare(&a, &b);
// Returns: SERAPH_CAUSAL_BEFORE, SERAPH_CAUSAL_AFTER,
//          SERAPH_CAUSAL_CONCURRENT, SERAPH_CAUSAL_EQUAL, or SERAPH_CAUSAL_VOID

Seraph_Vbit hb = seraph_vclock_happens_before(&a, &b);
// Returns: TRUE if a → b, FALSE if not, VOID if can't determine

Seraph_Vbit conc = seraph_vclock_is_concurrent(&a, &b);
// Returns: TRUE if a || b, FALSE if ordered, VOID if invalid
```

## Events and Causal Chains

### What is an Event?

An event is an immutable record of something that happened at a specific logical time:

```c
typedef struct {
    Seraph_Chronon timestamp;      // When (logical time)
    uint64_t       predecessor;    // Hash of causal predecessor
    uint32_t       source_id;      // Who (which node)
    uint32_t       sequence;       // Local sequence number
    uint64_t       payload_hash;   // What (hash of event data)
} Seraph_Event;
```

### Building a Causal Chain

Events link to their predecessors via hashes, forming a directed acyclic graph (DAG):

```c
Seraph_LocalClock clock;
seraph_localclock_init(&clock, 0);

// Genesis event (no predecessor)
Seraph_Chronon t1 = seraph_localclock_tick(&clock);
Seraph_Event genesis = seraph_event_create(t1, 0, 0, hash_of_data);

// Chained event
Seraph_Chronon t2 = seraph_localclock_tick(&clock);
Seraph_Event child = seraph_event_chain(genesis, t2, 0, 1, hash_of_data);
// child.predecessor == seraph_event_hash(genesis)
```

### Why Hash Linking?

1. **Integrity**: If anyone modifies an event, its hash changes, breaking all links to it
2. **Uniqueness**: Each event has a unique identifier (its hash)
3. **Efficiency**: 64-bit hash is smaller than storing full predecessor

## VOID Semantics

### Invalid Timestamps

Like all SERAPH types, Chronon values can be VOID:

```c
// VOID timestamp (all 1s)
SERAPH_CHRONON_VOID == 0xFFFFFFFFFFFFFFFF

// Check for VOID
seraph_chronon_is_void(timestamp);  // true if VOID

// VOID propagates
seraph_chronon_add(SERAPH_CHRONON_VOID, 1);  // Returns VOID
seraph_chronon_max(5, SERAPH_CHRONON_VOID);  // Returns VOID
```

### Overflow Handling

When a timestamp would overflow (reach VOID value), it becomes VOID instead of wrapping:

```c
Seraph_Chronon near_max = SERAPH_CHRONON_MAX;  // 0xFFFFFFFFFFFFFFFE
Seraph_Chronon overflow = seraph_chronon_add(near_max, 1);
// overflow == SERAPH_CHRONON_VOID (can't increment past max)
```

### Vector Clock VOID Propagation

A vector clock with any VOID component is entirely invalid:

```c
// If node 2's timestamp becomes VOID (overflow), comparisons fail
Seraph_CausalOrder order = seraph_vclock_compare(&a, &b);
// Returns SERAPH_CAUSAL_VOID if any component is VOID
```

## Branchless Implementation

### Why Branchless?

Branch mispredictions are expensive. SERAPH's Chronon operations use branchless code where possible:

```c
// Branchless max (no if/else)
static inline Seraph_Chronon seraph_chronon_max(Seraph_Chronon a, Seraph_Chronon b) {
    uint64_t void_mask = seraph_chronon_void_mask2(a, b);
    uint64_t a_ge_b = -(uint64_t)(a >= b);  // All 1s if true, all 0s if false
    Seraph_Chronon result = (a & a_ge_b) | (b & ~a_ge_b);
    return seraph_chronon_select(SERAPH_CHRONON_VOID, result, void_mask);
}
```

### How Void Masks Work

```c
// If x == VOID: returns 0xFFFFFFFFFFFFFFFF (all 1s)
// If x != VOID: returns 0x0000000000000000 (all 0s)
uint64_t mask = seraph_chronon_void_mask(x);

// Branchless select using the mask:
// If mask is all 1s: returns if_void
// If mask is all 0s: returns if_valid
result = seraph_chronon_select(if_void, if_valid, mask);
// Expands to: (if_void & mask) | (if_valid & ~mask)
```

## API Reference

### Types

| Type | Description |
|------|-------------|
| `Seraph_Chronon` | 64-bit logical timestamp |
| `Seraph_Event` | Event with timestamp and predecessor link |
| `Seraph_VectorClock` | Array of timestamps for distributed causality |
| `Seraph_LocalClock` | Single-node clock wrapper |
| `Seraph_CausalOrder` | Comparison result enum |

### Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `SERAPH_CHRONON_VOID` | All 1s | Invalid timestamp |
| `SERAPH_CHRONON_ZERO` | 0 | Beginning of time |
| `SERAPH_CHRONON_MAX` | VOID - 1 | Maximum valid timestamp |
| `SERAPH_EVENT_VOID` | All VOID fields | Invalid event |
| `SERAPH_EVENT_GENESIS` | All zero | First event (no predecessor) |

### Causal Ordering Values

| Value | Meaning |
|-------|---------|
| `SERAPH_CAUSAL_BEFORE` | A → B (A happens-before B) |
| `SERAPH_CAUSAL_EQUAL` | A == B (same event) |
| `SERAPH_CAUSAL_AFTER` | B → A (B happens-before A) |
| `SERAPH_CAUSAL_CONCURRENT` | A \|\| B (neither ordered) |
| `SERAPH_CAUSAL_VOID` | Cannot determine (invalid input) |

### Local Clock Functions

```c
// Initialize a local clock
Seraph_Vbit seraph_localclock_init(Seraph_LocalClock* clock, uint32_t id);

// Read current timestamp
Seraph_Chronon seraph_localclock_read(const Seraph_LocalClock* clock);

// Increment clock by 1
Seraph_Chronon seraph_localclock_tick(Seraph_LocalClock* clock);

// Lamport receive: max(current, received) + 1
Seraph_Chronon seraph_localclock_merge(Seraph_LocalClock* clock, Seraph_Chronon received);
```

### Scalar Chronon Functions

```c
// Check VOID status
bool seraph_chronon_is_void(Seraph_Chronon t);
bool seraph_chronon_exists(Seraph_Chronon t);

// Comparison (returns CausalOrder)
Seraph_CausalOrder seraph_chronon_compare(Seraph_Chronon a, Seraph_Chronon b);

// Arithmetic (branchless, VOID-propagating)
Seraph_Chronon seraph_chronon_max(Seraph_Chronon a, Seraph_Chronon b);
Seraph_Chronon seraph_chronon_min(Seraph_Chronon a, Seraph_Chronon b);
Seraph_Chronon seraph_chronon_add(Seraph_Chronon t, uint64_t delta);

// Branchless primitives
uint64_t seraph_chronon_void_mask(Seraph_Chronon t);
uint64_t seraph_chronon_void_mask2(Seraph_Chronon a, Seraph_Chronon b);
Seraph_Chronon seraph_chronon_select(Seraph_Chronon if_void,
                                      Seraph_Chronon if_valid, uint64_t mask);
```

### Event Functions

```c
// Create genesis event (no predecessor)
Seraph_Event seraph_event_create(Seraph_Chronon timestamp, uint32_t source_id,
                                  uint32_t sequence, uint64_t payload_hash);

// Create event chained to predecessor
Seraph_Event seraph_event_chain(Seraph_Event predecessor, Seraph_Chronon timestamp,
                                 uint32_t source_id, uint32_t sequence,
                                 uint64_t payload_hash);

// Compute event hash (FNV-1a)
uint64_t seraph_event_hash(Seraph_Event e);

// Check event status
bool seraph_event_is_void(Seraph_Event e);
bool seraph_event_exists(Seraph_Event e);
bool seraph_event_is_genesis(Seraph_Event e);

// Compare events by timestamp
Seraph_CausalOrder seraph_event_compare(Seraph_Event a, Seraph_Event b);
```

### Vector Clock Functions

```c
// Lifecycle
Seraph_Vbit seraph_vclock_init(Seraph_VectorClock* vclock, uint32_t node_count,
                                uint32_t self_id);
void seraph_vclock_destroy(Seraph_VectorClock* vclock);
bool seraph_vclock_is_valid(const Seraph_VectorClock* vclock);

// Operations
Seraph_Chronon seraph_vclock_tick(Seraph_VectorClock* vclock);
Seraph_Chronon seraph_vclock_get(const Seraph_VectorClock* vclock, uint32_t node_id);

// Message passing
uint32_t seraph_vclock_snapshot(const Seraph_VectorClock* vclock,
                                 Seraph_Chronon* buffer, uint32_t buffer_size);
Seraph_Vbit seraph_vclock_receive(Seraph_VectorClock* vclock,
                                   const Seraph_Chronon* received, uint32_t count);

// Comparison
Seraph_CausalOrder seraph_vclock_compare(const Seraph_VectorClock* a,
                                          const Seraph_VectorClock* b);
Seraph_CausalOrder seraph_vclock_compare_snapshot(const Seraph_VectorClock* vclock,
                                                   const Seraph_Chronon* snapshot,
                                                   uint32_t count);
Seraph_Vbit seraph_vclock_happens_before(const Seraph_VectorClock* a,
                                          const Seraph_VectorClock* b);
Seraph_Vbit seraph_vclock_is_concurrent(const Seraph_VectorClock* a,
                                         const Seraph_VectorClock* b);

// Utilities
Seraph_Vbit seraph_vclock_copy(Seraph_VectorClock* dst,
                                const Seraph_VectorClock* src);
Seraph_Vbit seraph_vclock_merge(Seraph_VectorClock* dst,
                                 const Seraph_VectorClock* other);
```

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|-----------------|-------|
| `localclock_tick` | O(1) | Single increment |
| `localclock_merge` | O(1) | Max + increment |
| `chronon_compare` | O(1) | Branchless |
| `chronon_max/min` | O(1) | Branchless |
| `event_hash` | O(1) | FNV-1a, 5 field XORs |
| `event_chain` | O(1) | Hash + struct init |
| `vclock_tick` | O(1) | Single increment |
| `vclock_receive` | O(n) | n = node count |
| `vclock_compare` | O(n) | Branchless accumulation |
| `vclock_snapshot` | O(n) | memcpy |

## Integration Examples

### With Sovereign Processes

Each Sovereign (process) maintains its own local clock:

```c
typedef struct {
    Seraph_LocalClock clock;
    // ... other Sovereign fields
} Seraph_Sovereign;

// Event created by Sovereign
Seraph_Event sovereign_create_event(Seraph_Sovereign* sov, uint64_t payload_hash) {
    Seraph_Chronon ts = seraph_localclock_tick(&sov->clock);
    return seraph_event_create(ts, sov->id, sov->next_seq++, payload_hash);
}
```

### With Whisper IPC

Messages carry vector clock snapshots:

```c
typedef struct {
    Seraph_Chronon* sender_clock;  // Vector clock snapshot
    uint32_t clock_size;
    // ... message payload
} Seraph_WhisperMessage;

// On receive
void handle_whisper(Seraph_VectorClock* my_clock, Seraph_WhisperMessage* msg) {
    seraph_vclock_receive(my_clock, msg->sender_clock, msg->clock_size);
    // Now my_clock reflects causal dependency on sender
}
```

### Distributed Database Scenario

```c
// Node A writes key "x" = 1
Seraph_VectorClock* a_clock = get_node_clock(A);
seraph_vclock_tick(a_clock);
write_with_clock("x", 1, a_clock);

// Node B writes key "x" = 2 (concurrently)
Seraph_VectorClock* b_clock = get_node_clock(B);
seraph_vclock_tick(b_clock);
write_with_clock("x", 2, b_clock);

// On conflict detection:
Seraph_CausalOrder order = seraph_vclock_compare(a_clock, b_clock);
if (order == SERAPH_CAUSAL_CONCURRENT) {
    // True conflict! Need resolution (e.g., last-writer-wins, CRDTs)
} else if (order == SERAPH_CAUSAL_BEFORE) {
    // A's write happened-before B's, B's value wins
} else {
    // B's write happened-before A's, A's value wins
}
```

## Comparison with Other Systems

| Feature | Wall Clock | Lamport | Vector Clock | SERAPH Chronon |
|---------|------------|---------|--------------|----------------|
| Detects ordering | Unreliable | Partial | Full | Full |
| Detects concurrency | No | No | Yes | Yes |
| Size per timestamp | 64 bits | 64 bits | 64×n bits | 64×n bits |
| VOID semantics | No | No | No | Yes |
| Branchless ops | N/A | No | No | Yes |
| Overflow handling | Undefined | Undefined | Undefined | Returns VOID |

## Common Pitfalls

### 1. Confusing Lamport with Vector Clocks

```c
// WRONG: Assuming Lamport timestamp order implies causality
if (lamport_ts_a < lamport_ts_b) {
    // Does NOT mean a → b!
}

// RIGHT: Use vector clocks to determine causality
Seraph_CausalOrder order = seraph_vclock_compare(&a, &b);
if (order == SERAPH_CAUSAL_BEFORE) {
    // Now we KNOW a → b
}
```

### 2. Forgetting to Tick on Send

```c
// WRONG: Send without ticking
snapshot = seraph_vclock_snapshot(&vclock, buf, size);
send_message(buf);  // Receiver won't see our latest state

// RIGHT: Tick before send
seraph_vclock_tick(&vclock);  // Record the send event
snapshot = seraph_vclock_snapshot(&vclock, buf, size);
send_message(buf);
```

### 3. Ignoring VOID Returns

```c
// WRONG: Ignoring potential overflow
while (true) {
    seraph_localclock_tick(&clock);  // Eventually returns VOID
}

// RIGHT: Check for VOID
Seraph_Chronon ts = seraph_localclock_tick(&clock);
if (seraph_chronon_is_void(ts)) {
    // Handle overflow - clock exhausted
}
```

### 4. Size Mismatch in Vector Clocks

```c
// WRONG: Comparing clocks from different-sized systems
Seraph_VectorClock a, b;
seraph_vclock_init(&a, 3, 0);  // 3 nodes
seraph_vclock_init(&b, 5, 0);  // 5 nodes
seraph_vclock_compare(&a, &b);  // Returns SERAPH_CAUSAL_VOID!

// RIGHT: Ensure same node count
seraph_vclock_init(&a, 5, 0);
seraph_vclock_init(&b, 5, 1);
// Now comparison works
```

## Source Files

| File | Description |
|------|-------------|
| `src/chronon.c` | Logical timestamps, Lamport clocks, vector clocks |
| `src/chronon_event.c` | Event creation, causal chaining, hash linking |
| `include/seraph/chronon.h` | Chronon types, clock structures, API |
