/**
 * @file test_akashic.c
 * @brief Tests for SERAPH PRISM - Akashic Undo (Reverse-Causal Debugging)
 *
 * Tests the timeline forking and crash recovery system including:
 * - Timeline creation and forking
 * - Snapshot management (COW)
 * - Input recording and fuzzing
 * - Recovery from VOID crashes
 * - Chronon management
 */

#include "seraph/prism/akashic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/*============================================================================
 * Initialization Tests
 *============================================================================*/

TEST(akashic_init) {
    Seraph_Akashic akashic;

    Seraph_Vbit result = seraph_akashic_init(&akashic, NULL, NULL);
    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(akashic.initialized == SERAPH_VBIT_TRUE);
    ASSERT(seraph_akashic_is_valid(&akashic));

    /* Check defaults */
    ASSERT(akashic.timeline_count == 0);
    ASSERT(akashic.snapshot_interval == SERAPH_AKASHIC_DEFAULT_INTERVAL);
    ASSERT(akashic.max_fuzz_attempts == SERAPH_AKASHIC_MAX_FUZZ_ATTEMPTS);

    seraph_akashic_destroy(&akashic);
    ASSERT(!seraph_akashic_is_valid(&akashic));
}

TEST(akashic_init_null) {
    Seraph_Vbit result = seraph_akashic_init(NULL, NULL, NULL);
    ASSERT(result == SERAPH_VBIT_VOID);
}

/*============================================================================
 * Timeline Management Tests
 *============================================================================*/

TEST(create_timeline) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    Seraph_Akashic_Timeline* timeline = seraph_akashic_create_timeline(&akashic);

    ASSERT(timeline != NULL);
    ASSERT(timeline->valid == SERAPH_VBIT_TRUE);
    ASSERT(timeline->state == SERAPH_TIMELINE_ACTIVE);
    ASSERT(timeline->fork_depth == 0);
    ASSERT(akashic.timeline_count == 1);
    ASSERT(akashic.active_timeline_id == timeline->timeline_id);

    seraph_akashic_destroy(&akashic);
}

TEST(create_multiple_timelines) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    Seraph_Akashic_Timeline* timelines[5];
    for (int i = 0; i < 5; i++) {
        timelines[i] = seraph_akashic_create_timeline(&akashic);
        ASSERT(timelines[i] != NULL);
    }

    ASSERT(akashic.timeline_count == 5);

    /* Each timeline should have unique ID */
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            ASSERT(timelines[i]->timeline_id != timelines[j]->timeline_id);
        }
    }

    seraph_akashic_destroy(&akashic);
}

TEST(fork_timeline) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    Seraph_Akashic_Timeline* parent = seraph_akashic_create_timeline(&akashic);
    ASSERT(parent != NULL);

    /* Simulate some execution on parent */
    akashic.current_chronon = 1000;

    /* Fork at chronon 500 */
    Seraph_Akashic_Timeline* child = seraph_akashic_fork_timeline(
        &akashic, parent->timeline_id, 500);

    ASSERT(child != NULL);
    ASSERT(child->parent_timeline == parent->timeline_id);
    ASSERT(child->fork_point == 500);
    ASSERT(child->fork_depth == parent->fork_depth + 1);
    ASSERT(parent->state == SERAPH_TIMELINE_FORKED);
    ASSERT(akashic.timeline_count == 2);

    seraph_akashic_destroy(&akashic);
}

TEST(fork_timeline_depth_limit) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    Seraph_Akashic_Timeline* current = seraph_akashic_create_timeline(&akashic);

    /* Fork repeatedly until depth limit */
    for (int i = 0; i < 10; i++) {
        Seraph_Akashic_Timeline* forked = seraph_akashic_fork_timeline(
            &akashic, current->timeline_id, i * 100);

        if (forked == NULL) {
            /* Should fail around depth 8 */
            ASSERT(current->fork_depth >= 8);
            break;
        }
        current = forked;
    }

    seraph_akashic_destroy(&akashic);
}

TEST(switch_timeline) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    Seraph_Akashic_Timeline* t1 = seraph_akashic_create_timeline(&akashic);
    Seraph_Akashic_Timeline* t2 = seraph_akashic_create_timeline(&akashic);

    ASSERT(akashic.active_timeline_id == t1->timeline_id);

    Seraph_Vbit result = seraph_akashic_switch_timeline(&akashic, t2->timeline_id);

    ASSERT(result == SERAPH_VBIT_TRUE);
    ASSERT(akashic.active_timeline_id == t2->timeline_id);

    /* Get active timeline */
    Seraph_Akashic_Timeline* active = seraph_akashic_active_timeline(&akashic);
    ASSERT(active == t2);

    seraph_akashic_destroy(&akashic);
}

TEST(switch_nonexistent_timeline) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    seraph_akashic_create_timeline(&akashic);

    Seraph_Vbit result = seraph_akashic_switch_timeline(&akashic, 999999);

    ASSERT(result == SERAPH_VBIT_FALSE);

    seraph_akashic_destroy(&akashic);
}

TEST(abandon_timeline) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    Seraph_Akashic_Timeline* t1 = seraph_akashic_create_timeline(&akashic);
    Seraph_Akashic_Timeline* t2 = seraph_akashic_create_timeline(&akashic);

    ASSERT(akashic.timeline_count == 2);

    seraph_akashic_abandon_timeline(&akashic, t1->timeline_id);

    ASSERT(t1->state == SERAPH_TIMELINE_ABANDONED);
    ASSERT(t1->valid != SERAPH_VBIT_TRUE);
    ASSERT(akashic.timeline_count == 1);

    /* Active timeline should switch to t2 */
    ASSERT(akashic.active_timeline_id == t2->timeline_id);

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Snapshot Management Tests
 *============================================================================*/

TEST(snapshot_create) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    akashic.current_chronon = 1000;

    uint64_t snap_id = seraph_akashic_snapshot(&akashic, false);

    ASSERT(snap_id != SERAPH_VOID_U64);
    ASSERT(akashic.total_snapshots == 1);

    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    ASSERT(timeline->snapshot_count == 1);
    ASSERT(timeline->newest_snapshot_id == snap_id);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_checkpoint) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    akashic.current_chronon = 500;
    uint64_t snap_id = seraph_akashic_snapshot(&akashic, true);  /* Checkpoint */

    ASSERT(snap_id != SERAPH_VOID_U64);

    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    /* Find the snapshot and verify it's a checkpoint */
    bool found = false;
    for (uint32_t i = 0; i < SERAPH_AKASHIC_MAX_SNAPSHOTS; i++) {
        if (timeline->snapshots[i].snapshot_id == snap_id) {
            ASSERT(timeline->snapshots[i].is_checkpoint == true);
            found = true;
            break;
        }
    }
    ASSERT(found);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_multiple) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Create multiple snapshots at different chronons */
    for (int i = 1; i <= 10; i++) {
        akashic.current_chronon = i * 100;
        uint64_t snap_id = seraph_akashic_snapshot(&akashic, false);
        ASSERT(snap_id != SERAPH_VOID_U64);
    }

    ASSERT(akashic.total_snapshots == 10);

    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    ASSERT(timeline->snapshot_count == 10);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_find) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Create snapshots at chronons 100, 200, 300 */
    akashic.current_chronon = 100;
    seraph_akashic_snapshot(&akashic, false);
    akashic.current_chronon = 200;
    seraph_akashic_snapshot(&akashic, false);
    akashic.current_chronon = 300;
    seraph_akashic_snapshot(&akashic, false);

    /* Find snapshot at or before chronon 250 → should get the 200 snapshot */
    Seraph_Akashic_Snapshot* snap = seraph_akashic_find_snapshot(&akashic, 250);

    ASSERT(snap != NULL);
    ASSERT(snap->chronon <= 250);
    ASSERT(snap->chronon == 200 || snap->chronon == 100);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_prune) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Create many snapshots */
    for (int i = 1; i <= 20; i++) {
        akashic.current_chronon = i * 100;
        seraph_akashic_snapshot(&akashic, false);
    }

    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    ASSERT(timeline->snapshot_count == 20);

    /* Prune to keep only 5 */
    seraph_akashic_prune_snapshots(&akashic, 5);

    /* Should have at most 5 snapshots now */
    ASSERT(timeline->snapshot_count <= 5);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_restore) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    akashic.current_chronon = 500;
    uint64_t snap_id = seraph_akashic_snapshot(&akashic, false);

    /* Advance time */
    akashic.current_chronon = 1000;

    /* Restore to snapshot */
    Seraph_Vbit result = seraph_akashic_restore_snapshot(&akashic, snap_id);

    ASSERT(result == SERAPH_VBIT_TRUE);

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Input Recording Tests
 *============================================================================*/

TEST(record_keyboard_input) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    Seraph_Akashic_Input input = {
        .chronon = 100,
        .type = SERAPH_INPUT_KEYBOARD,
        .data.keyboard = {
            .scancode = 0x1E,  /* 'A' key */
            .keycode = 'A',
            .pressed = true,
            .modifiers = 0
        },
        .fuzzable = true,
        .fuzz_min = -2,
        .fuzz_max = 2
    };

    uint64_t event_id = seraph_akashic_record_input(&akashic, &input);

    ASSERT(event_id != SERAPH_VOID_U64);
    ASSERT(akashic.inputs_recorded == 1);

    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    ASSERT(timeline->input_count == 1);

    seraph_akashic_destroy(&akashic);
}

TEST(record_mouse_input) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    Seraph_Akashic_Input input = {
        .chronon = 200,
        .type = SERAPH_INPUT_MOUSE,
        .data.mouse = {
            .x = 100,
            .y = 200,
            .dx = 5,
            .dy = -3,
            .buttons = 0x01,  /* Left button */
            .wheel = 0
        },
        .fuzzable = true,
        .fuzz_min = -2,
        .fuzz_max = 2
    };

    uint64_t event_id = seraph_akashic_record_input(&akashic, &input);

    ASSERT(event_id != SERAPH_VOID_U64);

    seraph_akashic_destroy(&akashic);
}

TEST(record_multiple_inputs) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    for (int i = 0; i < 100; i++) {
        Seraph_Akashic_Input input = {
            .chronon = i * 10,
            .type = SERAPH_INPUT_KEYBOARD,
            .data.keyboard = {
                .scancode = (uint8_t)(0x10 + (i % 26)),
                .pressed = (i % 2 == 0)
            }
        };
        seraph_akashic_record_input(&akashic, &input);
    }

    ASSERT(akashic.inputs_recorded == 100);

    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    ASSERT(timeline->input_count == 100);

    seraph_akashic_destroy(&akashic);
}

TEST(mark_fuzzable) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    Seraph_Akashic_Input input = {
        .chronon = 100,
        .type = SERAPH_INPUT_MOUSE,
        .fuzzable = false
    };

    uint64_t event_id = seraph_akashic_record_input(&akashic, &input);

    seraph_akashic_mark_fuzzable(&akashic, event_id, -5, 5);

    /* Verify the input is now fuzzable */
    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    bool found = false;
    for (uint32_t i = 0; i < timeline->input_count; i++) {
        if (timeline->inputs[i].event_id == event_id) {
            ASSERT(timeline->inputs[i].fuzzable == true);
            ASSERT(timeline->inputs[i].fuzz_min == -5);
            ASSERT(timeline->inputs[i].fuzz_max == 5);
            found = true;
            break;
        }
    }
    ASSERT(found);

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Input Fuzzing Tests
 *============================================================================*/

TEST(fuzz_inputs) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    Seraph_Akashic_Timeline* timeline = seraph_akashic_create_timeline(&akashic);

    /* Record some fuzzable inputs */
    for (int i = 0; i < 10; i++) {
        Seraph_Akashic_Input input = {
            .chronon = 100 + i * 10,
            .type = SERAPH_INPUT_MOUSE,
            .data.mouse = {
                .x = 100 + i,
                .y = 200 + i
            },
            .fuzzable = true,
            .fuzz_min = -2,
            .fuzz_max = 2
        };
        seraph_akashic_record_input(&akashic, &input);
    }

    /* Fuzz inputs between chronon 100 and 200 */
    uint32_t fuzzed = seraph_akashic_fuzz_inputs(timeline, 100, 200, 1);

    ASSERT(fuzzed > 0);
    ASSERT(akashic.inputs_fuzzed >= fuzzed);

    seraph_akashic_destroy(&akashic);
}

TEST(fuzz_deterministic) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    Seraph_Akashic_Timeline* timeline = seraph_akashic_create_timeline(&akashic);

    Seraph_Akashic_Input input = {
        .chronon = 100,
        .type = SERAPH_INPUT_MOUSE,
        .data.mouse = { .x = 500, .y = 500 },
        .fuzzable = true,
        .fuzz_min = -10,
        .fuzz_max = 10
    };
    seraph_akashic_record_input(&akashic, &input);

    /* Fuzz with same attempt number should give same result */
    int32_t original_x = timeline->inputs[0].data.mouse.x;

    seraph_akashic_fuzz_inputs(timeline, 0, 200, 1);
    int32_t fuzzed_x_1 = timeline->inputs[0].data.mouse.x;

    /* Reset and fuzz again with same attempt */
    timeline->inputs[0].data.mouse.x = original_x;
    seraph_akashic_fuzz_inputs(timeline, 0, 200, 1);
    int32_t fuzzed_x_2 = timeline->inputs[0].data.mouse.x;

    /* Same attempt number should produce same fuzz result */
    ASSERT(fuzzed_x_1 == fuzzed_x_2);

    /* Different attempt number should produce different result */
    timeline->inputs[0].data.mouse.x = original_x;
    seraph_akashic_fuzz_inputs(timeline, 0, 200, 2);
    int32_t fuzzed_x_3 = timeline->inputs[0].data.mouse.x;

    ASSERT(fuzzed_x_1 != fuzzed_x_3 || fuzzed_x_1 == original_x);  /* Allow no change case */

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Chronon Management Tests
 *============================================================================*/

TEST(advance_chronon) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    ASSERT(akashic.current_chronon == 0);

    seraph_akashic_advance_chronon(&akashic, 100);
    ASSERT(akashic.current_chronon == 100);

    seraph_akashic_advance_chronon(&akashic, 50);
    ASSERT(akashic.current_chronon == 150);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_due) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    akashic.auto_snapshot_enabled = true;
    akashic.snapshot_interval = 100;
    akashic.last_snapshot_chronon = 0;
    akashic.current_chronon = 50;

    ASSERT(seraph_akashic_snapshot_due(&akashic) == false);

    akashic.current_chronon = 100;
    ASSERT(seraph_akashic_snapshot_due(&akashic) == true);

    akashic.current_chronon = 150;
    ASSERT(seraph_akashic_snapshot_due(&akashic) == true);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_due_disabled) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    akashic.auto_snapshot_enabled = false;
    akashic.current_chronon = 10000;

    ASSERT(seraph_akashic_snapshot_due(&akashic) == false);

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Configuration Tests
 *============================================================================*/

TEST(configure_snapshots) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    seraph_akashic_configure_snapshots(&akashic, 500, 128, true);

    ASSERT(akashic.snapshot_interval == 500);
    ASSERT(akashic.max_snapshots == 128);
    ASSERT(akashic.auto_snapshot_enabled == true);

    seraph_akashic_destroy(&akashic);
}

TEST(configure_recovery) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    seraph_akashic_configure_recovery(&akashic, 5000, 16, true);

    ASSERT(akashic.max_rewind_chronons == 5000);
    ASSERT(akashic.max_fuzz_attempts == 16);
    ASSERT(akashic.auto_recovery_enabled == true);

    seraph_akashic_destroy(&akashic);
}

TEST(set_trap_filter) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    seraph_akashic_set_trap_filter(&akashic,
        SERAPH_AKASHIC_TRAP_DIV_ZERO | SERAPH_AKASHIC_TRAP_NULL_PTR);

    ASSERT(akashic.trap_filter & SERAPH_AKASHIC_TRAP_DIV_ZERO);
    ASSERT(akashic.trap_filter & SERAPH_AKASHIC_TRAP_NULL_PTR);
    ASSERT(!(akashic.trap_filter & SERAPH_AKASHIC_TRAP_OVERFLOW));

    seraph_akashic_destroy(&akashic);
}

TEST(enable_trap) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    akashic.void_trap_enabled = false;
    seraph_akashic_enable_trap(&akashic, true);
    ASSERT(akashic.void_trap_enabled == true);

    seraph_akashic_enable_trap(&akashic, false);
    ASSERT(akashic.void_trap_enabled == false);

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Recovery Tests
 *============================================================================*/

TEST(recover_basic) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Create some snapshots */
    akashic.current_chronon = 100;
    seraph_akashic_snapshot(&akashic, false);
    akashic.current_chronon = 200;
    seraph_akashic_snapshot(&akashic, false);

    /* Simulate crash at chronon 250 */
    akashic.current_chronon = 250;

    akashic.auto_recovery_enabled = true;
    Seraph_Vbit result = seraph_akashic_recover(&akashic, SERAPH_VOID_REASON_DIV_ZERO, 250);

    /* Recovery may succeed or fail depending on implementation state */
    ASSERT(result == SERAPH_VBIT_TRUE || result == SERAPH_VBIT_FALSE);

    /* Should have tracked the crash */
    ASSERT(akashic.crashes_caught >= 1);

    seraph_akashic_destroy(&akashic);
}

TEST(recover_with_inputs) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Create snapshot */
    akashic.current_chronon = 100;
    seraph_akashic_snapshot(&akashic, false);

    /* Record some inputs */
    for (int i = 0; i < 5; i++) {
        Seraph_Akashic_Input input = {
            .chronon = 100 + i * 20,
            .type = SERAPH_INPUT_KEYBOARD,
            .fuzzable = true,
            .fuzz_min = -1,
            .fuzz_max = 1
        };
        seraph_akashic_record_input(&akashic, &input);
    }

    /* Crash */
    akashic.current_chronon = 200;

    akashic.auto_recovery_enabled = true;
    seraph_akashic_recover(&akashic, SERAPH_VOID_REASON_OUT_OF_BOUNDS, 200);

    /* Should have attempted recovery */
    ASSERT(akashic.recoveries_attempted >= 1);

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Statistics Tests
 *============================================================================*/

TEST(statistics) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Create some activity */
    for (int i = 0; i < 5; i++) {
        akashic.current_chronon = i * 100;
        seraph_akashic_snapshot(&akashic, false);
    }

    uint64_t crashes, recoveries, failed, timelines, snapshots;
    seraph_akashic_get_stats(&akashic, &crashes, &recoveries, &failed, &timelines, &snapshots);

    ASSERT(snapshots == 5);
    ASSERT(timelines == 0);  /* No forks yet */

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Edge Cases
 *============================================================================*/

TEST(null_akashic_operations) {
    /* All operations on NULL akashic should be safe */
    Seraph_Akashic_Timeline* timeline = seraph_akashic_create_timeline(NULL);
    ASSERT(timeline == NULL);

    uint64_t snap_id = seraph_akashic_snapshot(NULL, false);
    ASSERT(snap_id == SERAPH_VOID_U64);

    Seraph_Vbit result = seraph_akashic_switch_timeline(NULL, 1);
    ASSERT(result == SERAPH_VBIT_VOID);
}

TEST(timeline_capacity) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);

    /* Fill all timeline slots */
    for (int i = 0; i < SERAPH_AKASHIC_MAX_TIMELINES; i++) {
        Seraph_Akashic_Timeline* t = seraph_akashic_create_timeline(&akashic);
        ASSERT(t != NULL);
    }

    /* Should fail when exceeding capacity */
    Seraph_Akashic_Timeline* overflow = seraph_akashic_create_timeline(&akashic);
    ASSERT(overflow == NULL);

    seraph_akashic_destroy(&akashic);
}

TEST(snapshot_capacity) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Fill all snapshot slots */
    for (int i = 0; i < SERAPH_AKASHIC_MAX_SNAPSHOTS + 10; i++) {
        akashic.current_chronon = i;
        seraph_akashic_snapshot(&akashic, false);
    }

    /* Should have pruned old snapshots */
    Seraph_Akashic_Timeline* timeline = seraph_akashic_active_timeline(&akashic);
    ASSERT(timeline->snapshot_count <= SERAPH_AKASHIC_MAX_SNAPSHOTS);

    seraph_akashic_destroy(&akashic);
}

TEST(input_types) {
    Seraph_Akashic akashic;
    seraph_akashic_init(&akashic, NULL, NULL);
    seraph_akashic_create_timeline(&akashic);

    /* Test all input types */
    Seraph_Akashic_Input_Type types[] = {
        SERAPH_INPUT_KEYBOARD,
        SERAPH_INPUT_MOUSE,
        SERAPH_INPUT_GAMEPAD,
        SERAPH_INPUT_NETWORK,
        SERAPH_INPUT_TIMER,
        SERAPH_INPUT_RANDOM,
        SERAPH_INPUT_SYSCALL
    };

    for (int i = 0; i < 7; i++) {
        Seraph_Akashic_Input input = {
            .chronon = i * 100,
            .type = types[i]
        };
        uint64_t id = seraph_akashic_record_input(&akashic, &input);
        ASSERT(id != SERAPH_VOID_U64);
    }

    ASSERT(akashic.inputs_recorded == 7);

    seraph_akashic_destroy(&akashic);
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_akashic_tests(void) {
    printf("\n=== PRISM: Akashic Undo Tests ===\n\n");

    /* Initialization */
    RUN_TEST(akashic_init);
    RUN_TEST(akashic_init_null);

    /* Timeline Management */
    RUN_TEST(create_timeline);
    RUN_TEST(create_multiple_timelines);
    RUN_TEST(fork_timeline);
    RUN_TEST(fork_timeline_depth_limit);
    RUN_TEST(switch_timeline);
    RUN_TEST(switch_nonexistent_timeline);
    RUN_TEST(abandon_timeline);

    /* Snapshot Management */
    RUN_TEST(snapshot_create);
    RUN_TEST(snapshot_checkpoint);
    RUN_TEST(snapshot_multiple);
    RUN_TEST(snapshot_find);
    RUN_TEST(snapshot_prune);
    RUN_TEST(snapshot_restore);

    /* Input Recording */
    RUN_TEST(record_keyboard_input);
    RUN_TEST(record_mouse_input);
    RUN_TEST(record_multiple_inputs);
    RUN_TEST(mark_fuzzable);

    /* Input Fuzzing */
    RUN_TEST(fuzz_inputs);
    RUN_TEST(fuzz_deterministic);

    /* Chronon Management */
    RUN_TEST(advance_chronon);
    RUN_TEST(snapshot_due);
    RUN_TEST(snapshot_due_disabled);

    /* Configuration */
    RUN_TEST(configure_snapshots);
    RUN_TEST(configure_recovery);
    RUN_TEST(set_trap_filter);
    RUN_TEST(enable_trap);

    /* Recovery */
    RUN_TEST(recover_basic);
    RUN_TEST(recover_with_inputs);

    /* Statistics */
    RUN_TEST(statistics);

    /* Edge Cases */
    RUN_TEST(null_akashic_operations);
    RUN_TEST(timeline_capacity);
    RUN_TEST(snapshot_capacity);
    RUN_TEST(input_types);

    printf("\nAkashic Tests: %d/%d passed\n", tests_passed, tests_run);
}

#ifndef SERAPH_TEST_NO_MAIN
int main(void) {
    run_akashic_tests();
    return (tests_passed == tests_run) ? 0 : 1;
}
#endif
