/*
 * test_lotrs_basic.c — LoTRS basic keygen/sign/verify test.
 *
 * Validates the TEST parameter set (d=32, N=4, T=2).
 */

#include <dap_common.h>
#include <dap_hash_sha3.h>
#include <dap_test.h>

#include <stdint.h>
#include <string.h>

#include "sig/lotrs/lotrs.h"
#include "sig/lotrs/lotrs_params.h"
#include "sig/lotrs/lotrs_ring.h"

#define LOG_TAG "test_lotrs_basic"

static void s_fill_seed(uint8_t *a_out, size_t a_len, uint8_t a_salt)
{
    for (size_t i = 0u; i < a_len; ++i) {
        a_out[i] = (uint8_t)(0x42u ^ (uint8_t)i ^ a_salt);
    }
}

static void test_keygen(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};

    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0x42 + i);

    int l_rc = lotrs_keygen(&l_kp, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen OK");

    int l_nonzero = 0;
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            if (l_kp.pk.a_hat.polys[i]->coeffs[j] != 0u) l_nonzero = 1;
        }
    }
    dap_assert(l_nonzero, "PK non-zero");

    int l_short_ok = 1;
    for (uint32_t i = 0u; i < l_par->l + l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            uint64_t l_c = l_kp.sk.s.polys[i]->coeffs[j];
            int64_t l_centered = lotrs_center(l_c, l_par->q);
            if (l_centered < -(int64_t)l_par->eta || l_centered > (int64_t)l_par->eta) {
                l_short_ok = 0;
            }
        }
    }
    dap_assert(l_short_ok, "SK short (eta=1)");

    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
}

static void test_sign_verify(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};

    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0x42 + i);
    int l_rc = lotrs_keygen(&l_kp, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen OK");

    const uint8_t l_msg[] = "lotrs-test-message";
    lotrs_signature_t l_sig = {0};
    uint8_t l_sign_seed[32];
    for (int i = 0; i < 32; ++i) l_sign_seed[i] = (uint8_t)(0xAA + i);

    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = 1;
    l_ring.T = 1;
    l_ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, 1);
    dap_assert(l_ring.pks != NULL, "ring alloc");

    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);
    }

    l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                      l_msg, sizeof(l_msg) - 1, l_sign_seed);
    if (l_rc == -2) {
        l_sign_seed[0] ^= 0xFF;
        l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                          l_msg, sizeof(l_msg) - 1, l_sign_seed);
    }
    dap_assert(l_rc == 0, "sign OK");
    dap_assert(l_sig.data != NULL && l_sig.len > 0, "signature non-empty");

    l_rc = lotrs_verify(&l_sig, l_par, &l_ring, l_msg, sizeof(l_msg) - 1);
    dap_assert(l_rc == 0, "verify OK");

    const uint8_t l_bad_msg[] = "wrong-message";
    l_rc = lotrs_verify(&l_sig, l_par, &l_ring, l_bad_msg, sizeof(l_bad_msg) - 1);
    dap_assert(l_rc < 0, "wrong message fails");

    lotrs_signature_free(&l_sig);
    lotrs_ring_pk_free(&l_ring);
    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
}

static void test_determinism(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x55;

    int l_rc = lotrs_keygen(&l_kp, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen OK");

    lotrs_keypair_t l_kp2 = {0};
    l_rc = lotrs_keygen(&l_kp2, l_par, l_seed);
    dap_assert(l_rc == 0, "keygen2 OK");

    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            dap_assert(l_kp.pk.a_hat.polys[i]->coeffs[j] ==
                       l_kp2.pk.a_hat.polys[i]->coeffs[j],
                       "deterministic PK");
        }
    }

    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
    lotrs_pk_free(&l_kp2.pk);
    lotrs_sk_free(&l_kp2.sk);
}

static void test_pack_roundtrip(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_poly_t *l_p = lotrs_poly_alloc(l_par);
    dap_assert(l_p != NULL, "poly alloc");

    for (uint32_t i = 0u; i < l_par->d; ++i) {
        l_p->coeffs[i] = (uint64_t)i * 1000u + 42u;
    }

    size_t l_bytes = lotrs_poly_bytes(l_par);
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_bytes);
    dap_assert(lotrs_poly_pack(l_buf, l_bytes, l_p, l_par) == 0, "pack OK");

    lotrs_poly_t *l_q = lotrs_poly_alloc(l_par);
    dap_assert(l_q != NULL, "poly alloc 2");
    dap_assert(lotrs_poly_unpack(l_q, l_buf, l_bytes, l_par) == 0, "unpack OK");

    int l_match = 1;
    for (uint32_t i = 0u; i < l_par->d; ++i) {
        if (l_p->coeffs[i] != l_q->coeffs[i]) {
            l_match = 0;
            break;
        }
    }
    dap_assert(l_match, "pack/unpack roundtrip");

    dap_hash_sha3_256_t l_h1, l_h2;
    dap_hash_sha3_256(l_p->coeffs, l_par->d * 8, &l_h1);
    dap_hash_sha3_256(l_q->coeffs, l_par->d * 8, &l_h2);
    dap_assert(memcmp(&l_h1, &l_h2, 32) == 0, "SHA3 hash match");

    /* Polynomial multiplication sanity. */
    lotrs_poly_t *l_a = lotrs_poly_alloc(l_par);
    lotrs_poly_t *l_b = lotrs_poly_alloc(l_par);
    lotrs_poly_t *l_c = lotrs_poly_alloc(l_par);
    l_a->coeffs[0] = 3;
    l_b->coeffs[0] = 5;
    lotrs_poly_mul(l_c, l_a, l_b, l_par);
    dap_assert(l_c->coeffs[0] == 15u % l_par->q, "mul 3*5=15");

    l_a->coeffs[12] = l_par->q - 1u;
    lotrs_poly_mul(l_c, l_a, l_b, l_par);
    dap_assert(l_c->coeffs[12] == (l_par->q - 5u) % l_par->q, "mul (q-1)*5 = q-5");

    lotrs_poly_zero(l_a, l_par);
    lotrs_poly_zero(l_b, l_par);
    l_a->coeffs[20] = 100u;
    l_b->coeffs[12] = 50u;
    lotrs_poly_mul(l_c, l_a, l_b, l_par);
    dap_assert(l_c->coeffs[0] == (l_par->q - 5000u) % l_par->q,
               "negacyclic wrap: a[20]*b[12] -> -c[0]");

    lotrs_poly_free(l_a); lotrs_poly_free(l_b); lotrs_poly_free(l_c);
    DAP_DELETE(l_buf);
    lotrs_poly_free(l_p);
    lotrs_poly_free(l_q);
}

int main(void)
{
    dap_set_appname("test_lotrs_basic");
    dap_common_init("test_lotrs_basic", NULL);

    test_keygen();
    test_pack_roundtrip();
    test_sign_verify();
    test_determinism();

    log_it(L_INFO, "=== ALL LoTRS basic tests PASSED ===");
    dap_common_deinit();
    return 0;
}
