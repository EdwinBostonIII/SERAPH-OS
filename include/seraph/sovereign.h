/**
 * @file sovereign.h
 * @brief MC10: The Sovereign - Capability-Based Process Isolation
 *
 * A Sovereign is SERAPH's fundamental unit of isolation and authority.
 * Unlike traditional processes that can access anything they have permission
 * for, a Sovereign can ONLY access what its capability tokens explicitly allow.
 *
 * CORE PRINCIPLES:
 *
 *   1. CAPABILITY-BASED IDENTITY: A Sovereign is identified by a 256-bit
 *      cryptographically random token, not a simple integer PID. This token
 *      is unforgeable and never reused.
 *
 *   2. LAW OF DIMINISHING SOVEREIGNTY: A child Sovereign can NEVER have more
 *      authority than its parent. Authority only decreases down the tree.
 *
 *   3. VOID PROPAGATION: When a Sovereign dies, all capabilities pointing to
 *      it become VOID. References to dead Sovereigns naturally fail.
 *
 *   4. STATE MACHINE: Sovereigns transition through well-defined states:
 *      CONCEIVING → NASCENT → RUNNING ↔ WAITING ↔ SUSPENDED → TERMINAL → VOID
 *
 * THE PRIMORDIAL:
 *   The root of all authority. Created at boot with all permissions.
 *   Has no parent (parent_id is VOID). Cannot be killed. If THE PRIMORDIAL
 *   exits, the system halts.
 *
 * SOVEREIGN ANATOMY:
 *   - Capability Table: All capabilities this Sovereign holds
 *   - Memory Arenas: Primary, code, and scratch arenas
 *   - Strand Pool: Green threads within this Sovereign
 *   - Child Registry: References to child Sovereigns
 */

#ifndef SERAPH_SOVEREIGN_H
#define SERAPH_SOVEREIGN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/capability.h"
#include "seraph/arena.h"
#include "seraph/chronon.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Sovereign State Enumeration
 *============================================================================*/

/**
 * @brief Lifecycle states for a Sovereign
 *
 * State transitions are generally one-way:
 *   CONCEIVING → NASCENT → RUNNING ↔ WAITING ↔ SUSPENDED → TERMINAL → VOID
 *
 * The only reversible transitions are between RUNNING, WAITING, and SUSPENDED.
 */
typedef enum {
    /**
     * Parent is preparing the child's initial capability set.
     * The Sovereign exists as a concept but has no allocated resources.
     */
    SERAPH_SOVEREIGN_CONCEIVING = 0,

    /**
     * Arenas allocated, code loaded, capabilities granted.
     * The Sovereign exists but has not begun execution.
     * This is the "frozen embryo" state - fully formed but not alive.
     */
    SERAPH_SOVEREIGN_NASCENT = 1,

    /**
     * At least one Strand is actively executing code.
     * The Sovereign is consuming Chronons and making progress.
     */
    SERAPH_SOVEREIGN_RUNNING = 2,

    /**
     * All Strands are blocked waiting for:
     *   - I/O completion (Conduit operations)
     *   - Capability availability (borrowed caps returning)
     *   - Time passage (Chronon threshold)
     *   - Child Sovereign state changes
     */
    SERAPH_SOVEREIGN_WAITING = 3,

    /**
     * Parent has paused this Sovereign.
     * No Strands may execute. Chronon stream frozen.
     * The Sovereign is "cryogenically preserved."
     */
    SERAPH_SOVEREIGN_SUSPENDED = 4,

    /**
     * Voluntary termination initiated by the Sovereign itself.
     * Cleanup in progress: child Sovereigns being terminated,
     * borrowed capabilities being returned, arenas being freed.
     */
    SERAPH_SOVEREIGN_EXITING = 5,

    /**
     * Involuntary termination by parent or system.
     * Same cleanup as EXITING but may be more abrupt.
     */
    SERAPH_SOVEREIGN_KILLED = 6,

    /**
     * The Sovereign violated a capability constraint and was
     * forcibly terminated. Unlike KILLED, VOIDED propagates:
     * children are also VOIDED.
     */
    SERAPH_SOVEREIGN_VOIDED = 7,

    /**
     * Terminal state. The Sovereign no longer exists.
     * All capabilities pointing to this Sovereign now return VOID.
     * The Sovereign ID will NEVER be reused.
     */
    SERAPH_SOVEREIGN_VOID = 0xFF  /* Matches SERAPH_VOID pattern */

} Seraph_Sovereign_State;

/**
 * @brief Check if state is VOID
 */
static inline bool seraph_sovereign_state_is_void(Seraph_Sovereign_State state) {
    return state == SERAPH_SOVEREIGN_VOID;
}

/**
 * @brief Check if state indicates the Sovereign is alive (can execute)
 */
static inline bool seraph_sovereign_state_is_alive(Seraph_Sovereign_State state) {
    return state >= SERAPH_SOVEREIGN_NASCENT && state <= SERAPH_SOVEREIGN_SUSPENDED;
}

/**
 * @brief Check if state is terminal (Sovereign is dying or dead)
 */
static inline bool seraph_sovereign_state_is_terminal(Seraph_Sovereign_State state) {
    return state >= SERAPH_SOVEREIGN_EXITING;
}

/*============================================================================
 * Sovereign Authority Flags
 *============================================================================*/

/**
 * @brief Authority flags defining what operations a Sovereign may perform
 *
 * A child Sovereign can NEVER have more authority flags than its parent.
 * Authority is MONOTONICALLY DECREASING down the Sovereign tree.
 */
typedef enum {
    /*
     * Core lifecycle authorities
     */
    SERAPH_AUTH_SPAWN           = (1ULL << 0),   /**< Can create child Sovereigns */
    SERAPH_AUTH_KILL            = (1ULL << 1),   /**< Can terminate child Sovereigns */
    SERAPH_AUTH_SUSPEND         = (1ULL << 2),   /**< Can suspend/resume children */

    /*
     * Capability management authorities
     */
    SERAPH_AUTH_GRANT           = (1ULL << 8),   /**< Can permanently transfer capabilities */
    SERAPH_AUTH_LEND            = (1ULL << 9),   /**< Can temporarily lend capabilities */
    SERAPH_AUTH_REVOKE          = (1ULL << 10),  /**< Can revoke granted/lent capabilities */
    SERAPH_AUTH_DERIVE          = (1ULL << 11),  /**< Can create narrowed capability copies */

    /*
     * Memory authorities
     */
    SERAPH_AUTH_ARENA_CREATE    = (1ULL << 16),  /**< Can create new Spectral Arenas */
    SERAPH_AUTH_ARENA_DESTROY   = (1ULL << 17),  /**< Can destroy owned Arenas */
    SERAPH_AUTH_ARENA_RESIZE    = (1ULL << 18),  /**< Can grow/shrink Arena bounds */
    SERAPH_AUTH_MEMORY_EXECUTE  = (1ULL << 19),  /**< Can mark memory executable */

    /*
     * Temporal authorities
     */
    SERAPH_AUTH_CHRONON_READ    = (1ULL << 24),  /**< Can read current Chronon */
    SERAPH_AUTH_CHRONON_WAIT    = (1ULL << 25),  /**< Can wait for Chronon threshold */
    SERAPH_AUTH_CHRONON_INJECT  = (1ULL << 26),  /**< Can inject events into Chronon stream */

    /*
     * Threading authorities
     */
    SERAPH_AUTH_STRAND_CREATE   = (1ULL << 32),  /**< Can create new Strands */
    SERAPH_AUTH_STRAND_JOIN     = (1ULL << 33),  /**< Can wait for Strand completion */
    SERAPH_AUTH_STRAND_KILL     = (1ULL << 34),  /**< Can terminate Strands */

    /*
     * I/O authorities
     */
    SERAPH_AUTH_CONDUIT_OPEN    = (1ULL << 40),  /**< Can open new Conduits */
    SERAPH_AUTH_CONDUIT_READ    = (1ULL << 41),  /**< Can read from Conduits */
    SERAPH_AUTH_CONDUIT_WRITE   = (1ULL << 42),  /**< Can write to Conduits */

    /*
     * Input authorities
     */
    SERAPH_AUTH_SENSE_ATTACH    = (1ULL << 48),  /**< Can attach to input devices */
    SERAPH_AUTH_SENSE_GRAB      = (1ULL << 49),  /**< Can grab exclusive input focus */

    /*
     * Display authorities
     */
    SERAPH_AUTH_GLYPH_CREATE    = (1ULL << 56),  /**< Can create Glyphs */
    SERAPH_AUTH_GLYPH_RENDER    = (1ULL << 57),  /**< Can submit Glyphs for rendering */
    SERAPH_AUTH_FRAMEBUFFER     = (1ULL << 58),  /**< Can access raw framebuffer */

    /*
     * Composite authority masks for common patterns
     */
    SERAPH_AUTH_NONE            = 0ULL,

    SERAPH_AUTH_MINIMAL         = SERAPH_AUTH_CHRONON_READ,

    SERAPH_AUTH_WORKER          = SERAPH_AUTH_CHRONON_READ |
                                  SERAPH_AUTH_CHRONON_WAIT |
                                  SERAPH_AUTH_CONDUIT_READ |
                                  SERAPH_AUTH_CONDUIT_WRITE,

    SERAPH_AUTH_APPLICATION     = SERAPH_AUTH_WORKER |
                                  SERAPH_AUTH_STRAND_CREATE |
                                  SERAPH_AUTH_STRAND_JOIN |
                                  SERAPH_AUTH_ARENA_CREATE |
                                  SERAPH_AUTH_GLYPH_CREATE |
                                  SERAPH_AUTH_GLYPH_RENDER |
                                  SERAPH_AUTH_SENSE_ATTACH,

    SERAPH_AUTH_SYSTEM          = SERAPH_AUTH_APPLICATION |
                                  SERAPH_AUTH_SPAWN |
                                  SERAPH_AUTH_KILL |
                                  SERAPH_AUTH_GRANT |
                                  SERAPH_AUTH_LEND |
                                  SERAPH_AUTH_REVOKE,

    SERAPH_AUTH_PRIMORDIAL      = ~0ULL  /**< All authorities - only THE PRIMORDIAL has this */

} Seraph_Authority;

/**
 * @brief Validate that child authority is a subset of parent authority
 * @param parent_auth Parent's authority mask
 * @param child_auth Child's authority mask
 * @return TRUE if valid, FALSE if child has unauthorized bits, VOID if child_auth is explicitly VOID
 *
 * Note: SERAPH_AUTH_PRIMORDIAL is ~0ULL which equals SERAPH_VOID_U64.
 * We treat ~0ULL as valid authority (all permissions), not as VOID.
 * Only return VOID if the child_auth is VOID and parent is not PRIMORDIAL.
 */
static inline Seraph_Vbit seraph_authority_valid(uint64_t parent_auth, uint64_t child_auth) {
    /* PRIMORDIAL authority (~0ULL) is valid, not VOID.
     * Child with ~0ULL authority is only valid if parent is also PRIMORDIAL. */

    /* If child is requesting PRIMORDIAL authority, it's only valid if parent has it */
    if (child_auth == SERAPH_AUTH_PRIMORDIAL) {
        return (parent_auth == SERAPH_AUTH_PRIMORDIAL) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
    }

    /* Check if child has no unauthorized bits */
    uint64_t unauthorized = child_auth & ~parent_auth;
    return (unauthorized == 0) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Check if authority has a specific permission
 *
 * Note: SERAPH_AUTH_PRIMORDIAL (~0ULL) has ALL authorities.
 * We use simple bitwise logic: (authority & required) == required.
 * For PRIMORDIAL: ~0ULL & anything = anything, so it has everything.
 * For NONE (0): 0 & anything = 0, so it has nothing.
 */
static inline bool seraph_authority_has(uint64_t authority, uint64_t required) {
    return (authority & required) == required;
}

/*============================================================================
 * Sovereign Identifier (256-bit)
 *============================================================================*/

/**
 * @brief 256-bit Sovereign identifier
 *
 * Unlike PIDs, Sovereign IDs are:
 *   - Cryptographically random (unforgeable)
 *   - Never reused (no PID-reuse attacks)
 *   - Self-validating (contains checksum)
 *
 * Layout:
 *   quads[0]: random_id       - Cryptographically random ID
 *   quads[1]: generation      - Generation counter + epoch
 *   quads[2]: authority_mask  - Authority flags at creation
 *   quads[3]: nonce_checksum  - Nonce + XOR checksum
 */
typedef struct {
    uint64_t quads[4];
} Seraph_Sovereign_ID;

/** VOID Sovereign ID - represents non-existence */
#define SERAPH_SOVEREIGN_ID_VOID ((Seraph_Sovereign_ID){ \
    .quads = { SERAPH_VOID_U64, SERAPH_VOID_U64, SERAPH_VOID_U64, SERAPH_VOID_U64 } \
})

/**
 * @brief Check if Sovereign ID is VOID
 */
static inline bool seraph_sovereign_id_is_void(Seraph_Sovereign_ID id) {
    return id.quads[0] == SERAPH_VOID_U64 &&
           id.quads[1] == SERAPH_VOID_U64 &&
           id.quads[2] == SERAPH_VOID_U64 &&
           id.quads[3] == SERAPH_VOID_U64;
}

/**
 * @brief Compare two Sovereign IDs for equality
 * @return TRUE if equal, FALSE if not, VOID if either input is VOID
 */
static inline Seraph_Vbit seraph_sovereign_id_equal(Seraph_Sovereign_ID a, Seraph_Sovereign_ID b) {
    if (seraph_sovereign_id_is_void(a) || seraph_sovereign_id_is_void(b)) {
        return SERAPH_VBIT_VOID;
    }
    bool equal = (a.quads[0] == b.quads[0]) &&
                 (a.quads[1] == b.quads[1]) &&
                 (a.quads[2] == b.quads[2]) &&
                 (a.quads[3] == b.quads[3]);
    return equal ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/**
 * @brief Generate a new Sovereign ID
 * @param authority Authority mask to embed in ID
 * @return New Sovereign ID, or VOID on failure
 */
Seraph_Sovereign_ID seraph_sovereign_id_generate(uint64_t authority);

/**
 * @brief Validate Sovereign ID checksum
 * @return TRUE if valid, FALSE if corrupted, VOID if ID is VOID
 */
Seraph_Vbit seraph_sovereign_id_validate(Seraph_Sovereign_ID id);

/*============================================================================
 * Capability Table Entry
 *============================================================================*/

/**
 * @brief Capability slot states
 */
typedef enum {
    SERAPH_CAP_SLOT_EMPTY        = 0,    /**< Slot available for use */
    SERAPH_CAP_SLOT_OWNED        = 1,    /**< We own this capability outright */
    SERAPH_CAP_SLOT_BORROWED_OUT = 2,    /**< We own it but have lent it out */
    SERAPH_CAP_SLOT_BORROWED_IN  = 3,    /**< We borrowed this from another Sovereign */
    SERAPH_CAP_SLOT_VOID         = 0xFF  /**< Slot is void (capability was revoked) */
} Seraph_Cap_Slot_State;

/**
 * @brief Capability table entry
 */
typedef struct {
    Seraph_Capability   cap;          /**< The capability token */
    uint32_t            slot_state;   /**< EMPTY, OWNED, BORROWED_OUT, BORROWED_IN */
    uint32_t            borrow_count; /**< Number of times this cap has been lent */
    Seraph_Chronon      expiry;       /**< When this capability expires (0 = never) */
} Seraph_Cap_Entry;

/*============================================================================
 * Child Reference
 *============================================================================*/

/**
 * @brief Reference to a child Sovereign
 */
typedef struct {
    Seraph_Sovereign_ID     child_id;    /**< ID of the child Sovereign */
    Seraph_Sovereign_State  child_state; /**< Cached state (may be stale) */
    uint32_t                exit_code;   /**< Exit code if terminated */
    Seraph_Vbit             valid;       /**< Is this entry valid? */
} Seraph_Child_Ref;

/*============================================================================
 * Sovereign Limits
 *============================================================================*/

/** Maximum number of capabilities a Sovereign can hold */
#define SERAPH_SOVEREIGN_MAX_CAPABILITIES 1024

/** Maximum number of Strands (threads) per Sovereign */
#define SERAPH_SOVEREIGN_MAX_STRANDS 256

/** Maximum number of child Sovereigns */
#define SERAPH_SOVEREIGN_MAX_CHILDREN 1024

/*============================================================================
 * Sovereign Structure
 *============================================================================*/

/* Forward declaration for Strand (implemented in MC13) */
typedef struct Seraph_Strand Seraph_Strand;

/**
 * @brief The Sovereign structure - SERAPH's process abstraction
 *
 * This structure is NEVER directly accessible to user code - only through capabilities.
 * All fields are managed by the kernel/runtime.
 */
typedef struct Seraph_Sovereign {
    /*
     * IDENTITY (read-only after creation)
     */
    Seraph_Sovereign_ID    id;              /**< This Sovereign's unique identifier */
    Seraph_Sovereign_ID    parent_id;       /**< Parent's identifier (VOID for Primordial) */
    uint64_t               authority;       /**< Authority mask (subset of parent's) */
    Seraph_Chronon         birth_chronon;   /**< When this Sovereign was spawned */

    /*
     * STATE (mutable, should be atomic in production)
     */
    Seraph_Sovereign_State state;           /**< Current lifecycle state */
    uint32_t               exit_code;       /**< Exit code (valid in terminal states) */
    Seraph_Chronon         last_active;     /**< Last Chronon when a Strand ran */

    /*
     * CAPABILITY TABLE
     */
    Seraph_Cap_Entry       capabilities[SERAPH_SOVEREIGN_MAX_CAPABILITIES];
    uint32_t               cap_count;       /**< Number of valid capabilities */
    uint32_t               cap_generation;  /**< Incremented on any cap change */

    /*
     * MEMORY (Spectral Arenas)
     */
    Seraph_Arena*          primary_arena;   /**< Main data arena */
    Seraph_Arena*          code_arena;      /**< Executable code arena */
    Seraph_Arena*          scratch_arena;   /**< Frame-scoped temporary arena */
    uint64_t               memory_limit;    /**< Maximum total memory (bytes) */
    uint64_t               memory_used;     /**< Current memory usage */

    /*
     * CODE RELOCATION INFO
     */
    uint64_t               code_base;       /**< Actual base address of loaded code */
    uint64_t               code_load_addr;  /**< Requested load address from ELF */
    int64_t                code_delta;      /**< Relocation delta (code_base - code_load_addr) */

    /*
     * STRANDS (Threads) - deferred to MC13
     */
    Seraph_Strand*         strands[SERAPH_SOVEREIGN_MAX_STRANDS];
    uint32_t               strand_count;    /**< Number of active Strands */
    uint32_t               running_strands; /**< Number of RUNNING Strands */
    uint32_t               main_strand_idx; /**< Index of the main Strand */

    /*
     * CHILDREN
     */
    Seraph_Child_Ref       children[SERAPH_SOVEREIGN_MAX_CHILDREN];
    uint32_t               child_count;     /**< Number of living children */

    /*
     * STATISTICS (for debugging and profiling)
     */
    uint64_t               total_chronons;  /**< Total Chronons consumed */
    uint64_t               total_allocs;    /**< Total allocations made */
    uint64_t               total_frees;     /**< Total frees made */
    uint64_t               cap_grants;      /**< Capabilities granted to children */
    uint64_t               cap_revokes;     /**< Capabilities revoked */
    uint64_t               whispers_sent;   /**< Messages sent */
    uint64_t               whispers_recv;   /**< Messages received */

} Seraph_Sovereign;

/*============================================================================
 * Spawn Configuration
 *============================================================================*/

/**
 * @brief Configuration for spawning a new Sovereign
 */
typedef struct {
    uint64_t               authority;       /**< Authority to grant (must be subset of parent's) */
    uint64_t               memory_limit;    /**< Max total memory (bytes) */
    uint64_t               stack_size;      /**< Stack size per Strand */
    uint32_t               max_strands;     /**< Maximum Strands allowed */
    uint32_t               max_children;    /**< Maximum child Sovereigns */
    uint32_t               initial_caps_count;
    uint32_t               initial_caps_indices[64]; /**< Parent's cap table indices to grant */
    uint64_t               entry_point;     /**< Address of entry function */
    uint64_t               entry_arg;       /**< Argument to pass to entry */
} Seraph_Spawn_Config;

/** Default spawn configuration */
#define SERAPH_SPAWN_CONFIG_DEFAULT ((Seraph_Spawn_Config){ \
    .authority = SERAPH_AUTH_MINIMAL,                       \
    .memory_limit = 64 * 1024 * 1024,  /* 64 MB */          \
    .stack_size = 1 * 1024 * 1024,     /* 1 MB */           \
    .max_strands = 16,                                      \
    .max_children = 64,                                     \
    .initial_caps_count = 0,                                \
    .initial_caps_indices = {0},                            \
    .entry_point = 0,                                       \
    .entry_arg = 0                                          \
})

/*============================================================================
 * Grant Flags
 *============================================================================*/

/**
 * @brief Flags for capability grant operations
 */
typedef enum {
    SERAPH_GRANT_COPY     = 0,        /**< Copy the capability (default) */
    SERAPH_GRANT_TRANSFER = (1 << 0), /**< Transfer: parent loses the capability */
    SERAPH_GRANT_NARROW   = (1 << 1), /**< Narrow the capability's permissions */
} Seraph_Grant_Flags;

/*============================================================================
 * Sovereign Creation API
 *============================================================================*/

/**
 * @brief Begin creation of a new child Sovereign
 *
 * Creates a new Sovereign in CONCEIVING state. The parent must then:
 * 1. Grant initial capabilities with seraph_sovereign_grant_cap()
 * 2. Load code with seraph_sovereign_load_code()
 * 3. Start execution with seraph_sovereign_vivify()
 *
 * @param parent_cap Capability to the parent Sovereign (must have SPAWN authority)
 * @param config Configuration for the new Sovereign
 * @return Capability to the NASCENT child Sovereign, or VOID on failure
 */
Seraph_Capability seraph_sovereign_conceive(
    Seraph_Capability parent_cap,
    Seraph_Spawn_Config config
);

/**
 * @brief Grant a capability to a NASCENT child Sovereign
 *
 * @param child_cap Capability to the NASCENT child
 * @param cap_to_grant The capability to grant
 * @param grant_flags Flags controlling the grant behavior
 * @return TRUE if success, FALSE if failed, VOID if inputs are VOID
 */
Seraph_Vbit seraph_sovereign_grant_cap(
    Seraph_Capability child_cap,
    Seraph_Capability cap_to_grant,
    Seraph_Grant_Flags grant_flags
);

/**
 * @brief Load executable code into a NASCENT child Sovereign
 *
 * @param child_cap Capability to the NASCENT child
 * @param code Pointer to the code to load
 * @param code_size Size of the code in bytes
 * @param load_addr Address within child's space to load at
 * @return TRUE if success, FALSE if failed, VOID if inputs are VOID
 */
Seraph_Vbit seraph_sovereign_load_code(
    Seraph_Capability child_cap,
    const void* code,
    uint64_t code_size,
    uint64_t load_addr
);

/**
 * @brief Bring a NASCENT Sovereign to life (transition to RUNNING)
 *
 * @param child_cap Capability to the NASCENT child
 * @return TRUE if success, FALSE if failed, VOID if input is VOID
 */
Seraph_Vbit seraph_sovereign_vivify(Seraph_Capability child_cap);

/*============================================================================
 * Sovereign Termination API
 *============================================================================*/

/**
 * @brief Voluntarily terminate the current Sovereign
 *
 * This function does not return.
 *
 * @param exit_code Exit code to report to parent
 */
void seraph_sovereign_exit(uint32_t exit_code);

/**
 * @brief Forcibly terminate a child Sovereign
 *
 * @param child_cap Capability to the child (must have KILL authority)
 * @return TRUE if success, FALSE if failed, VOID if input is VOID
 */
Seraph_Vbit seraph_sovereign_kill(Seraph_Capability child_cap);

/**
 * @brief Wait for a child Sovereign to terminate
 *
 * @param child_cap Capability to the child
 * @param timeout Maximum Chronons to wait (0 = infinite, VOID = immediate check)
 * @param exit_code Output: child's exit code (if terminated)
 * @return TRUE if child has terminated, FALSE if timeout, VOID if input is VOID
 */
Seraph_Vbit seraph_sovereign_wait(
    Seraph_Capability child_cap,
    Seraph_Chronon timeout,
    uint32_t* exit_code
);

/*============================================================================
 * Sovereign State Queries
 *============================================================================*/

/**
 * @brief Query the current state of a Sovereign
 *
 * @param sov_cap Capability to the Sovereign
 * @return The Sovereign's current state, or SERAPH_SOVEREIGN_VOID if cap is VOID
 */
Seraph_Sovereign_State seraph_sovereign_get_state(Seraph_Capability sov_cap);

/**
 * @brief Get the unique identifier of a Sovereign
 *
 * @param sov_cap Capability to the Sovereign
 * @return The Sovereign's ID, or SERAPH_SOVEREIGN_ID_VOID if cap is VOID
 */
Seraph_Sovereign_ID seraph_sovereign_get_id(Seraph_Capability sov_cap);

/**
 * @brief Get a capability to the current Sovereign (self-reference)
 *
 * @return Capability to the current Sovereign (always valid - never VOID)
 */
Seraph_Capability seraph_sovereign_self(void);

/**
 * @brief Get a capability to the current Sovereign's parent
 *
 * @return Capability to the parent, or VOID if current Sovereign is THE PRIMORDIAL
 */
Seraph_Capability seraph_sovereign_parent(void);

/**
 * @brief Get the authority mask of the current Sovereign
 */
uint64_t seraph_sovereign_get_authority(void);

/*============================================================================
 * Sovereign Suspension API
 *============================================================================*/

/**
 * @brief Suspend a child Sovereign
 *
 * @param child_cap Capability to the child (must have SUSPEND authority)
 * @return TRUE if success, FALSE if failed, VOID if input is VOID
 */
Seraph_Vbit seraph_sovereign_suspend(Seraph_Capability child_cap);

/**
 * @brief Resume a suspended child Sovereign
 *
 * @param child_cap Capability to the child (must have SUSPEND authority)
 * @return TRUE if success, FALSE if failed, VOID if input is VOID
 */
Seraph_Vbit seraph_sovereign_resume(Seraph_Capability child_cap);

/*============================================================================
 * Subsystem Initialization
 *============================================================================*/

/**
 * @brief Initialize the Sovereign subsystem
 *
 * Called once during system boot to create THE PRIMORDIAL.
 * After this call, seraph_the_primordial is valid and the system can spawn
 * child Sovereigns.
 */
void seraph_sovereign_subsystem_init(void);

/**
 * @brief Shut down the Sovereign subsystem
 *
 * Terminates all Sovereigns except THE PRIMORDIAL and frees resources.
 */
void seraph_sovereign_subsystem_shutdown(void);

/**
 * @brief The global pointer to THE PRIMORDIAL (read-only after init)
 */
extern Seraph_Sovereign* seraph_the_primordial;

/**
 * @brief Get the currently executing Sovereign
 */
Seraph_Sovereign* seraph_sovereign_current(void);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SOVEREIGN_H */
