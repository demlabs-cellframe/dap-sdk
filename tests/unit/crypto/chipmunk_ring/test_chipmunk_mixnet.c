/*
 * test_chipmunk_mixnet.c — Mixnet batching and DC-net tests.
 *
 * Tests: batch init/add/shuffle, DC-net init/generate/combine.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_mixnet.h"

#define LOG_TAG "test_chipmunk_mixnet"

static void test_batch_init(void)
{
    chipmunk_mixnet_batch_t l_batch;
    int l_rc = chipmunk_mixnet_batch_init(&l_batch, 16);
    dap_assert(l_rc == 0, "batch init OK");
    dap_assert(l_batch.count == 0, "batch count = 0");
    dap_assert(l_batch.capacity == 16, "batch capacity = 16");
    dap_assert(!l_batch.finalized, "batch not finalized");
    chipmunk_mixnet_batch_free(&l_batch);
}

static void test_batch_add(void)
{
    chipmunk_mixnet_batch_t l_batch;
    chipmunk_mixnet_batch_init(&l_batch, 16);

    uint8_t l_sig1[] = {0x01, 0x02, 0x03};
    uint8_t l_sig2[] = {0x04, 0x05, 0x06};

    int l_rc = chipmunk_mixnet_batch_add(&l_batch, l_sig1, sizeof(l_sig1));
    dap_assert(l_rc == 0, "add sig1 OK");
    dap_assert(l_batch.count == 1, "count = 1");

    l_rc = chipmunk_mixnet_batch_add(&l_batch, l_sig2, sizeof(l_sig2));
    dap_assert(l_rc == 0, "add sig2 OK");
    dap_assert(l_batch.count == 2, "count = 2");

    chipmunk_mixnet_batch_free(&l_batch);
}

static void test_batch_shuffle(void)
{
    chipmunk_mixnet_batch_t l_batch;
    chipmunk_mixnet_batch_init(&l_batch, 16);

    /* Add 8 signatures */
    uint8_t l_sigs[8][4];
    for (int i = 0; i < 8; ++i) {
        l_sigs[i][0] = (uint8_t)i;
        l_sigs[i][1] = (uint8_t)(i + 10);
        l_sigs[i][2] = (uint8_t)(i + 20);
        l_sigs[i][3] = (uint8_t)(i + 30);
        chipmunk_mixnet_batch_add(&l_batch, l_sigs[i], 4);
    }

    /* Shuffle */
    int l_rc = chipmunk_mixnet_batch_shuffle(&l_batch);
    dap_assert(l_rc == 0, "shuffle OK");
    dap_assert(l_batch.finalized, "batch finalized");

    /* All signatures should still be present (just reordered) */
    int l_found[8] = {0};
    for (uint32_t i = 0; i < l_batch.count; ++i) {
        const uint8_t *l_sig;
        size_t l_size;
        chipmunk_mixnet_batch_get(&l_batch, i, &l_sig, &l_size);
        dap_assert(l_size == 4, "sig size = 4");
        int idx = l_sig[0];
        if (idx >= 0 && idx < 8) l_found[idx] = 1;
    }
    int l_all_found = 1;
    for (int i = 0; i < 8; ++i) {
        if (!l_found[i]) { l_all_found = 0; break; }
    }
    dap_assert(l_all_found, "all signatures present after shuffle");

    chipmunk_mixnet_batch_free(&l_batch);
}

static void test_batch_overflow(void)
{
    chipmunk_mixnet_batch_t l_batch;
    chipmunk_mixnet_batch_init(&l_batch, 2);

    uint8_t l_sig[] = {0x01};
    chipmunk_mixnet_batch_add(&l_batch, l_sig, 1);
    chipmunk_mixnet_batch_add(&l_batch, l_sig, 1);

    /* Third add should fail (capacity reached) */
    int l_rc = chipmunk_mixnet_batch_add(&l_batch, l_sig, 1);
    dap_assert(l_rc == -EAGAIN, "overflow returns -EAGAIN");

    chipmunk_mixnet_batch_free(&l_batch);
}

static void test_dcnet_init(void)
{
    chipmunk_dcnet_round_t l_round;
    int l_rc = chipmunk_dcnet_init(&l_round, 4);
    dap_assert(l_rc == 0, "DC-net init OK");
    dap_assert(l_round.participant_count == 4, "participant count = 4");
    chipmunk_dcnet_free(&l_round);
}

static void test_dcnet_combine(void)
{
    chipmunk_dcnet_round_t l_round;
    chipmunk_dcnet_init(&l_round, 3);

    /* Each participant generates shares */
    uint8_t l_msg1[] = {0x01, 0x02};
    uint8_t l_msg2[] = {0x03, 0x04};
    uint8_t l_msg3[] = {0x05, 0x06};

    chipmunk_dcnet_generate_shares(&l_round, 0, l_msg1, 2);
    chipmunk_dcnet_generate_shares(&l_round, 1, l_msg2, 2);
    chipmunk_dcnet_generate_shares(&l_round, 2, l_msg3, 2);

    /* Combine */
    int l_rc = chipmunk_dcnet_combine(&l_round);
    dap_assert(l_rc == 0, "DC-net combine OK");

    /* Output should be nonzero */
    const uint8_t *l_output;
    size_t l_output_size;
    l_rc = chipmunk_dcnet_get_output(&l_round, &l_output, &l_output_size);
    dap_assert(l_rc == 0, "get output OK");
    dap_assert(l_output_size > 0, "output size > 0");

    chipmunk_dcnet_free(&l_round);
}

int main(void)
{
    dap_set_appname("test_chipmunk_mixnet");
    dap_common_init("test_chipmunk_mixnet", NULL);

    test_batch_init();
    test_batch_add();
    test_batch_shuffle();
    test_batch_overflow();
    test_dcnet_init();
    test_dcnet_combine();

    log_it(L_INFO, "=== ALL Chipmunk Mixnet tests PASSED ===");
    dap_common_deinit();
    return 0;
}
