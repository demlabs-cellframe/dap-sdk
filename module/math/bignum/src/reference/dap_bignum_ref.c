/*
 * Reference multi-precision integer arithmetic — portable C, no assembly.
 *
 * Limbs are 32-bit unsigned, stored little-endian (limbs[0] is LSB).
 * nlimbs tracks the number of significant limbs (no leading zeros).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_bignum.h"
#include <string.h>

static void s_strip(dap_bignum_t *a_bn)
{
    while (a_bn->nlimbs > 0 && a_bn->limbs[a_bn->nlimbs - 1] == 0)
        a_bn->nlimbs--;
    if (a_bn->nlimbs == 0)
        a_bn->sign = 0;
}

void dap_bignum_init(dap_bignum_t *a_bn)
{
    memset(a_bn, 0, sizeof(*a_bn));
}

void dap_bignum_set_u64(dap_bignum_t *a_bn, uint64_t a_val)
{
    dap_bignum_init(a_bn);
    a_bn->limbs[0] = (uint32_t)(a_val & 0xFFFFFFFF);
    a_bn->limbs[1] = (uint32_t)(a_val >> 32);
    a_bn->nlimbs = a_bn->limbs[1] ? 2 : (a_bn->limbs[0] ? 1 : 0);
}

void dap_bignum_set_i32(dap_bignum_t *a_bn, int32_t a_val)
{
    dap_bignum_init(a_bn);
    if (a_val < 0) {
        a_bn->sign = 1;
        a_bn->limbs[0] = (uint32_t)(-(int64_t)a_val);
    } else {
        a_bn->limbs[0] = (uint32_t)a_val;
    }
    a_bn->nlimbs = a_bn->limbs[0] ? 1 : 0;
}

void dap_bignum_copy(dap_bignum_t *a_dst, const dap_bignum_t *a_src)
{
    memcpy(a_dst, a_src, sizeof(*a_dst));
}

int dap_bignum_is_zero(const dap_bignum_t *a_bn)
{
    return a_bn->nlimbs == 0;
}

/* Unsigned magnitude comparison: -1, 0, +1 */
static int s_cmp_abs(const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    if (a_a->nlimbs != a_b->nlimbs)
        return a_a->nlimbs > a_b->nlimbs ? 1 : -1;
    for (int i = a_a->nlimbs - 1; i >= 0; i--) {
        if (a_a->limbs[i] != a_b->limbs[i])
            return a_a->limbs[i] > a_b->limbs[i] ? 1 : -1;
    }
    return 0;
}

int dap_bignum_cmp(const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    if (a_a->sign != a_b->sign) {
        if (dap_bignum_is_zero(a_a) && dap_bignum_is_zero(a_b))
            return 0;
        return a_a->sign ? -1 : 1;
    }
    int l_c = s_cmp_abs(a_a, a_b);
    return a_a->sign ? -l_c : l_c;
}

/* Unsigned add: |a| + |b| -> r  (ignores signs) */
static void s_add_abs(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    uint64_t l_carry = 0;
    int l_max = a_a->nlimbs > a_b->nlimbs ? a_a->nlimbs : a_b->nlimbs;
    for (int i = 0; i < l_max || l_carry; i++) {
        uint64_t l_sum = l_carry;
        if (i < a_a->nlimbs) l_sum += a_a->limbs[i];
        if (i < a_b->nlimbs) l_sum += a_b->limbs[i];
        a_r->limbs[i] = (uint32_t)(l_sum & 0xFFFFFFFF);
        l_carry = l_sum >> 32;
        if (i + 1 > a_r->nlimbs) a_r->nlimbs = i + 1;
    }
    if (l_carry && a_r->nlimbs < DAP_BIGNUM_MAX_LIMBS) {
        a_r->limbs[a_r->nlimbs] = (uint32_t)l_carry;
        a_r->nlimbs++;
    }
}

/* Unsigned sub: |a| - |b| -> r, assumes |a| >= |b| */
static void s_sub_abs(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    int64_t l_borrow = 0;
    for (int i = 0; i < a_a->nlimbs; i++) {
        int64_t l_diff = (int64_t)a_a->limbs[i] - l_borrow;
        if (i < a_b->nlimbs) l_diff -= a_b->limbs[i];
        if (l_diff < 0) {
            l_diff += (int64_t)1 << 32;
            l_borrow = 1;
        } else {
            l_borrow = 0;
        }
        a_r->limbs[i] = (uint32_t)l_diff;
    }
    a_r->nlimbs = a_a->nlimbs;
    s_strip(a_r);
}

int dap_bignum_add(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    dap_bignum_t l_tmp;
    dap_bignum_init(&l_tmp);

    if (a_a->sign == a_b->sign) {
        s_add_abs(&l_tmp, a_a, a_b);
        l_tmp.sign = a_a->sign;
    } else {
        int l_c = s_cmp_abs(a_a, a_b);
        if (l_c >= 0) {
            s_sub_abs(&l_tmp, a_a, a_b);
            l_tmp.sign = a_a->sign;
        } else {
            s_sub_abs(&l_tmp, a_b, a_a);
            l_tmp.sign = a_b->sign;
        }
    }
    s_strip(&l_tmp);
    *a_r = l_tmp;
    return 0;
}

int dap_bignum_sub(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    dap_bignum_t l_neg_b;
    dap_bignum_copy(&l_neg_b, a_b);
    l_neg_b.sign ^= 1;
    if (dap_bignum_is_zero(&l_neg_b))
        l_neg_b.sign = 0;
    return dap_bignum_add(a_r, a_a, &l_neg_b);
}

int dap_bignum_mul(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    dap_bignum_t l_tmp;
    dap_bignum_init(&l_tmp);

    for (int i = 0; i < a_a->nlimbs; i++) {
        uint64_t l_carry = 0;
        for (int j = 0; j < a_b->nlimbs; j++) {
            int l_k = i + j;
            if (l_k >= DAP_BIGNUM_MAX_LIMBS)
                return -1;
            uint64_t l_prod = (uint64_t)a_a->limbs[i] * a_b->limbs[j]
                            + l_tmp.limbs[l_k] + l_carry;
            l_tmp.limbs[l_k] = (uint32_t)(l_prod & 0xFFFFFFFF);
            l_carry = l_prod >> 32;
        }
        int l_k = i + a_b->nlimbs;
        if (l_carry && l_k < DAP_BIGNUM_MAX_LIMBS)
            l_tmp.limbs[l_k] += (uint32_t)l_carry;
    }
    l_tmp.nlimbs = a_a->nlimbs + a_b->nlimbs;
    if (l_tmp.nlimbs > DAP_BIGNUM_MAX_LIMBS)
        l_tmp.nlimbs = DAP_BIGNUM_MAX_LIMBS;
    l_tmp.sign = a_a->sign ^ a_b->sign;
    s_strip(&l_tmp);
    *a_r = l_tmp;
    return 0;
}

/* Unsigned left-shift by 32*shift limbs */
static void s_shl_limbs(dap_bignum_t *a_bn, int a_shift)
{
    if (a_shift <= 0) return;
    for (int i = a_bn->nlimbs - 1; i >= 0; i--) {
        if (i + a_shift < DAP_BIGNUM_MAX_LIMBS)
            a_bn->limbs[i + a_shift] = a_bn->limbs[i];
        a_bn->limbs[i] = 0;
    }
    a_bn->nlimbs += a_shift;
    if (a_bn->nlimbs > DAP_BIGNUM_MAX_LIMBS)
        a_bn->nlimbs = DAP_BIGNUM_MAX_LIMBS;
}

/* Unsigned left-shift by 1 bit */
static void s_shl1(dap_bignum_t *a_bn)
{
    uint32_t l_carry = 0;
    for (int i = 0; i < a_bn->nlimbs; i++) {
        uint32_t l_new_carry = a_bn->limbs[i] >> 31;
        a_bn->limbs[i] = (a_bn->limbs[i] << 1) | l_carry;
        l_carry = l_new_carry;
    }
    if (l_carry && a_bn->nlimbs < DAP_BIGNUM_MAX_LIMBS) {
        a_bn->limbs[a_bn->nlimbs] = l_carry;
        a_bn->nlimbs++;
    }
}

/* Unsigned right-shift by 1 bit */
static void s_shr1(dap_bignum_t *a_bn)
{
    for (int i = 0; i < a_bn->nlimbs; i++) {
        a_bn->limbs[i] >>= 1;
        if (i + 1 < a_bn->nlimbs)
            a_bn->limbs[i] |= (a_bn->limbs[i + 1] & 1) << 31;
    }
    s_strip(a_bn);
}

/*
 * Unsigned division via binary long division: |a| = |m| * q + r.
 * Sets a_r = |a| mod |m|.
 */
static int s_divmod(dap_bignum_t *a_q, dap_bignum_t *a_rem,
                    const dap_bignum_t *a_a, const dap_bignum_t *a_m)
{
    if (dap_bignum_is_zero(a_m))
        return -1;

    dap_bignum_t l_a_copy;
    const dap_bignum_t *l_a = a_a;
    if (a_a == a_rem || a_a == a_q) {
        dap_bignum_copy(&l_a_copy, a_a);
        l_a = &l_a_copy;
    }

    dap_bignum_init(a_q);
    dap_bignum_init(a_rem);

    if (s_cmp_abs(l_a, a_m) < 0) {
        dap_bignum_copy(a_rem, l_a);
        a_rem->sign = 0;
        return 0;
    }

    /* Count total bits in a */
    int l_bits = 0;
    {
        dap_bignum_t l_tmp_a;
        dap_bignum_copy(&l_tmp_a, l_a);
        l_tmp_a.sign = 0;
        for (int i = l_tmp_a.nlimbs - 1; i >= 0; i--) {
            if (l_tmp_a.limbs[i]) {
                uint32_t l_v = l_tmp_a.limbs[i];
                int l_b = 0;
                while (l_v) { l_b++; l_v >>= 1; }
                l_bits = i * 32 + l_b;
                break;
            }
        }
    }

    dap_bignum_t l_rem;
    dap_bignum_init(&l_rem);

    for (int i = l_bits - 1; i >= 0; i--) {
        s_shl1(&l_rem);
        int l_limb_idx = i / 32;
        int l_bit_idx = i % 32;
        if (l_a->limbs[l_limb_idx] & (1U << l_bit_idx))
            l_rem.limbs[0] |= 1;
        if (l_rem.nlimbs == 0 && l_rem.limbs[0])
            l_rem.nlimbs = 1;
        s_strip(&l_rem);

        if (s_cmp_abs(&l_rem, a_m) >= 0) {
            s_sub_abs(&l_rem, &l_rem, a_m);
            int l_ql = i / 32;
            int l_qb = i % 32;
            a_q->limbs[l_ql] |= (1U << l_qb);
            if (l_ql + 1 > a_q->nlimbs)
                a_q->nlimbs = l_ql + 1;
        }
    }

    s_strip(a_q);
    s_strip(&l_rem);
    *a_rem = l_rem;
    return 0;
}

int dap_bignum_mod(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_m)
{
    dap_bignum_t l_q;
    int l_rc = s_divmod(&l_q, a_r, a_a, a_m);
    if (l_rc) return l_rc;

    /* For negative a, result = m - rem (Python-style mod) */
    if (a_a->sign && !dap_bignum_is_zero(a_r)) {
        dap_bignum_t l_tmp;
        dap_bignum_copy(&l_tmp, a_m);
        l_tmp.sign = 0;
        s_sub_abs(a_r, &l_tmp, a_r);
    }
    a_r->sign = 0;
    return 0;
}

int dap_bignum_mod_exp(dap_bignum_t *a_r, const dap_bignum_t *a_base,
                       const dap_bignum_t *a_exp, const dap_bignum_t *a_mod)
{
    if (dap_bignum_is_zero(a_mod))
        return -1;

    dap_bignum_t l_result, l_b, l_tmp;
    dap_bignum_set_u64(&l_result, 1);

    dap_bignum_copy(&l_b, a_base);
    l_b.sign = 0;
    dap_bignum_mod(&l_b, &l_b, a_mod);

    for (int i = 0; i < a_exp->nlimbs; i++) {
        for (int bit = 0; bit < 32; bit++) {
            if (a_exp->limbs[i] & (1U << bit)) {
                dap_bignum_mul(&l_tmp, &l_result, &l_b);
                dap_bignum_mod(&l_result, &l_tmp, a_mod);
            }
            dap_bignum_mul(&l_tmp, &l_b, &l_b);
            dap_bignum_mod(&l_b, &l_tmp, a_mod);
        }
    }

    *a_r = l_result;
    return 0;
}

int dap_bignum_gcd(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b)
{
    dap_bignum_t l_x, l_y, l_tmp;
    dap_bignum_copy(&l_x, a_a);
    dap_bignum_copy(&l_y, a_b);
    l_x.sign = 0;
    l_y.sign = 0;

    while (!dap_bignum_is_zero(&l_y)) {
        dap_bignum_mod(&l_tmp, &l_x, &l_y);
        l_x = l_y;
        l_y = l_tmp;
    }
    *a_r = l_x;
    return 0;
}

int dap_bignum_mod_inv(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_m)
{
    /*
     * Extended Euclidean algorithm for modular inverse.
     * Uses signed bignums for the Bezout coefficients.
     */
    if (dap_bignum_is_zero(a_m))
        return -1;

    dap_bignum_t l_old_r, l_r, l_old_s, l_s, l_q, l_rem, l_tmp;

    dap_bignum_copy(&l_old_r, a_a);
    l_old_r.sign = 0;
    dap_bignum_copy(&l_r, a_m);
    l_r.sign = 0;

    dap_bignum_set_u64(&l_old_s, 1);
    dap_bignum_init(&l_s);

    while (!dap_bignum_is_zero(&l_r)) {
        s_divmod(&l_q, &l_rem, &l_old_r, &l_r);

        l_old_r = l_r;
        l_r = l_rem;

        dap_bignum_mul(&l_tmp, &l_q, &l_s);
        dap_bignum_t l_new_s;
        dap_bignum_sub(&l_new_s, &l_old_s, &l_tmp);
        l_old_s = l_s;
        l_s = l_new_s;
    }

    /* gcd must be 1 for inverse to exist */
    dap_bignum_t l_one;
    dap_bignum_set_u64(&l_one, 1);
    if (dap_bignum_cmp(&l_old_r, &l_one) != 0)
        return -1;

    /* Normalize to [0, m) */
    dap_bignum_mod(a_r, &l_old_s, a_m);
    return 0;
}
