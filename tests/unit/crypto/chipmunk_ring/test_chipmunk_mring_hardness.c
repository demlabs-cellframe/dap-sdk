/*
 * test_chipmunk_mring_hardness.c — MRNG G1 gate: MSIS bit-security floor.
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Asserts that the MRV1 binding
 * instance reports at least CHIPMUNK_MRING_MSIS_BITS_MIN (= 128) classical
 * core-SVP bits.  This is the precondition for unblocking M2 (BDLOP /
 * unified-statement crypto core).
 */

#include <dap_common.h>
#include <dap_test.h>

#include <stdbool.h>
#include <stdint.h>

#include "chipmunk/chipmunk_mring_hardness.h"
#include "chipmunk/chipmunk_mring_params.h"

#define LOG_TAG "test_chipmunk_mring_hardness"

static bool s_test_msis_floor(void)
{
    const uint32_t l_bits = chipmunk_mring_hardness_msis_bits();
    log_it(L_INFO,
           "MRNG MSIS estimator (G1): classical core-SVP ≈ %u bits "
           "(floor = %u, sanity ceiling = 10000)",
           l_bits, (unsigned)CHIPMUNK_MRING_MSIS_BITS_MIN);
    dap_assert(l_bits >= CHIPMUNK_MRING_MSIS_BITS_MIN,
               "MRNG: MSIS estimator must meet 128-bit floor");
    dap_assert(l_bits != UINT32_MAX,
               "MRNG: MSIS estimator BKZ sweep must converge (no UINT32_MAX)");
    dap_assert(l_bits <= 10000u,
               "MRNG: MSIS estimator sanity ceiling (params not absurd)");
    return true;
}

static bool s_test_msis_relaxed_floor(void)
{
    /* M4.0b: the fold's relaxed binding norm β* = 2^D·β must still clear
     * the 128-bit MSIS floor at the worst-case depth D = FOLD_DEPTH_MAX.
     * Also check monotone non-increase in D and agreement with the
     * unrelaxed estimator at D = 0. */
    const uint32_t l_base    = chipmunk_mring_hardness_msis_bits();
    const uint32_t l_relaxed0 = chipmunk_mring_hardness_msis_bits_relaxed(0u);
    dap_assert(l_relaxed0 == l_base,
               "MRNG: relaxed MSIS at D=0 must equal the unrelaxed estimate");

    uint32_t l_prev = l_relaxed0;
    for (uint32_t d = 1u; d <= CHIPMUNK_MRING_FOLD_DEPTH_MAX; ++d) {
        const uint32_t l_bits = chipmunk_mring_hardness_msis_bits_relaxed(d);
        dap_assert(l_bits <= l_prev,
                   "MRNG: relaxed MSIS must be non-increasing in fold depth");
        l_prev = l_bits;
    }

    const uint32_t l_worst =
        chipmunk_mring_hardness_msis_bits_relaxed(CHIPMUNK_MRING_FOLD_DEPTH_MAX);
    log_it(L_INFO,
           "MRNG relaxed MSIS (M4.0b): β*=2^D·β at D=%u ⇒ ≈ %u bits "
           "(unrelaxed %u; floor %u)",
           (unsigned)CHIPMUNK_MRING_FOLD_DEPTH_MAX, l_worst, l_base,
           (unsigned)CHIPMUNK_MRING_MSIS_BITS_MIN);
    dap_assert(l_worst >= CHIPMUNK_MRING_MSIS_BITS_MIN,
               "MRNG: relaxed binding MSIS must meet 128-bit floor at D_max "
               "(NOGAP_LEMMA §6 obligation)");
    dap_assert(l_worst != UINT32_MAX && l_worst <= 10000u,
               "MRNG: relaxed MSIS estimator converges within sanity ceiling");

    /* clamp: depths beyond the max must not exceed the D_max result */
    dap_assert(chipmunk_mring_hardness_msis_bits_relaxed(
                   CHIPMUNK_MRING_FOLD_DEPTH_MAX + 5u) == l_worst,
               "MRNG: fold depth above max is clamped");
    return true;
}

static bool s_test_mlwe_floor(void)
{
    const uint32_t l_bits = chipmunk_mring_hardness_mlwe_bits();
    log_it(L_INFO,
           "MRNG MLWE estimator (G2 v2 §A5): classical core-SVP ≈ %u bits "
           "(floor = %u)",
           l_bits, (unsigned)CHIPMUNK_MRING_MLWE_BITS_MIN);
    dap_assert(l_bits >= CHIPMUNK_MRING_MLWE_BITS_MIN,
               "MRNG: MLWE estimator must meet 128-bit floor (Cb hiding)");
    dap_assert(l_bits != UINT32_MAX,
               "MRNG: MLWE estimator must converge");
    dap_assert(l_bits <= 10000u,
               "MRNG: MLWE estimator sanity ceiling");
    return true;
}

static bool s_test_invertibility_floor(void)
{
    /* G3.1 §9.5: this now reports the SUBTRACTIVE-SET size log₂|S| of the
     * corrected fold (S = F_{qᵉ}\{0} in R_q^{(e)}, e=6), NOT the retired
     * partial-splitting λ_inv ≈ 980.  Expected ≈ e·log₂ q ≈ 129.6 ⇒
     * floor 129.  See chipmunk_mring_hardness.c history note. */
    const uint32_t l_bits = chipmunk_mring_hardness_invertibility_bits();
    log_it(L_INFO,
           "MRNG fold subtractive-set size (G3.1 §9.5): "
           "log₂|S| ≈ %u bits (floor = %u; |S| = q^e − 1, e = 6)",
           l_bits, (unsigned)CHIPMUNK_MRING_INVERTIBILITY_BITS_MIN);
    dap_assert(l_bits >= CHIPMUNK_MRING_INVERTIBILITY_BITS_MIN,
               "MRNG: fold subtractive-set size must meet 128-bit floor");
    /* e·log₂ q with e=6, q=3168257 ⇒ 129.57 ⇒ floor 129.  Pin a tight
     * window so an accidental reversion to the 980-bit model is caught. */
    dap_assert(l_bits >= 128u && l_bits <= 131u,
               "MRNG: log₂|S| must be ≈129 (e·log₂ q); a value near 980 "
               "would mean the stale partial-splitting model crept back");
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_hardness");
    dap_common_init("test_chipmunk_mring_hardness", NULL);

    int rc = 0;
    if (!s_test_msis_floor())          rc = 1;
    if (!s_test_msis_relaxed_floor())  rc = 1;
    if (!s_test_mlwe_floor())          rc = 1;
    if (!s_test_invertibility_floor()) rc = 1;

    if (rc == 0) {
        log_it(L_INFO, "MRNG G1 + G2 v2 hardness gates PASSED");
    }
    dap_common_deinit();
    return rc;
}
