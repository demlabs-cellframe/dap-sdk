/*
 * test_chipmunk_ring.c — Chipmunk Ring V2 basic tests.
 *
 * Non-interactive lattice ring signature based on LoTRS RS proof.
 */

#include <dap_common.h>
#include <dap_test.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_ring.h"
#include "sig/lotrs/lotrs_params.h"

#define LOG_TAG "test_chipmunk_ring"

static void test_keygen(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;

    int l_rc = chipmunk_ring_keygen(&l_kp, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen OK");

    int l_nonzero = 0;
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            if (l_kp.pk.a_hat.polys[i]->coeffs[j] != 0u) l_nonzero = 1;
        }
    }
    dap_assert(l_nonzero, "PK non-zero");

    chipmunk_ring_keypair_free(&l_kp);
}

static void test_sign_verify(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    int l_rc = chipmunk_ring_keygen(&l_kp, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen OK");

    chipmunk_ring_table_t l_ring = {0};
    l_ring.N = 1;
    l_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, 1);
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);
    }

    const uint8_t l_msg[] = "chipmunk-ring-v2-test";
    chipmunk_ring_sig_t l_sig = {0};
    uint8_t l_sign_seed[32];
    for (int i = 0; i < 32; ++i) l_sign_seed[i] = 0xBB + i;

    l_rc = chipmunk_ring_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                                 l_msg, sizeof(l_msg) - 1, l_sign_seed);
    if (l_rc == -2) {
        l_sign_seed[0] ^= 0xFF;
        l_rc = chipmunk_ring_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                                     l_msg, sizeof(l_msg) - 1, l_sign_seed);
    }
    dap_assert(l_rc == 0, "sign OK");
    dap_assert(l_sig.data != NULL && l_sig.len > 0, "signature non-empty");

    l_rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_msg, sizeof(l_msg) - 1);
    dap_assert(l_rc == 0, "verify OK");

    const uint8_t l_bad[] = "wrong-message";
    l_rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_bad, sizeof(l_bad) - 1);
    dap_assert(l_rc != 0, "wrong message fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_determinism(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp1 = {0}, l_kp2 = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x55;

    int l_rc = chipmunk_ring_keygen(&l_kp1, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen1 OK");
    l_rc = chipmunk_ring_keygen(&l_kp2, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen2 OK");

    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            dap_assert(l_kp1.pk.a_hat.polys[i]->coeffs[j] ==
                       l_kp2.pk.a_hat.polys[i]->coeffs[j],
                       "deterministic PK");
        }
    }

    chipmunk_ring_keypair_free(&l_kp1);
    chipmunk_ring_keypair_free(&l_kp2);
}

static void test_ring_n2(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp0 = {0}, l_kp1 = {0};
    uint8_t l_seed0[32], l_seed1[32];
    for (int i = 0; i < 32; ++i) { l_seed0[i] = 0x42 + i; l_seed1[i] = 0x99 + i; }

    int l_rc = chipmunk_ring_keygen(&l_kp0, l_par, l_seed0);
    dap_assert(l_rc == 0, "keygen0 OK");
    l_rc = chipmunk_ring_keygen(&l_kp1, l_par, l_seed1);
    dap_assert(l_rc == 0, "keygen1 OK");

    chipmunk_ring_table_t l_ring = {0};
    l_ring.N = 2;
    l_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, 2);
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    l_ring.pks[1].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp0.pk.a_hat.polys[i], l_par);
        lotrs_poly_copy(l_ring.pks[1].a_hat.polys[i], l_kp1.pk.a_hat.polys[i], l_par);
    }

    const uint8_t l_msg[] = "ring-n2-test";
    chipmunk_ring_sig_t l_sig = {0};
    uint8_t l_sign_seed[32];
    for (int i = 0; i < 32; ++i) l_sign_seed[i] = 0xCC + i;

    /* Sign as member 0. */
    l_rc = chipmunk_ring_sign(&l_sig, l_par, &l_ring, &l_kp0.sk, 0,
                                 l_msg, sizeof(l_msg) - 1, l_sign_seed);
    if (l_rc == -EAGAIN) {
        l_sign_seed[0] ^= 0xFF;
        l_rc = chipmunk_ring_sign(&l_sig, l_par, &l_ring, &l_kp0.sk, 0,
                                     l_msg, sizeof(l_msg) - 1, l_sign_seed);
    }
    dap_assert(l_rc == 0, "sign N=2 OK");

    l_rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_msg, sizeof(l_msg) - 1);
    dap_assert(l_rc == 0, "verify N=2 OK");

    /* Wrong message. */
    const uint8_t l_bad[] = "wrong";
    l_rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_bad, sizeof(l_bad) - 1);
    dap_assert(l_rc != 0, "wrong msg fails N=2");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp0);
    chipmunk_ring_keypair_free(&l_kp1);
}

static void test_wire_size(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    for (uint32_t N = 1u; N <= 4u; ++N) {
        size_t l_sz = chipmunk_ring_sig_bytes_max(l_par, N);
        dap_assert(l_sz > 0, "wire size > 0");
        size_t l_expected = chipmunk_ring_header_bytes()
                          + (size_t)N * lotrs_polyvec_bytes(l_par, l_par->k)
                          + (size_t)N * lotrs_poly_bytes(l_par)
                          + (size_t)N * lotrs_polyvec_bytes(l_par, l_par->l + l_par->k);
        dap_assert(l_sz == l_expected, "wire size formula");
    }
}

int main(void)
{
    dap_set_appname("test_chipmunk_ring");
    dap_common_init("test_chipmunk_ring", NULL);

    test_keygen();
    test_wire_size();
    test_sign_verify();
    test_determinism();
    test_ring_n2();

    log_it(L_INFO, "=== ALL Chipmunk Ring tests PASSED ===");
    dap_common_deinit();
    return 0;
}
