/**
 * @file sovereign.c
 * @brief MC10: The Sovereign - Implementation
 *
 * Implementation of capability-based process isolation.
 */

#include "seraph/sovereign.h"
#include "seraph/strand.h"
#include "seraph/context.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/*============================================================================
 * Static State
 *============================================================================*/

/** THE PRIMORDIAL - the root Sovereign created at boot */
Seraph_Sovereign* seraph_the_primordial = NULL;

/** Storage for THE PRIMORDIAL (statically allocated) */
static Seraph_Sovereign primordial_storage;

/** The currently executing Sovereign (thread-local in production) */
static Seraph_Sovereign* current_sovereign = NULL;

/** Global generation counter for Sovereign IDs */
static uint64_t sovereign_id_generation = 0;

/** Flag indicating subsystem is initialized */
static bool sovereign_subsystem_initialized = false;

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Simple random number generator (TODO: replace with cryptographic RNG)
 */
static uint64_t random_u64(void) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    uint64_t result = 0;
    for (int i = 0; i < 8; i++) {
        result = (result << 8) | (rand() & 0xFF);
    }
    return result;
}

/**
 * @brief Compute XOR checksum for Sovereign ID validation
 *
 * The checksum is a simple XOR of the first three quads.
 * This provides basic integrity checking but is NOT cryptographic.
 * For production, this should use a proper MAC or hash function.
 */
static uint64_t compute_id_checksum(Seraph_Sovereign_ID id) {
    return id.quads[0] ^ id.quads[1] ^ id.quads[2];
}

/**
 * @brief Find a free slot in the capability table
 * @return Index of free slot, or SERAPH_VOID_U32 if none
 */
static uint32_t find_free_cap_slot(Seraph_Sovereign* sov) {
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CAPABILITIES; i++) {
        if (sov->capabilities[i].slot_state == SERAPH_CAP_SLOT_EMPTY) {
            return i;
        }
    }
    return SERAPH_VOID_U32;
}

/**
 * @brief Find a free slot in the children array
 * @return Index of free slot, or SERAPH_VOID_U32 if none
 */
static uint32_t find_free_child_slot(Seraph_Sovereign* sov) {
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CHILDREN; i++) {
        if (!seraph_vbit_is_true(sov->children[i].valid)) {
            return i;
        }
    }
    return SERAPH_VOID_U32;
}

/**
 * @brief Extract Sovereign pointer from capability
 * @return Sovereign pointer, or NULL if capability is invalid
 */
static Seraph_Sovereign* sovereign_from_cap(Seraph_Capability cap) {
    if (seraph_cap_is_void(cap)) return NULL;
    if (cap.base == NULL) return NULL;
    if (cap.length < sizeof(Seraph_Sovereign)) return NULL;
    /* Verify it's a Sovereign capability (type tag check) */
    if (cap.type != 0 && cap.type != 10) return NULL;  /* type 10 = Sovereign */
    return (Seraph_Sovereign*)cap.base;
}

/**
 * @brief Create a capability for a Sovereign
 */
static Seraph_Capability capability_for_sovereign(Seraph_Sovereign* sov) {
    if (sov == NULL) return SERAPH_CAP_VOID;
    return (Seraph_Capability){
        .base = sov,
        .length = sizeof(Seraph_Sovereign),
        .generation = (uint32_t)(sov->id.quads[1] & 0xFFFFFFFF),
        .permissions = SERAPH_CAP_RW | SERAPH_CAP_DERIVE,
        .type = 10,  /* Sovereign type tag */
        .reserved = 0
    };
}

/*============================================================================
 * Sovereign ID Operations
 *============================================================================*/

Seraph_Sovereign_ID seraph_sovereign_id_generate(uint64_t authority) {
    /*
     * NOTE: We do NOT check for VOID authority here!
     *
     * In the context of Sovereign authority, ~0ULL (SERAPH_AUTH_PRIMORDIAL)
     * means "full authority" - all authority bits are set. This is a VALID
     * value, not VOID.
     *
     * VOID semantics (absence/error) don't apply to authority masks.
     * If we needed to signal "cannot create ID", we'd use a different
     * mechanism or use 0 as a sentinel (no authority).
     */

    Seraph_Sovereign_ID id;

    /* quads[0]: Random ID */
    id.quads[0] = random_u64();

    /* quads[1]: Generation counter (never VOID) */
    sovereign_id_generation++;
    if (sovereign_id_generation == SERAPH_VOID_U64) {
        sovereign_id_generation = 1;  /* Skip VOID value */
    }
    id.quads[1] = sovereign_id_generation;

    /* quads[2]: Authority mask at creation */
    id.quads[2] = authority;

    /* quads[3]: Nonce + checksum */
    uint64_t nonce = random_u64() & 0xFFFFFFFF00000000ULL;
    uint64_t checksum = compute_id_checksum(id);
    id.quads[3] = nonce | (checksum & 0xFFFFFFFFULL);

    return id;
}

Seraph_Vbit seraph_sovereign_id_validate(Seraph_Sovereign_ID id) {
    /* Check for VOID ID */
    if (seraph_sovereign_id_is_void(id)) {
        return SERAPH_VBIT_VOID;
    }

    /* Only compare lower 32 bits since we only stored 32 bits of checksum */
    uint64_t expected_checksum = compute_id_checksum(id) & 0xFFFFFFFFULL;
    uint64_t actual_checksum = id.quads[3] & 0xFFFFFFFFULL;

    return (expected_checksum == actual_checksum) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Subsystem Initialization
 *============================================================================*/

void seraph_sovereign_subsystem_init(void) {
    if (sovereign_subsystem_initialized) return;

    /* Initialize THE PRIMORDIAL */
    memset(&primordial_storage, 0, sizeof(primordial_storage));

    seraph_the_primordial = &primordial_storage;

    /* Generate Primordial's unique ID with full authority */
    seraph_the_primordial->id = seraph_sovereign_id_generate(SERAPH_AUTH_PRIMORDIAL);

    /* THE PRIMORDIAL has no parent */
    seraph_the_primordial->parent_id = SERAPH_SOVEREIGN_ID_VOID;

    /* THE PRIMORDIAL has all authorities */
    seraph_the_primordial->authority = SERAPH_AUTH_PRIMORDIAL;

    /* Birth at chronon zero */
    seraph_the_primordial->birth_chronon = 0;

    /* Start in RUNNING state */
    seraph_the_primordial->state = SERAPH_SOVEREIGN_RUNNING;

    /* Initialize capability table - all slots empty */
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CAPABILITIES; i++) {
        seraph_the_primordial->capabilities[i].slot_state = SERAPH_CAP_SLOT_EMPTY;
        seraph_the_primordial->capabilities[i].cap = SERAPH_CAP_NULL;
        seraph_the_primordial->capabilities[i].borrow_count = 0;
        seraph_the_primordial->capabilities[i].expiry = 0;
    }

    /* Initialize children - all invalid */
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CHILDREN; i++) {
        seraph_the_primordial->children[i].child_id = SERAPH_SOVEREIGN_ID_VOID;
        seraph_the_primordial->children[i].child_state = SERAPH_SOVEREIGN_VOID;
        seraph_the_primordial->children[i].exit_code = 0;
        seraph_the_primordial->children[i].valid = SERAPH_VBIT_FALSE;
    }

    /* Initialize strands - all NULL */
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_STRANDS; i++) {
        seraph_the_primordial->strands[i] = NULL;
    }

    /* No arenas yet (will be set up by caller) */
    seraph_the_primordial->primary_arena = NULL;
    seraph_the_primordial->code_arena = NULL;
    seraph_the_primordial->scratch_arena = NULL;
    seraph_the_primordial->memory_limit = SERAPH_VOID_U64;  /* No limit */
    seraph_the_primordial->memory_used = 0;

    /* THE PRIMORDIAL is the current Sovereign */
    current_sovereign = seraph_the_primordial;

    sovereign_subsystem_initialized = true;
}

void seraph_sovereign_subsystem_shutdown(void) {
    if (!sovereign_subsystem_initialized) return;

    /* In a full implementation, we would:
     * 1. Terminate all child Sovereigns (recursively)
     * 2. Free all allocated resources
     * 3. Clear THE PRIMORDIAL
     *
     * For now, just reset state.
     */

    current_sovereign = NULL;
    seraph_the_primordial = NULL;
    sovereign_subsystem_initialized = false;
}

/*============================================================================
 * Sovereign State Queries
 *============================================================================*/

Seraph_Sovereign* seraph_sovereign_current(void) {
    return current_sovereign;
}

Seraph_Sovereign_State seraph_sovereign_get_state(Seraph_Capability sov_cap) {
    Seraph_Sovereign* sov = sovereign_from_cap(sov_cap);
    if (sov == NULL) return SERAPH_SOVEREIGN_VOID;
    return sov->state;
}

Seraph_Sovereign_ID seraph_sovereign_get_id(Seraph_Capability sov_cap) {
    Seraph_Sovereign* sov = sovereign_from_cap(sov_cap);
    if (sov == NULL) return SERAPH_SOVEREIGN_ID_VOID;
    return sov->id;
}

Seraph_Capability seraph_sovereign_self(void) {
    if (current_sovereign == NULL) {
        return SERAPH_CAP_VOID;
    }
    return capability_for_sovereign(current_sovereign);
}

Seraph_Capability seraph_sovereign_parent(void) {
    if (current_sovereign == NULL) {
        return SERAPH_CAP_VOID;
    }
    /* THE PRIMORDIAL has no parent */
    if (current_sovereign == seraph_the_primordial) {
        return SERAPH_CAP_VOID;
    }
    /* In a full implementation, we'd look up the parent by ID.
     * For now, return VOID if not THE PRIMORDIAL (since we haven't
     * implemented full process trees yet).
     */
    return SERAPH_CAP_VOID;
}

uint64_t seraph_sovereign_get_authority(void) {
    if (current_sovereign == NULL) {
        return SERAPH_VOID_U64;
    }
    return current_sovereign->authority;
}

/*============================================================================
 * Sovereign Creation
 *============================================================================*/

Seraph_Capability seraph_sovereign_conceive(
    Seraph_Capability parent_cap,
    Seraph_Spawn_Config config
) {
    /* Validate parent capability */
    Seraph_Sovereign* parent = sovereign_from_cap(parent_cap);
    if (parent == NULL) {
        return SERAPH_CAP_VOID;
    }

    /* Check SPAWN authority */
    if (!seraph_authority_has(parent->authority, SERAPH_AUTH_SPAWN)) {
        return SERAPH_CAP_VOID;
    }

    /* Validate child authority is subset of parent's */
    Seraph_Vbit auth_valid = seraph_authority_valid(parent->authority, config.authority);
    if (!seraph_vbit_is_true(auth_valid)) {
        return SERAPH_CAP_VOID;
    }

    /* Find free child slot */
    uint32_t child_slot = find_free_child_slot(parent);
    if (child_slot == SERAPH_VOID_U32) {
        return SERAPH_CAP_VOID;  /* Too many children */
    }

    /* Allocate new Sovereign structure */
    Seraph_Sovereign* child = (Seraph_Sovereign*)malloc(sizeof(Seraph_Sovereign));
    if (child == NULL) {
        return SERAPH_CAP_VOID;
    }

    /* Initialize the child Sovereign */
    memset(child, 0, sizeof(Seraph_Sovereign));

    /* Generate child's unique ID */
    child->id = seraph_sovereign_id_generate(config.authority);
    if (seraph_sovereign_id_is_void(child->id)) {
        free(child);
        return SERAPH_CAP_VOID;
    }

    /* Set parent reference */
    child->parent_id = parent->id;

    /* Set authority (must be subset of parent's) */
    child->authority = config.authority;

    /* Record birth time */
    child->birth_chronon = parent->last_active;

    /* Start in NASCENT state (not yet running) */
    child->state = SERAPH_SOVEREIGN_NASCENT;

    /* Initialize capability table - all slots empty */
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CAPABILITIES; i++) {
        child->capabilities[i].slot_state = SERAPH_CAP_SLOT_EMPTY;
        child->capabilities[i].cap = SERAPH_CAP_NULL;
        child->capabilities[i].borrow_count = 0;
        child->capabilities[i].expiry = 0;
    }

    /* Initialize children - all invalid */
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CHILDREN; i++) {
        child->children[i].child_id = SERAPH_SOVEREIGN_ID_VOID;
        child->children[i].child_state = SERAPH_SOVEREIGN_VOID;
        child->children[i].exit_code = 0;
        child->children[i].valid = SERAPH_VBIT_FALSE;
    }

    /* Initialize strands - all NULL */
    for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_STRANDS; i++) {
        child->strands[i] = NULL;
    }

    /* Set memory limits from config */
    child->memory_limit = config.memory_limit;
    child->memory_used = 0;

    /* Allocate primary arena for the child */
    child->primary_arena = (Seraph_Arena*)malloc(sizeof(Seraph_Arena));
    if (child->primary_arena != NULL) {
        size_t arena_size = (config.memory_limit > 0 && config.memory_limit < 16 * 1024 * 1024)
                          ? (size_t)config.memory_limit
                          : 16 * 1024 * 1024;  /* Default 16 MB */
        Seraph_Vbit init_result = seraph_arena_create(
            child->primary_arena,
            arena_size,
            SERAPH_ARENA_DEFAULT_ALIGNMENT,
            SERAPH_ARENA_FLAG_ZERO_ON_ALLOC
        );
        if (!seraph_vbit_is_true(init_result)) {
            free(child->primary_arena);
            child->primary_arena = NULL;
        }
    }

    /* Register child with parent */
    parent->children[child_slot].child_id = child->id;
    parent->children[child_slot].child_state = SERAPH_SOVEREIGN_NASCENT;
    parent->children[child_slot].exit_code = 0;
    parent->children[child_slot].valid = SERAPH_VBIT_TRUE;
    parent->child_count++;

    /* Return capability to the child */
    return capability_for_sovereign(child);
}

Seraph_Vbit seraph_sovereign_grant_cap(
    Seraph_Capability child_cap,
    Seraph_Capability cap_to_grant,
    Seraph_Grant_Flags grant_flags
) {
    Seraph_Sovereign* child = sovereign_from_cap(child_cap);
    if (child == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Can only grant to NASCENT children */
    if (child->state != SERAPH_SOVEREIGN_NASCENT) {
        return SERAPH_VBIT_FALSE;
    }

    /* Validate capability to grant */
    if (seraph_cap_is_void(cap_to_grant)) {
        return SERAPH_VBIT_VOID;
    }

    /* Find free slot in child's capability table */
    uint32_t slot = find_free_cap_slot(child);
    if (slot == SERAPH_VOID_U32) {
        return SERAPH_VBIT_FALSE;  /* Capability table full */
    }

    /* Store the capability */
    child->capabilities[slot].cap = cap_to_grant;
    child->capabilities[slot].slot_state = SERAPH_CAP_SLOT_OWNED;
    child->capabilities[slot].borrow_count = 0;
    child->capabilities[slot].expiry = 0;
    child->cap_count++;
    child->cap_generation++;

    /* Handle grant flags */
    if (grant_flags & SERAPH_GRANT_TRANSFER) {
        /* Parent loses the capability - would need to find it in parent's table
         * and mark as EMPTY. For now, just log that this should happen.
         */
        /* TODO: Implement transfer semantics */
    }

    if (grant_flags & SERAPH_GRANT_NARROW) {
        /* Narrow permissions - already handled in cap_to_grant by caller */
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_sovereign_load_code(
    Seraph_Capability child_cap,
    const void* code,
    uint64_t code_size,
    uint64_t load_addr
) {
    Seraph_Sovereign* child = sovereign_from_cap(child_cap);
    if (child == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Can only load code into NASCENT children */
    if (child->state != SERAPH_SOVEREIGN_NASCENT) {
        return SERAPH_VBIT_FALSE;
    }

    if (code == NULL || code_size == 0) {
        return SERAPH_VBIT_FALSE;
    }

    /* Create code arena if not exists */
    if (child->code_arena == NULL) {
        child->code_arena = (Seraph_Arena*)malloc(sizeof(Seraph_Arena));
        if (child->code_arena == NULL) {
            return SERAPH_VBIT_FALSE;
        }
        size_t arena_size = (code_size < 1024 * 1024) ? 1024 * 1024 : (size_t)code_size * 2;
        Seraph_Vbit init_result = seraph_arena_create(
            child->code_arena,
            arena_size,
            16,  /* Code alignment */
            SERAPH_ARENA_FLAG_NONE
        );
        if (!seraph_vbit_is_true(init_result)) {
            free(child->code_arena);
            child->code_arena = NULL;
            return SERAPH_VBIT_FALSE;
        }
    }

    /* Allocate space in code arena */
    void* code_dest = seraph_arena_alloc(child->code_arena, (size_t)code_size, 16);
    if (code_dest == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    /* Copy code */
    memcpy(code_dest, code, (size_t)code_size);

    /* Record load address (for entry point resolution) */
    (void)load_addr;  /* TODO: Use for relocation */

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_sovereign_vivify(Seraph_Capability child_cap) {
    Seraph_Sovereign* child = sovereign_from_cap(child_cap);
    if (child == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Can only vivify NASCENT children */
    if (child->state != SERAPH_SOVEREIGN_NASCENT) {
        return SERAPH_VBIT_FALSE;
    }

    /*
     * MC27: The Pulse - Create main strand and schedule for execution
     *
     * 1. Allocate main strand structure
     * 2. Initialize CPU context for entry point
     * 3. Set up stack with capability
     * 4. Add strand to scheduler ready queue
     */

    /* Allocate main strand structure */
    Seraph_Strand* main_strand = (Seraph_Strand*)seraph_arena_alloc(
        child->primary_arena, sizeof(Seraph_Strand), _Alignof(Seraph_Strand)
    );
    if (main_strand == NULL) {
        return SERAPH_VBIT_FALSE;
    }
    memset(main_strand, 0, sizeof(Seraph_Strand));

    /* Initialize strand identity */
    static uint64_t strand_id_counter = 1;
    main_strand->strand_id = strand_id_counter++;
    main_strand->id = (uint32_t)(main_strand->strand_id & 0xFFFFFFFF);
    main_strand->state = SERAPH_STRAND_READY;

    /* Set up strand priority (normal priority for main strand) */
    main_strand->priority = 3;  /* SERAPH_PRIORITY_NORMAL */
    main_strand->base_priority = 3;
    main_strand->cpu_affinity = ~0ULL;  /* Can run on any CPU */

    /* Allocate stack for main strand */
    size_t stack_size = SERAPH_STRAND_DEFAULT_STACK_SIZE;
    void* stack = seraph_arena_alloc(child->primary_arena, stack_size, 16);
    if (stack == NULL) {
        return SERAPH_VBIT_FALSE;
    }
    main_strand->stack_base = stack;
    main_strand->stack_size = stack_size;

    /* Create stack capability */
    main_strand->stack_cap = (Seraph_Capability){
        .base = stack,
        .length = stack_size,
        .generation = 1,
        .permissions = SERAPH_CAP_RW,
        .type = 1,  /* Stack type */
        .reserved = 0
    };

    /* Initialize private spectral band (memory region) */
    size_t band_size = SERAPH_STRAND_DEFAULT_BAND_SIZE;
    if (child->memory_limit > 0 && child->memory_limit < band_size) {
        band_size = (size_t)child->memory_limit / 2;
    }
    Seraph_Vbit band_init = seraph_arena_create(
        &main_strand->band,
        band_size,
        16,
        SERAPH_ARENA_FLAG_ZERO_ON_ALLOC
    );
    if (!seraph_vbit_is_true(band_init)) {
        /* Non-fatal: strand can still run without private band */
    }

    /* Set entry point from spawn config
     * For now, use a placeholder entry wrapper that will call the actual entry
     */
    main_strand->entry_point = NULL;  /* Will be set by code loader */
    main_strand->entry_arg = NULL;
    main_strand->started = false;

    /* Initialize CPU context for execution
     * The context is set up so that when switched to, execution begins
     * at the entry point with the configured stack
     */
    void* stack_top = (uint8_t*)stack + stack_size;
    seraph_context_init(
        &main_strand->cpu_context,
        (void (*)(void*))main_strand->entry_point,
        stack_top,
        main_strand->entry_arg,
        child->primary_arena != NULL ? 0 : 0  /* Use current CR3 for now */
    );
    main_strand->context_valid = true;
    main_strand->preempted = false;

    /* Initialize chronon (strand-local time) */
    main_strand->chronon = 0;
    main_strand->chronon_limit = SERAPH_STRAND_DEFAULT_CHRONON_LIMIT;

    /* Register strand with Sovereign */
    child->strands[0] = main_strand;
    child->strand_count = 1;
    child->running_strands = 1;
    child->main_strand_idx = 0;

    /* Transition Sovereign to RUNNING state */
    child->state = SERAPH_SOVEREIGN_RUNNING;

    /* Add strand to scheduler ready queue
     * The scheduler will dispatch this strand when it becomes the
     * highest priority runnable strand
     */
    /* Note: In production, would call seraph_scheduler_ready(main_strand) */

    /* Update parent's cached child state */
    Seraph_Sovereign* parent = current_sovereign;
    if (parent != NULL) {
        for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CHILDREN; i++) {
            if (seraph_vbit_is_true(parent->children[i].valid)) {
                Seraph_Vbit id_match = seraph_sovereign_id_equal(
                    parent->children[i].child_id, child->id);
                if (seraph_vbit_is_true(id_match)) {
                    parent->children[i].child_state = SERAPH_SOVEREIGN_RUNNING;
                    break;
                }
            }
        }
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Sovereign Termination
 *============================================================================*/

void seraph_sovereign_exit(uint32_t exit_code) {
    if (current_sovereign == NULL) {
        return;
    }

    /* THE PRIMORDIAL cannot exit (system would halt) */
    if (current_sovereign == seraph_the_primordial) {
        /* In a real OS, this would halt the system.
         * For now, just refuse to exit.
         */
        return;
    }

    /* Transition to EXITING state */
    current_sovereign->state = SERAPH_SOVEREIGN_EXITING;
    current_sovereign->exit_code = exit_code;

    /* In a full implementation:
     * 1. Kill all child Sovereigns
     * 2. Return borrowed capabilities
     * 3. Free arenas
     * 4. Notify parent
     * 5. Transition to VOID
     *
     * For now, just mark as VOID.
     */

    current_sovereign->state = SERAPH_SOVEREIGN_VOID;
}

Seraph_Vbit seraph_sovereign_kill(Seraph_Capability child_cap) {
    Seraph_Sovereign* child = sovereign_from_cap(child_cap);
    if (child == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Check KILL authority */
    if (current_sovereign == NULL ||
        !seraph_authority_has(current_sovereign->authority, SERAPH_AUTH_KILL)) {
        return SERAPH_VBIT_FALSE;
    }

    /* THE PRIMORDIAL cannot be killed */
    if (child == seraph_the_primordial) {
        return SERAPH_VBIT_VOID;  /* Not an error, just impossible */
    }

    /* Can only kill ALIVE children */
    if (!seraph_sovereign_state_is_alive(child->state)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Verify this is actually our child (by checking parent_id) */
    if (current_sovereign != NULL) {
        Seraph_Vbit is_our_child = seraph_sovereign_id_equal(
            child->parent_id, current_sovereign->id);
        if (!seraph_vbit_is_true(is_our_child)) {
            return SERAPH_VBIT_FALSE;  /* Not our child */
        }
    }

    /* Transition to KILLED state */
    child->state = SERAPH_SOVEREIGN_KILLED;
    child->exit_code = SERAPH_VOID_U32;  /* Killed, no exit code */

    /* In a full implementation, we would:
     * 1. Forcibly terminate all Strands
     * 2. Recursively kill all children
     * 3. Free all resources
     * 4. Transition to VOID
     */

    /* Update parent's cached state */
    if (current_sovereign != NULL) {
        for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CHILDREN; i++) {
            if (seraph_vbit_is_true(current_sovereign->children[i].valid)) {
                Seraph_Vbit id_match = seraph_sovereign_id_equal(
                    current_sovereign->children[i].child_id, child->id);
                if (seraph_vbit_is_true(id_match)) {
                    current_sovereign->children[i].child_state = SERAPH_SOVEREIGN_KILLED;
                    break;
                }
            }
        }
    }

    /* Free child's resources */
    if (child->primary_arena != NULL) {
        seraph_arena_destroy(child->primary_arena);
        free(child->primary_arena);
        child->primary_arena = NULL;
    }
    if (child->code_arena != NULL) {
        seraph_arena_destroy(child->code_arena);
        free(child->code_arena);
        child->code_arena = NULL;
    }
    if (child->scratch_arena != NULL) {
        seraph_arena_destroy(child->scratch_arena);
        free(child->scratch_arena);
        child->scratch_arena = NULL;
    }

    /* Transition to VOID */
    child->state = SERAPH_SOVEREIGN_VOID;

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_sovereign_wait(
    Seraph_Capability child_cap,
    Seraph_Chronon timeout,
    uint32_t* exit_code
) {
    Seraph_Sovereign* child = sovereign_from_cap(child_cap);
    if (child == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Check if child is in a terminal state */
    if (seraph_sovereign_state_is_terminal(child->state)) {
        if (exit_code != NULL) {
            *exit_code = child->exit_code;
        }
        return SERAPH_VBIT_TRUE;  /* Child has terminated */
    }

    /* Immediate check (timeout = VOID) */
    if (timeout == SERAPH_CHRONON_VOID) {
        return SERAPH_VBIT_FALSE;  /* Not terminated yet */
    }

    /* In a full implementation with actual scheduling:
     * 1. Block the current Strand
     * 2. Set up a wakeup when child terminates or timeout expires
     * 3. Return result when woken
     *
     * For now, just return FALSE (not terminated).
     */
    (void)timeout;  /* Unused in this simplified implementation */

    return SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Sovereign Suspension
 *============================================================================*/

Seraph_Vbit seraph_sovereign_suspend(Seraph_Capability child_cap) {
    Seraph_Sovereign* child = sovereign_from_cap(child_cap);
    if (child == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Check SUSPEND authority */
    if (current_sovereign == NULL ||
        !seraph_authority_has(current_sovereign->authority, SERAPH_AUTH_SUSPEND)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Can only suspend RUNNING or WAITING children */
    if (child->state != SERAPH_SOVEREIGN_RUNNING &&
        child->state != SERAPH_SOVEREIGN_WAITING) {
        return SERAPH_VBIT_FALSE;
    }

    /* Verify this is our child */
    if (current_sovereign != NULL) {
        Seraph_Vbit is_our_child = seraph_sovereign_id_equal(
            child->parent_id, current_sovereign->id);
        if (!seraph_vbit_is_true(is_our_child)) {
            return SERAPH_VBIT_FALSE;
        }
    }

    /* Transition to SUSPENDED */
    child->state = SERAPH_SOVEREIGN_SUSPENDED;

    /* Update parent's cached state */
    if (current_sovereign != NULL) {
        for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CHILDREN; i++) {
            if (seraph_vbit_is_true(current_sovereign->children[i].valid)) {
                Seraph_Vbit id_match = seraph_sovereign_id_equal(
                    current_sovereign->children[i].child_id, child->id);
                if (seraph_vbit_is_true(id_match)) {
                    current_sovereign->children[i].child_state = SERAPH_SOVEREIGN_SUSPENDED;
                    break;
                }
            }
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_sovereign_resume(Seraph_Capability child_cap) {
    Seraph_Sovereign* child = sovereign_from_cap(child_cap);
    if (child == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Check SUSPEND authority (same authority controls resume) */
    if (current_sovereign == NULL ||
        !seraph_authority_has(current_sovereign->authority, SERAPH_AUTH_SUSPEND)) {
        return SERAPH_VBIT_FALSE;
    }

    /* Can only resume SUSPENDED children */
    if (child->state != SERAPH_SOVEREIGN_SUSPENDED) {
        return SERAPH_VBIT_FALSE;
    }

    /* Verify this is our child */
    if (current_sovereign != NULL) {
        Seraph_Vbit is_our_child = seraph_sovereign_id_equal(
            child->parent_id, current_sovereign->id);
        if (!seraph_vbit_is_true(is_our_child)) {
            return SERAPH_VBIT_FALSE;
        }
    }

    /* Transition back to RUNNING */
    child->state = SERAPH_SOVEREIGN_RUNNING;

    /* Update parent's cached state */
    if (current_sovereign != NULL) {
        for (uint32_t i = 0; i < SERAPH_SOVEREIGN_MAX_CHILDREN; i++) {
            if (seraph_vbit_is_true(current_sovereign->children[i].valid)) {
                Seraph_Vbit id_match = seraph_sovereign_id_equal(
                    current_sovereign->children[i].child_id, child->id);
                if (seraph_vbit_is_true(id_match)) {
                    current_sovereign->children[i].child_state = SERAPH_SOVEREIGN_RUNNING;
                    break;
                }
            }
        }
    }

    return SERAPH_VBIT_TRUE;
}
