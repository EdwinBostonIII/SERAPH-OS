/**
 * @file test_galactic_scheduler.c
 * @brief Tests for MC5+/13: Galactic Predictive Scheduling
 *
 * Tests the Galactic number based predictive scheduler:
 * - Execution time tracking as Galactic numbers
 * - Prediction accuracy and feedback
 * - Gradient descent priority adjustment
 * - Learning rate adaptation
 * - Convergence detection
 */

#include "seraph/galactic_scheduler.h"
#include "seraph/galactic.h"
#include "seraph/q128.h"
#include "seraph/vbit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*============================================================================
 * Test Utilities
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    printf("  Testing %s... ", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

/* Helper: Check approximate equality */
static int approx_eq(double a, double b, double tolerance) {
    return fabs(a - b) < tolerance;
}

/*============================================================================
 * Test: Initialization
 *============================================================================*/

TEST(galactic_sched_init) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, 0);

    /* Check that Galactic values are zero */
    ASSERT(approx_eq(seraph_galactic_primal_to_double(stats.exec_time), 0.0, 0.001));
    ASSERT(approx_eq(seraph_galactic_tangent_to_double(stats.exec_time), 0.0, 0.001));

    /* Check that stats are initialized */
    ASSERT(stats.prediction_count == 0);
    ASSERT(stats.accurate_predictions == 0);

    /* Check that flags include enabled and warmup */
    ASSERT(stats.flags & SERAPH_GALACTIC_SCHED_ENABLED);
    ASSERT(stats.flags & SERAPH_GALACTIC_SCHED_WARMUP);
}

/*============================================================================
 * Test: Execution Time Updates
 *============================================================================*/

TEST(galactic_sched_exec_update) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats,
        SERAPH_GALACTIC_SCHED_ENABLED |
        SERAPH_GALACTIC_SCHED_AUTOADJUST);

    /* First update - warmup mode, just sets value */
    seraph_galactic_sched_update_exec(&stats, 5, 10, 1);
    ASSERT(approx_eq(seraph_galactic_primal_to_double(stats.exec_time), 5.0, 0.1));

    /* Subsequent updates - should track derivative */
    for (int i = 2; i <= 15; i++) {
        /* Simulate increasing execution time */
        seraph_galactic_sched_update_exec(&stats, 5 + i, 10, (uint64_t)i);
    }

    /* After warmup, should have positive tangent (exec time increasing) */
    double tangent = seraph_galactic_tangent_to_double(stats.exec_time);
    ASSERT(tangent > 0.0);  /* Execution time is growing */

    /* Primal should be close to last value */
    double primal = seraph_galactic_primal_to_double(stats.exec_time);
    ASSERT(primal > 15.0);
}

/*============================================================================
 * Test: Prediction
 *============================================================================*/

TEST(galactic_sched_prediction) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Set up a stable state with known values */
    /* Warmup period */
    for (int i = 0; i < 12; i++) {
        seraph_galactic_sched_update_exec(&stats, 8, 10, (uint64_t)i);
    }

    /* Get current primal */
    double primal = seraph_galactic_primal_to_double(stats.exec_time);
    double tangent = seraph_galactic_tangent_to_double(stats.exec_time);

    /* Predict ahead */
    Seraph_Q128 predicted = seraph_galactic_sched_predict_exec(&stats, 10);
    double pred_val = seraph_q128_to_double(predicted);

    /* Prediction should be: primal + tangent * horizon */
    double expected = primal + tangent * 10.0;
    ASSERT(approx_eq(pred_val, expected, 0.5));
}

/*============================================================================
 * Test: Growing/Shrinking Detection
 *============================================================================*/

TEST(galactic_sched_growth_detection) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Create growing execution time pattern */
    for (int i = 0; i < 20; i++) {
        seraph_galactic_sched_update_exec(&stats, (uint32_t)(5 + i * 2), 20, (uint64_t)i);
    }

    /* Should detect as growing */
    ASSERT(seraph_vbit_is_true(seraph_galactic_sched_is_growing(&stats)));
    ASSERT(!seraph_vbit_is_true(seraph_galactic_sched_is_shrinking(&stats)));

    /* Reset and create shrinking pattern */
    seraph_galactic_sched_reset(&stats);

    for (int i = 0; i < 20; i++) {
        seraph_galactic_sched_update_exec(&stats, (uint32_t)(40 - i * 2), 50, (uint64_t)i);
    }

    /* Should detect as shrinking */
    ASSERT(!seraph_vbit_is_true(seraph_galactic_sched_is_growing(&stats)));
    ASSERT(seraph_vbit_is_true(seraph_galactic_sched_is_shrinking(&stats)));
}

/*============================================================================
 * Test: Priority Adjustment
 *============================================================================*/

TEST(galactic_sched_priority_adjustment) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats,
        SERAPH_GALACTIC_SCHED_ENABLED |
        SERAPH_GALACTIC_SCHED_AUTOADJUST);

    /* Warmup period */
    for (int i = 0; i < 15; i++) {
        seraph_galactic_sched_update_exec(&stats, 8, 10, (uint64_t)i);
    }

    /* Skip cooldown */
    stats.ticks_since_adjustment = 200;

    /* Create a situation where priority should be adjusted */
    /* Simulate high execution time with growing trend */
    for (int i = 15; i < 50; i++) {
        seraph_galactic_sched_update_exec(&stats, (uint32_t)(15 + i / 2), 20, (uint64_t)i);
        stats.ticks_since_adjustment = 200;  /* Skip cooldown for testing */
    }

    /* Compute priority delta */
    int32_t delta = seraph_galactic_sched_compute_priority_delta(&stats, 8);

    /* Just verify no crash and reasonable range */
    ASSERT(delta >= -SERAPH_GALACTIC_SCHED_MAX_DELTA);
    ASSERT(delta <= SERAPH_GALACTIC_SCHED_MAX_DELTA);
}

/*============================================================================
 * Test: Learning Rate Adaptation
 *============================================================================*/

TEST(galactic_sched_learning_rate) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats,
        SERAPH_GALACTIC_SCHED_ENABLED |
        SERAPH_GALACTIC_SCHED_ADAPTIVE_LR);

    double initial_lr = seraph_q128_to_double(stats.learning_rate);

    /* Simulate high accuracy predictions (stable pattern) */
    for (int i = 0; i < 120; i++) {
        seraph_galactic_sched_update_exec(&stats, 8, 10, (uint64_t)i);
        stats.accurate_predictions = stats.prediction_count;  /* 100% accuracy */
    }

    /* Adapt learning rate */
    seraph_galactic_sched_adapt_learning_rate(&stats);

    double final_lr = seraph_q128_to_double(stats.learning_rate);

    /* Learning rate should decrease with high accuracy */
    ASSERT(final_lr <= initial_lr);
    ASSERT(final_lr >= SERAPH_GALACTIC_SCHED_LR_MIN);
}

/*============================================================================
 * Test: Accuracy Calculation
 *============================================================================*/

TEST(galactic_sched_accuracy) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Simulate predictions */
    stats.prediction_count = 100;
    stats.accurate_predictions = 85;

    Seraph_Q128 accuracy = seraph_galactic_sched_accuracy(&stats);
    double acc_val = seraph_q128_to_double(accuracy);

    ASSERT(approx_eq(acc_val, 0.85, 0.01));
}

/*============================================================================
 * Test: Convergence Detection
 *============================================================================*/

TEST(galactic_sched_convergence) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Not converged during warmup */
    ASSERT(!seraph_galactic_sched_is_converged(&stats));

    /* Exit warmup */
    stats.flags &= ~SERAPH_GALACTIC_SCHED_WARMUP;

    /* Simulate stable pattern with high accuracy */
    stats.prediction_count = 150;
    stats.accurate_predictions = 145;  /* ~97% accuracy */
    stats.priority_delta_accum = seraph_q128_from_double(0.05);

    /* Set stable execution time (small tangent) */
    stats.exec_time = seraph_galactic_create(
        seraph_q128_from_double(8.0),
        seraph_q128_from_double(0.001)
    );

    /* Should be converged */
    ASSERT(seraph_galactic_sched_is_converged(&stats));
}

/*============================================================================
 * Test: CPU/IO Bound Detection
 *============================================================================*/

TEST(galactic_sched_bound_detection) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Simulate CPU-bound strand (high CPU usage, stable) */
    stats.cpu_usage = seraph_galactic_create(
        seraph_q128_from_double(0.95),  /* 95% CPU */
        seraph_q128_from_double(0.01)   /* Stable */
    );
    stats.wait_time = seraph_galactic_create(
        seraph_q128_from_double(0.0),
        seraph_q128_from_double(0.0)
    );

    ASSERT(seraph_galactic_sched_is_cpu_bound(&stats));
    ASSERT(!seraph_galactic_sched_is_io_bound(&stats));

    /* Simulate I/O-bound strand (low CPU, waiting) */
    stats.cpu_usage = seraph_galactic_create(
        seraph_q128_from_double(0.15),  /* 15% CPU */
        seraph_q128_from_double(-0.01)  /* Decreasing */
    );
    stats.wait_time = seraph_galactic_create(
        seraph_q128_from_double(50.0),  /* High wait */
        seraph_q128_from_double(0.1)    /* Increasing */
    );

    ASSERT(!seraph_galactic_sched_is_cpu_bound(&stats));
    ASSERT(seraph_galactic_sched_is_io_bound(&stats));
}

/*============================================================================
 * Test: Wait Time Tracking
 *============================================================================*/

TEST(galactic_sched_wait_time) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Update wait times */
    seraph_galactic_sched_update_wait(&stats, 10, 1);
    seraph_galactic_sched_update_wait(&stats, 15, 2);
    seraph_galactic_sched_update_wait(&stats, 20, 3);

    double wait_primal = seraph_galactic_primal_to_double(stats.wait_time);
    double wait_tangent = seraph_galactic_tangent_to_double(stats.wait_time);

    /* Wait time should reflect recent values */
    ASSERT(wait_primal > 0.0);

    /* Tangent should be positive (increasing wait time) */
    ASSERT(wait_tangent > 0.0);
}

/*============================================================================
 * Test: Response Time Tracking
 *============================================================================*/

TEST(galactic_sched_response_time) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Update response times (decreasing = better) */
    seraph_galactic_sched_update_response(&stats, 50, 1);
    seraph_galactic_sched_update_response(&stats, 40, 2);
    seraph_galactic_sched_update_response(&stats, 30, 3);

    double resp_tangent = seraph_galactic_tangent_to_double(stats.response_time);

    /* Tangent should be negative (decreasing response time) */
    ASSERT(resp_tangent < 0.0);
}

/*============================================================================
 * Test: Statistics Formatting
 *============================================================================*/

TEST(galactic_sched_format_stats) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Set some values */
    stats.exec_time = seraph_galactic_create(
        seraph_q128_from_double(8.5),
        seraph_q128_from_double(0.15)
    );
    stats.cpu_usage = seraph_galactic_create(
        seraph_q128_from_double(0.75),
        seraph_q128_from_double(-0.02)
    );
    stats.prediction_count = 1000;
    stats.accurate_predictions = 850;

    char buffer[512];
    int len = seraph_galactic_sched_format_stats(&stats, buffer, sizeof(buffer));

    ASSERT(len > 0);
    ASSERT(len < (int)sizeof(buffer));

    /* Verify output contains expected strings */
    ASSERT(strstr(buffer, "Galactic") != NULL);
    ASSERT(strstr(buffer, "exec_time") != NULL);
    ASSERT(strstr(buffer, "cpu_usage") != NULL);
}

/*============================================================================
 * Test: Global Statistics
 *============================================================================*/

TEST(galactic_sched_global_stats) {
    seraph_galactic_sched_global_init();

    uint64_t adjustments, boosts, demotions;
    seraph_galactic_sched_global_stats(&adjustments, &boosts, &demotions);

    /* After init, should be zero */
    ASSERT(adjustments == 0);
    ASSERT(boosts == 0);
    ASSERT(demotions == 0);
}

/*============================================================================
 * Test: Null Safety
 *============================================================================*/

TEST(galactic_sched_null_safety) {
    /* All functions should handle NULL gracefully */
    seraph_galactic_sched_init(NULL, 0);
    seraph_galactic_sched_reset(NULL);
    seraph_galactic_sched_update_exec(NULL, 10, 20, 1);
    seraph_galactic_sched_update_wait(NULL, 10, 1);
    seraph_galactic_sched_update_response(NULL, 10, 1);

    Seraph_Q128 pred = seraph_galactic_sched_predict_exec(NULL, 10);
    ASSERT(seraph_q128_is_void(pred));

    Seraph_Vbit growing = seraph_galactic_sched_is_growing(NULL);
    ASSERT(seraph_vbit_is_void(growing));

    int32_t delta = seraph_galactic_sched_compute_priority_delta(NULL, 8);
    ASSERT(delta == 0);

    Seraph_Q128 accuracy = seraph_galactic_sched_accuracy(NULL);
    ASSERT(seraph_q128_is_void(accuracy));

    ASSERT(!seraph_galactic_sched_is_converged(NULL));
    ASSERT(!seraph_galactic_sched_is_cpu_bound(NULL));
    ASSERT(!seraph_galactic_sched_is_io_bound(NULL));
}

/*============================================================================
 * Test: Convergence Score
 *============================================================================*/

TEST(galactic_sched_convergence_score) {
    Seraph_Galactic_Exec_Stats stats;

    seraph_galactic_sched_init(&stats, SERAPH_GALACTIC_SCHED_ENABLED);

    /* Low convergence: high tangent, low accuracy */
    stats.flags &= ~SERAPH_GALACTIC_SCHED_WARMUP;
    stats.exec_time = seraph_galactic_create(
        seraph_q128_from_double(10.0),
        seraph_q128_from_double(0.5)  /* High tangent */
    );
    stats.prediction_count = 100;
    stats.accurate_predictions = 50;  /* 50% accuracy */

    Seraph_Q128 low_score = seraph_galactic_sched_convergence_score(&stats);
    double low_val = seraph_q128_to_double(low_score);

    /* High convergence: low tangent, high accuracy */
    stats.exec_time = seraph_galactic_create(
        seraph_q128_from_double(8.0),
        seraph_q128_from_double(0.01)  /* Low tangent */
    );
    stats.accurate_predictions = 95;  /* 95% accuracy */

    Seraph_Q128 high_score = seraph_galactic_sched_convergence_score(&stats);
    double high_val = seraph_q128_to_double(high_score);

    /* High convergence should have higher score */
    ASSERT(high_val > low_val);
    ASSERT(high_val >= 0.0 && high_val <= 1.0);
    ASSERT(low_val >= 0.0 && low_val <= 1.0);
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_galactic_scheduler_tests(void) {
    printf("\n=== Galactic Predictive Scheduling Tests ===\n\n");

    tests_run = 0;
    tests_passed = 0;

    RUN_TEST(galactic_sched_init);
    RUN_TEST(galactic_sched_exec_update);
    RUN_TEST(galactic_sched_prediction);
    RUN_TEST(galactic_sched_growth_detection);
    RUN_TEST(galactic_sched_priority_adjustment);
    RUN_TEST(galactic_sched_learning_rate);
    RUN_TEST(galactic_sched_accuracy);
    RUN_TEST(galactic_sched_convergence);
    RUN_TEST(galactic_sched_bound_detection);
    RUN_TEST(galactic_sched_wait_time);
    RUN_TEST(galactic_sched_response_time);
    RUN_TEST(galactic_sched_format_stats);
    RUN_TEST(galactic_sched_global_stats);
    RUN_TEST(galactic_sched_null_safety);
    RUN_TEST(galactic_sched_convergence_score);

    printf("\nGalactic Scheduler Tests: %d/%d passed\n", tests_passed, tests_run);
}
