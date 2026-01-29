/**
 * @file scheduler.c
 * @brief Preemptive Scheduler Implementation
 *
 * MC13/27: The Pulse - Preemptive Scheduler
 *
 * Implements priority-based preemptive scheduling for Strands.
 */

#include "seraph/scheduler.h"
#include "seraph/apic.h"
#include "seraph/context.h"
#include "seraph/galactic_scheduler.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Configuration
 *============================================================================*/

/* Maximum strands per priority queue */
#define MAX_STRANDS_PER_QUEUE   256

/* Default preemption rate (Hz) */
#define DEFAULT_PREEMPTION_HZ   1000

/* Time quantum per priority level (in ticks) */
static const uint32_t priority_quantum[SERAPH_PRIORITY_MAX] = {
    SERAPH_QUANTUM_IDLE,
    SERAPH_QUANTUM_BACKGROUND,
    SERAPH_QUANTUM_LOW,
    SERAPH_QUANTUM_NORMAL,
    SERAPH_QUANTUM_HIGH,
    SERAPH_QUANTUM_REALTIME,
    SERAPH_QUANTUM_CRITICAL
};

/*============================================================================
 * Run Queue
 *============================================================================*/

/**
 * @brief Per-priority run queue
 */
typedef struct {
    Seraph_Strand* head;        /**< First strand in queue */
    Seraph_Strand* tail;        /**< Last strand in queue */
    size_t count;               /**< Number of strands */
} Seraph_RunQueue;

/*============================================================================
 * Scheduler State
 *============================================================================*/

static struct {
    /* Run queues per priority level */
    Seraph_RunQueue ready_queues[SERAPH_PRIORITY_MAX];

    /* Currently running strand */
    Seraph_Strand* current;

    /* Idle strand (always runnable) */
    Seraph_Strand* idle_strand;

    /* Idle strand storage */
    Seraph_Strand idle_strand_storage;
    uint8_t idle_stack[4096] __attribute__((aligned(16)));

    /* Blocked strand list */
    Seraph_Strand* blocked_head;
    size_t blocked_count;

    /* Scheduler state */
    bool running;
    bool in_scheduler;          /**< Prevent re-entry */
    uint32_t preemption_hz;

    /* Current time slice */
    uint32_t quantum_remaining;
    uint32_t current_priority;

    /* Statistics */
    Seraph_Scheduler_Stats stats;

    /* Lock for SMP safety */
    volatile int lock;

    /* MC5+: Galactic Predictive Scheduling */
    uint64_t global_tick;           /**< Global tick counter for Galactic */
    bool galactic_enabled;          /**< Global Galactic scheduling enable */

} scheduler = {0};

/*============================================================================
 * Lock Operations
 *============================================================================*/

static inline void scheduler_lock(void) {
    while (__sync_lock_test_and_set(&scheduler.lock, 1)) {
        __asm__ volatile("pause");
    }
}

static inline void scheduler_unlock(void) {
    __sync_lock_release(&scheduler.lock);
}

static inline void disable_interrupts(void) {
    __asm__ volatile("cli");
}

static inline void enable_interrupts(void) {
    __asm__ volatile("sti");
}

/*============================================================================
 * Run Queue Operations
 *============================================================================*/

static void runqueue_add(Seraph_RunQueue* queue, Seraph_Strand* strand) {
    strand->next_in_queue = NULL;

    if (queue->tail != NULL) {
        queue->tail->next_in_queue = strand;
    } else {
        queue->head = strand;
    }
    queue->tail = strand;
    queue->count++;
}

static Seraph_Strand* runqueue_pop(Seraph_RunQueue* queue) {
    Seraph_Strand* strand = queue->head;
    if (strand == NULL) return NULL;

    queue->head = strand->next_in_queue;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->count--;

    strand->next_in_queue = NULL;
    return strand;
}

static void runqueue_remove(Seraph_RunQueue* queue, Seraph_Strand* strand) {
    if (queue->head == strand) {
        runqueue_pop(queue);
        return;
    }

    Seraph_Strand* prev = queue->head;
    while (prev != NULL && prev->next_in_queue != strand) {
        prev = prev->next_in_queue;
    }

    if (prev != NULL) {
        prev->next_in_queue = strand->next_in_queue;
        if (queue->tail == strand) {
            queue->tail = prev;
        }
        queue->count--;
    }
}

/*============================================================================
 * Idle Strand
 *============================================================================*/

static void idle_strand_entry(void* arg) {
    (void)arg;

    /* Idle loop - just wait for interrupts */
    while (1) {
        __asm__ volatile("hlt");
    }
}

static void create_idle_strand(void) {
    Seraph_Strand* idle = &scheduler.idle_strand_storage;
    memset(idle, 0, sizeof(Seraph_Strand));

    idle->id = 0;
    idle->state = SERAPH_STRAND_RUNNING;
    idle->priority = SERAPH_PRIORITY_IDLE;
    idle->base_priority = SERAPH_PRIORITY_IDLE;
    idle->context_valid = true;
    idle->flags = SERAPH_STRAND_FLAG_KERNEL;

    /* Initialize context */
    void* stack_top = scheduler.idle_stack + sizeof(scheduler.idle_stack);
    seraph_context_init_kernel(&idle->cpu_context, idle_strand_entry, stack_top, NULL);

    scheduler.idle_strand = idle;
}

/*============================================================================
 * Initialization
 *============================================================================*/

void seraph_scheduler_init(void) {
    memset(&scheduler, 0, sizeof(scheduler));

    /* Initialize run queues */
    for (int i = 0; i < SERAPH_PRIORITY_MAX; i++) {
        scheduler.ready_queues[i].head = NULL;
        scheduler.ready_queues[i].tail = NULL;
        scheduler.ready_queues[i].count = 0;
    }

    /* Create idle strand */
    create_idle_strand();

    scheduler.preemption_hz = DEFAULT_PREEMPTION_HZ;
    scheduler.running = false;
    scheduler.in_scheduler = false;

    /* MC5+: Initialize Galactic Predictive Scheduling */
    scheduler.global_tick = 0;
    scheduler.galactic_enabled = true;
    seraph_galactic_sched_global_init();
}

void seraph_scheduler_start(void) {
    /* Initialize APIC timer for preemption */
    if (!seraph_apic_init()) {
        /* APIC not available - can't do preemptive scheduling */
        return;
    }

    scheduler.running = true;

    /* Start with idle strand */
    scheduler.current = scheduler.idle_strand;
    scheduler.quantum_remaining = SERAPH_QUANTUM_IDLE;

    /* Start timer for preemption */
    seraph_apic_timer_start_hz(scheduler.preemption_hz);

    /* Enable interrupts and let scheduling begin */
    enable_interrupts();
}

void seraph_scheduler_stop(void) {
    disable_interrupts();
    seraph_apic_timer_stop();
    scheduler.running = false;
}

bool seraph_scheduler_running(void) {
    return scheduler.running;
}

/*============================================================================
 * Strand Management
 *============================================================================*/

void seraph_scheduler_ready(Seraph_Strand* strand) {
    if (strand == NULL) return;

    scheduler_lock();

    strand->state = SERAPH_STRAND_READY;

    /* MC5+: Track timestamp for response time measurement */
    strand->ready_timestamp = scheduler.global_tick;

    runqueue_add(&scheduler.ready_queues[strand->priority], strand);
    scheduler.stats.ready_count++;

    scheduler_unlock();
}

void seraph_scheduler_remove(Seraph_Strand* strand) {
    if (strand == NULL) return;

    scheduler_lock();

    /* Remove from ready queue if present */
    if (strand->state == SERAPH_STRAND_READY) {
        runqueue_remove(&scheduler.ready_queues[strand->priority], strand);
        scheduler.stats.ready_count--;
    }

    /* Remove from blocked list if present */
    if (strand->state == SERAPH_STRAND_BLOCKED) {
        if (scheduler.blocked_head == strand) {
            scheduler.blocked_head = strand->next_in_queue;
        } else {
            Seraph_Strand* prev = scheduler.blocked_head;
            while (prev != NULL && prev->next_in_queue != strand) {
                prev = prev->next_in_queue;
            }
            if (prev != NULL) {
                prev->next_in_queue = strand->next_in_queue;
            }
        }
        scheduler.blocked_count--;
    }

    /* MC5+: Free Galactic stats if allocated */
    if (strand->galactic_stats != NULL) {
        free(strand->galactic_stats);
        strand->galactic_stats = NULL;
    }

    strand->state = SERAPH_STRAND_TERMINATED;
    scheduler.stats.strands_destroyed++;

    scheduler_unlock();
}

Seraph_Strand* seraph_scheduler_current(void) {
    return scheduler.current;
}

Seraph_Strand* seraph_scheduler_idle(void) {
    return scheduler.idle_strand;
}

/*============================================================================
 * Core Scheduling
 *============================================================================*/

/**
 * @brief Select next strand to run
 *
 * Picks the highest priority ready strand.
 */
static Seraph_Strand* pick_next_strand(void) {
    /* Check priority queues from highest to lowest */
    for (int i = SERAPH_PRIORITY_MAX - 1; i >= 0; i--) {
        Seraph_Strand* strand = runqueue_pop(&scheduler.ready_queues[i]);
        if (strand != NULL) {
            scheduler.stats.ready_count--;
            return strand;
        }
    }

    /* No ready strands - return idle */
    return scheduler.idle_strand;
}

/**
 * @brief Perform context switch to new strand
 */
static void switch_to(Seraph_Strand* next) {
    if (next == scheduler.current) return;

    Seraph_Strand* prev = scheduler.current;

    /* Update state */
    if (prev != scheduler.idle_strand && prev->state == SERAPH_STRAND_RUNNING) {
        prev->state = SERAPH_STRAND_READY;
    }
    next->state = SERAPH_STRAND_RUNNING;

    /* Set up new quantum */
    scheduler.quantum_remaining = priority_quantum[next->priority];
    scheduler.current_priority = next->priority;
    scheduler.current = next;

    scheduler.stats.total_switches++;

    /* Perform context switch */
    if (prev->context_valid && next->context_valid) {
        seraph_context_switch(&prev->cpu_context, &next->cpu_context);
    } else if (next->context_valid) {
        seraph_context_restore(&next->cpu_context);
    }
}

void seraph_scheduler_tick(Seraph_InterruptFrame* frame) {
    (void)frame;

    if (!scheduler.running || scheduler.in_scheduler) {
        seraph_apic_eoi();
        return;
    }

    scheduler.in_scheduler = true;
    scheduler.global_tick++;

    /* Track ticks used by current strand */
    Seraph_Strand* current = scheduler.current;
    if (current != scheduler.idle_strand && current != NULL) {
        current->quantum_ticks_used++;
    }

    /* Decrement quantum */
    if (scheduler.quantum_remaining > 0) {
        scheduler.quantum_remaining--;
    }

    /* Check if preemption needed */
    if (scheduler.quantum_remaining == 0) {
        scheduler_lock();

        /* MC5+: Update Galactic execution metrics for the strand that just ran */
        if (scheduler.galactic_enabled && current != scheduler.idle_strand && current != NULL) {
            uint32_t ticks_used = current->quantum_ticks_used;
            uint32_t quantum_size = priority_quantum[current->priority];

            /* Initialize Galactic stats if needed */
            if (current->galactic_stats == NULL) {
                current->galactic_stats = (Seraph_Galactic_Exec_Stats*)malloc(
                    sizeof(Seraph_Galactic_Exec_Stats));
                if (current->galactic_stats != NULL) {
                    seraph_galactic_sched_init(current->galactic_stats,
                        SERAPH_GALACTIC_SCHED_ENABLED |
                        SERAPH_GALACTIC_SCHED_AUTOADJUST |
                        SERAPH_GALACTIC_SCHED_ADAPTIVE_LR);
                }
            }

            if (current->galactic_stats != NULL) {
                /* Update execution metrics */
                seraph_galactic_sched_update_exec(
                    current->galactic_stats,
                    ticks_used,
                    quantum_size,
                    scheduler.global_tick);

                /* Copy to inline field for fast access */
                current->exec_time_galactic = current->galactic_stats->exec_time;

                /* Compute predicted execution for next quantum */
                current->predicted_exec = seraph_galactic_sched_predict_exec(
                    current->galactic_stats,
                    SERAPH_GALACTIC_SCHED_HORIZON);

                /* Apply Galactic-based priority adjustment */
                uint32_t new_priority = seraph_galactic_sched_adjust_priority(
                    current->galactic_stats,
                    current->priority,
                    SERAPH_GALACTIC_SCHED_TARGET,
                    SERAPH_PRIORITY_BACKGROUND,  /* Min (can't go to IDLE) */
                    SERAPH_PRIORITY_REALTIME     /* Max (leave CRITICAL alone) */
                );

                /* Apply priority change if different */
                if (new_priority != current->priority) {
                    /* Move to new priority queue */
                    current->priority = new_priority;
                }

                /* Periodically adapt learning rate */
                if ((scheduler.global_tick % 1000) == 0) {
                    seraph_galactic_sched_adapt_learning_rate(current->galactic_stats);
                }
            }

            /* Reset tick counter for next quantum */
            current->quantum_ticks_used = 0;
        }

        /* Put current back in ready queue (if not idle or blocked) */
        if (current != scheduler.idle_strand &&
            current->state == SERAPH_STRAND_RUNNING) {
            current->preempted = true;
            runqueue_add(&scheduler.ready_queues[current->priority], current);
            scheduler.stats.ready_count++;
        }

        /* Pick next strand */
        Seraph_Strand* next = pick_next_strand();

        /* MC5+: Track response time for Galactic scheduling */
        if (scheduler.galactic_enabled && next != scheduler.idle_strand && next != NULL) {
            if (next->ready_timestamp > 0 && next->galactic_stats != NULL) {
                uint64_t response_time = scheduler.global_tick - next->ready_timestamp;
                seraph_galactic_sched_update_response(
                    next->galactic_stats,
                    response_time,
                    scheduler.global_tick);
            }
        }

        scheduler_unlock();

        if (next != current) {
            scheduler.stats.preemptions++;
            switch_to(next);
        } else {
            /* Same strand continues - reset quantum */
            scheduler.quantum_remaining = priority_quantum[current->priority];
        }
    }

    /* MC5+: Global Galactic tick processing */
    if (scheduler.galactic_enabled) {
        seraph_galactic_sched_tick(scheduler.global_tick);
    }

    scheduler.in_scheduler = false;
    seraph_apic_eoi();
}

void seraph_scheduler_yield(void) {
    if (!scheduler.running) return;

    disable_interrupts();
    scheduler_lock();

    /* Put current back in ready queue */
    Seraph_Strand* current = scheduler.current;
    if (current != scheduler.idle_strand) {
        current->preempted = false;
        runqueue_add(&scheduler.ready_queues[current->priority], current);
        scheduler.stats.ready_count++;
    }

    /* Pick next strand */
    Seraph_Strand* next = pick_next_strand();

    scheduler_unlock();

    if (next != current) {
        scheduler.stats.yields++;
        switch_to(next);
    }

    enable_interrupts();
}

void seraph_scheduler_block(void) {
    if (!scheduler.running) return;

    disable_interrupts();
    scheduler_lock();

    Seraph_Strand* current = scheduler.current;
    if (current != scheduler.idle_strand) {
        current->state = SERAPH_STRAND_BLOCKED;

        /* MC5+: Track block timestamp for wait time measurement */
        current->block_timestamp = scheduler.global_tick;

        /* Add to blocked list */
        current->next_in_queue = scheduler.blocked_head;
        scheduler.blocked_head = current;
        scheduler.blocked_count++;
        scheduler.stats.blocked_count++;
    }

    /* Pick next strand */
    Seraph_Strand* next = pick_next_strand();

    scheduler_unlock();

    if (next != current) {
        switch_to(next);
    }

    enable_interrupts();
}

void seraph_scheduler_wake(Seraph_Strand* strand) {
    if (strand == NULL) return;

    scheduler_lock();

    if (strand->state == SERAPH_STRAND_BLOCKED) {
        /* MC5+: Update wait time statistics */
        if (scheduler.galactic_enabled && strand->galactic_stats != NULL &&
            strand->block_timestamp > 0) {
            uint64_t wait_time = scheduler.global_tick - strand->block_timestamp;
            seraph_galactic_sched_update_wait(
                strand->galactic_stats,
                wait_time,
                scheduler.global_tick);
        }

        /* Remove from blocked list */
        if (scheduler.blocked_head == strand) {
            scheduler.blocked_head = strand->next_in_queue;
        } else {
            Seraph_Strand* prev = scheduler.blocked_head;
            while (prev != NULL && prev->next_in_queue != strand) {
                prev = prev->next_in_queue;
            }
            if (prev != NULL) {
                prev->next_in_queue = strand->next_in_queue;
            }
        }
        scheduler.blocked_count--;
        scheduler.stats.blocked_count--;

        /* Add to ready queue */
        strand->state = SERAPH_STRAND_READY;

        /* MC5+: Track ready timestamp */
        strand->ready_timestamp = scheduler.global_tick;

        runqueue_add(&scheduler.ready_queues[strand->priority], strand);
        scheduler.stats.ready_count++;
    }

    scheduler_unlock();
}

void seraph_scheduler_reschedule(void) {
    /* Trigger software interrupt or just yield */
    seraph_scheduler_yield();
}

/*============================================================================
 * Priority Management
 *============================================================================*/

void seraph_scheduler_set_priority(Seraph_Strand* strand, Seraph_Priority priority) {
    if (strand == NULL || priority >= SERAPH_PRIORITY_MAX) return;

    scheduler_lock();

    Seraph_Priority old_priority = strand->priority;

    /* If strand is in ready queue, move it */
    if (strand->state == SERAPH_STRAND_READY && old_priority != priority) {
        runqueue_remove(&scheduler.ready_queues[old_priority], strand);
        strand->priority = priority;
        runqueue_add(&scheduler.ready_queues[priority], strand);
    } else {
        strand->priority = priority;
    }

    strand->base_priority = priority;

    scheduler_unlock();
}

Seraph_Priority seraph_scheduler_get_priority(const Seraph_Strand* strand) {
    if (strand == NULL) return SERAPH_PRIORITY_NORMAL;
    return strand->priority;
}

void seraph_scheduler_priority_boost(Seraph_Strand* strand, Seraph_Priority min_priority) {
    if (strand == NULL) return;

    if (strand->priority < min_priority) {
        seraph_scheduler_set_priority(strand, min_priority);
    }
}

void seraph_scheduler_priority_restore(Seraph_Strand* strand) {
    if (strand == NULL) return;

    if (strand->priority != strand->base_priority) {
        seraph_scheduler_set_priority(strand, strand->base_priority);
    }
}

/*============================================================================
 * Time Slice Management
 *============================================================================*/

uint32_t seraph_scheduler_remaining_quantum(void) {
    return scheduler.quantum_remaining;
}

void seraph_scheduler_set_preemption_rate(uint32_t hz) {
    if (hz == 0) return;
    scheduler.preemption_hz = hz;

    if (scheduler.running) {
        seraph_apic_timer_start_hz(hz);
    }
}

uint32_t seraph_scheduler_get_preemption_rate(void) {
    return scheduler.preemption_hz;
}

/*============================================================================
 * Statistics
 *============================================================================*/

const Seraph_Scheduler_Stats* seraph_scheduler_stats(void) {
    return &scheduler.stats;
}

void seraph_scheduler_stats_reset(void) {
    scheduler_lock();
    memset(&scheduler.stats, 0, sizeof(scheduler.stats));
    scheduler_unlock();
}

/*============================================================================
 * CPU Affinity
 *============================================================================*/

void seraph_scheduler_set_affinity(Seraph_Strand* strand, uint64_t cpu_mask) {
    if (strand != NULL) {
        strand->cpu_affinity = cpu_mask;
    }
}

uint64_t seraph_scheduler_get_affinity(const Seraph_Strand* strand) {
    if (strand == NULL) return ~0ULL;  /* All CPUs */
    return strand->cpu_affinity;
}

bool seraph_scheduler_migrate(Seraph_Strand* strand, uint32_t cpu) {
    if (strand == NULL) return false;

    /* Check affinity allows this CPU */
    if (!(strand->cpu_affinity & (1ULL << cpu))) {
        return false;
    }

    /* In SMP implementation, would move strand to target CPU's queue */
    (void)cpu;
    return true;
}

/*============================================================================
 * IPC Integration
 *============================================================================*/

void seraph_scheduler_on_ipc_lend(Seraph_Strand* lender, Seraph_Strand* receiver) {
    if (lender == NULL || receiver == NULL) return;

    /* Priority inheritance: boost receiver to lender's priority */
    if (receiver->priority < lender->priority) {
        seraph_scheduler_priority_boost(receiver, lender->priority);
    }
}

void seraph_scheduler_on_ipc_return(Seraph_Strand* lender, Seraph_Strand* receiver) {
    if (lender == NULL || receiver == NULL) return;

    /* Restore receiver's original priority */
    seraph_scheduler_priority_restore(receiver);

    /* Lender may now continue */
    if (lender->state == SERAPH_STRAND_BLOCKED) {
        seraph_scheduler_wake(lender);
    }
}

/*============================================================================
 * Debug
 *============================================================================*/

size_t seraph_scheduler_ready_count(void) {
    return scheduler.stats.ready_count;
}

size_t seraph_scheduler_blocked_count(void) {
    return scheduler.blocked_count;
}

void seraph_scheduler_dump(void) {
    /* Would print scheduler state to console */
}

/*============================================================================
 * MC5+: Galactic Predictive Scheduling Integration
 *============================================================================*/

/**
 * @brief Enable or disable Galactic predictive scheduling globally
 *
 * @param enable true to enable, false to disable
 */
void seraph_scheduler_set_galactic_enabled(bool enable) {
    scheduler.galactic_enabled = enable;
}

/**
 * @brief Check if Galactic scheduling is enabled
 *
 * @return true if enabled
 */
bool seraph_scheduler_is_galactic_enabled(void) {
    return scheduler.galactic_enabled;
}

/**
 * @brief Get Galactic scheduling statistics
 *
 * @param out_adjustments Total priority adjustments
 * @param out_boosts      Total priority boosts
 * @param out_demotions   Total priority demotions
 */
void seraph_scheduler_galactic_stats(
    uint64_t* out_adjustments,
    uint64_t* out_boosts,
    uint64_t* out_demotions)
{
    seraph_galactic_sched_global_stats(out_adjustments, out_boosts, out_demotions);
}

/**
 * @brief Get predicted execution time for a strand
 *
 * @param strand  The strand to query
 * @param horizon Prediction horizon in ticks
 * @return Predicted execution time, or -1.0 if unavailable
 */
double seraph_scheduler_predict_exec(const Seraph_Strand* strand, uint32_t horizon) {
    if (strand == NULL || strand->galactic_stats == NULL) {
        return -1.0;
    }

    Seraph_Q128 predicted = seraph_galactic_sched_predict_exec(
        strand->galactic_stats, horizon);

    if (seraph_q128_is_void(predicted)) {
        return -1.0;
    }

    return seraph_q128_to_double(predicted);
}

/**
 * @brief Get execution time trend for a strand
 *
 * @param strand The strand to query
 * @return Trend (positive = growing, negative = shrinking), or 0.0 if unavailable
 */
double seraph_scheduler_exec_trend(const Seraph_Strand* strand) {
    if (strand == NULL || strand->galactic_stats == NULL) {
        return 0.0;
    }

    Seraph_Q128 tangent = seraph_galactic_sched_exec_trend(strand->galactic_stats);

    if (seraph_q128_is_void(tangent)) {
        return 0.0;
    }

    return seraph_q128_to_double(tangent);
}

/**
 * @brief Get prediction accuracy for a strand
 *
 * @param strand The strand to query
 * @return Accuracy ratio (0.0 to 1.0), or -1.0 if unavailable
 */
double seraph_scheduler_prediction_accuracy(const Seraph_Strand* strand) {
    if (strand == NULL || strand->galactic_stats == NULL) {
        return -1.0;
    }

    Seraph_Q128 accuracy = seraph_galactic_sched_accuracy(strand->galactic_stats);

    if (seraph_q128_is_void(accuracy)) {
        return -1.0;
    }

    return seraph_q128_to_double(accuracy);
}

/**
 * @brief Check if a strand's scheduling has converged
 *
 * @param strand The strand to query
 * @return true if gradient descent has converged
 */
bool seraph_scheduler_is_converged(const Seraph_Strand* strand) {
    if (strand == NULL || strand->galactic_stats == NULL) {
        return false;
    }

    return seraph_galactic_sched_is_converged(strand->galactic_stats);
}

/**
 * @brief Manually boost strand priority based on Galactic prediction
 *
 * Useful for latency-sensitive operations that need immediate priority
 * adjustment without waiting for the gradient descent to converge.
 *
 * @param strand  The strand to boost
 * @param urgency Urgency level (1-3, higher = more boost)
 */
void seraph_scheduler_galactic_boost(Seraph_Strand* strand, uint32_t urgency) {
    if (strand == NULL) return;
    if (urgency == 0 || urgency > 3) return;

    scheduler_lock();

    Seraph_Priority old_priority = strand->priority;
    Seraph_Priority new_priority = old_priority + urgency;

    if (new_priority > SERAPH_PRIORITY_REALTIME) {
        new_priority = SERAPH_PRIORITY_REALTIME;
    }

    if (new_priority != old_priority && strand->state == SERAPH_STRAND_READY) {
        runqueue_remove(&scheduler.ready_queues[old_priority], strand);
        strand->priority = new_priority;
        runqueue_add(&scheduler.ready_queues[new_priority], strand);
    } else {
        strand->priority = new_priority;
    }

    /* Mark as force-boosted in Galactic stats */
    if (strand->galactic_stats != NULL) {
        strand->galactic_stats->flags |= SERAPH_GALACTIC_SCHED_FORCE_BOOST;
        strand->galactic_stats->ticks_since_adjustment = 0;
    }

    scheduler_unlock();
}

/**
 * @brief Get current global tick counter
 *
 * @return Global tick count since scheduler start
 */
uint64_t seraph_scheduler_get_global_tick(void) {
    return scheduler.global_tick;
}
