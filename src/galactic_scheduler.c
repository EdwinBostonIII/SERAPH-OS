/**
 * @file galactic_scheduler.c
 * @brief MC5+/13: Galactic Predictive Scheduling Implementation
 *
 * Implements predictive scheduling using Galactic numbers (hyper-dual numbers
 * for automatic differentiation). Execution time is tracked as:
 *
 *   G = (primal, tangent) = (value, derivative)
 *
 * Enabling prediction via: predicted = primal + tangent * horizon
 *
 * ALGORITHM OVERVIEW:
 *
 * 1. METRIC UPDATE (on each quantum completion):
 *    - Measure actual execution ticks
 *    - Compare against previous prediction for accuracy feedback
 *    - Update tangent using exponential moving average:
 *      new_tangent = alpha * (actual - last_primal) + (1-alpha) * old_tangent
 *    - Update primal = actual
 *
 * 2. PREDICTION (before scheduling decisions):
 *    - predicted_exec = primal + tangent * horizon
 *    - Used to anticipate future CPU needs
 *
 * 3. GRADIENT DESCENT PRIORITY ADJUSTMENT:
 *    - error = predicted_exec - target_exec
 *    - gradient = tangent (rate of change)
 *    - raw_delta = -learning_rate * error * sign(gradient)
 *    - velocity = momentum * prev_velocity + (1-momentum) * raw_delta
 *    - priority_change = round(accumulated_velocity) when |accum| >= 1.0
 *
 * 4. LEARNING RATE ADAPTATION:
 *    - Track prediction accuracy = accurate_count / total_count
 *    - High accuracy (>90%): reduce learning rate (fine tuning)
 *    - Low accuracy (<60%): increase learning rate (catch up)
 *
 * NIH COMPLIANCE:
 * - No external dependencies beyond SERAPH core
 * - All math using Q128 fixed-point or Galactic
 * - Branchless operations where possible
 * - VOID propagation for error handling
 */

#include "seraph/galactic_scheduler.h"
#include "seraph/galactic.h"
#include "seraph/q128.h"
#include "seraph/vbit.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Internal Constants (as Q128)
 *============================================================================*/

/** EMA alpha as Q128 (0.1) */
static const Seraph_Q128 EMA_ALPHA = { 0, 0x1999999999999999ULL }; /* ~0.1 */

/** 1.0 - EMA alpha as Q128 (0.9) */
static const Seraph_Q128 EMA_ONE_MINUS_ALPHA = { 0, 0xE666666666666666ULL }; /* ~0.9 */

/** Momentum coefficient (0.9) */
static const Seraph_Q128 MOMENTUM = { 0, 0xE666666666666666ULL }; /* ~0.9 */

/** 1.0 - Momentum (0.1) */
static const Seraph_Q128 ONE_MINUS_MOMENTUM = { 0, 0x1999999999999999ULL }; /* ~0.1 */

/** Tangent threshold for adjustments */
static const Seraph_Q128 TANGENT_THRESHOLD = { 0, 0x0CCCCCCCCCCCCCCDULL }; /* ~0.05 */

/** Prediction tolerance (0.2 = 20%) */
static const Seraph_Q128 TOLERANCE = { 0, 0x3333333333333333ULL }; /* ~0.2 */

/** Learning rate bounds */
static const Seraph_Q128 LR_MIN = { 0, 0x004189374BC6A7EFULL }; /* ~0.001 */
static const Seraph_Q128 LR_MAX = { 0, 0x8000000000000000ULL }; /* 0.5 */
static const Seraph_Q128 LR_DEFAULT = { 0, 0x0CCCCCCCCCCCCCCDULL }; /* ~0.05 */

/** Learning rate adjustment factors */
static const Seraph_Q128 LR_SHRINK = { 0, 0xE666666666666666ULL }; /* 0.9 */
static const Seraph_Q128 LR_GROW = { 0, 0x119999999999999AULL }; /* 1.1 (as ~0.1 added) */

/** High/low accuracy thresholds */
static const Seraph_Q128 ACCURACY_HIGH = { 0, 0xE666666666666666ULL }; /* 0.9 */
static const Seraph_Q128 ACCURACY_LOW = { 0, 0x999999999999999AULL }; /* 0.6 */

/*============================================================================
 * Global State
 *============================================================================*/

static struct {
    uint64_t total_adjustments;     /**< Total priority adjustments */
    uint64_t total_boosts;          /**< Priority increases */
    uint64_t total_demotions;       /**< Priority decreases */
    uint64_t total_predictions;     /**< Total predictions made */
    uint64_t accurate_predictions;  /**< Predictions within tolerance */
    bool initialized;               /**< Global init completed */
} galactic_global = {0};

/*============================================================================
 * Internal Helper Functions
 *============================================================================*/

/**
 * @brief Sign function for Q128 (-1, 0, or 1)
 *
 * Returns:
 *   -1 if x < 0
 *    0 if x == 0
 *   +1 if x > 0
 */
static inline int32_t q128_sign(Seraph_Q128 x) {
    if (seraph_q128_is_void(x)) return 0;
    if (seraph_q128_is_zero(x)) return 0;
    if (seraph_q128_is_negative(x)) return -1;
    return 1;
}

/**
 * @brief Absolute value of int32
 */
static inline int32_t abs_i32(int32_t x) {
    return x < 0 ? -x : x;
}

/**
 * @brief Clamp int32 to range
 */
static inline int32_t clamp_i32(int32_t x, int32_t lo, int32_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/**
 * @brief Convert uint32 to Q128
 */
static inline Seraph_Q128 q128_from_u32(uint32_t x) {
    return seraph_q128_from_i64((int64_t)x);
}

/*============================================================================
 * Initialization and Lifecycle
 *============================================================================*/

void seraph_galactic_sched_init(Seraph_Galactic_Exec_Stats* stats, uint32_t flags) {
    if (stats == NULL) return;

    /* Zero initialize entire structure */
    memset(stats, 0, sizeof(Seraph_Galactic_Exec_Stats));

    /* Set initial Galactic values to zero with zero derivatives */
    stats->exec_time = SERAPH_GALACTIC_ZERO;
    stats->cpu_usage = SERAPH_GALACTIC_ZERO;
    stats->wait_time = SERAPH_GALACTIC_ZERO;
    stats->response_time = SERAPH_GALACTIC_ZERO;

    /* Initialize prediction feedback */
    stats->last_predicted = SERAPH_Q128_ZERO;
    stats->prediction_error = SERAPH_Q128_ZERO;
    stats->prediction_count = 0;
    stats->accurate_predictions = 0;

    /* Initialize gradient descent state */
    stats->learning_rate = LR_DEFAULT;
    stats->momentum_velocity = SERAPH_Q128_ZERO;
    stats->priority_delta_accum = SERAPH_Q128_ZERO;
    stats->ticks_since_adjustment = 0;

    /* Set flags with warmup enabled */
    stats->flags = flags | SERAPH_GALACTIC_SCHED_ENABLED | SERAPH_GALACTIC_SCHED_WARMUP;

    stats->last_update_tick = 0;
}

void seraph_galactic_sched_reset(Seraph_Galactic_Exec_Stats* stats) {
    if (stats == NULL) return;

    uint32_t saved_flags = stats->flags;
    seraph_galactic_sched_init(stats, saved_flags);
}

/*============================================================================
 * Metric Updates
 *============================================================================*/

void seraph_galactic_sched_update_exec(
    Seraph_Galactic_Exec_Stats* stats,
    uint32_t actual_ticks,
    uint32_t quantum_size,
    uint64_t current_tick)
{
    if (stats == NULL) return;
    if (!(stats->flags & SERAPH_GALACTIC_SCHED_ENABLED)) return;

    /* Convert measurements to Q128 */
    Seraph_Q128 actual = q128_from_u32(actual_ticks);
    Seraph_Q128 quantum = q128_from_u32(quantum_size);

    /* Current primal (execution time) */
    Seraph_Q128 old_primal = seraph_galactic_primal(stats->exec_time);
    Seraph_Q128 old_tangent = seraph_galactic_tangent(stats->exec_time);

    /*------------------------------------------------------------------------
     * Update prediction error feedback
     *------------------------------------------------------------------------*/
    if (stats->prediction_count > 0 && !seraph_q128_is_void(stats->last_predicted)) {
        /* error = |predicted - actual| / actual (relative error) */
        Seraph_Q128 diff = seraph_q128_sub(stats->last_predicted, actual);
        Seraph_Q128 abs_diff = seraph_q128_abs(diff);
        Seraph_Q128 rel_error = SERAPH_Q128_ZERO;

        if (!seraph_q128_is_zero(actual)) {
            rel_error = seraph_q128_div(abs_diff, actual);
        }

        /* Update EMA of prediction error */
        /* new_error = alpha * rel_error + (1-alpha) * old_error */
        Seraph_Q128 weighted_new = seraph_q128_mul(EMA_ALPHA, rel_error);
        Seraph_Q128 weighted_old = seraph_q128_mul(EMA_ONE_MINUS_ALPHA, stats->prediction_error);
        stats->prediction_error = seraph_q128_add(weighted_new, weighted_old);

        /* Check if prediction was accurate (within tolerance) */
        if (seraph_vbit_is_true(seraph_q128_le(rel_error, TOLERANCE))) {
            stats->accurate_predictions++;
            galactic_global.accurate_predictions++;
        }
    }

    /*------------------------------------------------------------------------
     * Update Galactic exec_time
     *------------------------------------------------------------------------*/
    if (stats->flags & SERAPH_GALACTIC_SCHED_WARMUP) {
        /* During warmup, just set values directly */
        stats->exec_time = seraph_galactic_constant(actual);

        /* Exit warmup after sufficient samples */
        if (stats->prediction_count >= 10) {
            stats->flags &= ~SERAPH_GALACTIC_SCHED_WARMUP;
        }
    } else {
        /* Compute new tangent using EMA */
        /* delta = actual - old_primal (rate of change) */
        Seraph_Q128 delta = seraph_q128_sub(actual, old_primal);

        /* new_tangent = alpha * delta + (1-alpha) * old_tangent */
        Seraph_Q128 weighted_delta = seraph_q128_mul(EMA_ALPHA, delta);
        Seraph_Q128 weighted_tangent = seraph_q128_mul(EMA_ONE_MINUS_ALPHA, old_tangent);
        Seraph_Q128 new_tangent = seraph_q128_add(weighted_delta, weighted_tangent);

        /* Update Galactic: new primal = actual, new tangent = computed */
        stats->exec_time = seraph_galactic_create(actual, new_tangent);
    }

    /*------------------------------------------------------------------------
     * Update CPU usage Galactic
     *------------------------------------------------------------------------*/
    if (!seraph_q128_is_zero(quantum)) {
        Seraph_Q128 usage = seraph_q128_div(actual, quantum);

        Seraph_Q128 old_cpu_primal = seraph_galactic_primal(stats->cpu_usage);
        Seraph_Q128 old_cpu_tangent = seraph_galactic_tangent(stats->cpu_usage);

        Seraph_Q128 cpu_delta = seraph_q128_sub(usage, old_cpu_primal);
        Seraph_Q128 weighted_cpu_delta = seraph_q128_mul(EMA_ALPHA, cpu_delta);
        Seraph_Q128 weighted_cpu_tangent = seraph_q128_mul(EMA_ONE_MINUS_ALPHA, old_cpu_tangent);
        Seraph_Q128 new_cpu_tangent = seraph_q128_add(weighted_cpu_delta, weighted_cpu_tangent);

        stats->cpu_usage = seraph_galactic_create(usage, new_cpu_tangent);
    }

    /*------------------------------------------------------------------------
     * Update state
     *------------------------------------------------------------------------*/
    stats->last_update_tick = current_tick;
    stats->ticks_since_adjustment++;
    stats->prediction_count++;
    galactic_global.total_predictions++;
}

void seraph_galactic_sched_update_wait(
    Seraph_Galactic_Exec_Stats* stats,
    uint64_t wait_ticks,
    uint64_t current_tick)
{
    if (stats == NULL) return;
    if (!(stats->flags & SERAPH_GALACTIC_SCHED_ENABLED)) return;

    Seraph_Q128 wait = seraph_q128_from_i64((int64_t)wait_ticks);

    Seraph_Q128 old_primal = seraph_galactic_primal(stats->wait_time);
    Seraph_Q128 old_tangent = seraph_galactic_tangent(stats->wait_time);

    Seraph_Q128 delta = seraph_q128_sub(wait, old_primal);
    Seraph_Q128 weighted_delta = seraph_q128_mul(EMA_ALPHA, delta);
    Seraph_Q128 weighted_tangent = seraph_q128_mul(EMA_ONE_MINUS_ALPHA, old_tangent);
    Seraph_Q128 new_tangent = seraph_q128_add(weighted_delta, weighted_tangent);

    stats->wait_time = seraph_galactic_create(wait, new_tangent);
    stats->last_update_tick = current_tick;
}

void seraph_galactic_sched_update_response(
    Seraph_Galactic_Exec_Stats* stats,
    uint64_t response_ticks,
    uint64_t current_tick)
{
    if (stats == NULL) return;
    if (!(stats->flags & SERAPH_GALACTIC_SCHED_ENABLED)) return;

    Seraph_Q128 response = seraph_q128_from_i64((int64_t)response_ticks);

    Seraph_Q128 old_primal = seraph_galactic_primal(stats->response_time);
    Seraph_Q128 old_tangent = seraph_galactic_tangent(stats->response_time);

    Seraph_Q128 delta = seraph_q128_sub(response, old_primal);
    Seraph_Q128 weighted_delta = seraph_q128_mul(EMA_ALPHA, delta);
    Seraph_Q128 weighted_tangent = seraph_q128_mul(EMA_ONE_MINUS_ALPHA, old_tangent);
    Seraph_Q128 new_tangent = seraph_q128_add(weighted_delta, weighted_tangent);

    stats->response_time = seraph_galactic_create(response, new_tangent);
    stats->last_update_tick = current_tick;
}

/*============================================================================
 * Prediction Functions
 *============================================================================*/

Seraph_Q128 seraph_galactic_sched_predict_exec(
    const Seraph_Galactic_Exec_Stats* stats,
    uint32_t horizon)
{
    if (stats == NULL) return SERAPH_Q128_VOID;
    if (!(stats->flags & SERAPH_GALACTIC_SCHED_ENABLED)) return SERAPH_Q128_VOID;

    /* predicted = primal + tangent * horizon */
    Seraph_Q128 dt = q128_from_u32(horizon);
    return seraph_galactic_predict(stats->exec_time, dt);
}

Seraph_Q128 seraph_galactic_sched_predict_cpu(
    const Seraph_Galactic_Exec_Stats* stats,
    uint32_t horizon)
{
    if (stats == NULL) return SERAPH_Q128_VOID;
    if (!(stats->flags & SERAPH_GALACTIC_SCHED_ENABLED)) return SERAPH_Q128_VOID;

    Seraph_Q128 dt = q128_from_u32(horizon);
    Seraph_Q128 predicted = seraph_galactic_predict(stats->cpu_usage, dt);

    /* Clamp to [0, 1] range */
    if (seraph_q128_is_negative(predicted)) {
        return SERAPH_Q128_ZERO;
    }
    if (seraph_vbit_is_true(seraph_q128_gt(predicted, SERAPH_Q128_ONE))) {
        return SERAPH_Q128_ONE;
    }
    return predicted;
}

Seraph_Vbit seraph_galactic_sched_is_growing(
    const Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return SERAPH_VBIT_VOID;

    Seraph_Q128 tangent = seraph_galactic_tangent(stats->exec_time);
    return seraph_q128_gt(tangent, TANGENT_THRESHOLD);
}

Seraph_Vbit seraph_galactic_sched_is_shrinking(
    const Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return SERAPH_VBIT_VOID;

    Seraph_Q128 tangent = seraph_galactic_tangent(stats->exec_time);
    Seraph_Q128 neg_threshold = seraph_q128_neg(TANGENT_THRESHOLD);
    return seraph_q128_lt(tangent, neg_threshold);
}

/*============================================================================
 * Priority Gradient Descent
 *============================================================================*/

int32_t seraph_galactic_sched_compute_priority_delta(
    Seraph_Galactic_Exec_Stats* stats,
    uint32_t target_exec)
{
    if (stats == NULL) return 0;
    if (!(stats->flags & SERAPH_GALACTIC_SCHED_AUTOADJUST)) return 0;

    /* Check cooldown */
    if (stats->ticks_since_adjustment < SERAPH_GALACTIC_SCHED_COOLDOWN) {
        return 0;
    }

    /* During warmup, no adjustments */
    if (stats->flags & SERAPH_GALACTIC_SCHED_WARMUP) {
        return 0;
    }

    /*------------------------------------------------------------------------
     * Compute prediction
     *------------------------------------------------------------------------*/
    Seraph_Q128 predicted = seraph_galactic_sched_predict_exec(
        stats, SERAPH_GALACTIC_SCHED_HORIZON);

    if (seraph_q128_is_void(predicted)) return 0;

    /* Store prediction for feedback */
    stats->last_predicted = predicted;

    /*------------------------------------------------------------------------
     * Compute error = predicted - target
     *------------------------------------------------------------------------*/
    Seraph_Q128 target = q128_from_u32(target_exec);
    Seraph_Q128 error = seraph_q128_sub(predicted, target);

    /*------------------------------------------------------------------------
     * Get gradient (tangent = rate of change)
     *------------------------------------------------------------------------*/
    Seraph_Q128 tangent = seraph_galactic_tangent(stats->exec_time);

    /* Check if tangent is significant */
    Seraph_Q128 abs_tangent = seraph_q128_abs(tangent);
    if (seraph_vbit_is_true(seraph_q128_lt(abs_tangent, TANGENT_THRESHOLD))) {
        /* Tangent too small, no adjustment */
        return 0;
    }

    /*------------------------------------------------------------------------
     * Compute raw gradient descent step
     * raw_delta = -learning_rate * error * sign(gradient)
     *
     * Logic:
     * - If error > 0 (predicted > target) and tangent > 0 (growing):
     *   -> Strand needs MORE CPU -> boost priority (negative sign makes positive)
     * - If error > 0 and tangent < 0 (shrinking):
     *   -> Strand will naturally need less soon -> no urgent action
     * - If error < 0 (predicted < target) and tangent < 0 (shrinking):
     *   -> Strand needs less CPU -> demote priority
     * - If error < 0 and tangent > 0 (growing):
     *   -> Strand might need more soon -> wait
     *------------------------------------------------------------------------*/
    int32_t gradient_sign = q128_sign(tangent);
    Seraph_Q128 signed_error = seraph_q128_mul(error, seraph_q128_from_i64(gradient_sign));
    Seraph_Q128 raw_delta = seraph_q128_mul(stats->learning_rate, signed_error);
    raw_delta = seraph_q128_neg(raw_delta);  /* Gradient descent: move opposite to error */

    /*------------------------------------------------------------------------
     * Apply momentum
     * velocity = momentum * prev_velocity + (1-momentum) * raw_delta
     *------------------------------------------------------------------------*/
    Seraph_Q128 momentum_term = seraph_q128_mul(MOMENTUM, stats->momentum_velocity);
    Seraph_Q128 new_term = seraph_q128_mul(ONE_MINUS_MOMENTUM, raw_delta);
    stats->momentum_velocity = seraph_q128_add(momentum_term, new_term);

    /*------------------------------------------------------------------------
     * Accumulate into priority delta
     *------------------------------------------------------------------------*/
    stats->priority_delta_accum = seraph_q128_add(
        stats->priority_delta_accum,
        stats->momentum_velocity
    );

    /*------------------------------------------------------------------------
     * Check if accumulated delta exceeds threshold (|accum| >= 1.0)
     *------------------------------------------------------------------------*/
    double accum = seraph_q128_to_double(stats->priority_delta_accum);

    if (accum >= 1.0) {
        /* Boost priority (higher priority = lower number in some systems,
           but SERAPH uses higher number = higher priority) */
        int32_t delta = (int32_t)accum;
        delta = clamp_i32(delta, 1, SERAPH_GALACTIC_SCHED_MAX_DELTA);

        /* Subtract integer part from accumulator */
        stats->priority_delta_accum = seraph_q128_sub(
            stats->priority_delta_accum,
            seraph_q128_from_i64(delta)
        );

        /* Reset cooldown */
        stats->ticks_since_adjustment = 0;
        galactic_global.total_adjustments++;
        galactic_global.total_boosts++;

        return delta;  /* Positive = boost priority */
    }
    else if (accum <= -1.0) {
        /* Demote priority */
        int32_t delta = (int32_t)accum;
        delta = clamp_i32(delta, -SERAPH_GALACTIC_SCHED_MAX_DELTA, -1);

        /* Add (subtract negative) integer part from accumulator */
        stats->priority_delta_accum = seraph_q128_sub(
            stats->priority_delta_accum,
            seraph_q128_from_i64(delta)
        );

        /* Reset cooldown */
        stats->ticks_since_adjustment = 0;
        galactic_global.total_adjustments++;
        galactic_global.total_demotions++;

        return delta;  /* Negative = demote priority */
    }

    return 0;  /* Not enough accumulated delta yet */
}

uint32_t seraph_galactic_sched_adjust_priority(
    Seraph_Galactic_Exec_Stats* stats,
    uint32_t current_priority,
    uint32_t target_exec,
    uint32_t min_priority,
    uint32_t max_priority)
{
    if (stats == NULL) return current_priority;

    int32_t delta = seraph_galactic_sched_compute_priority_delta(stats, target_exec);

    if (delta == 0) return current_priority;

    /* Apply delta with bounds */
    int32_t new_priority = (int32_t)current_priority + delta;

    if (new_priority < (int32_t)min_priority) {
        new_priority = (int32_t)min_priority;
    }
    if (new_priority > (int32_t)max_priority) {
        new_priority = (int32_t)max_priority;
    }

    return (uint32_t)new_priority;
}

/*============================================================================
 * Learning Rate Adaptation
 *============================================================================*/

Seraph_Q128 seraph_galactic_sched_accuracy(
    const Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return SERAPH_Q128_VOID;
    if (stats->prediction_count == 0) return SERAPH_Q128_VOID;

    Seraph_Q128 accurate = seraph_q128_from_i64((int64_t)stats->accurate_predictions);
    Seraph_Q128 total = seraph_q128_from_i64((int64_t)stats->prediction_count);

    return seraph_q128_div(accurate, total);
}

void seraph_galactic_sched_adapt_learning_rate(
    Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return;
    if (!(stats->flags & SERAPH_GALACTIC_SCHED_ADAPTIVE_LR)) return;
    if (stats->prediction_count < 100) return;  /* Need sufficient data */

    Seraph_Q128 accuracy = seraph_galactic_sched_accuracy(stats);
    if (seraph_q128_is_void(accuracy)) return;

    if (seraph_vbit_is_true(seraph_q128_gt(accuracy, ACCURACY_HIGH))) {
        /* High accuracy: reduce learning rate (fine tuning) */
        stats->learning_rate = seraph_q128_mul(stats->learning_rate, LR_SHRINK);
    }
    else if (seraph_vbit_is_true(seraph_q128_lt(accuracy, ACCURACY_LOW))) {
        /* Low accuracy: increase learning rate */
        Seraph_Q128 boost = seraph_q128_mul(stats->learning_rate, LR_GROW);
        stats->learning_rate = seraph_q128_add(stats->learning_rate, boost);
    }

    /* Clamp to valid range */
    if (seraph_vbit_is_true(seraph_q128_lt(stats->learning_rate, LR_MIN))) {
        stats->learning_rate = LR_MIN;
    }
    if (seraph_vbit_is_true(seraph_q128_gt(stats->learning_rate, LR_MAX))) {
        stats->learning_rate = LR_MAX;
    }
}

void seraph_galactic_sched_set_learning_rate(
    Seraph_Galactic_Exec_Stats* stats,
    double rate)
{
    if (stats == NULL) return;

    if (rate < SERAPH_GALACTIC_SCHED_LR_MIN) {
        rate = SERAPH_GALACTIC_SCHED_LR_MIN;
    }
    if (rate > SERAPH_GALACTIC_SCHED_LR_MAX) {
        rate = SERAPH_GALACTIC_SCHED_LR_MAX;
    }

    stats->learning_rate = seraph_q128_from_double(rate);
}

/*============================================================================
 * Convergence Detection
 *============================================================================*/

bool seraph_galactic_sched_is_converged(
    const Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return false;
    if (stats->flags & SERAPH_GALACTIC_SCHED_WARMUP) return false;
    if (stats->prediction_count < 100) return false;

    /* Check tangent is small */
    Seraph_Q128 tangent = seraph_galactic_tangent(stats->exec_time);
    Seraph_Q128 abs_tangent = seraph_q128_abs(tangent);
    if (seraph_vbit_is_false(seraph_q128_lt(abs_tangent, TANGENT_THRESHOLD))) {
        return false;
    }

    /* Check accumulated delta is small */
    Seraph_Q128 abs_accum = seraph_q128_abs(stats->priority_delta_accum);
    Seraph_Q128 small_threshold = seraph_q128_from_double(0.1);
    if (seraph_vbit_is_false(seraph_q128_lt(abs_accum, small_threshold))) {
        return false;
    }

    /* Check accuracy is high */
    Seraph_Q128 accuracy = seraph_galactic_sched_accuracy(stats);
    if (seraph_vbit_is_false(seraph_q128_gt(accuracy, ACCURACY_HIGH))) {
        return false;
    }

    return true;
}

Seraph_Q128 seraph_galactic_sched_convergence_score(
    const Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return SERAPH_Q128_VOID;

    /* Convergence score is combination of:
     * - Low tangent magnitude
     * - Low accumulated delta
     * - High prediction accuracy
     */

    Seraph_Q128 tangent = seraph_galactic_tangent(stats->exec_time);
    Seraph_Q128 abs_tangent = seraph_q128_abs(tangent);

    /* Tangent score: 1 - min(|tangent| / threshold, 1) */
    Seraph_Q128 tangent_ratio = seraph_q128_div(abs_tangent, TANGENT_THRESHOLD);
    if (seraph_vbit_is_true(seraph_q128_gt(tangent_ratio, SERAPH_Q128_ONE))) {
        tangent_ratio = SERAPH_Q128_ONE;
    }
    Seraph_Q128 tangent_score = seraph_q128_sub(SERAPH_Q128_ONE, tangent_ratio);

    /* Accuracy score (capped at 1.0) */
    Seraph_Q128 accuracy = seraph_galactic_sched_accuracy(stats);
    if (seraph_q128_is_void(accuracy)) {
        accuracy = SERAPH_Q128_ZERO;
    }

    /* Combined: (tangent_score + accuracy) / 2 */
    Seraph_Q128 sum = seraph_q128_add(tangent_score, accuracy);
    Seraph_Q128 two = seraph_q128_from_i64(2);
    return seraph_q128_div(sum, two);
}

/*============================================================================
 * Statistics and Debugging
 *============================================================================*/

void seraph_galactic_sched_get_prediction_stats(
    const Seraph_Galactic_Exec_Stats* stats,
    uint64_t* out_count,
    uint64_t* out_accurate,
    Seraph_Q128* out_error)
{
    if (stats == NULL) {
        if (out_count) *out_count = 0;
        if (out_accurate) *out_accurate = 0;
        if (out_error) *out_error = SERAPH_Q128_VOID;
        return;
    }

    if (out_count) *out_count = stats->prediction_count;
    if (out_accurate) *out_accurate = stats->accurate_predictions;
    if (out_error) *out_error = stats->prediction_error;
}

int seraph_galactic_sched_format_stats(
    const Seraph_Galactic_Exec_Stats* stats,
    char* buffer,
    size_t size)
{
    if (stats == NULL || buffer == NULL || size == 0) return 0;

    double exec_primal = seraph_q128_to_double(seraph_galactic_primal(stats->exec_time));
    double exec_tangent = seraph_q128_to_double(seraph_galactic_tangent(stats->exec_time));
    double cpu_primal = seraph_q128_to_double(seraph_galactic_primal(stats->cpu_usage));
    double cpu_tangent = seraph_q128_to_double(seraph_galactic_tangent(stats->cpu_usage));
    double accuracy = stats->prediction_count > 0 ?
        (double)stats->accurate_predictions / (double)stats->prediction_count : 0.0;
    double lr = seraph_q128_to_double(stats->learning_rate);
    double accum = seraph_q128_to_double(stats->priority_delta_accum);

    return snprintf(buffer, size,
        "Galactic Exec Stats:\n"
        "  exec_time: %.3f (d/dt: %+.4f)\n"
        "  cpu_usage: %.1f%% (d/dt: %+.4f)\n"
        "  predictions: %llu (%.1f%% accurate)\n"
        "  learning_rate: %.4f\n"
        "  priority_delta_accum: %+.3f\n"
        "  flags: 0x%08X\n",
        exec_primal, exec_tangent,
        cpu_primal * 100.0, cpu_tangent,
        (unsigned long long)stats->prediction_count, accuracy * 100.0,
        lr,
        accum,
        stats->flags);
}

/*============================================================================
 * Global Scheduler Integration
 *============================================================================*/

void seraph_galactic_sched_global_init(void) {
    memset(&galactic_global, 0, sizeof(galactic_global));
    galactic_global.initialized = true;
}

void seraph_galactic_sched_tick(uint64_t current_tick) {
    /* This function would be called from the main scheduler tick handler.
     * It would iterate through all strands and update their Galactic stats.
     *
     * The actual integration is done in scheduler.c which has access to
     * the strand list. This function provides the global tick counter
     * and any global processing needed.
     */
    (void)current_tick;

    /* Periodically adapt learning rates globally */
    /* (Actual per-strand adaptation is done in update functions) */
}

void seraph_galactic_sched_global_stats(
    uint64_t* out_total_adjustments,
    uint64_t* out_boosts,
    uint64_t* out_demotions)
{
    if (out_total_adjustments) *out_total_adjustments = galactic_global.total_adjustments;
    if (out_boosts) *out_boosts = galactic_global.total_boosts;
    if (out_demotions) *out_demotions = galactic_global.total_demotions;
}

/*============================================================================
 * Advanced Prediction Functions
 *============================================================================*/

/**
 * @brief Predict optimal quantum size based on execution trend
 *
 * @param stats   Galactic stats
 * @param horizon Prediction horizon
 * @return Recommended quantum size (ticks)
 *
 * Uses the predicted execution time to recommend a quantum size that
 * balances responsiveness with context switch overhead.
 */
uint32_t seraph_galactic_sched_predict_optimal_quantum(
    const Seraph_Galactic_Exec_Stats* stats,
    uint32_t horizon)
{
    if (stats == NULL) return SERAPH_GALACTIC_SCHED_TARGET;

    Seraph_Q128 predicted = seraph_galactic_sched_predict_exec(stats, horizon);
    if (seraph_q128_is_void(predicted)) return SERAPH_GALACTIC_SCHED_TARGET;

    /* Optimal quantum = predicted_exec * 1.5 (allow some headroom) */
    Seraph_Q128 factor = seraph_q128_from_double(1.5);
    Seraph_Q128 optimal = seraph_q128_mul(predicted, factor);

    double result = seraph_q128_to_double(optimal);

    /* Clamp to reasonable range */
    if (result < 1.0) return 1;
    if (result > 128.0) return 128;

    return (uint32_t)result;
}

/**
 * @brief Detect if strand is becoming CPU-bound
 *
 * @param stats   Galactic stats
 * @return true if strand shows CPU-bound behavior
 *
 * A strand is CPU-bound if:
 * - High CPU usage (>80%) with positive trend
 * - Low wait time with negative trend (not waiting on I/O)
 */
bool seraph_galactic_sched_is_cpu_bound(
    const Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return false;

    /* Check CPU usage > 0.8 */
    Seraph_Q128 cpu = seraph_galactic_primal(stats->cpu_usage);
    Seraph_Q128 threshold = seraph_q128_from_double(0.8);

    if (seraph_vbit_is_false(seraph_q128_gt(cpu, threshold))) {
        return false;
    }

    /* Check CPU usage trend is positive or stable */
    Seraph_Q128 cpu_trend = seraph_galactic_tangent(stats->cpu_usage);
    Seraph_Q128 neg_threshold = seraph_q128_neg(TANGENT_THRESHOLD);

    if (seraph_vbit_is_true(seraph_q128_lt(cpu_trend, neg_threshold))) {
        return false;  /* CPU usage is decreasing */
    }

    return true;
}

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
    const Seraph_Galactic_Exec_Stats* stats)
{
    if (stats == NULL) return false;

    /* Check CPU usage < 0.3 */
    Seraph_Q128 cpu = seraph_galactic_primal(stats->cpu_usage);
    Seraph_Q128 threshold = seraph_q128_from_double(0.3);

    if (seraph_vbit_is_false(seraph_q128_lt(cpu, threshold))) {
        return false;
    }

    /* Check wait time trend is non-negative (stable or increasing) */
    Seraph_Q128 wait_trend = seraph_galactic_tangent(stats->wait_time);
    Seraph_Q128 neg_threshold = seraph_q128_neg(TANGENT_THRESHOLD);

    if (seraph_vbit_is_true(seraph_q128_lt(wait_trend, neg_threshold))) {
        return false;  /* Wait time is decreasing - becoming less I/O bound */
    }

    return true;
}
