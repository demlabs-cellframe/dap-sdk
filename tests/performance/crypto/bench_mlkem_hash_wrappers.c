#include <stdint.h>
#include <stddef.h>
#include "dap_mlkem_params.h"
#include "dap_mlkem_symmetric.h"

void dap_mlkem_hash_h_bench(uint8_t *out, const uint8_t *in, size_t len) {
    dap_mlkem_hash_h(out, in, len);
}

void dap_mlkem_hash_g_bench(uint8_t *out, const uint8_t *in, size_t len) {
    dap_mlkem_hash_g(out, in, len);
}

void dap_mlkem_kdf_bench(uint8_t *out, const uint8_t *in, size_t len) {
    dap_mlkem_kdf(out, in, len);
}
