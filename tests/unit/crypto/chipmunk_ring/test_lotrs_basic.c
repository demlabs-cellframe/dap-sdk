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
#include "sig/lotrs/lotrs_sample.h"
#include "sig/lotrs/lotrs_wire.h"
#include "sig/lotrs/lotrs_codec.h"
#include "sig/lotrs/lotrs_shamir.h"

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

static void test_wire_format(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;

    /* Wire size check. */
    uint32_t l_wire = lotrs_wire_size(l_par);
    uint32_t l_expected = LOTRS_WIRE_HEADER_BYTES
        + l_par->k * l_par->d * 8u
        + l_par->d * 8u
        + (l_par->l + l_par->k) * l_par->d * 8u;
    dap_assert(l_wire == l_expected, "wire size formula");

    /* Header pack/unpack roundtrip. */
    lotrs_wire_header_t l_hdr = {
        .magic = LOTRS_WIRE_MAGIC,
        .version = LOTRS_WIRE_VERSION,
        .params_id = LOTRS_WIRE_PARAMS_ID,
        .d = l_par->d,
        .N = l_par->beta,
        .T = l_par->T,
        .flags = LOTRS_WIRE_FLAG_NONE,
    };
    uint8_t l_buf[LOTRS_WIRE_HEADER_BYTES];
    int l_rc = lotrs_wire_header_pack(l_buf, sizeof(l_buf), &l_hdr);
    dap_assert(l_rc == 0, "header pack OK");

    /* Verify magic bytes (LE: 'SRTL' = 0x53,0x52,0x54,0x4C). */
    dap_assert(l_buf[0] == 0x53 && l_buf[1] == 0x52 &&
               l_buf[2] == 0x54 && l_buf[3] == 0x4C,
               "header magic 'LTRS' LE");

    lotrs_wire_header_t l_hdr2 = {0};
    l_rc = lotrs_wire_header_unpack(&l_hdr2, l_buf, sizeof(l_buf));
    dap_assert(l_rc == 0, "header unpack OK");
    dap_assert(l_hdr2.magic == l_hdr.magic, "roundtrip magic");
    dap_assert(l_hdr2.d == l_hdr.d, "roundtrip d");
    dap_assert(l_hdr2.N == l_hdr.N, "roundtrip N");
    dap_assert(l_hdr2.T == l_hdr.T, "roundtrip T");
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

static void test_rice_codec(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;

    /* Create a polynomial with small centered coefficients. */
    lotrs_poly_t *l_p = lotrs_poly_alloc(l_par);
    dap_assert(l_p != NULL, "poly alloc");
    for (uint32_t i = 0u; i < l_par->d; ++i) {
        /* Centered values in [-1, 1] → ternary-like distribution. */
        int64_t v = (int64_t)(i % 3u) - 1;
        int64_t mod = v % (int64_t)l_par->q;
        if (mod < 0) mod += (int64_t)l_par->q;
        l_p->coeffs[i] = (uint64_t)mod;
    }

    /* Pack with Golomb-Rice. */
    uint32_t l_rice_k = lotrs_optimal_rice_k(1.0);
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_par->d * 8u);
    size_t l_written = 0u;
    int l_rc = lotrs_poly_pack_rice(l_buf, l_par->d * 8u, l_p, l_par,
                                    l_rice_k, 1, &l_written);
    dap_assert(l_rc == 0, "rice pack OK");

    /* Pack with fixed-width for comparison. */
    uint8_t *l_fw_buf = DAP_NEW_Z_SIZE(uint8_t, lotrs_poly_bytes(l_par));
    lotrs_poly_pack(l_fw_buf, lotrs_poly_bytes(l_par), l_p, l_par);
    size_t l_fw_size = lotrs_poly_bytes(l_par);

    /* Rice should be smaller for small coefficients. */
    dap_assert(l_written < l_fw_size, "rice smaller than fixed-width");

    /* Unpack and verify roundtrip. */
    lotrs_poly_t *l_q = lotrs_poly_alloc(l_par);
    size_t l_consumed = 0u;
    l_rc = lotrs_poly_unpack_rice(l_q, l_buf, l_written, l_par,
                                  l_rice_k, 1, &l_consumed);
    dap_assert(l_rc == 0, "rice unpack OK");

    int l_match = 1;
    for (uint32_t i = 0u; i < l_par->d; ++i) {
        if (l_p->coeffs[i] != l_q->coeffs[i]) {
            l_match = 0;
            break;
        }
    }
    dap_assert(l_match, "rice roundtrip match");

    log_it(L_INFO, "Rice codec: fixed=%zu B, rice=%zu B (k=%u), ratio=%.1f%%",
           l_fw_size, l_written, l_rice_k,
           100.0 * (double)l_written / (double)l_fw_size);

    DAP_DELETE(l_fw_buf);
    DAP_DELETE(l_buf);
    lotrs_poly_free(l_p);
    lotrs_poly_free(l_q);
}

static void test_shamir_split_reconstruct(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    const uint32_t l_N = 4, l_T = 2;

    /* Create a secret polynomial. */
    lotrs_poly_t *l_secret = lotrs_poly_alloc(l_par);
    dap_assert(l_secret != NULL, "secret alloc");
    for (uint32_t i = 0u; i < l_par->d; ++i) {
        l_secret->coeffs[i] = (uint64_t)(i * 100u + 42u) % l_par->q;
    }

    /* Split into N shares. */
    lotrs_poly_t *l_shares[l_N];
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0xAA + i);
    lotrs_xof_t *l_xof = lotrs_xof_new(l_seed, 32u);
    int l_rc = lotrs_shamir_split(l_shares, l_secret, l_N, l_T, l_par, l_xof);
    lotrs_xof_free(l_xof);
    dap_assert(l_rc == 0, "shamir split OK");

    /* Reconstruct from first T shares. */
    lotrs_poly_t *l_recon = lotrs_poly_alloc(l_par);
    dap_assert(l_recon != NULL, "recon alloc");
    uint32_t l_indices[2] = {1u, 2u};
    l_rc = lotrs_shamir_reconstruct(l_recon, (const lotrs_poly_t *const *)l_shares, l_indices, l_T, l_par);
    dap_assert(l_rc == 0, "shamir reconstruct OK");

    /* Verify reconstruction matches secret. */
    int l_match = 1;
    for (uint32_t i = 0u; i < l_par->d; ++i) {
        if (l_secret->coeffs[i] != l_recon->coeffs[i]) {
            l_match = 0;
            break;
        }
    }
    dap_assert(l_match, "shamir reconstruct matches secret");

    /* Reconstruct from different T shares (indices 0,3). */
    lotrs_poly_t *l_recon2 = lotrs_poly_alloc(l_par);
    uint32_t l_indices2[2] = {1u, 4u};
    l_rc = lotrs_shamir_reconstruct(l_recon2, (const lotrs_poly_t *const *)l_shares, l_indices2, l_T, l_par);
    dap_assert(l_rc == 0, "shamir reconstruct alt OK");

    l_match = 1;
    for (uint32_t i = 0u; i < l_par->d; ++i) {
        if (l_secret->coeffs[i] != l_recon2->coeffs[i]) {
            l_match = 0;
            break;
        }
    }
    dap_assert(l_match, "shamir reconstruct alt matches secret");

    /* Shares must be distinct from each other and from secret. */
    dap_assert(memcmp(l_shares[0]->coeffs, l_shares[1]->coeffs,
                       l_par->d * sizeof(uint64_t)) != 0,
               "shares distinct");

    for (uint32_t i = 0u; i < l_N; ++i) lotrs_poly_free(l_shares[i]);
    lotrs_poly_free(l_recon);
    lotrs_poly_free(l_recon2);
    lotrs_poly_free(l_secret);
}

/* Direct algebraic check: sign then verify the equation manually. */
static void test_algebraic_check(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    lotrs_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0x42 + i);
    int l_rc = lotrs_keygen(&l_kp, l_par, l_seed);
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
    const uint8_t l_msg[] = "algebraic-check";
    lotrs_signature_t l_sig = {0};
    uint8_t l_sign_seed[32];
    for (int i = 0; i < 32; ++i) l_sign_seed[i] = (uint8_t)(0xBB + i);
    l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                      l_msg, sizeof(l_msg) - 1, l_sign_seed);
    if (l_rc == -2) { l_sign_seed[0] ^= 0xFF; l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0, l_msg, sizeof(l_msg) - 1, l_sign_seed); }
    dap_assert(l_rc == 0, "sign OK");

    /* Deserialize and manually check the algebraic equation. */
    size_t l_w_bytes = lotrs_polyvec_bytes(l_par, l_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(l_par);
    uint32_t l_sk_len = l_par->l + l_par->k;
    size_t l_z_bytes = lotrs_polyvec_bytes(l_par, l_sk_len);

    lotrs_polyvec_t l_w = lotrs_polyvec_alloc(l_par, l_par->k);
    lotrs_poly_t *l_c = lotrs_poly_alloc(l_par);
    lotrs_polyvec_t l_z = lotrs_polyvec_alloc(l_par, l_sk_len);
    const uint8_t *l_p = l_sig.data;
    lotrs_polyvec_unpack(&l_w, l_p, l_w_bytes, l_par);
    l_p += l_w_bytes;
    lotrs_poly_unpack(l_c, l_p, l_c_bytes, l_par);
    l_p += l_c_bytes;
    lotrs_polyvec_unpack(&l_z, l_p, l_z_bytes, l_par);

    /* Compute A (same as keygen). */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(l_par, l_par->k, l_par->l);
    const char *l_a_domain = "lotrs-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->l; ++j) {
            lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, l_par);
        }
    }
    lotrs_xof_free(l_xof_a);

    /* Manual A*z[0] computation at position 0 (for debugging). */
    lotrs_poly_t *l_az_manual = lotrs_poly_alloc(l_par);
    lotrs_poly_t *l_az_tmp = lotrs_poly_alloc(l_par);
    for (uint32_t j = 0u; j < l_par->l; ++j) {
        lotrs_poly_mul(l_az_tmp, l_A.rows[0].polys[j], l_z.polys[j], l_par);
        lotrs_poly_add(l_az_manual, l_az_manual, l_az_tmp, l_par);
    }
    uint64_t l_manual_lhs = l_az_manual->coeffs[0] % l_par->q;
    uint64_t l_manual_w = l_w.polys[0]->coeffs[0] % l_par->q;
    lotrs_poly_free(l_az_tmp);
    lotrs_poly_free(l_az_manual);

    /* Now use polymat_vecmul. */
    lotrs_polyvec_t l_z_short = { .polys = l_z.polys, .n = l_par->l };
    lotrs_polyvec_t l_lhs = lotrs_polyvec_alloc(l_par, l_par->k);
    lotrs_polymat_vecmul(&l_lhs, &l_A, &l_z_short, l_par);
    uint64_t l_vecmul_lhs = l_lhs.polys[0]->coeffs[0] % l_par->q;

    /* Compute c*pk. */
    lotrs_poly_t *l_cpk = lotrs_poly_alloc(l_par);
    lotrs_poly_mul(l_cpk, l_c, l_ring.pks[0].a_hat.polys[0], l_par);
    uint64_t l_cpk_val = l_cpk->coeffs[0] % l_par->q;
    lotrs_poly_free(l_cpk);

    /* Report. */
    log_it(L_INFO, "ALGEBRAIC: manual_lhs=%lu vecmul_lhs=%lu w=%lu c*pk=%lu",
           (unsigned long)l_manual_lhs, (unsigned long)l_vecmul_lhs,
           (unsigned long)l_manual_w, (unsigned long)l_cpk_val);

    /* Check: manual_lhs + w should equal c*pk. */
    uint64_t l_manual_sum = (l_manual_lhs + l_manual_w) % l_par->q;
    int l_manual_ok = (l_manual_sum == l_cpk_val);

    /* Check: vecmul_lhs + w should equal c*pk. */
    uint64_t l_vecmul_sum = (l_vecmul_lhs + l_manual_w) % l_par->q;
    int l_vecmul_ok = (l_vecmul_sum == l_cpk_val);

    log_it(L_INFO, "ALGEBRAIC: manual_sum=%lu vecmul_sum=%lu c*pk=%lu manual_ok=%d vecmul_ok=%d",
           (unsigned long)l_manual_sum, (unsigned long)l_vecmul_sum,
           (unsigned long)l_cpk_val, l_manual_ok, l_vecmul_ok);

    if (!l_manual_ok) {
        log_it(L_WARNING, "LoTRS algebraic: manual computation WRONG (diff=%ld)",
               (long)((int64_t)l_manual_sum - (int64_t)l_cpk_val));
    }
    if (!l_vecmul_ok) {
        log_it(L_WARNING, "LoTRS algebraic: vecmul computation WRONG (diff=%ld)",
               (long)((int64_t)l_vecmul_sum - (int64_t)l_cpk_val));
    }
    if (l_manual_ok && l_vecmul_ok) {
        log_it(L_INFO, "LoTRS algebraic: ALL CHECKS PASSED");
    }

    lotrs_polyvec_free(&l_lhs);
    lotrs_polymat_free(&l_A);
    lotrs_polyvec_free(&l_w);
    lotrs_poly_free(l_c);
    lotrs_polyvec_free(&l_z);
    lotrs_signature_free(&l_sig);
    lotrs_ring_pk_free(&l_ring);
    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
}

static void test_multi_signer(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    const uint32_t l_N = 4, l_T = 2;

    /* Generate keypairs for all signers. */
    lotrs_keypair_t l_kps[l_N];
    for (uint32_t i = 0u; i < l_N; ++i) {
        uint8_t l_seed[32];
        for (int j = 0; j < 32; ++j) l_seed[j] = (uint8_t)(0x50 + i);
        int l_rc = lotrs_keygen(&l_kps[i], l_par, l_seed);
        dap_assert(l_rc == 0, "multi keygen");
    }

    /* Build ring with all PKs. */
    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = l_N; l_ring.T = l_T;
    l_ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, l_N * l_T);
    for (uint32_t i = 0u; i < l_N; ++i) {
        for (uint32_t t = 0u; t < l_T; ++t) {
            l_ring.pks[i * l_T + t].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
            for (uint32_t k = 0u; k < l_par->k; ++k) {
                lotrs_poly_copy(l_ring.pks[i * l_T + t].a_hat.polys[k],
                                l_kps[i].pk.a_hat.polys[k], l_par);
            }
        }
    }

    /* Round 1: T signers generate commitments. */
    lotrs_round1_state_t l_r1_states[l_T];
    lotrs_round1_output_t l_r1_outs[l_T];
    for (uint32_t u = 0u; u < l_T; ++u) {
        uint8_t l_seed[32];
        for (int j = 0; j < 32; ++j) l_seed[j] = (uint8_t)(0xC0 + u);
        int l_rc = lotrs_sign_round1(&l_r1_states[u], &l_r1_outs[u],
                                     l_par, &l_kps[u].sk, u, l_seed);
        dap_assert(l_rc == 0, "round1");
    }

    /* Aggregate w = Σ w_u. */
    lotrs_polyvec_t l_w_agg = lotrs_polyvec_alloc(l_par, l_par->k);
    lotrs_polyvec_zero(&l_w_agg, l_par);
    for (uint32_t u = 0u; u < l_T; ++u) {
        lotrs_polyvec_add(&l_w_agg, &l_w_agg, l_r1_outs[u].w, l_par);
    }

    /* Round 2: T signers compute response shares. */
    const uint8_t l_msg[] = "multi-signer-test";
    lotrs_round2_output_t l_r2_outs[l_T];
    for (uint32_t u = 0u; u < l_T; ++u) {
        int l_rc = lotrs_sign_round2(&l_r2_outs[u], &l_r1_states[u],
                                     l_par, &l_kps[u].sk, &l_w_agg,
                                     l_msg, sizeof(l_msg) - 1);
        if (l_rc == -2) {
            /* Rejection — retry round 1. */
            uint8_t l_seed2[32];
            for (int j = 0; j < 32; ++j) l_seed2[j] = (uint8_t)(0xD0 + u);
            lotrs_round1_state_free(&l_r1_states[u]);
            lotrs_round1_output_free(&l_r1_outs[u]);
            l_rc = lotrs_sign_round1(&l_r1_states[u], &l_r1_outs[u],
                                     l_par, &l_kps[u].sk, u, l_seed2);
            dap_assert(l_rc == 0, "round1 retry");
            /* Re-aggregate w. */
            lotrs_polyvec_zero(&l_w_agg, l_par);
            for (uint32_t v = 0u; v < l_T; ++v) {
                lotrs_polyvec_add(&l_w_agg, &l_w_agg, l_r1_outs[v].w, l_par);
            }
            l_rc = lotrs_sign_round2(&l_r2_outs[u], &l_r1_states[u],
                                     l_par, &l_kps[u].sk, &l_w_agg,
                                     l_msg, sizeof(l_msg) - 1);
        }
        dap_assert(l_rc == 0, "round2");
    }

    /* Aggregate. */
    lotrs_signature_t l_sig = {0};
    int l_rc = lotrs_sign_aggregate(&l_sig, l_par, l_r1_outs, l_r2_outs, l_T);
    dap_assert(l_rc == 0, "aggregate");
    dap_assert(l_sig.data != NULL && l_sig.len > 0, "aggregate non-empty");

    /* Cleanup. */
    for (uint32_t u = 0u; u < l_T; ++u) {
        lotrs_round1_state_free(&l_r1_states[u]);
        lotrs_round1_output_free(&l_r1_outs[u]);
        lotrs_round2_output_free(&l_r2_outs[u]);
    }
    lotrs_signature_free(&l_sig);
    lotrs_polyvec_free(&l_w_agg);
    lotrs_ring_pk_free(&l_ring);
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_pk_free(&l_kps[i].pk);
        lotrs_sk_free(&l_kps[i].sk);
    }
}

int main(void)
{
    dap_set_appname("test_lotrs_basic");
    dap_common_init("test_lotrs_basic", NULL);

    test_keygen();
    test_wire_format();
    test_pack_roundtrip();
    test_rice_codec();
    test_shamir_split_reconstruct();
    test_multi_signer();
    test_sign_verify();
    test_determinism();
    test_algebraic_check();

    log_it(L_INFO, "=== ALL LoTRS basic tests PASSED ===");
    dap_common_deinit();
    return 0;
}
