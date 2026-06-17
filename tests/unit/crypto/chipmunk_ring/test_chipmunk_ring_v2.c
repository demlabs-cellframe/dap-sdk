/*
 * test_chipmunk_ring_v2.c — Chipmunk Ring V2 basic tests.
 *
 * Non-interactive lattice ring signature based on LoTRS RS proof.
 */

#include <dap_common.h>
#include <dap_test.h>

#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_ring_v2.h"
#include "sig/lotrs/lotrs_params.h"

#define LOG_TAG "test_chipmunk_ring_v2"

static void test_keygen(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_v2_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;

    int l_rc = chipmunk_ring_v2_keygen(&l_kp, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen OK");

    int l_nonzero = 0;
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            if (l_kp.pk.a_hat.polys[i]->coeffs[j] != 0u) l_nonzero = 1;
        }
    }
    dap_assert(l_nonzero, "PK non-zero");

    chipmunk_ring_v2_keypair_free(&l_kp);
}

static void test_sign_verify(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_v2_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    int l_rc = chipmunk_ring_v2_keygen(&l_kp, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen OK");

    chipmunk_ring_v2_ring_t l_ring = {0};
    l_ring.N = 1;
    l_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_v2_pk_t, 1);
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);
    }

    const uint8_t l_msg[] = "chipmunk-ring-v2-test";
    chipmunk_ring_v2_sig_t l_sig = {0};
    uint8_t l_sign_seed[32];
    for (int i = 0; i < 32; ++i) l_sign_seed[i] = 0xBB + i;

    l_rc = chipmunk_ring_v2_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                                 l_msg, sizeof(l_msg) - 1, l_sign_seed);
    if (l_rc == -2) {
        l_sign_seed[0] ^= 0xFF;
        l_rc = chipmunk_ring_v2_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                                     l_msg, sizeof(l_msg) - 1, l_sign_seed);
    }
    dap_assert(l_rc == 0, "sign OK");
    dap_assert(l_sig.data != NULL && l_sig.len > 0, "signature non-empty");

    l_rc = chipmunk_ring_v2_verify(&l_sig, l_par, &l_ring, l_msg, sizeof(l_msg) - 1);
    dap_assert(l_rc == 0, "verify OK");

    const uint8_t l_bad[] = "wrong-message";
    l_rc = chipmunk_ring_v2_verify(&l_sig, l_par, &l_ring, l_bad, sizeof(l_bad) - 1);
    dap_assert(l_rc != 0, "wrong message fails");

    chipmunk_ring_v2_sig_free(&l_sig);
    chipmunk_ring_v2_ring_free(&l_ring);
    chipmunk_ring_v2_keypair_free(&l_kp);
}

static void test_determinism(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_v2_keypair_t l_kp1 = {0}, l_kp2 = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x55;

    int l_rc = chipmunk_ring_v2_keygen(&l_kp1, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen1 OK");
    l_rc = chipmunk_ring_v2_keygen(&l_kp2, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen2 OK");

    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            dap_assert(l_kp1.pk.a_hat.polys[i]->coeffs[j] ==
                       l_kp2.pk.a_hat.polys[i]->coeffs[j],
                       "deterministic PK");
        }
    }

    chipmunk_ring_v2_keypair_free(&l_kp1);
    chipmunk_ring_v2_keypair_free(&l_kp2);
}

static void test_wire_size(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    size_t l_sz = chipmunk_ring_v2_sig_bytes(l_par);
    dap_assert(l_sz > 0, "wire size > 0");
    dap_assert(l_sz == CHIPMUNK_RING_V2_HEADER_BYTES
               + lotrs_polyvec_bytes(l_par, l_par->k)
               + lotrs_poly_bytes(l_par)
               + lotrs_polyvec_bytes(l_par, l_par->l + l_par->k),
               "wire size formula");
}

int main(void)
{
    dap_set_appname("test_chipmunk_ring_v2");
    dap_common_init("test_chipmunk_ring_v2", NULL);

    test_keygen();
    test_wire_size();
    test_sign_verify();
    test_determinism();

    log_it(L_INFO, "=== ALL Chipmunk Ring V2 tests PASSED ===");
    dap_common_deinit();
    return 0;
}
