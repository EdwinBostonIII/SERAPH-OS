/**
 * @file scheduler.h
 * @brief Preemptive Scheduler API
 *
 * MC13/27: The Pulse - Preemptive Scheduler
 *
 * Implements priority-based preemptive scheduling for Strands (threads).
 * The scheduler supports:
 *
 * - Priority-based scheduling with multiple priority levels
 * - Preemptive multitasking via APIC timer interrupts
 * - Cooperative yields for voluntarily releasing the CPU
 * - Blocking/waking for synchronization primitives
 * - Idle strand for when no work is available
 * - SMP support via per-CPU run queues
 *
 * Scheduling Policy:
 * - Higher priority Strands always run before lower priority
 * - Equal priority Strands are scheduled round-robin
 * - Time slices are based on priority (higher = longer)
 * - Blocked Strands don't consume CPU time
 */

#ifndef SERAPH_SCHEDULER_H
#define SERAPH_SCHEDULER_H

#include "seraph/interrupts.h"
#include "seraph/strand.h"
#include "seraph/context.h"
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Priority Levels
 *============================================================================*/

/**
 * @brief Strand priority levels
 *
 * Higher values = higher priority.
 * Priority 0 is reserved for the idle strand.
 */
typedef enum {
    SERAPH_PRIORITY_IDLE        = 0,    /**< Idle strand only */
    SERAPH_PRIORITY_BACKGROUND  = 1,    /**< Background tasks */
    SERAPH_PRIORITY_LOW         = 2,    /**< Low priority */
    SERAPH_PRIORITY_NORMAL      = 3,    /**< Normal priority (default) */
    SERAPH_PRIORITY_HIGH        = 4,    /**< High priority */
    SERAPH_PRIORITY_REALTIME    = 5,    /**< Real-time priority */
    SERAPH_PRIORITY_CRITICAL    = 6,    /**< System-critical */
    SERAPH_PRIORITY_MAX         = 7,    /**< Maximum priority levels */
} Seraph_Priority;

/* Default time quantum in timer ticks per priority level */
#define SERAPH_QUANTUM_IDLE         1
#define SERAPH_QUANTUM_BACKGROUND   2
#define SERAPH_QUANTUM_LOW          4
#define SERAPH_QUANTUM_NORMAL       8
#define SERAPH_QUANTUM_HIGH         16
#define SERAPH_QUANTUM_REALTIME     32
#define SERAPH_QUANTUM_CRITICAL     64

/*============================================================================
 * Scheduler Statistics
 *============================================================================*/

/**
 * @brief Scheduler statistics
 */
typedef struct {
    uint64_t total_switches;        /**< Total context switches */
    uint64_t preemptions;           /**< Preemptive switches */
    uint64_t yields;                /**< Voluntary yields */
    uint64_t idle_time;             /**< Time spent in idle */
    uint64_t strands_created;       /**< Total strands created */
    uint64_t strands_destroyed;     /**< Total strands destroyed */
    uint64_t ready_count;           /**< Currently ready strands */
    uint64_t blocked_count;         /**< Currently blocked strands */
} Seraph_Scheduler_Stats;

/*============================================================================
 * Scheduler Initialization
 *============================================================================*/

/**
 * @brief Initialize the scheduler
 *
 * Must be called once at boot before any strands are created.
 * Sets up the idle strand and run queues.
 */
void seraph_scheduler_init(void);

/**
 * @brief Start the scheduler
 *
 * Enables the APIC timer and begins preemptive scheduling.
 * This function may not return on the current stack.
 */
void seraph_scheduler_start(void);

/**
 * @brief Stop the scheduler
 *
 * Disables preemption. Used for shutdown.
 */
void seraph_scheduler_stop(void);

/**
 * @brief Check if scheduler is running
 *
 * @return true if scheduler has been started
 */
bool seraph_scheduler_running(void);

/*============================================================================
 * Strand Management
 *============================================================================*/

/**
 * @brief Add a strand to the ready queue
 *
 * Makes a strand eligible for scheduling.
 *
 * @param strand The strand to add
 */
void seraph_scheduler_ready(Seraph_Strand* strand);

/**
 * @brief Remove a strand from scheduling
 *
 * Called when a strand terminates.
 *
 * @param strand The strand to remove
 */
void seraph_scheduler_remove(Seraph_Strand* strand);

/**
 * @brief Get the currently running strand
 *
 * @return Pointer to current strand, or NULL if none
 */
Seraph_Strand* seraph_scheduler_current(void);

/**
 * @brief Get the idle strand
 *
 * @return Pointer to the idle strand
 */
Seraph_Strand* seraph_scheduler_idle(void);

/*============================================================================
 * Scheduling Operations
 *============================================================================*/

/**
 * @brief Timer tick handler
 *
 * Called from the APIC timer interrupt handler.
 * Handles preemption and time slice management.
 *
 * @param frame Interrupt frame from CPU
 */
void seraph_scheduler_tick(Seraph_InterruptFrame* frame);

/**
 * @brief Yield the current time slice
 *
 * Voluntarily gives up the CPU to allow other strands to run.
 * The current strand remains in the ready queue.
 */
void seraph_scheduler_yield(void);

/**
 * @brief Block the current strand
 *
 * Removes the current strand from the ready queue.
 * The strand must be woken by seraph_scheduler_wake().
 */
void seraph_scheduler_block(void);

/**
 * @brief Wake a blocked strand
 *
 * Adds a blocked strand back to the ready queue.
 *
 * @param strand The strand to wake
 */
void seraph_scheduler_wake(Seraph_Strand* strand);

/**
 * @brief Force a reschedule
 *
 * Triggers an immediate reschedule without waiting for
 * the next timer tick.
 */
void seraph_scheduler_reschedule(void);

/*============================================================================
 * Priority Management
 *============================================================================*/

/**
 * @brief Set strand priority
 *
 * @param strand The strand to modify
 * @param priority New priority level
 */
void seraph_scheduler_set_priority(Seraph_Strand* strand, Seraph_Priority priority);

/**
 * @brief Get strand priority
 *
 * @param strand The strand to query
 * @return Current priority level
 */
Seraph_Priority seraph_scheduler_get_priority(const Seraph_Strand* strand);

/**
 * @brief Boost strand priority temporarily
 *
 * Used for priority inheritance in mutexes.
 *
 * @param strand The strand to boost
 * @param min_priority Minimum priority to set
 */
void seraph_scheduler_priority_boost(Seraph_Strand* strand, Seraph_Priority min_priority);

/**
 * @brief Restore strand's base priority
 *
 * @param strand The strand to restore
 */
void seraph_scheduler_priority_restore(Seraph_Strand* strand);

/*============================================================================
 * Time Slice Management
 *============================================================================*/

/**
 * @brief Get remaining time slice
 *
 * @return Remaining ticks in current time slice
 */
uint32_t seraph_scheduler_remaining_quantum(void);

/**
 * @brief Set preemption rate
 *
 * @param hz Timer interrupts per second
 */
void seraph_scheduler_set_preemption_rate(uint32_t hz);

/**
 * @brief Get preemption rate
 *
 * @return Timer interrupts per second
 */
uint32_t seraph_scheduler_get_preemption_rate(void);

/*============================================================================
 * Statistics
 *============================================================================*/

/**
 * @brief Get scheduler statistics
 *
 * @return Pointer to statistics structure
 */
const Seraph_Scheduler_Stats* seraph_scheduler_stats(void);

/**
 * @brief Reset statistics counters
 */
void seraph_scheduler_stats_reset(void);

/*============================================================================
 * CPU Affinity (SMP)
 *============================================================================*/

/**
 * @brief Set strand CPU affinity
 *
 * @param strand The strand to modify
 * @param cpu_mask Bitmask of allowed CPUs
 */
void seraph_scheduler_set_affinity(Seraph_Strand* strand, uint64_t cpu_mask);

/**
 * @brief Get strand CPU affinity
 *
 * @param strand The strand to query
 * @return CPU affinity mask
 */
uint64_t seraph_scheduler_get_affinity(const Seraph_Strand* strand);

/**
 * @brief Migrate strand to specific CPU
 *
 * @param strand The strand to migrate
 * @param cpu Target CPU ID
 * @return true if migration succeeded
 */
bool seraph_scheduler_migrate(Seraph_Strand* strand, uint32_t cpu);

/*============================================================================
 * IPC Integration
 *============================================================================*/

/**
 * @brief Handle IPC lend operation
 *
 * When a strand lends a capability, it may need to
 * temporarily boost the receiver's priority.
 *
 * @param lender The lending strand
 * @param receiver The receiving strand
 */
void seraph_scheduler_on_ipc_lend(Seraph_Strand* lender, Seraph_Strand* receiver);

/**
 * @brief Handle IPC return operation
 *
 * When a borrowed capability is returned.
 *
 * @param lender The lending strand
 * @param receiver The receiving strand
 */
void seraph_scheduler_on_ipc_return(Seraph_Strand* lender, Seraph_Strand* receiver);

/*============================================================================
 * Debug / Introspection
 *============================================================================*/

/**
 * @brief Print scheduler state to console
 */
void seraph_scheduler_dump(void);

/**
 * @brief Get number of ready strands
 */
size_t seraph_scheduler_ready_count(void);

/**
 * @brief Get number of blocked strands
 */
size_t seraph_scheduler_blocked_count(void);

/*============================================================================
 * MC5+: Galactic Predictive Scheduling
 *============================================================================*/

/**
 * @brief Enable or disable Galactic predictive scheduling globally
 *
 * When enabled, the scheduler uses Galactic numbers (hyper-dual numbers
 * for automatic differentiation) to track execution time trends and
 * predict future CPU needs. Predictions are used to proactively adjust
 * strand priorities via gradient descent optimization.
 *
 * @param enable true to enable, false to disable
 *
 * Performance impact when enabled:
 *   - Per-tick overhead: ~100 cycles
 *   - Per-strand memory: 128 bytes (Galactic stats structure)
 *
 * Benefits:
 *   - Predictive priority adjustment (anticipate CPU needs)
 *   - Self-tuning via gradient descent
 *   - Reduced response time variance
 */
void seraph_scheduler_set_galactic_enabled(bool enable);

/**
 * @brief Check if Galactic scheduling is enabled
 *
 * @return true if enabled
 */
bool seraph_scheduler_is_galactic_enabled(void);

/**
 * @brief Get Galactic scheduling statistics
 *
 * @param out_adjustments Total priority adjustments made
 * @param out_boosts      Total priority boosts (strand needed more CPU)
 * @param out_demotions   Total priority demotions (strand needed less CPU)
 */
void seraph_scheduler_galactic_stats(
    uint64_t* out_adjustments,
    uint64_t* out_boosts,
    uint64_t* out_demotions
);

/**
 * @brief Get predicted execution time for a strand
 *
 * Uses Galactic prediction: predicted = primal + tangent * horizon
 *
 * @param strand  The strand to query
 * @param horizon Prediction horizon in ticks
 * @return Predicted execution time (ticks), or -1.0 if unavailable
 */
double seraph_scheduler_predict_exec(const Seraph_Strand* strand, uint32_t horizon);

/**
 * @brief Get execution time trend for a strand
 *
 * The tangent (derivative) of execution time indicates whether the
 * strand is consuming more or less CPU over time.
 *
 * @param strand The strand to query
 * @return Trend value (positive = growing, negative = shrinking)
 */
double seraph_scheduler_exec_trend(const Seraph_Strand* strand);

/**
 * @brief Get prediction accuracy for a strand
 *
 * @param strand The strand to query
 * @return Accuracy ratio (0.0 to 1.0), or -1.0 if unavailable
 */
double seraph_scheduler_prediction_accuracy(const Seraph_Strand* strand);

/**
 * @brief Check if a strand's scheduling has converged
 *
 * A strand is converged when:
 * - Execution time trend is near zero (stable)
 * - Prediction accuracy is high (>90%)
 * - Priority adjustments have settled
 *
 * @param strand The strand to query
 * @return true if gradient descent has converged
 */
bool seraph_scheduler_is_converged(const Seraph_Strand* strand);

/**
 * @brief Manually boost strand priority based on Galactic prediction
 *
 * Useful for latency-sensitive operations that need immediate priority
 * adjustment without waiting for the gradient descent to converge.
 *
 * @param strand  The strand to boost
 * @param urgency Urgency level (1-3, higher = more boost)
 */
void seraph_scheduler_galactic_boost(Seraph_Strand* strand, uint32_t urgency);

/**
 * @brief Get current global tick counter
 *
 * @return Global tick count since scheduler start
 */
uint64_t seraph_scheduler_get_global_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SCHEDULER_H */
