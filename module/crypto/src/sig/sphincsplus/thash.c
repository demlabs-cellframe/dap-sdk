

#include "thash.h"
#include "params.h"
#include "dap_common.h"

#define LOG_TAG "dap_enc_sig_sphincsplus_thash"
/**
 * Takes an array of inblocks concatenated arrays of SPX_N bytes.
 */
#ifdef SPHINCSPLUS_FLEX
void thash(unsigned char *out, const unsigned char *in, unsigned int inblocks,
           const spx_ctx *ctx, uint32_t addr[8])
{
    if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_HARAKA_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_HARAKA_256S) {
        SPHINCSPLUS_DIFFICULTY == SPHINCSPLUS_ROBUST ? thash_haraka_robust(out, in, inblocks, ctx, addr) : thash_haraka_simple(out, in, inblocks, ctx, addr);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHA2_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHA2_256S) {
        SPHINCSPLUS_DIFFICULTY == SPHINCSPLUS_ROBUST ? thash_sha2_robust(out, in, inblocks, ctx, addr) : thash_sha2_simple(out, in, inblocks, ctx, addr);
    } else if(SPHINCSPLUS_CONFIG >= SPHINCSPLUS_SHAKE_128F && SPHINCSPLUS_CONFIG <= SPHINCSPLUS_SHAKE_256S) {
        SPHINCSPLUS_DIFFICULTY == SPHINCSPLUS_ROBUST ? thash_shake_robust(out, in, inblocks, ctx, addr) : thash_shake_simple(out, in, inblocks, ctx, addr);
    } else {
        log_it(L_ERROR, "Wrong sphincplus sig config");
    }
}
#endif
