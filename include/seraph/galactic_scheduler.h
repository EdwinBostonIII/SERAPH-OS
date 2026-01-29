/**
 * @file galactic_scheduler.h
 * @brief MC5+/13: Galactic Predictive Scheduling
 *
 * Implements predictive scheduling using Galactic numbers (hyper-dual numbers
 * for automatic differentiation). The scheduler tracks execution time as
 * Galactic values where:
 *
 *   primal  = current measured value
 *   tangent = rate of change (derivative over time)
 *
 * This enables the scheduler to PREDICT future execution behavior and
 * proactively adjust priority before performance issues occur.
 *
 * KEY INNOVATIONS:
 *
 *   1. PREDICTIVE PRIORITY: Uses execution time derivatives to anticipate
 *      CPU needs before they become critical.
 *
 *   2. GRADIENT DESCENT SCHEDULING: Priority adjustments follow the gradient
 *      toward optimal resource allocation.
 *
 *   3. SELF-TUNING: Adaptive learning rate based on prediction accuracy
 *      feedback loop.
 *
 *   4. VOID-SAFE: All operations propagate VOID for error handling.
 *
 * MATHEMATICAL MODEL:
 *
 *   Let T(t) be execution time at scheduler tick t.
 *   We track this as Galactic: G = (T, dT/dt)
 *
 *   Prediction at lookahead L:
 *     T_predicted(t+L) = T(t) + (dT/dt) * L
 *
 *   Priority gradient:
 *     delta_priority = -eta * (T_predicted - T_target) * sign(dT/dt)
 *
 *   Where eta is the adaptive learning rate.
 *
 * PERFORMANCE:
 *   - Prediction: ~50 cycles (single Galactic operation)
 *   - Priority adjustment: ~100 cycles (includes gradient calculation)
 *   - Memory per strand: 128 bytes (Galactic stats structure)
 */

#ifndef SERAPH_GALACTIC_SCHEDULER_H
#define SERAPH_GALACTIC_SCHEDULER_H

#include "seraph/galactic.h"
#include "seraph/q128.h"
#include "seraph/vbit.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration Constants
 *============================================================================*/

/**
 * @brief Default exponential moving average alpha (0.1 = 10% weight to new)
 *
 * Controls how quickly the tangent (derivative) responds to changes.
 * Lower values = smoother but slower adaptation
 * Higher values = faster response but more noise
 */
#define SERAPH_GALACTIC_SCHED_EMA_ALPHA    0.1

/**
 * @brief Default prediction horizon (ticks into the future)
 *
 * How far ahead to predict when making scheduling decisions.
 * Typical value: 10-100 ticks (10-100ms at 1kHz)
 */
#define SERAPH_GALACTIC_SCHED_HORIZON      50

/**
 * @brief Default target execution time (in ticks)
 *
 * The "ideal" quantum usage. Strands consistently above this
 * may be boosted; consistently below may be demoted.
 */
#define SERAPH_GALACTIC_SCHED_TARGET       8

/**
 * @brief Minimum learning rate for gradient descent
 */
#define SERAPH_GALACTIC_SCHED_LR_MIN       0.001

/**
 * @brief Maximum learning rate for gradient descent
 */
#define SERAPH_GALACTIC_SCHED_LR_MAX       0.5

/**
 * @brief Default initial learning rate
 */
#define SERAPH_GALACTIC_SCHED_LR_DEFAULT   0.05

/**
 * @brief Momentum coefficient for gradient descent (0.9 typical)
 */
#define SERAPH_GALACTIC_SCHED_MOMENTUM     0.9

/**
 * @brief Accuracy threshold for learning rate adaptation (90%)
 */
#define SERAPH_GALACTIC_SCHED_ACCURACY_HIGH  0.9

/**
 * @brief Low accuracy threshold (60%)
 */
#define SERAPH_GALACTIC_SCHED_ACCURACY_LOW   0.6

/**
 * @brief Prediction error tolerance (within 20% is "accurate")
 */
#define SERAPH_GALACTIC_SCHED_TOLERANCE    0.2

/**
 * @brief Tangent magnitude threshold for priority adjustment
 *
 * Only adjust priority if |tangent| exceeds this threshold.
 * Prevents jitter from tiny fluctuations.
 */
#define SERAPH_GALACTIC_SCHED_TANGENT_THRESHOLD  0.05

/**
 * @brief Maximum priority delta per adjustment cycle
 */
#define SERAPH_GALACTIC_SCHED_MAX_DELTA    2

/**
 * @brief Minimum adjustments between priority changes
 *
 * Prevents rapid oscillation by requiring N ticks between adjustments.
 */
#define SERAPH_GALACTIC_SCHED_COOLDOWN     100

/*============================================================================
 * Galactic Execution Statistics
 *============================================================================*/

/**
 * @brief Per-strand Galactic execution tracking
 *
 * Tracks execution behavior using Galactic numbers for prediction.
 * All times are in scheduler ticks.
 */
typedef struct Seraph_Galactic_Exec_Stats {
    /*------------------------------------------------------------------------
     * Galactic Metrics (value + derivative)
     *------------------------------------------------------------------------*/

    /**
     * @brief Execution time as Galactic number
     *
     * primal  = current execution time (ticks consumed in quantum)
     * tangent = rate of change of execution time over recent history
     *
     * Positive tangent: strand is using MORE CPU over time
     * Negative tangent: strand is using LESS CPU over time
     */
    Seraph_Galactic exec_time;

    /**
     * @brief CPU usage percentage as Galactic number
     *
     * primal  = current CPU usage (0.0 to 1.0)
     * tangent = rate of change of CPU usage
     *
     * Computed as: actual_ticks / allocated_quantum
     */
    Seraph_Galactic cpu_usage;

    /**
     * @brief Wait time as Galactic number
     *
     * primal  = average time spent in BLOCKED/WAITING state
     * tangent = rate of change of wait time
     *
     * Used to detect strands that are becoming I/O bound.
     */
    Seraph_Galactic wait_time;

    /**
     * @brief Response time as Galactic number
     *
     * primal  = time from READY to RUNNING
     * tangent = rate of change of response time
     *
     * Used to detect priority inversion issues.
     */
    Seraph_Galactic response_time;

    /*------------------------------------------------------------------------
     * Prediction Feedback
     *------------------------------------------------------------------------*/

    /**
     * @brief Last predicted execution time
     *
     * Stored to compare against actual for accuracy measurement.
     */
    Seraph_Q128 last_predicted;

    /**
     * @brief Exponential moving average of prediction error
     *
     * error = |predicted - actual| / actual
     * Smaller values = better predictions
     */
    Seraph_Q128 prediction_error;

    /**
     * @brief Total number of predictions made
     */
    uint64_t prediction_count;

    /**
     * @brief Number of predictions within tolerance
     */
    uint64_t accurate_predictions;

    /*------------------------------------------------------------------------
     * Gradient Descent State
     *------------------------------------------------------------------------*/

    /**
     * @brief Adaptive learning rate
     *
     * Adjusted based on prediction accuracy:
     * - High accuracy: reduce (fine tuning mode)
     * - Low accuracy: increase (catch up mode)
     */
    Seraph_Q128 learning_rate;

    /**
     * @brief Momentum term for smoother gradient descent
     *
     * Prevents oscillation by maintaining direction.
     */
    Seraph_Q128 momentum_velocity;

    /**
     * @brief Accumulated priority adjustment (sub-integer)
     *
     * Stored as Q128 to allow fractional accumulation.
     * Priority only changes when this exceeds +/- 1.0
     */
    Seraph_Q128 priority_delta_accum;

    /**
     * @brief Ticks since last priority adjustment
     *
     * Enforces cooldown between priority changes.
     */
    uint32_t ticks_since_adjustment;

    /**
     * @brief Scheduler tick at which stats were last updated
     */
    uint64_t last_update_tick;

    /**
     * @brief Flags for Galactic scheduler state
     */
    uint32_t flags;

    /**
     * @brief Reserved for alignment and future use
     */
    uint32_t _reserved;

} Seraph_Galactic_Exec_Stats;

/*============================================================================
 * Galactic Scheduler Flags
 *============================================================================*/

/** Galactic tracking is enabled for this strand */
#define SERAPH_GALACTIC_SCHED_ENABLED       (1 << 0)

/** Priority auto-adjustment is enabled */
#define SERAPH_GALACTIC_SCHED_AUTOADJUST    (1 << 1)

/** Learning rate adaptation is enabled */
#define SERAPH_GALACTIC_SCHED_ADAPTIVE_LR   (1 << 2)

/** Strand is in warmup phase (collecting initial data) */
#define SERAPH_GALACTIC_SCHED_WARMUP        (1 << 3)

/** Debug logging enabled */
#define SERAPH_GALACTIC_SCHED_DEBUG         (1 << 4)

/** Force priority boost (override gradient) */
#define SERAPH_GALACTIC_SCHED_FORCE_BOOST   (1 << 5)

/** Gradient descent is converged */
#define SERAPH_GALACTIC_SCHED_CONVERGED     (1 << 6)

/*============================================================================
 * Initialization and Lifecycle
 *============================================================================*/

/**
 * @brief Initialize Galactic execution statistics
 *
 * @param stats   Stats structure to initialize
 * @param flags   Initial flags (SERAPH_GALACTIC_SCHED_*)
 *
 * Initializes all Galactic values to zero with zero derivatives.
 * Sets learning rate to default and enters warmup phase.
 */
void seraph_galactic_sched_init(Seraph_Galactic_Exec_Stats* stats, uint32_t flags);

/**
 * @brief Reset Galactic statistics to initial state
 *
 * @param stats   Stats structure to reset
 *
 * Preserves flags but resets all metrics and accumulators.
 */
void seraph_galactic_sched_reset(Seraph_Galactic_Exec_Stats* stats);

/**
 * @brief Check if Galactic scheduling is enabled
 *
 * @param stats   Stats structure to check
 * @return true if enabled, false otherwise
 */
static inline bool seraph_galactic_sched_is_enabled(
    const Seraph_Galactic_Exec_Stats* stats) {
    return stats != NULL && (stats->flags & SERAPH_GALACTIC_SCHED_ENABLED);
}

/*============================================================================
 * Metric Updates
 *============================================================================*/

/**
 * @brief Update execution time after quantum completion
 *
 * @param stats         Stats structure to update
 * @param actual_ticks  Actual ticks consumed in the quantum
 * @param quantum_size  Total ticks in the quantum
 * @param current_tick  Current scheduler tick (for timestamps)
 *
 * Updates:
 * - exec_time Galactic (value and derivative via EMA)
 * - cpu_usage Galactic
 * - Prediction error feedback
 *
 * This is the primary update function, called after each context switch.
 */
void seraph_galactic_sched_update_exec(
    Seraph_Galactic_Exec_Stats* stats,
    uint32_t actual_ticks,
    uint32_t quantum_size,
    uint64_t current_tick
);

/**
 * @brief Update wait time statistics
 *
 * @param stats        Stats structure to update
 * @param wait_ticks   Ticks spent in BLOCKED/WAITING state
 * @param current_tick Current scheduler tick
 *
 * Called when a strand transitions from BLOCKED/WAITING to READY.
 */
void seraph_galactic_sched_update_wait(
    Seraph_Galactic_Exec_Stats* stats,
    uint64_t wait_ticks,
    uint64_t current_tick
);

/**
 * @brief Update response time statistics
 *
 * @param stats          Stats structure to update
 * @param response_ticks Ticks from READY to RUNNING
 * @param current_tick   Current scheduler tick
 *
 * Called when a strand transitions from READY to RUNNING.
 */
void seraph_galactic_sched_update_response(
    Seraph_Galactic_Exec_Stats* stats,
    uint64_t response_ticks,
    uint64_t current_tick
);

/*============================================================================
 * Prediction Functions
 *============================================================================*/

/**
 * @brief Predict future execution time
 *
 * @param stats     Stats structure
 * @param horizon   Ticks into the future to predict
 * @return Predicted execution time (Q128), or VOID on error
 *
 * Uses: predicted = primal + tangent * horizon
 *
 * This is the core prediction function. The result indicates how much
 * CPU time the strand is expected to need `horizon` ticks from now.
 */
Seraph_Q128 seraph_galactic_sched_predict_exec(
    const Seraph_Galactic_Exec_Stats* stats,
    uint32_t horizon
);

/**
 * @brief Predict future CPU usage percentage
 *
 * @param stats     Stats structure
 * @param horizon   Ticks into the future
 * @return Predicted CPU usage (0.0 to 1.0), or VOID on error
 */
Seraph_Q128 seraph_galactic_sched_predict_cpu(
    const Seraph_Galactic_Exec_Stats* stats,
    uint32_t horizon
);

/**
 * @brief Get current execution time trend
 *
 * @param stats   Stats structure
 * @return Tangent (derivative) of execution time
 *
 * Positive: strand is consuming more CPU over time
 * Negative: strand is consuming less CPU over time
 * Near zero: stable execution pattern
 */
static inline Seraph_Q128 seraph_galactic_sched_exec_trend(
    const Seraph_Galactic_Exec_Stats* stats) {
    if (stats == NULL) return SERAPH_Q128_VOID;
    return seraph_galactic_tangent(stats->exec_time);
}

/**
 * @brief Check if execution time is increasing
 *
 * @param stats   Stats structure
 * @return VBIT_TRUE if tangent > threshold, VBIT_FALSE otherwise
 */
Seraph_Vbit seraph_galactic_sched_is_growing(
    const Seraph_Galactic_Exec_Stats* stats
);

/**
 * @brief Check if execution time is decreasing
 *
 * @param stats   Stats structure
 * @return VBIT_TRUE if tangent < -threshold, VBIT_FALSE otherwise
 */
Seraph_Vbit seraph_galactic_sched_is_shrinking(
    const Seraph_Galactic_Exec_Stats* stats
);

/*============================================================================
 * Priority Gradient Descent
 *============================================================================*/

/**
 * @brief Calculate priority adjustment using gradient descent
 *
 * @param stats       Stats structure
 * @param target_exec Target execution time (ticks)
 * @return Priority delta (-MAX_DELTA to +MAX_DELTA), 0 if no change needed
 *
 * Implements gradient descent toward optimal priority:
 *
 *   error = predicted_exec - target_exec
 *   gradient = tangent (rate of change of exec time)
 *   delta = -learning_rate * error * sign(gradient)
 *
 * With momentum for stability:
 *   velocity = momentum * prev_velocity + (1-momentum) * delta
 *   adjustment = round(velocity) when |velocity| >= 1.0
 *
 * Returns:
 *   +N: boost priority by N levels (strand needs more CPU)
 *   -N: demote priority by N levels (strand needs less CPU)
 *   0:  no change (within tolerance or cooldown)
 */
int32_t seraph_galactic_sched_compute_priority_delta(
    Seraph_Galactic_Exec_Stats* stats,
    uint32_t target_exec
);

/**
 * @brief Apply priority adjustment with cooldown
 *
 * @param stats           Stats structure
 * @param current_priority Current strand priority
 * @param target_exec     Target execution time
 * @param min_priority    Minimum allowed priority
 * @param max_priority    Maximum allowed priority
 * @return New priority (may be same as current if no change)
 *
 * Calls compute_priority_delta and applies bounds/cooldown.
 */
uint32_t seraph_galactic_sched_adjust_priority(
    Seraph_Galactic_Exec_Stats* stats,
    uint32_t current_priority,
    uint32_t target_exec,
    uint32_t min_priority,
    uint32_t max_priority
);

/*============================================================================
 * Learning Rate Adaptation
 *============================================================================*/

/**
 * @brief Get current prediction accuracy (0.0 to 1.0)
 *
 * @param stats   Stats structure
 * @return Accuracy ratio = accurate_predictions / prediction_count
 */
Seraph_Q128 seraph_galactic_sched_accuracy(
    const Seraph_Galactic_Exec_Stats* stats
);

/**
 * @brief Adapt learning rate based on prediction accuracy
 *
 * @param stats   Stats structure
 *
 * Automatically adjusts learning rate:
 * - Accuracy > 90%: decrease by 10% (fine tuning)
 * - Accuracy < 60%: increase by 10% (faster adaptation)
 * - Clamped to [LR_MIN, LR_MAX]
 */
void seraph_galactic_sched_adapt_learning_rate(
    Seraph_Galactic_Exec_Stats* stats
);

/**
 * @brief Set learning rate manually
 *
 * @param stats   Stats structure
 * @param rate    New learning rate (clamped to valid range)
 */
void seraph_galactic_sched_set_learning_rate(
    Seraph_Galactic_Exec_Stats* stats,
    double rate
);

/*============================================================================
 * Convergence Detection
 *============================================================================*/

/**
 * @brief Check if gradient descent has converged
 *
 * @param stats   Stats structure
 * @return true if converged (changes negligible), false otherwise
 *
 * Converged when:
 * - |tangent| < threshold (stable execution)
 * - |priority_delta_accum| < 0.1 (no pending adjustment)
 * - accuracy > 90% (predictions reliable)
 */
bool seraph_galactic_sched_is_converged(
    const Seraph_Galactic_Exec_Stats* stats
);

/**
 * @brief Get convergence score (0.0 = oscillating, 1.0 = fully converged)
 *
 * @param stats   Stats structure
 * @return Convergence score as Q128
 */
Seraph_Q128 seraph_galactic_sched_convergence_score(
    const Seraph_Galactic_Exec_Stats* stats
);

/**
 * @brief Detect if strand is becoming CPU-bound
 *
 * @param stats   Galactic stats
 * @return true if strand shows CPU-bound behavior
 *
 * A strand is CPU-bound if:
 * - High CPU usage (>80%) with positive or stable trend
 * - Low wait time
 */
bool seraph_galactic_sched_is_cpu_bound(
    const Seraph_Galactic_Exec_Stats* stats
);

/**
 * @brief Detect if strand is becoming I/O-bound
 *
 * @param stats   Galactic stats
 * @return true if strand shows I/O-bound behavior
 *
 * A strand is I/O-bound if:
 * - Wait time is high or increasing
 * - CPU usage is low (<30%)
 */
bool seraph_galactic_sched_is_io_bound(
    const Seraph_Galactic_Exec_Stats* stats
);

/*============================================================================
 * Statistics and Debugging
 *============================================================================*/

/**
 * @brief Get prediction statistics
 *
 * @param stats              Stats structure
 * @param out_count          Output: total predictions
 * @param out_accurate       Output: accurate predictions
 * @param out_error          Output: mean prediction error
 */
void seraph_galactic_sched_get_prediction_stats(
    const Seraph_Galactic_Exec_Stats* stats,
    uint64_t* out_count,
    uint64_t* out_accurate,
    Seraph_Q128* out_error
);

/**
 * @brief Format statistics as human-readable string
 *
 * @param stats   Stats structure
 * @param buffer  Output buffer
 * @param size    Buffer size
 * @return Number of characters written
 */
int seraph_galactic_sched_format_stats(
    const Seraph_Galactic_Exec_Stats* stats,
    char* buffer,
    size_t size
);

/*============================================================================
 * Global Scheduler Integration
 *============================================================================*/

/**
 * @brief Process Galactic scheduling for all strands
 *
 * Called by the main scheduler tick handler to update all Galactic
 * metrics and apply priority adjustments.
 *
 * @param current_tick   Current scheduler tick
 *
 * This function iterates through all active strands and:
 * 1. Updates metrics for the strand that just ran
 * 2. Computes predictions for priority adjustment
 * 3. Applies priority changes respecting cooldown
 */
void seraph_galactic_sched_tick(uint64_t current_tick);

/**
 * @brief Initialize global Galactic scheduler state
 *
 * Called once at scheduler startup.
 */
void seraph_galactic_sched_global_init(void);

/**
 * @brief Get global Galactic scheduler statistics
 *
 * @param out_total_adjustments  Output: total priority adjustments made
 * @param out_boosts             Output: total priority boosts
 * @param out_demotions          Output: total priority demotions
 */
void seraph_galactic_sched_global_stats(
    uint64_t* out_total_adjustments,
    uint64_t* out_boosts,
    uint64_t* out_demotions
);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_GALACTIC_SCHEDULER_H */
