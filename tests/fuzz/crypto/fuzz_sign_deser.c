/**
 * @file fuzz_sign_deser.c
 * @brief libFuzzer target for signature deserialization and size validation.
 *
 * Exercises dap_sign_get_size and dap_sign_verify_size with arbitrary input.
 * Goal: detect integer overflow, OOB access, and logic errors in header parsing.
 */

#include <stdint.h>
#include <stddef.h>

#include "dap_common.h"
#include "dap_sign.h"

static int s_inited = 0;

static void s_init(void)
{
    dap_log_level_set(L_CRITICAL);
    s_inited = 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *a_data, size_t a_size)
{
    if (!s_inited)
        s_init();

    if (a_size < sizeof(dap_sign_t))
        return 0;

    dap_sign_t *l_sign = (dap_sign_t *)a_data;

    dap_sign_verify_size(l_sign, a_size);
    dap_sign_get_size(l_sign);

    return 0;
}
