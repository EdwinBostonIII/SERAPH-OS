/**
 * @file test_whisper.c
 * @brief Tests for MC12: Whisper - Capability-Based Zero-Copy IPC
 */

#include "seraph/whisper.h"
#include "seraph/capability.h"
#include <stdio.h>
#include <string.h>

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s...", #name); \
    name(); \
    tests_run++; \
    tests_passed++; \
    printf(" PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED\n    Assertion failed: %s\n    At: %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_TRUE(x) ASSERT(x)
#define ASSERT_FALSE(x) ASSERT(!(x))

/*============================================================================
 * Static Storage for Large Structures
 *
 * Seraph_Whisper_Channel is 77KB - too large for stack allocation.
 * Use static storage to avoid stack overflow on Windows.
 *============================================================================*/
static Seraph_Whisper_Channel s_channel1;
static Seraph_Whisper_Channel s_channel2;

/*============================================================================
 * Message Tests
 *============================================================================*/

TEST(test_message_new) {
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_REQUEST);

    ASSERT_FALSE(seraph_whisper_message_is_void(msg));
    ASSERT_TRUE(seraph_whisper_message_exists(msg));
    ASSERT_EQ(msg.type, SERAPH_WHISPER_REQUEST);
    ASSERT_EQ(msg.cap_count, 0);
    ASSERT_NE(msg.message_id, SERAPH_VOID_U64);
}

TEST(test_message_void) {
    Seraph_Whisper_Message msg = SERAPH_WHISPER_MESSAGE_VOID;

    ASSERT_TRUE(seraph_whisper_message_is_void(msg));
    ASSERT_FALSE(seraph_whisper_message_exists(msg));
    ASSERT_EQ(msg.type, SERAPH_WHISPER_VOID);
}

TEST(test_message_unique_ids) {
    Seraph_Whisper_Message msg1 = seraph_whisper_message_new(SERAPH_WHISPER_REQUEST);
    Seraph_Whisper_Message msg2 = seraph_whisper_message_new(SERAPH_WHISPER_REQUEST);
    Seraph_Whisper_Message msg3 = seraph_whisper_message_new(SERAPH_WHISPER_REQUEST);

    ASSERT_NE(msg1.message_id, msg2.message_id);
    ASSERT_NE(msg2.message_id, msg3.message_id);
    ASSERT_NE(msg1.message_id, msg3.message_id);
}

TEST(test_message_add_cap) {
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_GRANT);

    uint8_t data[64] = {0};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    Seraph_Vbit result = seraph_whisper_message_add_cap(&msg, cap);
    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_EQ(msg.cap_count, 1);
}

TEST(test_message_add_multiple_caps) {
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_REQUEST);

    uint8_t data[64] = {0};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Add 7 capabilities (max allowed) */
    for (int i = 0; i < SERAPH_WHISPER_MAX_CAPS; i++) {
        Seraph_Vbit result = seraph_whisper_message_add_cap(&msg, cap);
        ASSERT_TRUE(seraph_vbit_is_true(result));
    }

    ASSERT_EQ(msg.cap_count, SERAPH_WHISPER_MAX_CAPS);

    /* 8th capability should fail */
    Seraph_Vbit result = seraph_whisper_message_add_cap(&msg, cap);
    ASSERT_TRUE(seraph_vbit_is_false(result));
    ASSERT_EQ(msg.cap_count, SERAPH_WHISPER_MAX_CAPS);
}

TEST(test_message_get_cap) {
    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_GRANT);

    uint8_t data1[32] = {1};
    uint8_t data2[64] = {2};

    Seraph_Capability cap1 = seraph_cap_create(data1, sizeof(data1), 1, SERAPH_CAP_READ);
    Seraph_Capability cap2 = seraph_cap_create(data2, sizeof(data2), 2, SERAPH_CAP_RW);

    seraph_whisper_message_add_cap(&msg, cap1);
    seraph_whisper_message_add_cap(&msg, cap2);

    Seraph_Capability retrieved1 = seraph_whisper_message_get_cap(&msg, 0);
    Seraph_Capability retrieved2 = seraph_whisper_message_get_cap(&msg, 1);
    Seraph_Capability retrieved_invalid = seraph_whisper_message_get_cap(&msg, 2);

    ASSERT_FALSE(seraph_cap_is_void(retrieved1));
    ASSERT_FALSE(seraph_cap_is_void(retrieved2));
    ASSERT_TRUE(seraph_cap_is_void(retrieved_invalid));
}

TEST(test_message_size) {
    /* Verify message is exactly 256 bytes */
    ASSERT_EQ(sizeof(Seraph_Whisper_Message), 256);
}

/*============================================================================
 * Channel Tests
 *============================================================================*/

TEST(test_channel_create) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    ASSERT_FALSE(seraph_whisper_channel_is_void(channel));
    ASSERT_TRUE(seraph_whisper_channel_is_active(channel));
    ASSERT_NE(channel->channel_id, SERAPH_VOID_U64);
}

TEST(test_channel_unique_ids) {
    Seraph_Whisper_Channel* ch1 = &s_channel1;
    Seraph_Whisper_Channel* ch2 = &s_channel2;
    seraph_whisper_channel_init(ch1);
    seraph_whisper_channel_init(ch2);

    ASSERT_NE(ch1->channel_id, ch2->channel_id);
}

TEST(test_channel_init) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    memset(channel, 0, sizeof(*channel));

    Seraph_Vbit result = seraph_whisper_channel_init(channel);

    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_TRUE(seraph_whisper_channel_is_active(channel));
}

TEST(test_channel_init_null) {
    Seraph_Vbit result = seraph_whisper_channel_init(NULL);
    ASSERT_TRUE(seraph_vbit_is_void(result));
}

TEST(test_channel_close) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);
    ASSERT_TRUE(seraph_whisper_channel_is_active(channel));

    Seraph_Vbit result = seraph_whisper_channel_close(channel);

    ASSERT_TRUE(seraph_vbit_is_true(result));
    ASSERT_FALSE(seraph_whisper_channel_is_active(channel));
}

TEST(test_channel_close_twice) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    seraph_whisper_channel_close(channel);
    Seraph_Vbit result = seraph_whisper_channel_close(channel);

    /* Second close returns VOID (already closed) */
    ASSERT_TRUE(seraph_vbit_is_void(result));
}

TEST(test_channel_destroy) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);
    uint64_t old_generation = channel->generation;

    seraph_whisper_channel_destroy(channel);

    ASSERT_TRUE(seraph_whisper_channel_is_void(channel));
    ASSERT_NE(channel->generation, old_generation);  /* Generation incremented */
}

TEST(test_channel_get_cap) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    Seraph_Capability parent_cap = seraph_whisper_channel_get_cap(channel, false);
    Seraph_Capability child_cap = seraph_whisper_channel_get_cap(channel, true);

    ASSERT_FALSE(seraph_cap_is_void(parent_cap));
    ASSERT_FALSE(seraph_cap_is_void(child_cap));
}

/*============================================================================
 * Send/Receive Tests
 *============================================================================*/

TEST(test_send_basic) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION);

    Seraph_Vbit result = seraph_whisper_send(&channel->parent_end, msg);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    Seraph_Whisper_Stats stats = seraph_whisper_get_stats(&channel->parent_end);
    ASSERT_EQ(stats.total_sent, 1);
}

TEST(test_send_receive_roundtrip) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    /* Parent sends */
    Seraph_Whisper_Message sent = seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION);
    uint64_t sent_id = sent.message_id;

    seraph_whisper_send(&channel->parent_end, sent);

    /* Transfer messages between endpoints */
    uint32_t transferred = seraph_whisper_channel_transfer(channel);
    ASSERT_EQ(transferred, 1);

    /* Child receives */
    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);

    ASSERT_FALSE(seraph_whisper_message_is_void(received));
    ASSERT_EQ(received.message_id, sent_id);
    ASSERT_EQ(received.type, SERAPH_WHISPER_NOTIFICATION);
}

TEST(test_bidirectional_communication) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    /* Parent sends to child */
    Seraph_Whisper_Message msg1 = seraph_whisper_message_new(SERAPH_WHISPER_REQUEST);
    seraph_whisper_send(&channel->parent_end, msg1);

    /* Child sends to parent */
    Seraph_Whisper_Message msg2 = seraph_whisper_message_new(SERAPH_WHISPER_RESPONSE);
    seraph_whisper_send(&channel->child_end, msg2);

    /* Transfer both directions */
    seraph_whisper_channel_transfer(channel);

    /* Both should receive */
    Seraph_Whisper_Message recv1 = seraph_whisper_recv(&channel->child_end, false);
    Seraph_Whisper_Message recv2 = seraph_whisper_recv(&channel->parent_end, false);

    ASSERT_FALSE(seraph_whisper_message_is_void(recv1));
    ASSERT_FALSE(seraph_whisper_message_is_void(recv2));
    ASSERT_EQ(recv1.type, SERAPH_WHISPER_REQUEST);
    ASSERT_EQ(recv2.type, SERAPH_WHISPER_RESPONSE);
}

TEST(test_recv_empty_nonblocking) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    /* Non-blocking receive on empty queue should return VOID */
    Seraph_Whisper_Message msg = seraph_whisper_recv(&channel->parent_end, false);
    ASSERT_TRUE(seraph_whisper_message_is_void(msg));
}

TEST(test_peek) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    Seraph_Whisper_Message sent = seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION);
    uint64_t sent_id = sent.message_id;

    seraph_whisper_send(&channel->parent_end, sent);
    seraph_whisper_channel_transfer(channel);

    /* Peek should show message without removing */
    Seraph_Whisper_Message peeked = seraph_whisper_peek(&channel->child_end);
    ASSERT_EQ(peeked.message_id, sent_id);

    /* Peek again - same message */
    Seraph_Whisper_Message peeked2 = seraph_whisper_peek(&channel->child_end);
    ASSERT_EQ(peeked2.message_id, sent_id);

    /* Receive removes it */
    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    ASSERT_EQ(received.message_id, sent_id);

    /* Now peek returns VOID */
    Seraph_Whisper_Message peeked3 = seraph_whisper_peek(&channel->child_end);
    ASSERT_TRUE(seraph_whisper_message_is_void(peeked3));
}

TEST(test_available) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    /* Initially empty */
    ASSERT_TRUE(seraph_vbit_is_false(seraph_whisper_available(&channel->child_end)));

    /* Send and transfer */
    seraph_whisper_send(&channel->parent_end, seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION));
    seraph_whisper_channel_transfer(channel);

    /* Now available */
    ASSERT_TRUE(seraph_vbit_is_true(seraph_whisper_available(&channel->child_end)));
}

TEST(test_pending_count) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    ASSERT_EQ(seraph_whisper_pending_count(&channel->child_end), 0);

    /* Send 3 messages */
    seraph_whisper_send(&channel->parent_end, seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION));
    seraph_whisper_send(&channel->parent_end, seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION));
    seraph_whisper_send(&channel->parent_end, seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION));
    seraph_whisper_channel_transfer(channel);

    ASSERT_EQ(seraph_whisper_pending_count(&channel->child_end), 3);

    /* Receive one */
    seraph_whisper_recv(&channel->child_end, false);
    ASSERT_EQ(seraph_whisper_pending_count(&channel->child_end), 2);
}

/*============================================================================
 * Grant/Lend/Return Tests
 *============================================================================*/

TEST(test_grant) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    Seraph_Vbit result = seraph_whisper_grant(&channel->parent_end, cap);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    ASSERT_EQ(received.type, SERAPH_WHISPER_GRANT);
    ASSERT_EQ(received.cap_count, 1);

    Seraph_Capability received_cap = seraph_whisper_message_get_cap(&received, 0);
    ASSERT_FALSE(seraph_cap_is_void(received_cap));
}

TEST(test_lend) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    Seraph_Vbit result = seraph_whisper_lend(&channel->parent_end, cap, 1000);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    ASSERT_EQ(received.type, SERAPH_WHISPER_LEND);
    ASSERT_EQ(received.lend_timeout, 1000);
    ASSERT_TRUE(received.flags & SERAPH_WHISPER_FLAG_BORROWED);
}

TEST(test_return_cap) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    Seraph_Vbit result = seraph_whisper_return_cap(&channel->child_end, cap);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->parent_end, false);
    ASSERT_EQ(received.type, SERAPH_WHISPER_RETURN);
}

/*============================================================================
 * Lend Tracking Tests (LEND semantics with registry)
 *============================================================================*/

TEST(test_lend_creates_registry_entry) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Before lend: no active lends */
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 0);

    /* Lend the capability */
    Seraph_Vbit result = seraph_whisper_lend(&channel->parent_end, cap, 1000);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    /* After lend: one active lend */
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 1);
}

TEST(test_lend_registry_tracks_message_id) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Lend and transfer */
    seraph_whisper_lend(&channel->parent_end, cap, 1000);
    seraph_whisper_channel_transfer(channel);

    /* Get the message to find its ID */
    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    uint64_t msg_id = received.message_id;

    /* Lend record should exist with this message ID */
    Seraph_Vbit is_active = seraph_whisper_lend_is_active(&channel->parent_end, msg_id);
    ASSERT_TRUE(seraph_vbit_is_true(is_active));
}

TEST(test_lend_expiration) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Lend with timeout of 100 chronons */
    seraph_whisper_lend(&channel->parent_end, cap, 100);
    seraph_whisper_channel_transfer(channel);

    /* Get message ID */
    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    uint64_t msg_id = received.message_id;

    /* Before expiration: lend is active */
    ASSERT_TRUE(seraph_vbit_is_true(seraph_whisper_lend_is_active(&channel->parent_end, msg_id)));
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 1);

    /* Process lends with time past expiration (current_chronon = 200) */
    uint32_t expired = seraph_whisper_process_lends(&channel->parent_end, 200);
    ASSERT_EQ(expired, 1);

    /* After expiration: lend is no longer active */
    ASSERT_TRUE(seraph_vbit_is_false(seraph_whisper_lend_is_active(&channel->parent_end, msg_id)));
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 0);

    /* Check the record status */
    Seraph_Whisper_Lend_Record* record = seraph_whisper_get_lend_record(&channel->parent_end, msg_id);
    ASSERT_NE(record, NULL);
    ASSERT_EQ(record->status, SERAPH_LEND_STATUS_EXPIRED);
}

TEST(test_lend_manual_revocation) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Lend with long timeout */
    seraph_whisper_lend(&channel->parent_end, cap, 10000);
    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    uint64_t msg_id = received.message_id;

    /* Lend is active */
    ASSERT_TRUE(seraph_vbit_is_true(seraph_whisper_lend_is_active(&channel->parent_end, msg_id)));

    /* Manually revoke */
    Seraph_Vbit result = seraph_whisper_revoke_lend(&channel->parent_end, msg_id);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    /* Lend is no longer active */
    ASSERT_TRUE(seraph_vbit_is_false(seraph_whisper_lend_is_active(&channel->parent_end, msg_id)));
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 0);

    /* Record shows REVOKED status */
    Seraph_Whisper_Lend_Record* record = seraph_whisper_get_lend_record(&channel->parent_end, msg_id);
    ASSERT_NE(record, NULL);
    ASSERT_EQ(record->status, SERAPH_LEND_STATUS_REVOKED);
}

TEST(test_lend_early_return) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Parent lends to child */
    seraph_whisper_lend(&channel->parent_end, cap, 10000);
    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message lend_msg = seraph_whisper_recv(&channel->child_end, false);
    uint64_t lend_id = lend_msg.message_id;
    Seraph_Capability borrowed = seraph_whisper_message_get_cap(&lend_msg, 0);

    /* Lend is active */
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 1);

    /* Child returns the capability early using the lend message ID */
    seraph_whisper_return_cap_by_id(&channel->child_end, borrowed, lend_id);
    seraph_whisper_channel_transfer(channel);

    /* The transfer should have called handle_return, marking lend as returned */
    ASSERT_TRUE(seraph_vbit_is_false(seraph_whisper_lend_is_active(&channel->parent_end, lend_id)));
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 0);

    /* Record shows RETURNED status */
    Seraph_Whisper_Lend_Record* record = seraph_whisper_get_lend_record(&channel->parent_end, lend_id);
    ASSERT_NE(record, NULL);
    ASSERT_EQ(record->status, SERAPH_LEND_STATUS_RETURNED);
}

TEST(test_lend_multiple_concurrent) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data1[32] = {0x01};
    uint8_t data2[32] = {0x02};
    uint8_t data3[32] = {0x03};

    Seraph_Capability cap1 = seraph_cap_create(data1, sizeof(data1), 1, SERAPH_CAP_RW);
    Seraph_Capability cap2 = seraph_cap_create(data2, sizeof(data2), 1, SERAPH_CAP_RW);
    Seraph_Capability cap3 = seraph_cap_create(data3, sizeof(data3), 1, SERAPH_CAP_RW);

    /* Lend all three */
    seraph_whisper_lend(&channel->parent_end, cap1, 1000);
    seraph_whisper_lend(&channel->parent_end, cap2, 2000);
    seraph_whisper_lend(&channel->parent_end, cap3, 3000);

    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 3);

    /* Process lends at time 1500 - only first should expire */
    uint32_t expired = seraph_whisper_process_lends(&channel->parent_end, 1500);
    ASSERT_EQ(expired, 1);
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 2);

    /* Process lends at time 2500 - second should expire */
    expired = seraph_whisper_process_lends(&channel->parent_end, 2500);
    ASSERT_EQ(expired, 1);
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 1);

    /* Process lends at time 3500 - third should expire */
    expired = seraph_whisper_process_lends(&channel->parent_end, 3500);
    ASSERT_EQ(expired, 1);
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 0);
}

TEST(test_lend_revoke_nonexistent) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    /* Try to revoke a non-existent lend */
    Seraph_Vbit result = seraph_whisper_revoke_lend(&channel->parent_end, 99999);
    ASSERT_TRUE(seraph_vbit_is_false(result));
}

TEST(test_lend_is_active_void_input) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    /* VOID message ID returns VOID */
    Seraph_Vbit result = seraph_whisper_lend_is_active(&channel->parent_end, SERAPH_VOID_U64);
    ASSERT_TRUE(seraph_vbit_is_void(result));

    /* NULL endpoint returns VOID */
    result = seraph_whisper_lend_is_active(NULL, 12345);
    ASSERT_TRUE(seraph_vbit_is_void(result));
}

TEST(test_lend_get_record) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    seraph_whisper_lend(&channel->parent_end, cap, 5000);
    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    uint64_t msg_id = received.message_id;

    /* Get the lend record */
    Seraph_Whisper_Lend_Record* record = seraph_whisper_get_lend_record(&channel->parent_end, msg_id);
    ASSERT_NE(record, NULL);
    ASSERT_EQ(record->lend_message_id, msg_id);
    ASSERT_EQ(record->status, SERAPH_LEND_STATUS_ACTIVE);
    ASSERT_EQ(record->expiry_chronon, 5000);  /* timeout becomes expiry when lend_chronon is 0 */
}

TEST(test_lend_handle_return_by_cap_match) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Parent lends to child */
    seraph_whisper_lend(&channel->parent_end, cap, 10000);
    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message lend_msg = seraph_whisper_recv(&channel->child_end, false);
    Seraph_Capability borrowed = seraph_whisper_message_get_cap(&lend_msg, 0);

    /* Child returns using basic return (no message ID) */
    seraph_whisper_return_cap(&channel->child_end, borrowed);
    seraph_whisper_channel_transfer(channel);

    /* Should match by capability base address */
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 0);
}

TEST(test_lend_no_expiry_with_zero_timeout) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0x42};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    /* Lend with timeout 0 = never expires */
    seraph_whisper_lend(&channel->parent_end, cap, 0);
    seraph_whisper_channel_transfer(channel);

    /* Process lends at far future time - should NOT expire */
    uint32_t expired = seraph_whisper_process_lends(&channel->parent_end, 999999999);
    ASSERT_EQ(expired, 0);
    ASSERT_EQ(seraph_whisper_active_lend_count(&channel->parent_end), 1);
}

/*============================================================================
 * Request/Response Tests
 *============================================================================*/

TEST(test_request) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0};
    Seraph_Capability caps[2] = {
        seraph_cap_create(data, 32, 1, SERAPH_CAP_READ),
        seraph_cap_create(data + 32, 32, 1, SERAPH_CAP_RW)
    };

    uint64_t request_id = seraph_whisper_request(
        &channel->parent_end, caps, 2,
        SERAPH_WHISPER_FLAG_URGENT
    );

    ASSERT_NE(request_id, SERAPH_VOID_U64);

    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    ASSERT_EQ(received.type, SERAPH_WHISPER_REQUEST);
    ASSERT_EQ(received.cap_count, 2);
    ASSERT_TRUE(received.flags & SERAPH_WHISPER_FLAG_URGENT);
    ASSERT_TRUE(received.flags & SERAPH_WHISPER_FLAG_REPLY_REQ);
}

TEST(test_respond) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_RW);

    Seraph_Vbit result = seraph_whisper_respond(&channel->child_end, 12345, &cap, 1);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->parent_end, false);
    ASSERT_EQ(received.type, SERAPH_WHISPER_RESPONSE);
    ASSERT_EQ(received.cap_count, 1);
}

TEST(test_notify) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    uint8_t data[64] = {0};
    Seraph_Capability cap = seraph_cap_create(data, sizeof(data), 1, SERAPH_CAP_READ);

    Seraph_Vbit result = seraph_whisper_notify(&channel->parent_end, &cap, 1);
    ASSERT_TRUE(seraph_vbit_is_true(result));

    seraph_whisper_channel_transfer(channel);

    Seraph_Whisper_Message received = seraph_whisper_recv(&channel->child_end, false);
    ASSERT_EQ(received.type, SERAPH_WHISPER_NOTIFICATION);
}

/*============================================================================
 * Statistics Tests
 *============================================================================*/

TEST(test_statistics) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);

    /* Initial stats */
    Seraph_Whisper_Stats stats = seraph_whisper_get_stats(&channel->parent_end);
    ASSERT_EQ(stats.total_sent, 0);
    ASSERT_EQ(stats.total_received, 0);
    ASSERT_TRUE(stats.connected);

    /* Send some messages */
    seraph_whisper_send(&channel->parent_end, seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION));
    seraph_whisper_send(&channel->parent_end, seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION));

    stats = seraph_whisper_get_stats(&channel->parent_end);
    ASSERT_EQ(stats.total_sent, 2);
}

/*============================================================================
 * Error Handling Tests
 *============================================================================*/

TEST(test_send_to_closed_channel) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);
    seraph_whisper_channel_close(channel);

    Seraph_Whisper_Message msg = seraph_whisper_message_new(SERAPH_WHISPER_NOTIFICATION);
    Seraph_Vbit result = seraph_whisper_send(&channel->parent_end, msg);

    ASSERT_TRUE(seraph_vbit_is_void(result));
}

TEST(test_recv_from_closed_channel) {
    Seraph_Whisper_Channel* channel = &s_channel1;
    seraph_whisper_channel_init(channel);
    seraph_whisper_channel_close(channel);

    Seraph_Whisper_Message msg = seraph_whisper_recv(&channel->parent_end, false);
    ASSERT_TRUE(seraph_whisper_message_is_void(msg));
}

TEST(test_null_endpoint) {
    ASSERT_TRUE(seraph_vbit_is_void(seraph_whisper_available(NULL)));
    ASSERT_EQ(seraph_whisper_pending_count(NULL), 0);
    ASSERT_TRUE(seraph_whisper_message_is_void(seraph_whisper_peek(NULL)));
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_whisper_tests(void) {
    printf("\n========================================\n");
    printf("     MC12: Whisper Tests\n");
    printf("========================================\n");

    /* Message tests */
    printf("\nMessage Tests:\n");
    RUN_TEST(test_message_new);
    RUN_TEST(test_message_void);
    RUN_TEST(test_message_unique_ids);
    RUN_TEST(test_message_add_cap);
    RUN_TEST(test_message_add_multiple_caps);
    RUN_TEST(test_message_get_cap);
    RUN_TEST(test_message_size);

    /* Channel tests */
    printf("\nChannel Tests:\n");
    RUN_TEST(test_channel_create);
    RUN_TEST(test_channel_unique_ids);
    RUN_TEST(test_channel_init);
    RUN_TEST(test_channel_init_null);
    RUN_TEST(test_channel_close);
    RUN_TEST(test_channel_close_twice);
    RUN_TEST(test_channel_destroy);
    RUN_TEST(test_channel_get_cap);

    /* Send/Receive tests */
    printf("\nSend/Receive Tests:\n");
    RUN_TEST(test_send_basic);
    RUN_TEST(test_send_receive_roundtrip);
    RUN_TEST(test_bidirectional_communication);
    RUN_TEST(test_recv_empty_nonblocking);
    RUN_TEST(test_peek);
    RUN_TEST(test_available);
    RUN_TEST(test_pending_count);

    /* Grant/Lend/Return tests */
    printf("\nGrant/Lend/Return Tests:\n");
    RUN_TEST(test_grant);
    RUN_TEST(test_lend);
    RUN_TEST(test_return_cap);

    /* Lend Tracking tests */
    printf("\nLend Tracking Tests (LEND semantics):\n");
    RUN_TEST(test_lend_creates_registry_entry);
    RUN_TEST(test_lend_registry_tracks_message_id);
    RUN_TEST(test_lend_expiration);
    RUN_TEST(test_lend_manual_revocation);
    RUN_TEST(test_lend_early_return);
    RUN_TEST(test_lend_multiple_concurrent);
    RUN_TEST(test_lend_revoke_nonexistent);
    RUN_TEST(test_lend_is_active_void_input);
    RUN_TEST(test_lend_get_record);
    RUN_TEST(test_lend_handle_return_by_cap_match);
    RUN_TEST(test_lend_no_expiry_with_zero_timeout);

    /* Request/Response tests */
    printf("\nRequest/Response Tests:\n");
    RUN_TEST(test_request);
    RUN_TEST(test_respond);
    RUN_TEST(test_notify);

    /* Statistics tests */
    printf("\nStatistics Tests:\n");
    RUN_TEST(test_statistics);

    /* Error handling tests */
    printf("\nError Handling Tests:\n");
    RUN_TEST(test_send_to_closed_channel);
    RUN_TEST(test_recv_from_closed_channel);
    RUN_TEST(test_null_endpoint);

    printf("\n----------------------------------------\n");
    printf("Whisper Tests: %d/%d passed\n", tests_passed, tests_run);
    printf("----------------------------------------\n");
}
