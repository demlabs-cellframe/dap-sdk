/*
 * test_chipmunk_fri_transcript.c — Unit tests for Fiat-Shamir transcript.
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <dap_common.h>
#include <dap_test.h>

#include "chipmunk.h"
#include "chipmunk_field.h"
#include "chipmunk_fri_transcript.h"

#define LOG_TAG "test_chipmunk_fri_transcript"

/* ========================================================================
 * Test 1: Init + absorb + squeeze determinism.
 * ======================================================================== */
static void test_transcript_init(void)
{
    chipmunk_fri_transcript_t tr;
    uint8_t domain[16]; memcpy(domain, "CHIPMUNK-FRI-TST", 16);
    int rc = chipmunk_fri_transcript_init(&tr, domain);
    dap_assert(rc == 0, "init ok");
    dap_assert(tr.initialized, "initialized flag");
    dap_assert(tr.buf_len == 16, "buf_len after init");

    /* Absorb some data. */
    uint8_t data[32];
    memset(data, 0xAB, sizeof(data));
    rc = chipmunk_fri_transcript_absorb(&tr, data, sizeof(data));
    dap_assert(rc == 0, "absorb ok");
    dap_assert(tr.buf_len == 48, "buf_len after absorb");

    chipmunk_fri_transcript_t tr2;
    uint8_t domain2[16]; memcpy(domain2, "CHIPMUNK-FRI-TST", 16);
    chipmunk_fri_transcript_init(&tr2, domain2);
    chipmunk_fri_transcript_absorb(&tr2, data, sizeof(data));

    /* Grinding should produce same nonce for same inputs. */
    uint32_t nonce1, nonce2;
    rc = chipmunk_fri_transcript_grind(&tr, &nonce1);
    dap_assert(rc == 0, "grind 1 ok");
    rc = chipmunk_fri_transcript_grind(&tr2, &nonce2);
    dap_assert(rc == 0, "grind 2 ok");
    dap_assert(nonce1 == nonce2, "grinding determinism");
}

/* ========================================================================
 * Test 2: Squeeze produces values in [0, q).
 * ======================================================================== */
static void test_transcript_squeeze_range(void)
{
    chipmunk_fri_transcript_t tr;
    uint8_t domain[16]; memcpy(domain, "SQ-RANGE-TEST-01", 16);
    chipmunk_fri_transcript_init(&tr, domain);

    /* Absorb enough data to have entropy. */
    uint8_t data[64];
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = (uint8_t)i;
    chipmunk_fri_transcript_absorb(&tr, data, sizeof(data));

    int rc = chipmunk_fri_transcript_finalize(&tr);
    dap_assert(rc == 0, "finalize ok");

    /* Squeeze 100 challenges and verify range. */
    for (int i = 0; i < 100; ++i) {
        int32_t val;
        rc = chipmunk_fri_transcript_squeeze_fq(&tr, &val);
        dap_assert(rc == 0, "squeeze ok");
        dap_assert(val >= 0 && val < (int32_t)CHIPMUNK_Q, "squeeze range");
    }
}

/* ========================================================================
 * Test 3: Squeeze determinism — same transcript → same challenges.
 * ======================================================================== */
static void test_transcript_squeeze_determinism(void)
{
    uint8_t domain[16]; memcpy(domain, "DET-SQUEEZE-001A", 16);
    uint8_t data[32];
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = (uint8_t)(i * 3 + 7);

    chipmunk_fri_transcript_t tr1, tr2;
    chipmunk_fri_transcript_init(&tr1, domain);
    chipmunk_fri_transcript_init(&tr2, domain);
    chipmunk_fri_transcript_absorb(&tr1, data, sizeof(data));
    chipmunk_fri_transcript_absorb(&tr2, data, sizeof(data));
    chipmunk_fri_transcript_finalize(&tr1);
    chipmunk_fri_transcript_finalize(&tr2);

    for (int i = 0; i < 20; ++i) {
        int32_t v1, v2;
        chipmunk_fri_transcript_squeeze_fq(&tr1, &v1);
        chipmunk_fri_transcript_squeeze_fq(&tr2, &v2);
        dap_assert(v1 == v2, "squeeze determinism");
    }
}

/* ========================================================================
 * Test 4: Absorb F_q field element.
 * ======================================================================== */
static void test_transcript_absorb_fq(void)
{
    chipmunk_fri_transcript_t tr;
    uint8_t domain[16]; memcpy(domain, "ABSORB-FQ-X0001", 16);
    chipmunk_fri_transcript_init(&tr, domain);

    int rc = chipmunk_fri_transcript_absorb_fq(&tr, 0);
    dap_assert(rc == 0, "absorb fq 0");

    rc = chipmunk_fri_transcript_absorb_fq(&tr, 1);
    dap_assert(rc == 0, "absorb fq 1");

    rc = chipmunk_fri_transcript_absorb_fq(&tr, CHIPMUNK_Q - 1);
    dap_assert(rc == 0, "absorb fq q-1");

    /* Out of range. */
    rc = chipmunk_fri_transcript_absorb_fq(&tr, -1);
    dap_assert(rc < 0, "absorb fq negative rejected");

    rc = chipmunk_fri_transcript_absorb_fq(&tr, CHIPMUNK_Q);
    dap_assert(rc < 0, "absorb fq >= q rejected");
}

/* ========================================================================
 * Test 5: Absorb Merkle cap.
 * ======================================================================== */
static void test_transcript_absorb_cap(void)
{
    chipmunk_fri_transcript_t tr;
    uint8_t domain[16]; memcpy(domain, "ABSORB-CAP-X001", 16);
    chipmunk_fri_transcript_init(&tr, domain);

    int32_t cap[16];
    for (unsigned i = 0; i < 16; ++i)
        cap[i] = (int32_t)(i * 1000 + 42) % CHIPMUNK_Q;

    int rc = chipmunk_fri_transcript_absorb_cap(&tr, cap, 16);
    dap_assert(rc == 0, "absorb cap 16 ok");

    /* Cap should add 16 * 4 = 64 bytes. */
    dap_assert(tr.buf_len == 16 + 64, "buf_len after cap");

    /* NULL cap. */
    rc = chipmunk_fri_transcript_absorb_cap(&tr, NULL, 1);
    dap_assert(rc < 0, "null cap rejected");

    /* Size 0. */
    rc = chipmunk_fri_transcript_absorb_cap(&tr, cap, 0);
    dap_assert(rc < 0, "size 0 rejected");

    /* Size > 16. */
    rc = chipmunk_fri_transcript_absorb_cap(&tr, cap, 17);
    dap_assert(rc < 0, "size > 16 rejected");
}

/* ========================================================================
 * Test 6: Grinding verification.
 * ======================================================================== */
static void test_transcript_grinding_verify(void)
{
    chipmunk_fri_transcript_t tr;
    uint8_t domain[16]; memcpy(domain, "GRIND-VERIFY-X01", 16);
    chipmunk_fri_transcript_init(&tr, domain);

    uint8_t data[32];
    memset(data, 0x42, sizeof(data));
    chipmunk_fri_transcript_absorb(&tr, data, sizeof(data));

    uint32_t nonce;
    int rc = chipmunk_fri_transcript_grind(&tr, &nonce);
    dap_assert(rc == 0, "grind ok");

    /* Verify the nonce is valid. */
    bool ok = chipmunk_fri_transcript_verify_grinding(&tr, nonce);
    dap_assert(ok, "verify grinding ok");

    /* Wrong nonce should fail. */
    ok = chipmunk_fri_transcript_verify_grinding(&tr, nonce ^ 1);
    dap_assert(!ok, "wrong nonce rejected");

    /* Different nonce should also fail (extremely unlikely to pass). */
    ok = chipmunk_fri_transcript_verify_grinding(&tr, nonce + 1000000);
    dap_assert(!ok, "far nonce rejected");
}

/* ========================================================================
 * Test 7: Different domain separators → different challenges.
 * ======================================================================== */
static void test_transcript_domain_separation(void)
{
    uint8_t domain1[16]; memcpy(domain1, "DOMAIN-AAAA-0001", 16);
    uint8_t domain2[16]; memcpy(domain2, "DOMAIN-BBBB-0001", 16);
    uint8_t data[16];
    memset(data, 0, sizeof(data));

    chipmunk_fri_transcript_t tr1, tr2;
    chipmunk_fri_transcript_init(&tr1, domain1);
    chipmunk_fri_transcript_init(&tr2, domain2);
    chipmunk_fri_transcript_absorb(&tr1, data, sizeof(data));
    chipmunk_fri_transcript_absorb(&tr2, data, sizeof(data));
    chipmunk_fri_transcript_finalize(&tr1);
    chipmunk_fri_transcript_finalize(&tr2);

    int32_t v1, v2;
    chipmunk_fri_transcript_squeeze_fq(&tr1, &v1);
    chipmunk_fri_transcript_squeeze_fq(&tr2, &v2);
    dap_assert(v1 != v2, "different domains → different challenges");
}

/* ========================================================================
 * Test 8: Squeeze many challenges (FRI needs 7 alphas).
 * ======================================================================== */
static void test_transcript_squeeze_many(void)
{
    chipmunk_fri_transcript_t tr;
    uint8_t domain[16]; memcpy(domain, "SQUEEZE-MANY-001", 16);
    chipmunk_fri_transcript_init(&tr, domain);

    /* Absorb 7 FRI round caps. */
    for (unsigned r = 0; r < 7; ++r) {
        int32_t cap[16];
        for (unsigned i = 0; i < 16; ++i)
            cap[i] = (int32_t)((r * 16 + i) * 31 % CHIPMUNK_Q);
        chipmunk_fri_transcript_absorb_cap(&tr, cap, 16);
    }

    int rc = chipmunk_fri_transcript_finalize(&tr);
    dap_assert(rc == 0, "finalize ok");

    /* Squeeze 7 FRI alpha challenges. */
    int32_t alphas[7];
    rc = chipmunk_fri_transcript_squeeze_fq_many(&tr, alphas, 7);
    dap_assert(rc == 0, "squeeze 7 alphas ok");

    /* All should be distinct (high probability). */
    int distinct = 0;
    for (unsigned i = 0; i < 7; ++i) {
        for (unsigned j = i + 1; j < 7; ++j) {
            if (alphas[i] != alphas[j])
                ++distinct;
        }
    }
    dap_assert(distinct >= 18, "alphas mostly distinct");
}

/* ========================================================================
 * Test 9: Invalid arguments.
 * ======================================================================== */
static void test_transcript_invalid_args(void)
{
    chipmunk_fri_transcript_t tr;
    uint8_t domain[16]; memcpy(domain, "INVALID-ARGS-X1", 16);
    chipmunk_fri_transcript_init(&tr, domain);

    /* Init NULL. */
    int rc = chipmunk_fri_transcript_init(NULL, domain);
    dap_assert(rc < 0, "init NULL tr");

    rc = chipmunk_fri_transcript_init(&tr, NULL);
    dap_assert(rc < 0, "init NULL domain");

    /* Absorb NULL data with nonzero length. */
    rc = chipmunk_fri_transcript_absorb(&tr, NULL, 10);
    dap_assert(rc < 0, "absorb NULL data");

    /* Absorb NULL with len=0 should be ok. */
    rc = chipmunk_fri_transcript_absorb(&tr, NULL, 0);
    dap_assert(rc == 0, "absorb NULL len=0 ok");

    /* Squeeze before finalize is ALLOWED (needed for alpha derivation in SNARK
     * before grinding). The old test expected rejection, but the design changed
     * in Phase 9.11 to support pre-finalize challenge derivation. */
    int32_t val;
    rc = chipmunk_fri_transcript_squeeze_fq(&tr, &val);
    dap_assert(rc == 0, "squeeze before finalize allowed (Phase 9.11 design)");

    /* Squeeze NULL out. */
    uint8_t data[16] = {0};
    chipmunk_fri_transcript_absorb(&tr, data, sizeof(data));
    rc = chipmunk_fri_transcript_finalize(&tr);
    dap_assert(rc == 0, "finalize ok");

    rc = chipmunk_fri_transcript_squeeze_fq(&tr, NULL);
    dap_assert(rc < 0, "squeeze NULL out");

    /* Clone NULL. */
    rc = chipmunk_fri_transcript_clone(NULL, &tr);
    dap_assert(rc < 0, "clone NULL dst");

    rc = chipmunk_fri_transcript_clone(&tr, NULL);
    dap_assert(rc < 0, "clone NULL src");

    /* Verify grinding before absorb. */
    chipmunk_fri_transcript_t tr_empty;
    memset(&tr_empty, 0, sizeof(tr_empty));
    bool ok = chipmunk_fri_transcript_verify_grinding(&tr_empty, 0);
    dap_assert(!ok, "verify empty transcript");
}

/* ========================================================================
 * Test 10: Transcript clone preserves squeeze sequence.
 * ======================================================================== */
static void test_transcript_clone_squeeze(void)
{
    uint8_t domain[16]; memcpy(domain, "CLONE-SQZ-X0001", 16);
    uint8_t data[32];
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = (uint8_t)(i ^ 0xFF);

    chipmunk_fri_transcript_t tr1;
    chipmunk_fri_transcript_init(&tr1, domain);
    chipmunk_fri_transcript_absorb(&tr1, data, sizeof(data));
    chipmunk_fri_transcript_finalize(&tr1);

    /* Squeeze a few values from tr1. */
    int32_t v1_a, v1_b;
    chipmunk_fri_transcript_squeeze_fq(&tr1, &v1_a);

    /* Clone at this point. */
    chipmunk_fri_transcript_t tr2;
    chipmunk_fri_transcript_clone(&tr2, &tr1);

    /* Continue squeezing from both — should match. */
    chipmunk_fri_transcript_squeeze_fq(&tr1, &v1_b);
    int32_t v2_b;
    chipmunk_fri_transcript_squeeze_fq(&tr2, &v2_b);
    dap_assert(v1_b == v2_b, "clone preserves squeeze state");
}

/* ========================================================================
 * Main
 * ======================================================================== */
int main(void)
{
    dap_set_appname("test_chipmunk_fri_transcript");
    if (0 != dap_common_init("test_chipmunk_fri_transcript", NULL)) {
        fprintf(stderr, "dap_common_init failed\n");
        return 1;
    }

    log_it(L_INFO, "=== FRI transcript tests ===");

    test_transcript_init();
    test_transcript_squeeze_range();
    test_transcript_squeeze_determinism();
    test_transcript_absorb_fq();
    test_transcript_absorb_cap();
    test_transcript_grinding_verify();
    test_transcript_domain_separation();
    test_transcript_squeeze_many();
    test_transcript_invalid_args();
    test_transcript_clone_squeeze();

    log_it(L_INFO, "All FRI transcript tests passed");

    dap_common_deinit();
    return 0;
}
