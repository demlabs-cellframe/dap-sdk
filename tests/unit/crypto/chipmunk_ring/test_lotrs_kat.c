/*
 * test_lotrs_kat.c — LoTRS KAT (Known-Answer Tests).
 *
 * Pins deterministic keygen/sign/verify vectors for the TEST parameter set.
 * Set LOTRS_KAT_DUMP=1 to regenerate expected hashes.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_hash_sha3.h>
#include <dap_test.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sig/lotrs/lotrs.h"
#include "sig/lotrs/lotrs_params.h"
#include "sig/lotrs/lotrs_ring.h"
#include "sig/lotrs/lotrs_wire.h"

#define LOG_TAG "test_lotrs_kat"

static const uint8_t k_seed_0[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const uint8_t k_sign_seed[32] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};

static const uint8_t k_msg[] = "lotrs-kat-canonical-test-vector";

/* Expected SHA3-256 digests. */
static const uint8_t k_expected_pk_sha3[32] = {
    0x02, 0x8f, 0xe8, 0x64, 0x51, 0x31, 0x04, 0x3a,
    0x73, 0x3b, 0x04, 0x45, 0xc3, 0x9d, 0x82, 0x2d,
    0x9d, 0xbe, 0xab, 0xdd, 0x2f, 0x61, 0xc8, 0xaf,
    0x46, 0x2f, 0x1c, 0x04, 0x51, 0xf0, 0x93, 0x9a,
};
static const uint8_t k_expected_sig_sha3[32] = {
    0x81, 0x3d, 0x8c, 0x87, 0x0e, 0x5c, 0x9e, 0xcb,
    0xdc, 0xcc, 0x63, 0xf5, 0x36, 0x57, 0x56, 0xbd,
    0x95, 0x60, 0x77, 0x72, 0x03, 0x8c, 0xc8, 0x83,
    0xe2, 0x92, 0x71, 0x79, 0x6d, 0x61, 0xb7, 0x74,
};
static const uint8_t k_expected_wire_sha3[32] = {
    0x1d, 0xea, 0x22, 0xa2, 0x89, 0x1e, 0xbf, 0x88,
    0x72, 0xf8, 0x6f, 0x3e, 0xc4, 0x7d, 0xeb, 0xa3,
    0xd4, 0x62, 0xfc, 0xc9, 0x21, 0xd6, 0x38, 0x5e,
    0x94, 0x8e, 0x36, 0x06, 0xe1, 0x3d, 0xd6, 0xea,
};

static bool s_dump_mode(void)
{
    const char *e = getenv("LOTRS_KAT_DUMP");
    return e && *e && *e != '0';
}

static bool s_all_zero(const uint8_t *a_buf, size_t a_size)
{
    for (size_t i = 0u; i < a_size; ++i) {
        if (a_buf[i] != 0u) return false;
    }
    return true;
}

static void s_print_vector(const char *a_name, const uint8_t *a_buf, size_t a_size)
{
    fprintf(stderr, "static const uint8_t %s[%zu] = {", a_name, a_size);
    for (size_t i = 0; i < a_size; ++i) {
        if (i % 8u == 0u) fprintf(stderr, "\n   ");
        fprintf(stderr, " 0x%02x,", a_buf[i]);
    }
    fprintf(stderr, "\n};\n");
}

static void s_sha3_256(const void *a_in, size_t a_in_size, uint8_t a_out[32])
{
    dap_hash_sha3_256_t h;
    dap_assert(dap_hash_sha3_256(a_in, a_in_size, &h), "sha3-256 ok");
    memcpy(a_out, &h, 32);
}

static bool s_check_or_dump(const char *a_name,
                            const uint8_t a_actual[32],
                            const uint8_t a_expected[32])
{
    if (s_dump_mode() || s_all_zero(a_expected, 32u)) {
        s_print_vector(a_name, a_actual, 32u);
        return false;
    }
    if (memcmp(a_actual, a_expected, 32u) != 0) {
        log_it(L_ERROR, "KAT mismatch: %s", a_name);
        s_print_vector("actual", a_actual, 32u);
        s_print_vector("expected", a_expected, 32u);
        return false;
    }
    return true;
}

/* -------------------------------------------------------------------------
 * T1. Deterministic keygen.
 * ---------------------------------------------------------------------- */

static bool s_test_keygen_deterministic(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};

    int l_rc = lotrs_keygen(&l_kp, l_par, k_seed_0);
    dap_assert(l_rc == 0, "keygen OK");

    /* PK must be non-zero. */
    int l_nonzero = 0;
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            if (l_kp.pk.a_hat.polys[i]->coeffs[j] != 0u) l_nonzero = 1;
        }
    }
    dap_assert(l_nonzero, "PK non-zero");

    /* Pinned SHA3-256 of PK. */
    uint8_t l_pk_buf[LOTRS_D_MAX * 8 * 2]; /* k=2 polys */
    size_t l_off = 0;
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        memcpy(l_pk_buf + l_off, l_kp.pk.a_hat.polys[i]->coeffs, l_par->d * 8u);
        l_off += l_par->d * 8u;
    }
    uint8_t l_h[32];
    s_sha3_256(l_pk_buf, l_off, l_h);
    bool l_ok = s_check_or_dump("k_expected_pk_sha3", l_h, k_expected_pk_sha3);

    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
    return l_ok;
}

/* -------------------------------------------------------------------------
 * T2. Deterministic sign + verify.
 * ---------------------------------------------------------------------- */

static bool s_test_sign_verify_deterministic(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};
    int l_rc = lotrs_keygen(&l_kp, l_par, k_seed_0);
    dap_assert(l_rc == 0, "keygen OK");

    /* Build ring with single PK. */
    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = 1; l_ring.T = 1;
    l_ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, 1);
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);
    }

    /* Sign. */
    lotrs_signature_t l_sig = {0};
    uint8_t l_sign_seed[32];
    memcpy(l_sign_seed, k_sign_seed, 32u);
    l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
                      k_msg, sizeof(k_msg) - 1, l_sign_seed);
    if (l_rc == -2) {
        l_sign_seed[0] ^= 0xFF;
        l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
                          k_msg, sizeof(k_msg) - 1, l_sign_seed);
    }
    dap_assert(l_rc == 0, "sign OK");

    /* Verify. */
    l_rc = lotrs_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(l_rc == 0, "verify OK");

    /* Pinned SHA3-256 of signature. */
    uint8_t l_h[32];
    s_sha3_256(l_sig.data, l_sig.len, l_h);
    bool l_ok = s_check_or_dump("k_expected_sig_sha3", l_h, k_expected_sig_sha3);

    lotrs_signature_free(&l_sig);
    lotrs_ring_pk_free(&l_ring);
    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
    return l_ok;
}

/* -------------------------------------------------------------------------
 * T3. Wire format roundtrip.
 * ---------------------------------------------------------------------- */

static bool s_test_wire_roundtrip(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};
    int l_rc = lotrs_keygen(&l_kp, l_par, k_seed_0);
    dap_assert(l_rc == 0, "keygen OK");

    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = 1; l_ring.T = 1;
    l_ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, 1);
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);
    }

    lotrs_signature_t l_sig = {0};
    uint8_t l_sign_seed[32];
    memcpy(l_sign_seed, k_sign_seed, 32u);
    l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
                      k_msg, sizeof(k_msg) - 1, l_sign_seed);
    if (l_rc == -2) { l_sign_seed[0] ^= 0xFF; l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u, k_msg, sizeof(k_msg) - 1, l_sign_seed); }
    dap_assert(l_rc == 0, "sign OK");

    /* Wire size = header + poly data. */
    uint32_t l_wire = lotrs_wire_size(l_par);
    dap_assert(l_sig.len + LOTRS_WIRE_HEADER_BYTES == (size_t)l_wire,
               "wire size matches");

    /* Header pack/unpack. */
    lotrs_wire_header_t l_hdr = {
        .magic = LOTRS_WIRE_MAGIC, .version = LOTRS_WIRE_VERSION,
        .params_id = LOTRS_WIRE_PARAMS_ID, .d = l_par->d,
        .N = l_par->beta, .T = l_par->T, .flags = 0u,
    };
    uint8_t l_hdr_buf[LOTRS_WIRE_HEADER_BYTES];
    l_rc = lotrs_wire_header_pack(l_hdr_buf, sizeof(l_hdr_buf), &l_hdr);
    dap_assert(l_rc == 0, "header pack OK");

    lotrs_wire_header_t l_hdr2 = {0};
    l_rc = lotrs_wire_header_unpack(&l_hdr2, l_hdr_buf, sizeof(l_hdr_buf));
    dap_assert(l_rc == 0, "header unpack OK");
    dap_assert(l_hdr2.magic == l_hdr.magic, "roundtrip magic");
    dap_assert(l_hdr2.d == l_hdr.d, "roundtrip d");

    /* Pinned SHA3-256 of full wire (header + sig). */
    uint8_t *l_wire_buf = DAP_NEW_Z_SIZE(uint8_t, LOTRS_WIRE_HEADER_BYTES + l_sig.len);
    memcpy(l_wire_buf, l_hdr_buf, LOTRS_WIRE_HEADER_BYTES);
    memcpy(l_wire_buf + LOTRS_WIRE_HEADER_BYTES, l_sig.data, l_sig.len);
    uint8_t l_h[32];
    s_sha3_256(l_wire_buf, LOTRS_WIRE_HEADER_BYTES + l_sig.len, l_h);
    bool l_ok = s_check_or_dump("k_expected_wire_sha3", l_h, k_expected_wire_sha3);

    DAP_DELETE(l_wire_buf);
    lotrs_signature_free(&l_sig);
    lotrs_ring_pk_free(&l_ring);
    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
    return l_ok;
}

/* -------------------------------------------------------------------------
 * T4. Negative: wrong message.
 * ---------------------------------------------------------------------- */

static bool s_test_wrong_message(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};
    lotrs_keygen(&l_kp, l_par, k_seed_0);

    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = 1; l_ring.T = 1;
    l_ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, 1);
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i)
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);

    lotrs_signature_t l_sig = {0};
    uint8_t l_seed[32]; memcpy(l_seed, k_sign_seed, 32u);
    lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u, k_msg, sizeof(k_msg) - 1, l_seed);

    const uint8_t l_bad[] = "wrong-message";
    int l_rc = lotrs_verify(&l_sig, l_par, &l_ring, l_bad, sizeof(l_bad) - 1);
    dap_assert(l_rc != 0, "wrong message fails");

    lotrs_signature_free(&l_sig);
    lotrs_ring_pk_free(&l_ring);
    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
    return true;
}

/* -------------------------------------------------------------------------
 * T5. Negative: NULL params.
 * ---------------------------------------------------------------------- */

static bool s_test_null_params(void)
{
    dap_assert(lotrs_keygen(NULL, NULL, NULL) != 0, "keygen NULL fails");
    dap_assert(lotrs_sign(NULL, NULL, NULL, NULL, 0, NULL, 0, NULL) != 0, "sign NULL fails");
    dap_assert(lotrs_verify(NULL, NULL, NULL, NULL, 0) != 0, "verify NULL fails");
    return true;
}

/* -------------------------------------------------------------------------
 * main.
 * ---------------------------------------------------------------------- */

int main(void)
{
    dap_set_appname("test_lotrs_kat");
    dap_common_init("test_lotrs_kat", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    int rc = 0;
    if (!s_test_keygen_deterministic())   rc = 1;
    if (!s_test_sign_verify_deterministic()) rc = 1;
    if (!s_test_wire_roundtrip())         rc = 1;
    if (!s_test_wrong_message())          rc = 1;
    if (!s_test_null_params())            rc = 1;

    if (s_dump_mode()) {
        log_it(L_WARNING, "LOTRS_KAT_DUMP active: dump is not a pass");
        rc = 1;
    } else if (rc == 0) {
        log_it(L_INFO, "=== ALL LoTRS KAT tests PASSED ===");
    }

    dap_common_deinit();
    return rc;
}
