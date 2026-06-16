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

#define LOG_TAG "test_lotrs_basic"

static void test_keygen(void)
{
    const lotrs_params_t *par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t kp = {0};

    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)(0x42 + i);

    int rc = lotrs_keygen(&kp, par, seed);
    dap_assert(rc == 0, "keygen OK");

    /* PK must be non-zero. */
    int nonzero = 0;
    for (uint32_t i = 0; i < par->k; ++i) {
        for (uint32_t j = 0; j < par->d; ++j) {
            if (kp.pk.a_hat.polys[i]->coeffs[j] != 0) nonzero = 1;
        }
    }
    dap_assert(nonzero, "PK non-zero");

    /* SK must be short (coefficients in {-1, 0, 1}). */
    int short_ok = 1;
    for (uint32_t i = 0; i < par->l + par->k; ++i) {
        for (uint32_t j = 0; j < par->d; ++j) {
            uint64_t c = kp.sk.s.polys[i]->coeffs[j];
            int64_t centered = lotrs_center(c, par->q);
            if (centered < -(int64_t)par->eta || centered > (int64_t)par->eta) {
                short_ok = 0;
            }
        }
    }
    dap_assert(short_ok, "SK short (eta=1)");

    lotrs_pk_free(&kp.pk);
    lotrs_sk_free(&kp.sk);
}

static void test_sign_verify(void)
{
    const lotrs_params_t *par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t kp = {0};

    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)(0x42 + i);
    int rc = lotrs_keygen(&kp, par, seed);
    dap_assert(rc == 0, "keygen OK");

    /* Single-signer test: just kp, no ring. */
    const uint8_t msg[] = "lotrs-test-message";
    lotrs_signature_t sig = {0};
    uint8_t sign_seed[32];
    for (int i = 0; i < 32; ++i) sign_seed[i] = (uint8_t)(0xAA + i);

    /* Build a minimal ring with just the signer's PK. */
    lotrs_ring_pk_t ring = {0};
    ring.N = 1;
    ring.T = 1;
    ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, 1);
    dap_assert(ring.pks != NULL, "ring alloc");

    /* Copy the signer's PK into the ring. */
    ring.pks[0].a_hat = lotrs_polyvec_alloc(par, par->k);
    for (uint32_t i = 0; i < par->k; ++i) {
        lotrs_poly_copy(ring.pks[0].a_hat.polys[i], kp.pk.a_hat.polys[i], par);
    }

    rc = lotrs_sign(&sig, par, &ring, &kp.sk, 0,
                    msg, sizeof(msg) - 1, sign_seed);
    if (rc == -2) {
        sign_seed[0] ^= 0xFF;
        rc = lotrs_sign(&sig, par, &ring, &kp.sk, 0,
                        msg, sizeof(msg) - 1, sign_seed);
    }
    dap_assert(rc == 0, "sign OK");
    dap_assert(sig.data != NULL && sig.len > 0, "signature non-empty");

    /* Verify. */
    rc = lotrs_verify(&sig, par, &ring, msg, sizeof(msg) - 1);
    dap_assert(rc == 0, "verify OK");

    /* Wrong message must fail. */
    const uint8_t bad_msg[] = "wrong-message";
    rc = lotrs_verify(&sig, par, &ring, bad_msg, sizeof(bad_msg) - 1);
    dap_assert(rc < 0, "wrong message fails");

    lotrs_signature_free(&sig);
    lotrs_ring_pk_free(&ring);
    lotrs_pk_free(&kp.pk);
    lotrs_sk_free(&kp.sk);
}

static void test_determinism(void)
{
    const lotrs_params_t *par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t kp = {0};
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = 0x55;

    int rc = lotrs_keygen(&kp, par, seed);
    dap_assert(rc == 0, "keygen OK");

    /* Same seed must produce same keypair. */
    lotrs_keypair_t kp2 = {0};
    rc = lotrs_keygen(&kp2, par, seed);
    dap_assert(rc == 0, "keygen2 OK");

    for (uint32_t i = 0; i < par->k; ++i) {
        for (uint32_t j = 0; j < par->d; ++j) {
            dap_assert(kp.pk.a_hat.polys[i]->coeffs[j] ==
                       kp2.pk.a_hat.polys[i]->coeffs[j],
                       "deterministic PK");
        }
    }

    lotrs_pk_free(&kp.pk);
    lotrs_sk_free(&kp.sk);
    lotrs_pk_free(&kp2.pk);
    lotrs_sk_free(&kp2.sk);
}

static void test_pack_roundtrip(void)
{
    const lotrs_params_t *par = &LOTRS_PARAMS_TEST;
    lotrs_poly_t *p = lotrs_poly_alloc(par);
    dap_assert(p != NULL, "poly alloc");

    /* Fill with known values in [0, q). */
    for (uint32_t i = 0; i < par->d; ++i) {
        p->coeffs[i] = (uint64_t)i * 1000 + 42;
    }

    /* Pack. */
    size_t bytes = lotrs_poly_bytes(par);
    uint8_t *buf = DAP_NEW_Z_SIZE(uint8_t, bytes);
    dap_assert(lotrs_poly_pack(buf, bytes, p, par) == 0, "pack OK");

    /* Unpack into new poly. */
    lotrs_poly_t *q = lotrs_poly_alloc(par);
    dap_assert(q != NULL, "poly alloc 2");
    dap_assert(lotrs_poly_unpack(q, buf, bytes, par) == 0, "unpack OK");

    /* Compare. */
    int match = 1;
    for (uint32_t i = 0; i < par->d; ++i) {
        if (p->coeffs[i] != q->coeffs[i]) {
            match = 0;
            break;
        }
    }
    dap_assert(match, "pack/unpack roundtrip");

    /* SHA3 hash must match. */
    dap_hash_sha3_256_t h1, h2;
    dap_hash_sha3_256(p->coeffs, par->d * 8, &h1);
    dap_hash_sha3_256(q->coeffs, par->d * 8, &h2);
    dap_assert(memcmp(&h1, &h2, 32) == 0, "SHA3 hash match");

    /* Polynomial multiplication sanity. */
    lotrs_poly_t *a = lotrs_poly_alloc(par);
    lotrs_poly_t *b = lotrs_poly_alloc(par);
    lotrs_poly_t *c = lotrs_poly_alloc(par);
    a->coeffs[0] = 3;
    b->coeffs[0] = 5;
    lotrs_poly_mul(c, a, b, par);
    /* 3 * 5 = 15 in R_q. */
    dap_assert(c->coeffs[0] == 15u % par->q, "mul 3*5=15");
    /* a[12] = q-1 (= -1 centered), b[0] = 5 → c[12] = q-5. */
    a->coeffs[12] = par->q - 1u;
    lotrs_poly_mul(c, a, b, par);
    dap_assert(c->coeffs[12] == (par->q - 5u) % par->q, "mul (q-1)*5 = q-5");
    lotrs_poly_free(a); lotrs_poly_free(b); lotrs_poly_free(c);

    DAP_DELETE(buf);
    lotrs_poly_free(p);
    lotrs_poly_free(q);
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
