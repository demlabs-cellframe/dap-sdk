#include <stdbool.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_enc_bliss.h"
#include "dap_enc_key.h"

#define LOG_TAG "test_regression_bliss_failure_cleanup"

bool __wrap_invert_polynomial(void *state, void *output, const int32_t *input)
{
    (void)state;
    (void)output;
    (void)input;
    return false;
}

int main(void)
{
    bool l_ok = true;
    dap_enc_key_t l_key = {0};

    dap_enc_sig_bliss_key_new(&l_key);
    dap_enc_sig_bliss_key_new_generate(&l_key, NULL, 0, NULL, 0, 0);

    if (l_key.priv_key_data != NULL || l_key.priv_key_data_size != 0 ||
        l_key.pub_key_data != NULL || l_key.pub_key_size != 0) {
        log_it(L_ERROR, "Inconsistent BLISS key state after failed generation: priv=%p priv_size=%zu pub=%p pub_size=%zu",
               l_key.priv_key_data, l_key.priv_key_data_size, l_key.pub_key_data, l_key.pub_key_size);
        l_ok = false;
    }

    dap_enc_sig_bliss_key_delete(&l_key);
    return l_ok ? 0 : 1;
}
