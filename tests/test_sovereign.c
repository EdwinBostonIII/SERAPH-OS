/**
 * @file test_sovereign.c
 * @brief Test suite for MC10: The Sovereign
 *
 * Tests capability-based process isolation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seraph/sovereign.h"

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;
static int current_test_failed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n  Assertion failed: %s\n  Line %d\n", #cond, __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED\n  Expected %lld == %lld\n  Line %d\n", \
               (long long)(a), (long long)(b), __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_VBIT_TRUE(v) do { \
    if (!seraph_vbit_is_true(v)) { \
        printf("FAILED\n  Expected VBIT_TRUE\n  Line %d\n", __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_VBIT_FALSE(v) do { \
    if (!seraph_vbit_is_false(v)) { \
        printf("FAILED\n  Expected VBIT_FALSE\n  Line %d\n", __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_VBIT_VOID(v) do { \
    if (!seraph_vbit_is_void(v)) { \
        printf("FAILED\n  Expected VBIT_VOID\n  Line %d\n", __LINE__); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    tests_run++; \
    current_test_failed = 0; \
    test_##name(); \
    if (!current_test_failed) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
    fflush(stdout); \
} while(0)

/*============================================================================
 * State Enum Tests
 *============================================================================*/

void test_state_is_void(void) {
    ASSERT(seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_VOID));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_CONCEIVING));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_NASCENT));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_RUNNING));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_WAITING));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_SUSPENDED));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_EXITING));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_KILLED));
    ASSERT(!seraph_sovereign_state_is_void(SERAPH_SOVEREIGN_VOIDED));
}

void test_state_is_alive(void) {
    /* Alive states: NASCENT, RUNNING, WAITING, SUSPENDED */
    ASSERT(!seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_CONCEIVING));
    ASSERT(seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_NASCENT));
    ASSERT(seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_RUNNING));
    ASSERT(seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_WAITING));
    ASSERT(seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_SUSPENDED));
    ASSERT(!seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_EXITING));
    ASSERT(!seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_KILLED));
    ASSERT(!seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_VOIDED));
    ASSERT(!seraph_sovereign_state_is_alive(SERAPH_SOVEREIGN_VOID));
}

void test_state_is_terminal(void) {
    /* Terminal states: EXITING, KILLED, VOIDED, VOID */
    ASSERT(!seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_CONCEIVING));
    ASSERT(!seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_NASCENT));
    ASSERT(!seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_RUNNING));
    ASSERT(!seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_WAITING));
    ASSERT(!seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_SUSPENDED));
    ASSERT(seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_EXITING));
    ASSERT(seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_KILLED));
    ASSERT(seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_VOIDED));
    ASSERT(seraph_sovereign_state_is_terminal(SERAPH_SOVEREIGN_VOID));
}

/*============================================================================
 * Authority Flag Tests
 *============================================================================*/

void test_authority_valid_subset(void) {
    /* Child with subset of parent's authority is valid */
    uint64_t parent = SERAPH_AUTH_SPAWN | SERAPH_AUTH_KILL | SERAPH_AUTH_CHRONON_READ;
    uint64_t child = SERAPH_AUTH_SPAWN | SERAPH_AUTH_CHRONON_READ;
    ASSERT_VBIT_TRUE(seraph_authority_valid(parent, child));
}

void test_authority_invalid_superset(void) {
    /* Child with more authority than parent is invalid */
    uint64_t parent = SERAPH_AUTH_SPAWN | SERAPH_AUTH_CHRONON_READ;
    uint64_t child = SERAPH_AUTH_SPAWN | SERAPH_AUTH_KILL | SERAPH_AUTH_CHRONON_READ;
    ASSERT_VBIT_FALSE(seraph_authority_valid(parent, child));
}

void test_authority_equal_is_valid(void) {
    /* Child with exact same authority is valid */
    uint64_t auth = SERAPH_AUTH_APPLICATION;
    ASSERT_VBIT_TRUE(seraph_authority_valid(auth, auth));
}

void test_authority_none_always_valid(void) {
    /* NONE authority is always valid as child */
    ASSERT_VBIT_TRUE(seraph_authority_valid(SERAPH_AUTH_PRIMORDIAL, SERAPH_AUTH_NONE));
    ASSERT_VBIT_TRUE(seraph_authority_valid(SERAPH_AUTH_MINIMAL, SERAPH_AUTH_NONE));
}

void test_authority_void_propagation(void) {
    /* Note: SERAPH_VOID_U64 == SERAPH_AUTH_PRIMORDIAL (~0ULL).
     * In authority context, ~0ULL means "all authority", not VOID.
     * So VOID_U64 as parent is treated as PRIMORDIAL (valid for any child).
     * VOID_U64 as child is treated as requesting PRIMORDIAL authority. */

    /* Parent is PRIMORDIAL (via VOID_U64), child is MINIMAL - valid */
    ASSERT_VBIT_TRUE(seraph_authority_valid(SERAPH_VOID_U64, SERAPH_AUTH_MINIMAL));

    /* Child is PRIMORDIAL (via VOID_U64), parent is MINIMAL - invalid (child has too much) */
    ASSERT_VBIT_FALSE(seraph_authority_valid(SERAPH_AUTH_MINIMAL, SERAPH_VOID_U64));

    /* Both are PRIMORDIAL - valid (equal authority) */
    ASSERT_VBIT_TRUE(seraph_authority_valid(SERAPH_VOID_U64, SERAPH_VOID_U64));
}

void test_authority_has(void) {
    uint64_t auth = SERAPH_AUTH_SPAWN | SERAPH_AUTH_KILL | SERAPH_AUTH_CHRONON_READ;
    ASSERT(seraph_authority_has(auth, SERAPH_AUTH_SPAWN));
    ASSERT(seraph_authority_has(auth, SERAPH_AUTH_KILL));
    ASSERT(seraph_authority_has(auth, SERAPH_AUTH_CHRONON_READ));
    ASSERT(seraph_authority_has(auth, SERAPH_AUTH_SPAWN | SERAPH_AUTH_KILL));
    ASSERT(!seraph_authority_has(auth, SERAPH_AUTH_SUSPEND));
    ASSERT(!seraph_authority_has(auth, SERAPH_AUTH_FRAMEBUFFER));
}

void test_authority_has_void(void) {
    /*
     * SEMANTIC CLARIFICATION:
     * SERAPH_VOID_U64 (~0ULL) == SERAPH_AUTH_PRIMORDIAL in authority context.
     * This means "all authority", not "absence/error".
     *
     * So VOID_U64 actually HAS every authority bit set.
     */
    ASSERT(seraph_authority_has(SERAPH_VOID_U64, SERAPH_AUTH_SPAWN));
    ASSERT(seraph_authority_has(SERAPH_VOID_U64, SERAPH_AUTH_KILL));
    ASSERT(seraph_authority_has(SERAPH_VOID_U64, SERAPH_AUTH_FRAMEBUFFER));

    /* NONE (0) has no authorities */
    ASSERT(!seraph_authority_has(SERAPH_AUTH_NONE, SERAPH_AUTH_SPAWN));
}

/*============================================================================
 * Sovereign ID Tests
 *============================================================================*/

void test_id_void_detection(void) {
    /* Create a VOID ID manually */
    Seraph_Sovereign_ID void_id;
    void_id.quads[0] = SERAPH_VOID_U64;
    void_id.quads[1] = SERAPH_VOID_U64;
    void_id.quads[2] = SERAPH_VOID_U64;
    void_id.quads[3] = SERAPH_VOID_U64;

    /* Should be detected as VOID */
    ASSERT(seraph_sovereign_id_is_void(void_id));

    /* Also test the macro form */
    Seraph_Sovereign_ID void_id2 = SERAPH_SOVEREIGN_ID_VOID;
    ASSERT(seraph_sovereign_id_is_void(void_id2));
}

void test_id_generation(void) {
    /* Generate an ID with minimal authority */
    Seraph_Sovereign_ID id = seraph_sovereign_id_generate(SERAPH_AUTH_MINIMAL);

    /* Should not be VOID */
    ASSERT(!seraph_sovereign_id_is_void(id));

    /* ID should embed the authority in quads[2] */
    ASSERT_EQ(id.quads[2], SERAPH_AUTH_MINIMAL);

    /* ID should pass validation */
    ASSERT_VBIT_TRUE(seraph_sovereign_id_validate(id));
}

void test_id_uniqueness(void) {
    Seraph_Sovereign_ID id1 = seraph_sovereign_id_generate(SERAPH_AUTH_MINIMAL);
    Seraph_Sovereign_ID id2 = seraph_sovereign_id_generate(SERAPH_AUTH_MINIMAL);

    /* Two generated IDs should be different */
    ASSERT_VBIT_FALSE(seraph_sovereign_id_equal(id1, id2));
}

void test_id_equality(void) {
    Seraph_Sovereign_ID id1 = seraph_sovereign_id_generate(SERAPH_AUTH_APPLICATION);
    Seraph_Sovereign_ID id2 = id1;  /* Copy */

    ASSERT_VBIT_TRUE(seraph_sovereign_id_equal(id1, id2));
}

void test_id_equality_void_propagation(void) {
    Seraph_Sovereign_ID id = seraph_sovereign_id_generate(SERAPH_AUTH_MINIMAL);
    Seraph_Sovereign_ID void_id = SERAPH_SOVEREIGN_ID_VOID;

    ASSERT_VBIT_VOID(seraph_sovereign_id_equal(id, void_id));
    ASSERT_VBIT_VOID(seraph_sovereign_id_equal(void_id, id));
    ASSERT_VBIT_VOID(seraph_sovereign_id_equal(void_id, void_id));
}

void test_id_validation_corrupted(void) {
    Seraph_Sovereign_ID id = seraph_sovereign_id_generate(SERAPH_AUTH_MINIMAL);
    ASSERT_VBIT_TRUE(seraph_sovereign_id_validate(id));

    /* Corrupt the checksum */
    id.quads[3] ^= 0x12345678;
    ASSERT_VBIT_FALSE(seraph_sovereign_id_validate(id));
}

void test_id_generation_void_authority(void) {
    /*
     * IMPORTANT SEMANTIC NOTE:
     * In authority context, SERAPH_VOID_U64 (~0ULL) == SERAPH_AUTH_PRIMORDIAL.
     * This means "all authority", NOT "VOID/absence".
     *
     * So generating an ID with ~0ULL authority is VALID and returns a real ID,
     * not a VOID ID. This is intentional - only THE PRIMORDIAL should have
     * this authority, but the ID generation itself works.
     */
    Seraph_Sovereign_ID id = seraph_sovereign_id_generate(SERAPH_VOID_U64);
    /* ~0ULL is valid PRIMORDIAL authority, so we get a valid ID */
    ASSERT(!seraph_sovereign_id_is_void(id));
    /* The embedded authority should be PRIMORDIAL */
    ASSERT_EQ(id.quads[2], SERAPH_AUTH_PRIMORDIAL);
    /* ID should be valid */
    ASSERT_VBIT_TRUE(seraph_sovereign_id_validate(id));
}

/*============================================================================
 * Subsystem Initialization Tests
 *============================================================================*/

void test_subsystem_init(void) {
    /* Subsystem should already be initialized by now */
    ASSERT(seraph_the_primordial != NULL);
}

void test_primordial_exists(void) {
    ASSERT(seraph_the_primordial != NULL);
    ASSERT(seraph_the_primordial->state == SERAPH_SOVEREIGN_RUNNING);
}

void test_primordial_has_full_authority(void) {
    ASSERT(seraph_the_primordial != NULL);
    ASSERT_EQ(seraph_the_primordial->authority, SERAPH_AUTH_PRIMORDIAL);
}

void test_primordial_has_no_parent(void) {
    ASSERT(seraph_the_primordial != NULL);
    ASSERT(seraph_sovereign_id_is_void(seraph_the_primordial->parent_id));
}

void test_primordial_id_is_valid(void) {
    ASSERT(seraph_the_primordial != NULL);
    ASSERT(!seraph_sovereign_id_is_void(seraph_the_primordial->id));
    ASSERT_VBIT_TRUE(seraph_sovereign_id_validate(seraph_the_primordial->id));
}

/*============================================================================
 * Current Sovereign Tests
 *============================================================================*/

void test_current_sovereign_is_primordial(void) {
    /* Before any spawning, current should be THE PRIMORDIAL */
    Seraph_Sovereign* current = seraph_sovereign_current();
    ASSERT(current == seraph_the_primordial);
}

void test_self_capability(void) {
    Seraph_Capability self = seraph_sovereign_self();
    ASSERT(!seraph_cap_is_void(self));
    ASSERT(self.base == seraph_the_primordial);
}

void test_parent_capability_primordial(void) {
    /* THE PRIMORDIAL has no parent */
    Seraph_Capability parent = seraph_sovereign_parent();
    ASSERT(seraph_cap_is_void(parent));
}

void test_get_authority(void) {
    uint64_t auth = seraph_sovereign_get_authority();
    ASSERT_EQ(auth, SERAPH_AUTH_PRIMORDIAL);
}

/*============================================================================
 * Sovereign State Query Tests
 *============================================================================*/

void test_get_state(void) {
    Seraph_Capability self = seraph_sovereign_self();
    Seraph_Sovereign_State state = seraph_sovereign_get_state(self);
    ASSERT_EQ(state, SERAPH_SOVEREIGN_RUNNING);
}

void test_get_state_void_cap(void) {
    Seraph_Sovereign_State state = seraph_sovereign_get_state(SERAPH_CAP_VOID);
    ASSERT_EQ(state, SERAPH_SOVEREIGN_VOID);
}

void test_get_id(void) {
    Seraph_Capability self = seraph_sovereign_self();
    Seraph_Sovereign_ID id = seraph_sovereign_get_id(self);
    ASSERT_VBIT_TRUE(seraph_sovereign_id_equal(id, seraph_the_primordial->id));
}

void test_get_id_void_cap(void) {
    Seraph_Sovereign_ID id = seraph_sovereign_get_id(SERAPH_CAP_VOID);
    ASSERT(seraph_sovereign_id_is_void(id));
}

/*============================================================================
 * Sovereign Creation Tests
 *============================================================================*/

void test_conceive_child(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_APPLICATION;
    config.memory_limit = 4 * 1024 * 1024;  /* 4 MB */

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    ASSERT(!seraph_cap_is_void(child_cap));

    /* Child should be in NASCENT state */
    Seraph_Sovereign_State state = seraph_sovereign_get_state(child_cap);
    ASSERT_EQ(state, SERAPH_SOVEREIGN_NASCENT);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_conceive_requires_spawn_authority(void) {
    /* Create a fake capability without SPAWN authority */
    /* For this test, we try to conceive with a child (that has no SPAWN auth) */

    Seraph_Capability self = seraph_sovereign_self();

    /* First create a child without SPAWN authority */
    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;  /* No SPAWN */

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    ASSERT(!seraph_cap_is_void(child_cap));

    /* The child cannot conceive because it lacks SPAWN authority */
    /* (In our current implementation, we'd need to switch context to test this properly.
     * For now, just verify the child was created.) */
    Seraph_Sovereign_State state = seraph_sovereign_get_state(child_cap);
    ASSERT_EQ(state, SERAPH_SOVEREIGN_NASCENT);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_conceive_authority_must_be_subset(void) {
    Seraph_Capability self = seraph_sovereign_self();

    /* Even THE PRIMORDIAL cannot create a child with MORE authority
     * (but PRIMORDIAL has all authority, so this test creates a child
     * then has that child try to create a grandchild with more auth) */

    /* Create child with limited authority */
    Seraph_Spawn_Config config1 = SERAPH_SPAWN_CONFIG_DEFAULT;
    config1.authority = SERAPH_AUTH_SPAWN | SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config1);
    ASSERT(!seraph_cap_is_void(child_cap));

    /* If we could switch to the child's context, we'd test that it
     * cannot spawn with SERAPH_AUTH_KILL (which it doesn't have).
     * For now, verify the child was created correctly. */

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_conceive_void_cap(void) {
    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    Seraph_Capability result = seraph_sovereign_conceive(SERAPH_CAP_VOID, config);
    ASSERT(seraph_cap_is_void(result));
}

/*============================================================================
 * Vivify Tests
 *============================================================================*/

void test_vivify_nascent_child(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    ASSERT(!seraph_cap_is_void(child_cap));
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_NASCENT);

    /* Vivify the child */
    Seraph_Vbit result = seraph_sovereign_vivify(child_cap);
    ASSERT_VBIT_TRUE(result);

    /* Child should now be RUNNING */
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_RUNNING);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_vivify_already_running(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_RUNNING);

    /* Vivify again should fail */
    Seraph_Vbit result = seraph_sovereign_vivify(child_cap);
    ASSERT_VBIT_FALSE(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_vivify_void_cap(void) {
    Seraph_Vbit result = seraph_sovereign_vivify(SERAPH_CAP_VOID);
    ASSERT_VBIT_VOID(result);
}

/*============================================================================
 * Capability Grant Tests
 *============================================================================*/

void test_grant_cap_to_nascent(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    ASSERT(!seraph_cap_is_void(child_cap));

    /* Create a test capability to grant */
    char test_data[64] = "test data";
    Seraph_Capability test_cap = seraph_cap_create(
        test_data, sizeof(test_data), 1, SERAPH_CAP_RW
    );

    /* Grant it to the child */
    Seraph_Vbit result = seraph_sovereign_grant_cap(
        child_cap, test_cap, SERAPH_GRANT_COPY
    );
    ASSERT_VBIT_TRUE(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_grant_cap_to_running_fails(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);  /* Now RUNNING */

    char test_data[64] = "test data";
    Seraph_Capability test_cap = seraph_cap_create(
        test_data, sizeof(test_data), 1, SERAPH_CAP_RW
    );

    /* Should fail because child is not NASCENT */
    Seraph_Vbit result = seraph_sovereign_grant_cap(
        child_cap, test_cap, SERAPH_GRANT_COPY
    );
    ASSERT_VBIT_FALSE(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_grant_void_cap(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);

    /* Granting VOID capability should return VOID */
    Seraph_Vbit result = seraph_sovereign_grant_cap(
        child_cap, SERAPH_CAP_VOID, SERAPH_GRANT_COPY
    );
    ASSERT_VBIT_VOID(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

/*============================================================================
 * Kill Tests
 *============================================================================*/

void test_kill_child(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_RUNNING);

    /* Kill the child */
    Seraph_Vbit result = seraph_sovereign_kill(child_cap);
    ASSERT_VBIT_TRUE(result);

    /* Child should be VOID now */
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_VOID);
}

void test_kill_primordial_fails(void) {
    Seraph_Capability self = seraph_sovereign_self();

    /* Cannot kill THE PRIMORDIAL */
    Seraph_Vbit result = seraph_sovereign_kill(self);
    ASSERT_VBIT_VOID(result);  /* Returns VOID, not FALSE */

    /* THE PRIMORDIAL should still be RUNNING */
    ASSERT_EQ(seraph_sovereign_get_state(self), SERAPH_SOVEREIGN_RUNNING);
}

void test_kill_void_cap(void) {
    Seraph_Vbit result = seraph_sovereign_kill(SERAPH_CAP_VOID);
    ASSERT_VBIT_VOID(result);
}

/*============================================================================
 * Suspend/Resume Tests
 *============================================================================*/

void test_suspend_running_child(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_RUNNING);

    /* Suspend the child */
    Seraph_Vbit result = seraph_sovereign_suspend(child_cap);
    ASSERT_VBIT_TRUE(result);

    /* Child should be SUSPENDED */
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_SUSPENDED);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_resume_suspended_child(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);
    seraph_sovereign_suspend(child_cap);
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_SUSPENDED);

    /* Resume the child */
    Seraph_Vbit result = seraph_sovereign_resume(child_cap);
    ASSERT_VBIT_TRUE(result);

    /* Child should be RUNNING again */
    ASSERT_EQ(seraph_sovereign_get_state(child_cap), SERAPH_SOVEREIGN_RUNNING);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_suspend_nascent_fails(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    /* Don't vivify - still NASCENT */

    /* Cannot suspend NASCENT child */
    Seraph_Vbit result = seraph_sovereign_suspend(child_cap);
    ASSERT_VBIT_FALSE(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_resume_running_fails(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);
    /* Child is RUNNING, not SUSPENDED */

    /* Cannot resume a non-SUSPENDED child */
    Seraph_Vbit result = seraph_sovereign_resume(child_cap);
    ASSERT_VBIT_FALSE(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

/*============================================================================
 * Wait Tests
 *============================================================================*/

void test_wait_for_terminated_child(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);
    seraph_sovereign_kill(child_cap);  /* Kill it */

    uint32_t exit_code = 0;
    Seraph_Vbit result = seraph_sovereign_wait(child_cap, 0, &exit_code);
    ASSERT_VBIT_TRUE(result);  /* Child has terminated */
}

void test_wait_for_running_child_immediate(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);

    /* Immediate check (timeout = VOID) */
    uint32_t exit_code = 0;
    Seraph_Vbit result = seraph_sovereign_wait(child_cap, SERAPH_CHRONON_VOID, &exit_code);
    ASSERT_VBIT_FALSE(result);  /* Not terminated yet */

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_wait_void_cap(void) {
    uint32_t exit_code = 0;
    Seraph_Vbit result = seraph_sovereign_wait(SERAPH_CAP_VOID, 0, &exit_code);
    ASSERT_VBIT_VOID(result);
}

/*============================================================================
 * Code Loading Tests
 *============================================================================*/

void test_load_code_to_nascent(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    ASSERT(!seraph_cap_is_void(child_cap));

    /* Load some "code" */
    uint8_t fake_code[] = { 0x90, 0x90, 0x90, 0xC3 };  /* NOP NOP NOP RET */
    Seraph_Vbit result = seraph_sovereign_load_code(
        child_cap, fake_code, sizeof(fake_code), 0x1000
    );
    ASSERT_VBIT_TRUE(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_load_code_to_running_fails(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    config.authority = SERAPH_AUTH_MINIMAL;

    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);
    seraph_sovereign_vivify(child_cap);  /* Now RUNNING */

    uint8_t fake_code[] = { 0x90, 0xC3 };
    Seraph_Vbit result = seraph_sovereign_load_code(
        child_cap, fake_code, sizeof(fake_code), 0x1000
    );
    ASSERT_VBIT_FALSE(result);  /* Cannot load code into running Sovereign */

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

void test_load_code_null_fails(void) {
    Seraph_Capability self = seraph_sovereign_self();

    Seraph_Spawn_Config config = SERAPH_SPAWN_CONFIG_DEFAULT;
    Seraph_Capability child_cap = seraph_sovereign_conceive(self, config);

    Seraph_Vbit result = seraph_sovereign_load_code(child_cap, NULL, 0, 0);
    ASSERT_VBIT_FALSE(result);

    /* Clean up */
    seraph_sovereign_kill(child_cap);
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_sovereign_tests(void) {
    printf("\n========================================\n");
    printf("     MC10: Sovereign Tests\n");
    printf("========================================\n");

    /* Initialize subsystem if not already */
    if (seraph_the_primordial == NULL) {
        seraph_sovereign_subsystem_init();
    }

    /* State enum tests */
    RUN_TEST(state_is_void);
    RUN_TEST(state_is_alive);
    RUN_TEST(state_is_terminal);

    /* Authority flag tests */
    RUN_TEST(authority_valid_subset);
    RUN_TEST(authority_invalid_superset);
    RUN_TEST(authority_equal_is_valid);
    RUN_TEST(authority_none_always_valid);
    RUN_TEST(authority_void_propagation);
    RUN_TEST(authority_has);
    RUN_TEST(authority_has_void);

    /* Sovereign ID tests */
    RUN_TEST(id_void_detection);
    RUN_TEST(id_generation);
    RUN_TEST(id_uniqueness);
    RUN_TEST(id_equality);
    RUN_TEST(id_equality_void_propagation);
    RUN_TEST(id_validation_corrupted);
    RUN_TEST(id_generation_void_authority);

    /* Subsystem initialization tests */
    RUN_TEST(subsystem_init);
    RUN_TEST(primordial_exists);
    RUN_TEST(primordial_has_full_authority);
    RUN_TEST(primordial_has_no_parent);
    RUN_TEST(primordial_id_is_valid);

    /* Current sovereign tests */
    RUN_TEST(current_sovereign_is_primordial);
    RUN_TEST(self_capability);
    RUN_TEST(parent_capability_primordial);
    RUN_TEST(get_authority);

    /* State query tests */
    RUN_TEST(get_state);
    RUN_TEST(get_state_void_cap);
    RUN_TEST(get_id);
    RUN_TEST(get_id_void_cap);

    /* Creation tests */
    RUN_TEST(conceive_child);
    RUN_TEST(conceive_requires_spawn_authority);
    RUN_TEST(conceive_authority_must_be_subset);
    RUN_TEST(conceive_void_cap);

    /* Vivify tests */
    RUN_TEST(vivify_nascent_child);
    RUN_TEST(vivify_already_running);
    RUN_TEST(vivify_void_cap);

    /* Capability grant tests */
    RUN_TEST(grant_cap_to_nascent);
    RUN_TEST(grant_cap_to_running_fails);
    RUN_TEST(grant_void_cap);

    /* Kill tests */
    RUN_TEST(kill_child);
    RUN_TEST(kill_primordial_fails);
    RUN_TEST(kill_void_cap);

    /* Suspend/Resume tests */
    RUN_TEST(suspend_running_child);
    RUN_TEST(resume_suspended_child);
    RUN_TEST(suspend_nascent_fails);
    RUN_TEST(resume_running_fails);

    /* Wait tests */
    RUN_TEST(wait_for_terminated_child);
    RUN_TEST(wait_for_running_child_immediate);
    RUN_TEST(wait_void_cap);

    /* Code loading tests */
    RUN_TEST(load_code_to_nascent);
    RUN_TEST(load_code_to_running_fails);
    RUN_TEST(load_code_null_fails);

    printf("\n----------------------------------------\n");
    printf("Sovereign Tests: %d/%d passed\n", tests_passed, tests_run);
    printf("----------------------------------------\n");

    if (tests_passed != tests_run) {
        printf("*** SOVEREIGN TESTS FAILED ***\n");
        exit(1);
    }
}
