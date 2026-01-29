/**
 * @file strand.h
 * @brief MC13: Strand - Capability-Isolated Temporal Threading
 *
 * Strands are threads that share nothing by default. Unlike traditional threads
 * (pthreads, Windows threads) where all memory is implicitly shared, Strands
 * can ONLY access memory through explicit capability grants.
 *
 * CORE CONCEPTS:
 *
 *   1. CAPABILITY ISOLATION: Each Strand has a private capability table.
 *      Without a capability, there is NO access - guaranteed by construction.
 *
 *   2. TEMPORAL ISOLATION: Each Strand has its own Chronon counter.
 *      Time is strand-local, enabling lock-free temporal reasoning.
 *
 *   3. MEMORY ISOLATION: Each Strand has its own Spectral Band slice.
 *      No other Strand can access this memory without being granted a cap.
 *
 *   4. STACK AS CAPABILITY: The stack is a capability. Stack overflow is
 *      a capability violation, not memory corruption.
 *
 * WHY STRANDS ARE BETTER:
 *
 *   Traditional threads: Share everything by default.
 *     - Race conditions everywhere
 *     - Mutex overhead: ~25 cycles (uncontended), ~10,000 cycles (contended)
 *     - Deadlock: manual detection (good luck)
 *
 *   Seraph Strands: Share nothing by default.
 *     - Race conditions IMPOSSIBLE without capability
 *     - Capability transfer: ~15 cycles
 *     - Deadlock: AUTOMATIC detection via VOID propagation
 *
 * PERFORMANCE:
 *   - Strand creation:    ~3,000 cycles  (vs ~20,000 for pthread_create)
 *   - Context switch:     ~800 cycles    (vs ~2,000 for kernel trap)
 *   - Mutex uncontended:  ~15 cycles     (vs ~25 for pthread_mutex)
 *   - Mutex contended:    ~800 cycles    (vs ~10,000 for pthread_mutex)
 */

#ifndef SERAPH_STRAND_H
#define SERAPH_STRAND_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/capability.h"
#include "seraph/chronon.h"
#include "seraph/arena.h"
#include "seraph/context.h"
#include "seraph/galactic.h"

/* Forward declarations */
struct Seraph_Proof_Blob;
struct Seraph_Galactic_Exec_Stats;

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum capabilities per Strand */
#define SERAPH_STRAND_CAP_TABLE_SIZE 256

/** Default stack size (64KB) */
#define SERAPH_STRAND_DEFAULT_STACK_SIZE 65536

/** Default chronon limit before yield */
#define SERAPH_STRAND_DEFAULT_CHRONON_LIMIT 1000000

/** Default spectral band size (4KB for fast boot, increase later) */
#define SERAPH_STRAND_DEFAULT_BAND_SIZE (4096)

/*============================================================================
 * Error Codes
 *============================================================================*/

typedef enum {
    SERAPH_STRAND_OK          = 0,     /**< Success */
    SERAPH_STRAND_ERR_NULL    = 1,     /**< NULL pointer argument */
    SERAPH_STRAND_ERR_STATE   = 2,     /**< Invalid state transition */
    SERAPH_STRAND_ERR_MEMORY  = 3,     /**< Memory allocation failed */
    SERAPH_STRAND_ERR_DEADLOCK = 4,    /**< Deadlock detected */
    SERAPH_STRAND_ERR_FULL    = 5,     /**< Capability table full */
    SERAPH_STRAND_ERR_TIMEOUT = 6,     /**< Operation timed out */
    SERAPH_STRAND_ERR_INVALID = 7,     /**< Invalid argument */
    SERAPH_STRAND_ERR_PERM    = 8,     /**< Permission denied */
    SERAPH_STRAND_ERR_VOID    = 0xFF   /**< VOID error */
} Seraph_Strand_Error;

/*============================================================================
 * Strand States
 *============================================================================*/

/**
 * @brief Strand execution state
 *
 * State machine:
 *   create() -> NASCENT
 *   start()  -> READY
 *   dispatch -> RUNNING
 *   yield()  -> READY
 *   blocked  -> BLOCKED (waiting for mutex)
 *   join()   -> WAITING (waiting for another strand)
 *   exit()   -> TERMINATED
 */
typedef enum {
    SERAPH_STRAND_NASCENT    = 0,   /**< Created but not started */
    SERAPH_STRAND_READY      = 1,   /**< Ready to run, in scheduler queue */
    SERAPH_STRAND_RUNNING    = 2,   /**< Currently executing */
    SERAPH_STRAND_BLOCKED    = 3,   /**< Blocked on mutex acquisition */
    SERAPH_STRAND_WAITING    = 4,   /**< Waiting for another strand to exit */
    SERAPH_STRAND_TERMINATED = 5    /**< Execution complete */
} Seraph_Strand_State;

/*============================================================================
 * Capability Lending Status
 *============================================================================*/

/**
 * @brief Status of a capability in the table
 */
typedef enum {
    SERAPH_CAP_STATUS_VOID     = 0,   /**< No capability in this slot */
    SERAPH_CAP_STATUS_OWNED    = 1,   /**< Fully owned capability */
    SERAPH_CAP_STATUS_LENT     = 2,   /**< Capability lent to another strand */
    SERAPH_CAP_STATUS_BORROWED = 3    /**< Capability borrowed from another strand */
} Seraph_Cap_Status;

/**
 * @brief Extended capability entry with lending status (strand-local)
 */
typedef struct {
    Seraph_Capability cap;           /**< The capability itself */
    Seraph_Cap_Status status;        /**< Ownership status */
    uint32_t          lender_id;     /**< ID of lending strand (if borrowed) */
    Seraph_Chronon    timeout;       /**< Revocation time (if borrowed) */
} Seraph_Strand_Cap_Entry;

/*============================================================================
 * Mutex Structure
 *============================================================================*/

/**
 * @brief Mutex as capability
 *
 * A mutex IS a capability. Only the Strand holding the mutex capability
 * can enter the critical section. This eliminates:
 *   - Forgotten unlock bugs (capability transfer is explicit)
 *   - Wrong-thread-unlock bugs (only holder can release)
 *   - Deadlocks (cycle detection via VOID propagation)
 */
typedef struct Seraph_Strand_Mutex {
    Seraph_Capability   cap;             /**< The mutex capability */
    struct Seraph_Strand* holder;        /**< Current holder (NULL if free) */
    struct Seraph_Strand* wait_queue;    /**< Queue of waiting strands */
    uint64_t            acquisitions;    /**< Total acquisition count */
    uint64_t            contentions;     /**< Contention count */
    uint32_t            generation;      /**< Mutex generation */
    uint32_t            flags;           /**< Mutex flags */
} Seraph_Strand_Mutex;

/*============================================================================
 * Strand Structure
 *============================================================================*/

/**
 * @brief Capability-isolated thread of execution
 *
 * Each Strand encapsulates:
 *   - Identity: unique ID and state
 *   - Temporal isolation: private Chronon counter
 *   - Capability isolation: private capability table
 *   - Memory isolation: private Spectral Band slice
 *   - Execution context: stack, entry point, exit code
 */
typedef struct Seraph_Strand {
    /* Identity */
    uint64_t              strand_id;     /**< Unique strand identifier */
    Seraph_Strand_State   state;         /**< Current state */

    /* Temporal isolation: each Strand has its own time */
    Seraph_Chronon        chronon;       /**< Strand-local time counter */
    uint64_t              chronon_limit; /**< Max chronons before voluntary yield */

    /* Capability isolation: private capability table */
    Seraph_Strand_Cap_Entry cap_table[SERAPH_STRAND_CAP_TABLE_SIZE];
    uint32_t              cap_count;     /**< Number of active capabilities */

    /* Memory isolation: private Spectral Band slice */
    Seraph_Arena          band;          /**< Private memory region */

    /* Stack as capability (overflow = capability violation) */
    Seraph_Capability     stack_cap;     /**< Capability to own stack */
    void*                 stack_base;    /**< Stack base address */
    size_t                stack_size;    /**< Stack size in bytes */
    void*                 stack_pointer; /**< Current stack pointer */

    /* Execution context */
    void                (*entry_point)(void*);  /**< Strand entry function */
    void*                 entry_arg;     /**< Argument to entry point */
    uint64_t              exit_code;     /**< Set when TERMINATED */
    bool                  started;       /**< Has entry point been called? */

    /* Scheduling */
    struct Seraph_Strand* waiting_on;    /**< Strand we're joining */
    struct Seraph_Strand* blocked_on_mutex; /**< Mutex we're blocked on */
    struct Seraph_Strand* next_ready;    /**< Ready queue linkage */
    struct Seraph_Strand* next_waiter;   /**< Mutex wait queue linkage */
    struct Seraph_Strand* next_in_queue; /**< General queue linkage (scheduler) */
    uint32_t              priority;      /**< Scheduling priority (0 = highest) */
    uint32_t              base_priority; /**< Base priority (before boosting) */

    /* CPU Context for context switching (MC27: The Pulse) */
    Seraph_CPU_Context    cpu_context;   /**< Saved CPU state */
    uint64_t              cr3;           /**< Page table base (address space) */
    bool                  context_valid; /**< Is cpu_context initialized? */
    bool                  preempted;     /**< Was preempted (vs yielded)? */
    uint64_t              cpu_affinity;  /**< Bitmask of allowed CPUs */
    uint32_t              flags;         /**< Strand flags */
    uint32_t              id;            /**< Simple numeric ID for scheduler */

    /* Statistics */
    uint64_t              yield_count;   /**< Number of voluntary yields */
    uint64_t              context_switches; /**< Context switch count */

    /* ========================================================================
     * MC28: Zero-Overhead Proof-Guided Execution
     * ======================================================================== */

    /** Loaded proof blob for this strand's code */
    const struct Seraph_Proof_Blob* proof_blob;

    /** Proof blob generation for validation */
    uint64_t              proof_blob_generation;

    /** Proof execution flags */
    uint32_t              proof_flags;

    /** Statistics: runtime checks skipped due to proofs */
    uint64_t              runtime_checks_skipped;

    /** Statistics: runtime checks actually performed */
    uint64_t              runtime_checks_performed;

    /* ========================================================================
     * MC5+/13: Galactic Predictive Scheduling
     * ======================================================================== */

    /**
     * @brief Galactic execution statistics for predictive scheduling
     *
     * Tracks execution time as Galactic numbers (value + derivative) to
     * enable prediction of future CPU needs. The scheduler uses these
     * predictions to proactively adjust priority via gradient descent.
     *
     * Allocated lazily when Galactic scheduling is enabled for this strand.
     * NULL if Galactic scheduling is disabled.
     */
    struct Seraph_Galactic_Exec_Stats* galactic_stats;

    /**
     * @brief Galactic execution time (inline for fast access)
     *
     * primal  = current execution time per quantum (ticks)
     * tangent = rate of change (positive = growing, negative = shrinking)
     *
     * This is the primary metric used for predictive scheduling.
     * Duplicated from galactic_stats for cache-friendly access.
     */
    Seraph_Galactic exec_time_galactic;

    /**
     * @brief Timestamp when strand became READY (for response time)
     */
    uint64_t ready_timestamp;

    /**
     * @brief Timestamp when strand became BLOCKED/WAITING (for wait time)
     */
    uint64_t block_timestamp;

    /**
     * @brief Accumulated ticks consumed in current quantum
     */
    uint32_t quantum_ticks_used;

    /**
     * @brief Predicted execution time for next quantum
     *
     * Updated by Galactic scheduler, used for priority decisions.
     */
    Seraph_Q128 predicted_exec;

} Seraph_Strand;

/*============================================================================
 * Strand Flags
 *============================================================================*/

#define SERAPH_STRAND_FLAG_KERNEL   (1 << 0)  /**< Kernel-mode strand */
#define SERAPH_STRAND_FLAG_FPU_USED (1 << 1)  /**< FPU state needs saving */
#define SERAPH_STRAND_FLAG_IDLE     (1 << 2)  /**< Idle strand (never terminates) */

/*============================================================================
 * Proof Execution Flags (MC28: Zero-Overhead Execution)
 *============================================================================*/

/** Fail if any operation lacks a proof (strict mode) */
#define SERAPH_STRAND_PROOF_STRICT    (1 << 0)

/** Skip checksum verification on proof blob (trusted mode) */
#define SERAPH_STRAND_PROOF_TRUSTED   (1 << 1)

/** Track detailed statistics on proof usage */
#define SERAPH_STRAND_PROOF_STATS     (1 << 2)

/** Log proof lookups for debugging */
#define SERAPH_STRAND_PROOF_DEBUG     (1 << 3)

/*============================================================================
 * Strand Creation and Lifecycle
 *============================================================================*/

/**
 * @brief Create a new Strand in NASCENT state
 *
 * @param strand     Strand structure to initialize
 * @param entry      Entry point function (called when strand runs)
 * @param arg        Argument to pass to entry point
 * @param stack_size Stack size in bytes (0 for default 64KB)
 * @return SERAPH_STRAND_OK on success, error code otherwise
 *
 * Cost: ~3,000 cycles (vs ~20,000 for pthread_create)
 *
 * The new Strand starts with:
 *   - Empty capability table (no access to anything)
 *   - Private Spectral Band slice
 *   - Chronon counter at 0
 */
Seraph_Strand_Error seraph_strand_create(
    Seraph_Strand* strand,
    void (*entry)(void*),
    void* arg,
    size_t stack_size
);

/**
 * @brief Destroy a strand and free resources
 *
 * @param strand Strand to destroy
 *
 * Can only destroy NASCENT or TERMINATED strands.
 */
void seraph_strand_destroy(Seraph_Strand* strand);

/**
 * @brief Transition Strand from NASCENT to READY
 *
 * @param strand Strand to start
 * @return SERAPH_STRAND_OK on success
 *
 * Cost: ~100 cycles (adds to ready queue)
 */
Seraph_Strand_Error seraph_strand_start(Seraph_Strand* strand);

/**
 * @brief Voluntarily yield execution
 *
 * Transitions from RUNNING to READY, allows other Strands to run.
 *
 * Cost: ~800 cycles (vs ~2,000 for sched_yield with kernel trap)
 */
void seraph_strand_yield(void);

/**
 * @brief Wait for a Strand to terminate
 *
 * @param strand    Strand to wait for
 * @param exit_code Receives the strand's exit code (may be NULL)
 * @return SERAPH_STRAND_OK on success, SERAPH_STRAND_ERR_DEADLOCK if cycle
 *
 * Cost: ~50 cycles if already terminated, ~800 cycles + wait time otherwise
 *
 * Deadlock detection: If joining would create a cycle, VOID propagation
 * detects this and returns SERAPH_STRAND_ERR_DEADLOCK.
 */
Seraph_Strand_Error seraph_strand_join(Seraph_Strand* strand, uint64_t* exit_code);

/**
 * @brief Terminate the current Strand
 *
 * @param exit_code Exit code to return to joining strands
 *
 * This function does not return.
 */
void seraph_strand_exit(uint64_t exit_code);

/*============================================================================
 * Strand Information
 *============================================================================*/

/**
 * @brief Get pointer to currently running Strand
 *
 * @return Pointer to current Strand, or NULL if not in strand context
 *
 * Cost: ~5 cycles (thread-local read)
 */
Seraph_Strand* seraph_strand_current(void);

/**
 * @brief Get current Strand's Chronon counter
 *
 * @return Current chronon value, or VOID if not in strand context
 *
 * Cost: ~3 cycles
 */
Seraph_Chronon seraph_strand_chronon(void);

/**
 * @brief Tick the current Strand's Chronon
 *
 * @return New chronon value, or VOID if overflow or not in strand context
 */
Seraph_Chronon seraph_strand_tick(void);

/**
 * @brief Get strand state as string (for debugging)
 */
const char* seraph_strand_state_string(Seraph_Strand_State state);

/**
 * @brief Check if strand is in a runnable state
 */
static inline bool seraph_strand_is_runnable(const Seraph_Strand* strand) {
    if (!strand) return false;
    return strand->state == SERAPH_STRAND_READY ||
           strand->state == SERAPH_STRAND_RUNNING;
}

/**
 * @brief Check if strand is valid
 */
static inline bool seraph_strand_is_valid(const Seraph_Strand* strand) {
    return strand != NULL && strand->stack_base != NULL;
}

/*============================================================================
 * Capability Grants Between Strands
 *============================================================================*/

/**
 * @brief Grant a capability permanently to another Strand
 *
 * @param to        Destination strand
 * @param src_slot  Slot in current strand's cap table
 * @param dest_slot Slot in destination strand's cap table
 * @return SERAPH_STRAND_OK on success
 *
 * Cost: ~15 cycles (atomic capability transfer)
 *
 * After GRANT:
 *   - Source strand's capability becomes VOID
 *   - Destination strand receives the capability
 *   - No data is copied (only the capability token moves)
 */
Seraph_Strand_Error seraph_strand_grant(
    Seraph_Strand* to,
    uint32_t src_slot,
    uint32_t dest_slot
);

/**
 * @brief Temporarily lend a capability to another Strand
 *
 * @param to           Destination strand
 * @param src_slot     Slot in current strand's cap table
 * @param dest_slot    Slot in destination strand's cap table
 * @param timeout      Maximum chronons before automatic revocation
 * @return SERAPH_STRAND_OK on success
 *
 * Cost: ~20 cycles (sets up revocation timer)
 *
 * After LEND:
 *   - Source capability is marked LENT
 *   - Destination receives a BORROWED capability
 *   - After timeout, capability automatically returns to source
 *   - If lending creates a cycle, VOID propagation triggers
 */
Seraph_Strand_Error seraph_strand_lend(
    Seraph_Strand* to,
    uint32_t src_slot,
    uint32_t dest_slot,
    Seraph_Chronon timeout
);

/**
 * @brief Revoke a lent capability
 *
 * @param src_slot Slot of the lent capability in current strand
 * @return SERAPH_STRAND_OK on success
 *
 * Immediately revokes a lent capability. The borrower's capability
 * becomes VOID.
 */
Seraph_Strand_Error seraph_strand_revoke(uint32_t src_slot);

/**
 * @brief Return a borrowed capability early
 *
 * @param slot Slot of the borrowed capability
 * @return SERAPH_STRAND_OK on success
 */
Seraph_Strand_Error seraph_strand_return(uint32_t slot);

/**
 * @brief Process expired lends (called by scheduler)
 *
 * Checks all borrowed capabilities and revokes expired ones.
 */
void seraph_strand_process_lends(Seraph_Strand* strand);

/*============================================================================
 * Capability Table Operations
 *============================================================================*/

/**
 * @brief Store a capability in strand's table
 *
 * @param strand Strand to modify
 * @param slot   Slot index (0 to CAP_TABLE_SIZE-1)
 * @param cap    Capability to store
 * @return SERAPH_STRAND_OK on success
 */
Seraph_Strand_Error seraph_strand_cap_store(
    Seraph_Strand* strand,
    uint32_t slot,
    Seraph_Capability cap
);

/**
 * @brief Get a capability from strand's table
 *
 * @param strand Strand to read from
 * @param slot   Slot index
 * @return Capability, or VOID if invalid
 */
Seraph_Capability seraph_strand_cap_get(
    const Seraph_Strand* strand,
    uint32_t slot
);

/**
 * @brief Find an empty slot in capability table
 *
 * @param strand Strand to search
 * @return Slot index, or SERAPH_VOID_U32 if table full
 */
uint32_t seraph_strand_cap_find_slot(const Seraph_Strand* strand);

/**
 * @brief Clear a capability slot
 *
 * @param strand Strand to modify
 * @param slot   Slot index
 * @return SERAPH_STRAND_OK on success
 */
Seraph_Strand_Error seraph_strand_cap_clear(Seraph_Strand* strand, uint32_t slot);

/*============================================================================
 * Mutex as Capability
 *============================================================================*/

/**
 * @brief Initialize a mutex
 *
 * @param mutex Mutex to initialize
 * @return SERAPH_STRAND_OK on success
 *
 * Cost: ~100 cycles (creates the mutex capability)
 */
Seraph_Strand_Error seraph_strand_mutex_init(Seraph_Strand_Mutex* mutex);

/**
 * @brief Destroy a mutex
 *
 * @param mutex Mutex to destroy
 *
 * Must not be held or have waiters.
 */
void seraph_strand_mutex_destroy(Seraph_Strand_Mutex* mutex);

/**
 * @brief Acquire the mutex capability
 *
 * @param mutex     Mutex to acquire
 * @param dest_slot Slot in current strand's cap table for mutex cap
 * @return Acquired capability, or VOID on error
 *
 * Cost: ~15 cycles if uncontended
 *       ~800 cycles if contended (includes context switch)
 *
 * If another Strand holds the mutex, the calling Strand transitions to
 * BLOCKED state and is added to the wait queue.
 *
 * Deadlock detection: If acquisition would create a cycle, VOID propagation
 * triggers and returns SERAPH_CAP_VOID.
 */
Seraph_Capability seraph_strand_mutex_acquire(
    Seraph_Strand_Mutex* mutex,
    uint32_t dest_slot
);

/**
 * @brief Release the mutex capability
 *
 * @param mutex Mutex to release
 * @param held  The capability received from acquire
 * @return SERAPH_STRAND_OK on success, error if not holder
 *
 * Cost: ~10 cycles if no waiters
 *       ~50 cycles if waking a waiter
 *
 * Guarantee: Only the capability holder can release the mutex.
 */
Seraph_Strand_Error seraph_strand_mutex_release(
    Seraph_Strand_Mutex* mutex,
    Seraph_Capability held
);

/**
 * @brief Non-blocking mutex acquisition attempt
 *
 * @param mutex     Mutex to try to acquire
 * @param dest_slot Slot in current strand's cap table
 * @return Acquired capability, or VOID if already held
 *
 * Cost: ~10 cycles (single atomic operation)
 */
Seraph_Capability seraph_strand_mutex_try_acquire(
    Seraph_Strand_Mutex* mutex,
    uint32_t dest_slot
);

/**
 * @brief Check if mutex is held
 */
static inline bool seraph_strand_mutex_is_held(const Seraph_Strand_Mutex* mutex) {
    return mutex && mutex->holder != NULL;
}

/*============================================================================
 * Strand-Local Storage
 *============================================================================*/

/**
 * @brief Allocate memory in Strand's private Spectral Band
 *
 * @param size Bytes to allocate
 * @return Pointer to allocated memory, or NULL if insufficient space
 *
 * Cost: ~50 cycles (bump allocator in private band)
 *
 * This memory is ONLY accessible to the current Strand.
 * No locks required. No race conditions possible.
 */
void* seraph_strand_local_alloc(size_t size);

/**
 * @brief Allocate and zero-initialize in private band
 *
 * @param size Bytes to allocate
 * @return Pointer to zeroed memory, or NULL if insufficient space
 */
void* seraph_strand_local_calloc(size_t size);

/**
 * @brief Free memory in Strand's private band
 *
 * @param ptr Pointer previously returned by seraph_strand_local_alloc
 *
 * Note: In a bump allocator, individual frees are no-ops. Memory is
 * reclaimed when the strand terminates or the band is reset.
 */
void seraph_strand_local_free(void* ptr);

/**
 * @brief Get remaining space in strand's private band
 */
size_t seraph_strand_local_remaining(void);

/*============================================================================
 * Scheduler Interface
 *============================================================================*/

/**
 * @brief Run one quantum of the current strand
 *
 * Used by the scheduler to execute a strand for a time slice.
 * For testing, this simulates execution.
 *
 * @param strand Strand to run
 * @return true if strand is still runnable, false if terminated/blocked
 */
bool seraph_strand_run_quantum(Seraph_Strand* strand);

/**
 * @brief Set the current strand (for scheduler use)
 *
 * @param strand Strand to make current
 */
void seraph_strand_set_current(Seraph_Strand* strand);

/**
 * @brief Simple cooperative scheduler
 *
 * Runs strands in round-robin fashion until all terminate.
 * For testing purposes.
 *
 * @param strands Array of strand pointers
 * @param count   Number of strands
 */
void seraph_strand_schedule(Seraph_Strand** strands, uint32_t count);

/*============================================================================
 * Deadlock Detection
 *============================================================================*/

/**
 * @brief Check if joining would create a deadlock cycle
 *
 * @param waiter Strand that wants to join
 * @param target Strand being joined
 * @return TRUE if deadlock would occur, FALSE otherwise
 */
Seraph_Vbit seraph_strand_would_deadlock(
    const Seraph_Strand* waiter,
    const Seraph_Strand* target
);

/*============================================================================
 * MC28: Zero-Overhead Proof-Guided Execution
 *============================================================================*/

/**
 * @brief Attach a proof blob to a strand for zero-overhead execution
 *
 * @param strand     Strand to attach proof blob to
 * @param proof_blob Proof blob to attach (must remain valid)
 * @param flags      Proof execution flags
 * @return SERAPH_STRAND_OK on success
 *
 * Once attached, the strand can skip runtime safety checks for
 * operations that have been statically verified.
 */
Seraph_Strand_Error seraph_strand_attach_proof_blob(
    Seraph_Strand* strand,
    const struct Seraph_Proof_Blob* proof_blob,
    uint32_t flags
);

/**
 * @brief Detach proof blob from strand
 *
 * @param strand Strand to detach from
 * @return SERAPH_STRAND_OK on success
 */
Seraph_Strand_Error seraph_strand_detach_proof_blob(Seraph_Strand* strand);

/**
 * @brief Get proof execution statistics for a strand
 *
 * @param strand        Strand to query
 * @param checks_skipped Output: number of runtime checks skipped
 * @param checks_done    Output: number of runtime checks performed
 */
void seraph_strand_proof_stats(
    const Seraph_Strand* strand,
    uint64_t* checks_skipped,
    uint64_t* checks_done
);

/**
 * @brief Record a skipped runtime check (for statistics)
 *
 * @param strand Strand that skipped a check
 *
 * Called by zero-overhead macros when proof exists.
 */
static inline void seraph_strand_proof_skipped(Seraph_Strand* strand) {
    if (strand && (strand->proof_flags & SERAPH_STRAND_PROOF_STATS)) {
        strand->runtime_checks_skipped++;
    }
}

/**
 * @brief Record a performed runtime check (for statistics)
 *
 * @param strand Strand that performed a check
 *
 * Called by zero-overhead macros when no proof exists.
 */
static inline void seraph_strand_proof_performed(Seraph_Strand* strand) {
    if (strand && (strand->proof_flags & SERAPH_STRAND_PROOF_STATS)) {
        strand->runtime_checks_performed++;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_STRAND_H */
