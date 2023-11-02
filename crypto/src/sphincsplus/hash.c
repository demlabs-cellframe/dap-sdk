
#include "hash.h"
#include "dap_common.h"
#define LOG_TAG "dap_enc_sig_sphincsplus_hash"

#ifdef SPHINCSPLUS_FLEX

void initialize_hash_function(spx_ctx *ctx)
{
    if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_HARAKA_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_HARAKA_256S) {
        initialize_hash_function_haraka(ctx);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHA2_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHA2_256S) {
        initialize_hash_function_sha2(ctx);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHAKE_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHAKE_256S) {
        initialize_hash_function_shake(ctx);
    } else {
        log_it(L_ERROR, "Wrong sphincplus sig config");
    }
}

void prf_addr(unsigned char *out, const spx_ctx *ctx, const uint32_t addr[8])
{
    if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_HARAKA_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_HARAKA_256S) {
        prf_addr_haraka(out, ctx, addr);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHA2_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHA2_256S) {
        prf_addr_sha2(out, ctx, addr);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHAKE_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHAKE_256S) {
        prf_addr_shake(out, ctx, addr);
    } else {
        log_it(L_ERROR, "Wrong sphincplus sig config");
    }
}

void gen_message_random(unsigned char *R, const unsigned char *sk_prf,
                        const unsigned char *optrand,
                        const unsigned char *m, unsigned long long mlen,
                        const spx_ctx *ctx)
{
    if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_HARAKA_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_HARAKA_256S) {
        gen_message_random_haraka(R, sk_prf, optrand, m, mlen, ctx);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHA2_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHA2_256S) {
        gen_message_random_sha2(R, sk_prf, optrand, m, mlen, ctx);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHAKE_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHAKE_256S) {
        gen_message_random_shake(R, sk_prf, optrand, m, mlen, ctx);
    } else {
        log_it(L_ERROR, "Wrong sphincplus sig config");
    }
}

void hash_message(unsigned char *digest, uint64_t *tree, uint32_t *leaf_idx,
                  const unsigned char *R, const unsigned char *pk,
                  const unsigned char *m, unsigned long long mlen,
                  const spx_ctx *ctx)
{
    if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_HARAKA_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_HARAKA_256S) {
        hash_message_haraka(digest, tree, leaf_idx, R, pk, m, mlen, ctx);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHA2_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHA2_256S) {
        hash_message_sha2(digest, tree, leaf_idx, R, pk, m, mlen, ctx);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHAKE_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHAKE_256S) {
        hash_message_shake(digest, tree, leaf_idx, R, pk, m, mlen, ctx);
    } else {
        log_it(L_ERROR, "Wrong sphincplus sig config");
    }
}
#endif
