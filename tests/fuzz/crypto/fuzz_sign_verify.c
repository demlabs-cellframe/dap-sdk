/**
 * @file fuzz_sign_verify.c
 * @brief libFuzzer target for dap_sign_verify_all.
 *
 * Interprets fuzz input as a serialized dap_sign_t blob and tries to verify
 * it against a fixed 32-byte message.  Goal: detect parser bugs, OOB reads,
 * and crashes in all supported signature schemes.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "dap_common.h"
#include "dap_sign.h"

static const uint8_t s_fixed_msg[32] = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
};

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

    if (a_size < sizeof(dap_sign_hdr_t))
        return 0;

    dap_sign_t *l_sign = (dap_sign_t *)a_data;
    dap_sign_verify_all(l_sign, a_size, s_fixed_msg, sizeof(s_fixed_msg));

    return 0;
}
