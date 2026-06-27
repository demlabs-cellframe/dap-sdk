#include <dap_common.h>
#include <dap_test.h>
#include <stdint.h>
#include <string.h>
#include "sig/chipmunk/chipmunk_pedersen.h"

int main(void)
{
    dap_set_appname("test_pedersen_minimal");
    dap_common_init("test_pedersen_minimal", NULL);

    log_it(L_INFO, "About to allocate params");
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    dap_assert(l_params != NULL, "alloc OK");
    log_it(L_INFO, "Params allocated, size=%zu", sizeof(chipmunk_pedersen_params_t));

    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;

    log_it(L_INFO, "About to call init");
    int l_rc = chipmunk_pedersen_init(l_params, l_seed);
    log_it(L_INFO, "Init returned: %d", l_rc);
    dap_assert(l_rc == 0, "init OK");

    log_it(L_INFO, "=== Pedersen minimal test PASSED ===");
    DAP_DELETE(l_params);
    dap_common_deinit();
    return 0;
}
