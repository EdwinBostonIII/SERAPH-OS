/**
 * @file strand.c
 * @brief MC13: Strand - Capability-Isolated Temporal Threading Implementation
 *
 * Implementation of the Strand threading model. Strands are capability-isolated
 * threads that share nothing by default.
 */

#include "seraph/strand.h"
#include "seraph/galactic_scheduler.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Global State
 *============================================================================*/

/** Thread-local current strand pointer */
#if defined(_MSC_VER)
static __declspec(thread) Seraph_Strand* g_current_strand = NULL;
#else
static __thread Seraph_Strand* g_current_strand = NULL;
#endif

/** Atomic strand ID counter */
static uint64_t g_strand_id_counter = 1;

/** Mutex generation counter */
static uint32_t g_mutex_generation = 1;

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Generate a unique strand ID
 */
__attribute__((unused))
static uint64_t generate_strand_id(void) {
    /* Simple increment for now; in real OS would use atomic */
    return g_strand_id_counter++;
}

/**
 * @brief Initialize capability table to all VOID
 */
__attribute__((unused))
static void init_cap_table(Seraph_Strand* strand) {
    for (uint32_t i = 0; i < SERAPH_STRAND_CAP_TABLE_SIZE; i++) {
        strand->cap_table[i].cap = SERAPH_CAP_VOID;
        strand->cap_table[i].status = SERAPH_CAP_STATUS_VOID;
        strand->cap_table[i].lender_id = 0;
        strand->cap_table[i].timeout = SERAPH_CHRONON_VOID;
    }
    strand->cap_count = 0;
}

/**
 * @brief Allocate stack memory
 */
__attribute__((unused))
static void* allocate_stack(size_t size) {
    if (size == 0) size = SERAPH_STRAND_DEFAULT_STACK_SIZE;

    /* Align to 16 bytes for proper stack alignment */
    void* mem = malloc(size);
    if (!mem) return NULL;

    /* Zero initialize for security */
    memset(mem, 0, size);
    return mem;
}

/**
 * @brief Free stack memory
 */
static void free_stack(void* stack) {
    free(stack);
}

/*============================================================================
 * Strand State Names
 *============================================================================*/

const char* seraph_strand_state_string(Seraph_Strand_State state) {
    switch (state) {
        case SERAPH_STRAND_NASCENT:    return "NASCENT";
        case SERAPH_STRAND_READY:      return "READY";
        case SERAPH_STRAND_RUNNING:    return "RUNNING";
        case SERAPH_STRAND_BLOCKED:    return "BLOCKED";
        case SERAPH_STRAND_WAITING:    return "WAITING";
        case SERAPH_STRAND_TERMINATED: return "TERMINATED";
        default:                       return "UNKNOWN";
    }
}

/*============================================================================
 * Strand Creation and Lifecycle
 *============================================================================*/

/* Direct framebuffer debug - write colored bars */
static void strand_debug_bar(int row, uint32_t color) {
    volatile uint32_t* fb = (volatile uint32_t*)0xC0000000ULL;
    for (int x = 0; x < 200; x++) {
        fb[row * 1920 + x] = color;
    }
}

Seraph_Strand_Error seraph_strand_create(
    Seraph_Strand* strand,
    void (*entry)(void*),
    void* arg,
    size_t stack_size)
{
    strand_debug_bar(300, 0xFFFFFF00);  /* Yellow = enter */

    if (!strand) return SERAPH_STRAND_ERR_NULL;
    if (!entry) return SERAPH_STRAND_ERR_INVALID;

    strand_debug_bar(301, 0xFF00FF00);  /* Green = checks passed */

    /* Zero initialize entire structure */
    strand_debug_bar(302, 0xFFFF8000);  /* Orange = memset */
    memset(strand, 0, sizeof(Seraph_Strand));
    strand_debug_bar(303, 0xFF00FFFF);  /* Cyan = memset done */

    /* Identity */
    strand_debug_bar(312, 0xFFFFFFFF);  /* White = identity */
    strand->strand_id = generate_strand_id();
    strand->state = SERAPH_STRAND_NASCENT;

    /* Temporal isolation */
    strand->chronon = SERAPH_CHRONON_ZERO;
    strand->chronon_limit = SERAPH_STRAND_DEFAULT_CHRONON_LIMIT;

    /* Capability isolation */
    strand_debug_bar(314, 0xFFFFFFFF);  /* White = cap */
    init_cap_table(strand);

    /* Memory isolation: create private arena */
    strand_debug_bar(316, 0xFFFFFFFF);  /* White = arena */
    Seraph_Vbit arena_result = seraph_arena_create(
        &strand->band,
        SERAPH_STRAND_DEFAULT_BAND_SIZE,
        SERAPH_ARENA_DEFAULT_ALIGNMENT,
        SERAPH_ARENA_FLAG_ZERO_ON_ALLOC
    );
    strand_debug_bar(318, 0xFFFFFFFF);  /* White = arena done */
    if (arena_result != SERAPH_VBIT_TRUE) {
        return SERAPH_STRAND_ERR_MEMORY;
    }

    /* Stack allocation */
    if (stack_size == 0) {
        stack_size = SERAPH_STRAND_DEFAULT_STACK_SIZE;
    }
    strand_debug_bar(320, 0xFFFFFFFF);  /* White = stack */
    strand->stack_base = allocate_stack(stack_size);
    strand_debug_bar(322, 0xFFFFFFFF);  /* White = stack done */
    if (!strand->stack_base) {
        seraph_arena_destroy(&strand->band);
        return SERAPH_STRAND_ERR_MEMORY;
    }
    strand->stack_size = stack_size;
    /* Stack grows downward, so initial SP is at top */
    strand->stack_pointer = (uint8_t*)strand->stack_base + stack_size;

    /* Create stack capability */
    strand->stack_cap = seraph_cap_create(
        strand->stack_base,
        stack_size,
        strand->band.generation,
        SERAPH_CAP_RW
    );

    /* Execution context */
    strand->entry_point = entry;
    strand->entry_arg = arg;
    strand->exit_code = 0;
    strand->started = false;

    /* Scheduling */
    strand->waiting_on = NULL;
    strand->blocked_on_mutex = NULL;
    strand->next_ready = NULL;
    strand->next_waiter = NULL;
    strand->priority = 0;

    /* Statistics */
    strand->yield_count = 0;
    strand->context_switches = 0;

    /* MC28: Zero-overhead proof-guided execution */
    strand->proof_blob = NULL;
    strand->proof_blob_generation = 0;
    strand->proof_flags = 0;
    strand->runtime_checks_skipped = 0;
    strand->runtime_checks_performed = 0;

    /* MC5+: Galactic Predictive Scheduling */
    strand->galactic_stats = NULL;  /* Allocated lazily by scheduler */
    strand->exec_time_galactic = SERAPH_GALACTIC_ZERO;
    strand->ready_timestamp = 0;
    strand->block_timestamp = 0;
    strand->quantum_ticks_used = 0;
    strand->predicted_exec = SERAPH_Q128_ZERO;

    return SERAPH_STRAND_OK;
}

void seraph_strand_destroy(Seraph_Strand* strand) {
    if (!strand) return;

    /* Can only destroy NASCENT or TERMINATED strands */
    if (strand->state != SERAPH_STRAND_NASCENT &&
        strand->state != SERAPH_STRAND_TERMINATED) {
        return;
    }

    /* Free stack */
    if (strand->stack_base) {
        free_stack(strand->stack_base);
        strand->stack_base = NULL;
    }

    /* Destroy private arena */
    seraph_arena_destroy(&strand->band);

    /* Clear capability table */
    init_cap_table(strand);

    /* MC5+: Free Galactic stats if allocated */
    if (strand->galactic_stats) {
        free(strand->galactic_stats);
        strand->galactic_stats = NULL;
    }

    /* Zero the structure */
    memset(strand, 0, sizeof(Seraph_Strand));
}

Seraph_Strand_Error seraph_strand_start(Seraph_Strand* strand) {
    if (!strand) return SERAPH_STRAND_ERR_NULL;

    /* Must be in NASCENT state */
    if (strand->state != SERAPH_STRAND_NASCENT) {
        return SERAPH_STRAND_ERR_STATE;
    }

    /* Transition to READY */
    strand->state = SERAPH_STRAND_READY;

    return SERAPH_STRAND_OK;
}

void seraph_strand_yield(void) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return;

    /* Must be RUNNING to yield */
    if (current->state != SERAPH_STRAND_RUNNING) return;

    /* Transition to READY */
    current->state = SERAPH_STRAND_READY;
    current->yield_count++;

    /* Tick chronon */
    current->chronon = seraph_chronon_add(current->chronon, 1);
}

Seraph_Strand_Error seraph_strand_join(Seraph_Strand* strand, uint64_t* exit_code) {
    if (!strand) return SERAPH_STRAND_ERR_NULL;

    Seraph_Strand* current = g_current_strand;
    if (!current) return SERAPH_STRAND_ERR_STATE;

    /* If already terminated, return immediately */
    if (strand->state == SERAPH_STRAND_TERMINATED) {
        if (exit_code) *exit_code = strand->exit_code;
        return SERAPH_STRAND_OK;
    }

    /* Check for deadlock */
    if (seraph_strand_would_deadlock(current, strand) == SERAPH_VBIT_TRUE) {
        return SERAPH_STRAND_ERR_DEADLOCK;
    }

    /* Set waiting state */
    current->state = SERAPH_STRAND_WAITING;
    current->waiting_on = strand;

    /* In a real implementation, we'd yield here and be woken when target exits.
       For testing, we simulate by checking state. */

    /* When target terminates, get exit code */
    if (exit_code) *exit_code = strand->exit_code;

    return SERAPH_STRAND_OK;
}

void seraph_strand_exit(uint64_t exit_code) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return;

    /* Set exit code and transition to TERMINATED */
    current->exit_code = exit_code;
    current->state = SERAPH_STRAND_TERMINATED;

    /* Revoke all lent capabilities */
    for (uint32_t i = 0; i < SERAPH_STRAND_CAP_TABLE_SIZE; i++) {
        if (current->cap_table[i].status == SERAPH_CAP_STATUS_LENT) {
            seraph_strand_revoke(i);
        }
    }

    /* In a real implementation, wake any strands waiting on us */
}

/*============================================================================
 * Strand Information
 *============================================================================*/

Seraph_Strand* seraph_strand_current(void) {
    return g_current_strand;
}

Seraph_Chronon seraph_strand_chronon(void) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return SERAPH_CHRONON_VOID;
    return current->chronon;
}

Seraph_Chronon seraph_strand_tick(void) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return SERAPH_CHRONON_VOID;

    current->chronon = seraph_chronon_add(current->chronon, 1);
    return current->chronon;
}

/*============================================================================
 * Capability Table Operations
 *============================================================================*/

Seraph_Strand_Error seraph_strand_cap_store(
    Seraph_Strand* strand,
    uint32_t slot,
    Seraph_Capability cap)
{
    if (!strand) return SERAPH_STRAND_ERR_NULL;
    if (slot >= SERAPH_STRAND_CAP_TABLE_SIZE) return SERAPH_STRAND_ERR_INVALID;

    /* If slot was empty and new cap is valid, increment count */
    bool was_empty = (strand->cap_table[slot].status == SERAPH_CAP_STATUS_VOID);
    bool is_valid = !seraph_cap_is_void(cap);

    strand->cap_table[slot].cap = cap;
    strand->cap_table[slot].status = is_valid ? SERAPH_CAP_STATUS_OWNED : SERAPH_CAP_STATUS_VOID;
    strand->cap_table[slot].lender_id = 0;
    strand->cap_table[slot].timeout = SERAPH_CHRONON_VOID;

    if (was_empty && is_valid) strand->cap_count++;
    else if (!was_empty && !is_valid) strand->cap_count--;

    return SERAPH_STRAND_OK;
}

Seraph_Capability seraph_strand_cap_get(const Seraph_Strand* strand, uint32_t slot) {
    if (!strand) return SERAPH_CAP_VOID;
    if (slot >= SERAPH_STRAND_CAP_TABLE_SIZE) return SERAPH_CAP_VOID;
    if (strand->cap_table[slot].status == SERAPH_CAP_STATUS_VOID) return SERAPH_CAP_VOID;
    if (strand->cap_table[slot].status == SERAPH_CAP_STATUS_LENT) return SERAPH_CAP_VOID;

    return strand->cap_table[slot].cap;
}

uint32_t seraph_strand_cap_find_slot(const Seraph_Strand* strand) {
    if (!strand) return SERAPH_VOID_U32;

    for (uint32_t i = 0; i < SERAPH_STRAND_CAP_TABLE_SIZE; i++) {
        if (strand->cap_table[i].status == SERAPH_CAP_STATUS_VOID) {
            return i;
        }
    }
    return SERAPH_VOID_U32;
}

Seraph_Strand_Error seraph_strand_cap_clear(Seraph_Strand* strand, uint32_t slot) {
    if (!strand) return SERAPH_STRAND_ERR_NULL;
    if (slot >= SERAPH_STRAND_CAP_TABLE_SIZE) return SERAPH_STRAND_ERR_INVALID;

    if (strand->cap_table[slot].status != SERAPH_CAP_STATUS_VOID) {
        strand->cap_count--;
    }

    strand->cap_table[slot].cap = SERAPH_CAP_VOID;
    strand->cap_table[slot].status = SERAPH_CAP_STATUS_VOID;
    strand->cap_table[slot].lender_id = 0;
    strand->cap_table[slot].timeout = SERAPH_CHRONON_VOID;

    return SERAPH_STRAND_OK;
}

/*============================================================================
 * Capability Grants Between Strands
 *============================================================================*/

Seraph_Strand_Error seraph_strand_grant(
    Seraph_Strand* to,
    uint32_t src_slot,
    uint32_t dest_slot)
{
    Seraph_Strand* from = g_current_strand;
    if (!from || !to) return SERAPH_STRAND_ERR_NULL;
    if (src_slot >= SERAPH_STRAND_CAP_TABLE_SIZE ||
        dest_slot >= SERAPH_STRAND_CAP_TABLE_SIZE) {
        return SERAPH_STRAND_ERR_INVALID;
    }

    /* Source must be OWNED */
    if (from->cap_table[src_slot].status != SERAPH_CAP_STATUS_OWNED) {
        return SERAPH_STRAND_ERR_PERM;
    }

    /* Get the capability */
    Seraph_Capability cap = from->cap_table[src_slot].cap;

    /* Check if dest slot was empty BEFORE we change it */
    bool dest_was_empty = (to->cap_table[dest_slot].status == SERAPH_CAP_STATUS_VOID);

    /* Transfer to destination */
    to->cap_table[dest_slot].cap = cap;
    to->cap_table[dest_slot].status = SERAPH_CAP_STATUS_OWNED;
    to->cap_table[dest_slot].lender_id = 0;
    to->cap_table[dest_slot].timeout = SERAPH_CHRONON_VOID;
    if (dest_was_empty) {
        to->cap_count++;
    }

    /* VOID the source */
    from->cap_table[src_slot].cap = SERAPH_CAP_VOID;
    from->cap_table[src_slot].status = SERAPH_CAP_STATUS_VOID;
    from->cap_count--;

    return SERAPH_STRAND_OK;
}

Seraph_Strand_Error seraph_strand_lend(
    Seraph_Strand* to,
    uint32_t src_slot,
    uint32_t dest_slot,
    Seraph_Chronon timeout)
{
    Seraph_Strand* from = g_current_strand;
    if (!from || !to) return SERAPH_STRAND_ERR_NULL;
    if (src_slot >= SERAPH_STRAND_CAP_TABLE_SIZE ||
        dest_slot >= SERAPH_STRAND_CAP_TABLE_SIZE) {
        return SERAPH_STRAND_ERR_INVALID;
    }

    /* Source must be OWNED */
    if (from->cap_table[src_slot].status != SERAPH_CAP_STATUS_OWNED) {
        return SERAPH_STRAND_ERR_PERM;
    }

    /* Get the capability */
    Seraph_Capability cap = from->cap_table[src_slot].cap;

    /* Check if dest slot was empty BEFORE we change it */
    bool dest_was_empty = (to->cap_table[dest_slot].status == SERAPH_CAP_STATUS_VOID);

    /* Mark source as LENT */
    from->cap_table[src_slot].status = SERAPH_CAP_STATUS_LENT;

    /* Transfer to destination as BORROWED */
    to->cap_table[dest_slot].cap = cap;
    to->cap_table[dest_slot].status = SERAPH_CAP_STATUS_BORROWED;
    to->cap_table[dest_slot].lender_id = (uint32_t)from->strand_id;
    to->cap_table[dest_slot].timeout = timeout;
    if (dest_was_empty) {
        to->cap_count++;
    }

    return SERAPH_STRAND_OK;
}

Seraph_Strand_Error seraph_strand_revoke(uint32_t src_slot) {
    Seraph_Strand* from = g_current_strand;
    if (!from) return SERAPH_STRAND_ERR_NULL;
    if (src_slot >= SERAPH_STRAND_CAP_TABLE_SIZE) return SERAPH_STRAND_ERR_INVALID;

    /* Must be LENT */
    if (from->cap_table[src_slot].status != SERAPH_CAP_STATUS_LENT) {
        return SERAPH_STRAND_ERR_STATE;
    }

    /* Mark as OWNED again */
    from->cap_table[src_slot].status = SERAPH_CAP_STATUS_OWNED;

    /* In a real implementation, we'd find the borrower and VOID their copy.
       For now, the borrower's copy becomes invalid on timeout check. */

    return SERAPH_STRAND_OK;
}

Seraph_Strand_Error seraph_strand_return(uint32_t slot) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return SERAPH_STRAND_ERR_NULL;
    if (slot >= SERAPH_STRAND_CAP_TABLE_SIZE) return SERAPH_STRAND_ERR_INVALID;

    /* Must be BORROWED */
    if (current->cap_table[slot].status != SERAPH_CAP_STATUS_BORROWED) {
        return SERAPH_STRAND_ERR_STATE;
    }

    /* VOID our copy */
    current->cap_table[slot].cap = SERAPH_CAP_VOID;
    current->cap_table[slot].status = SERAPH_CAP_STATUS_VOID;
    current->cap_table[slot].lender_id = 0;
    current->cap_table[slot].timeout = SERAPH_CHRONON_VOID;
    current->cap_count--;

    /* In a real implementation, notify lender their cap is returned */

    return SERAPH_STRAND_OK;
}

void seraph_strand_process_lends(Seraph_Strand* strand) {
    if (!strand) return;

    Seraph_Chronon now = strand->chronon;

    for (uint32_t i = 0; i < SERAPH_STRAND_CAP_TABLE_SIZE; i++) {
        if (strand->cap_table[i].status == SERAPH_CAP_STATUS_BORROWED) {
            Seraph_Chronon timeout = strand->cap_table[i].timeout;

            /* Check if expired */
            if (seraph_chronon_exists(timeout) && now >= timeout) {
                /* Expired - VOID the capability */
                strand->cap_table[i].cap = SERAPH_CAP_VOID;
                strand->cap_table[i].status = SERAPH_CAP_STATUS_VOID;
                strand->cap_table[i].lender_id = 0;
                strand->cap_table[i].timeout = SERAPH_CHRONON_VOID;
                strand->cap_count--;
            }
        }
    }
}

/*============================================================================
 * Mutex Operations
 *============================================================================*/

Seraph_Strand_Error seraph_strand_mutex_init(Seraph_Strand_Mutex* mutex) {
    if (!mutex) return SERAPH_STRAND_ERR_NULL;

    memset(mutex, 0, sizeof(Seraph_Strand_Mutex));

    /* Create the mutex capability */
    /* The mutex doesn't actually protect memory - it's an access token.
       We create a minimal capability as the token. */
    mutex->cap = seraph_cap_create(
        &mutex->cap,  /* Self-referential base */
        sizeof(Seraph_Capability),
        g_mutex_generation++,
        SERAPH_CAP_READ
    );

    mutex->holder = NULL;
    mutex->wait_queue = NULL;
    mutex->acquisitions = 0;
    mutex->contentions = 0;
    mutex->generation = g_mutex_generation;
    mutex->flags = 0;

    return SERAPH_STRAND_OK;
}

void seraph_strand_mutex_destroy(Seraph_Strand_Mutex* mutex) {
    if (!mutex) return;

    /* Should not be held */
    if (mutex->holder != NULL) return;

    /* Should not have waiters */
    if (mutex->wait_queue != NULL) return;

    memset(mutex, 0, sizeof(Seraph_Strand_Mutex));
}

Seraph_Capability seraph_strand_mutex_acquire(
    Seraph_Strand_Mutex* mutex,
    uint32_t dest_slot)
{
    if (!mutex) return SERAPH_CAP_VOID;

    Seraph_Strand* current = g_current_strand;
    if (!current) return SERAPH_CAP_VOID;
    if (dest_slot >= SERAPH_STRAND_CAP_TABLE_SIZE) return SERAPH_CAP_VOID;

    /* Try to acquire */
    if (mutex->holder == NULL) {
        /* Uncontended - acquire immediately */
        mutex->holder = current;
        mutex->acquisitions++;

        /* Store capability in strand's table */
        current->cap_table[dest_slot].cap = mutex->cap;
        current->cap_table[dest_slot].status = SERAPH_CAP_STATUS_OWNED;
        current->cap_count++;

        return mutex->cap;
    }

    /* Check for deadlock - if holder is waiting on us, that's a cycle */
    if (mutex->holder->waiting_on == current) {
        /* Deadlock detected */
        return SERAPH_CAP_VOID;
    }

    /* Contended - would block */
    mutex->contentions++;

    /* Add to wait queue */
    current->next_waiter = mutex->wait_queue;
    mutex->wait_queue = current;
    current->state = SERAPH_STRAND_BLOCKED;
    current->blocked_on_mutex = (Seraph_Strand*)mutex;  /* Ugly cast for linking */

    /* In a real implementation, we'd yield here and be woken when mutex available */

    return SERAPH_CAP_VOID;  /* For now, return VOID on contention */
}

Seraph_Strand_Error seraph_strand_mutex_release(
    Seraph_Strand_Mutex* mutex,
    Seraph_Capability held)
{
    if (!mutex) return SERAPH_STRAND_ERR_NULL;

    Seraph_Strand* current = g_current_strand;
    if (!current) return SERAPH_STRAND_ERR_STATE;

    /* Must be the holder */
    if (mutex->holder != current) {
        return SERAPH_STRAND_ERR_PERM;
    }

    /* Verify the capability matches */
    if (!seraph_cap_same_region(held, mutex->cap)) {
        return SERAPH_STRAND_ERR_PERM;
    }

    /* Clear the capability from our table */
    for (uint32_t i = 0; i < SERAPH_STRAND_CAP_TABLE_SIZE; i++) {
        if (seraph_cap_same_region(current->cap_table[i].cap, mutex->cap)) {
            seraph_strand_cap_clear(current, i);
            break;
        }
    }

    /* Wake first waiter if any */
    if (mutex->wait_queue != NULL) {
        Seraph_Strand* waiter = mutex->wait_queue;
        mutex->wait_queue = waiter->next_waiter;
        waiter->next_waiter = NULL;
        waiter->state = SERAPH_STRAND_READY;
        waiter->blocked_on_mutex = NULL;

        /* Transfer ownership to waiter */
        mutex->holder = waiter;
        mutex->acquisitions++;
    } else {
        /* No waiters */
        mutex->holder = NULL;
    }

    return SERAPH_STRAND_OK;
}

Seraph_Capability seraph_strand_mutex_try_acquire(
    Seraph_Strand_Mutex* mutex,
    uint32_t dest_slot)
{
    if (!mutex) return SERAPH_CAP_VOID;

    Seraph_Strand* current = g_current_strand;
    if (!current) return SERAPH_CAP_VOID;
    if (dest_slot >= SERAPH_STRAND_CAP_TABLE_SIZE) return SERAPH_CAP_VOID;

    /* Try to acquire (non-blocking) */
    if (mutex->holder == NULL) {
        /* Success */
        mutex->holder = current;
        mutex->acquisitions++;

        current->cap_table[dest_slot].cap = mutex->cap;
        current->cap_table[dest_slot].status = SERAPH_CAP_STATUS_OWNED;
        current->cap_count++;

        return mutex->cap;
    }

    /* Already held - return VOID without blocking */
    return SERAPH_CAP_VOID;
}

/*============================================================================
 * Strand-Local Storage
 *============================================================================*/

void* seraph_strand_local_alloc(size_t size) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return NULL;

    return seraph_arena_alloc(&current->band, size, 0);
}

void* seraph_strand_local_calloc(size_t size) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return NULL;

    return seraph_arena_calloc(&current->band, size, 0);
}

void seraph_strand_local_free(void* ptr) {
    /* In a bump allocator, individual frees are no-ops */
    (void)ptr;
}

size_t seraph_strand_local_remaining(void) {
    Seraph_Strand* current = g_current_strand;
    if (!current) return 0;

    return seraph_arena_remaining(&current->band);
}

/*============================================================================
 * Scheduler Interface
 *============================================================================*/

bool seraph_strand_run_quantum(Seraph_Strand* strand) {
    if (!strand) return false;

    /* Must be READY to run */
    if (strand->state != SERAPH_STRAND_READY) return false;

    /* Set as current and RUNNING */
    Seraph_Strand* prev = g_current_strand;
    g_current_strand = strand;
    strand->state = SERAPH_STRAND_RUNNING;
    strand->context_switches++;

    /* Process any expired lends */
    seraph_strand_process_lends(strand);

    /* If not yet started, call entry point */
    if (!strand->started) {
        strand->started = true;
        if (strand->entry_point) {
            strand->entry_point(strand->entry_arg);
        }
        /* Entry returned - strand terminates */
        strand->state = SERAPH_STRAND_TERMINATED;
    }

    /* Tick chronon */
    strand->chronon = seraph_chronon_add(strand->chronon, 100);

    /* Restore previous current */
    g_current_strand = prev;

    return strand->state == SERAPH_STRAND_READY ||
           strand->state == SERAPH_STRAND_RUNNING;
}

void seraph_strand_set_current(Seraph_Strand* strand) {
    g_current_strand = strand;
}

void seraph_strand_schedule(Seraph_Strand** strands, uint32_t count) {
    if (!strands || count == 0) return;

    uint32_t active = count;
    uint32_t rounds = 0;
    const uint32_t max_rounds = 10000;  /* Safety limit */

    while (active > 0 && rounds < max_rounds) {
        rounds++;
        active = 0;

        for (uint32_t i = 0; i < count; i++) {
            Seraph_Strand* s = strands[i];
            if (!s) continue;

            /* Process lends for all strands */
            seraph_strand_process_lends(s);

            /* Check if runnable */
            if (s->state == SERAPH_STRAND_READY) {
                seraph_strand_run_quantum(s);
            }

            /* Check if waiter's target terminated */
            if (s->state == SERAPH_STRAND_WAITING && s->waiting_on) {
                if (s->waiting_on->state == SERAPH_STRAND_TERMINATED) {
                    s->state = SERAPH_STRAND_READY;
                    s->waiting_on = NULL;
                }
            }

            /* Count active strands */
            if (s->state != SERAPH_STRAND_TERMINATED) {
                active++;
            }
        }
    }
}

/*============================================================================
 * Deadlock Detection
 *============================================================================*/

Seraph_Vbit seraph_strand_would_deadlock(
    const Seraph_Strand* waiter,
    const Seraph_Strand* target)
{
    if (!waiter || !target) return SERAPH_VBIT_VOID;

    /* Walk the waiting chain from target to see if we reach waiter */
    const Seraph_Strand* current = target;
    uint32_t depth = 0;
    const uint32_t max_depth = SERAPH_STRAND_CAP_TABLE_SIZE;  /* Reasonable limit */

    while (current && depth < max_depth) {
        /* If target (or something in its chain) is waiting on waiter -> cycle */
        if (current->waiting_on == waiter) {
            return SERAPH_VBIT_TRUE;
        }

        /* Check blocked_on_mutex chain too */
        if (current->blocked_on_mutex) {
            Seraph_Strand_Mutex* mutex = (Seraph_Strand_Mutex*)current->blocked_on_mutex;
            if (mutex->holder == waiter) {
                return SERAPH_VBIT_TRUE;
            }
        }

        current = current->waiting_on;
        depth++;
    }

    return SERAPH_VBIT_FALSE;
}

/*============================================================================
 * MC28: Zero-Overhead Proof-Guided Execution
 *============================================================================*/

Seraph_Strand_Error seraph_strand_attach_proof_blob(
    Seraph_Strand* strand,
    const struct Seraph_Proof_Blob* proof_blob,
    uint32_t flags)
{
    if (!strand) return SERAPH_STRAND_ERR_NULL;

    /* Detach existing blob if any */
    strand->proof_blob = NULL;
    strand->proof_blob_generation = 0;
    strand->proof_flags = 0;
    strand->runtime_checks_skipped = 0;
    strand->runtime_checks_performed = 0;

    if (!proof_blob) {
        /* Just detaching, which is OK */
        return SERAPH_STRAND_OK;
    }

    /* Attach new blob */
    strand->proof_blob = proof_blob;
    strand->proof_flags = flags;

    return SERAPH_STRAND_OK;
}

Seraph_Strand_Error seraph_strand_detach_proof_blob(Seraph_Strand* strand) {
    return seraph_strand_attach_proof_blob(strand, NULL, 0);
}

void seraph_strand_proof_stats(
    const Seraph_Strand* strand,
    uint64_t* checks_skipped,
    uint64_t* checks_done)
{
    if (!strand) {
        if (checks_skipped) *checks_skipped = 0;
        if (checks_done) *checks_done = 0;
        return;
    }

    if (checks_skipped) *checks_skipped = strand->runtime_checks_skipped;
    if (checks_done) *checks_done = strand->runtime_checks_performed;
}
