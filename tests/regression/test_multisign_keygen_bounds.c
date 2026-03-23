#include <stdbool.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_mock.h"
#include "dap_enc_key.h"
#include "dap_enc_multisign.h"

#define LOG_TAG "test_regression_multisign_keygen_bounds"

DAP_MOCK_CUSTOM(dap_enc_key_t*, dap_enc_key_new_generate,
    (dap_enc_key_type_t a_key_type, const void *a_kex_buf, size_t a_kex_size,
     const void *a_seed, size_t a_seed_size, size_t a_key_size))
    UNUSED(a_kex_buf);
    UNUSED(a_kex_size);
    UNUSED(a_seed);
    UNUSED(a_seed_size);
    UNUSED(a_key_size);
    dap_enc_key_t *l_key = DAP_NEW_Z(dap_enc_key_t);
    if (l_key) {
        l_key->type = a_key_type;
    }
    return l_key;
}

DAP_MOCK_CUSTOM(void, dap_enc_key_delete, (dap_enc_key_t *a_key))
    DAP_DEL_Z(a_key);
}

DAP_MOCK_CUSTOM(size_t, dap_enc_ser_priv_key_size, (dap_enc_key_t *a_key))
    UNUSED(a_key);
    return 4;
}

DAP_MOCK_CUSTOM(size_t, dap_enc_ser_pub_key_size, (dap_enc_key_t *a_key))
    UNUSED(a_key);
    return 4;
}

DAP_MOCK_CUSTOM(uint8_t*, dap_enc_key_serialize_priv_key, (dap_enc_key_t *a_key, size_t *a_buflen_out))
    UNUSED(a_key);
    if (a_buflen_out) {
        *a_buflen_out = 4;
    }
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, 4);
    if (l_buf) {
        l_buf[0] = 0xA1;
        l_buf[1] = 0xA2;
        l_buf[2] = 0xA3;
        l_buf[3] = 0xA4;
    }
    return l_buf;
}

DAP_MOCK_CUSTOM(uint8_t*, dap_enc_key_serialize_pub_key, (dap_enc_key_t *a_key, size_t *a_buflen_out))
    UNUSED(a_key);
    if (a_buflen_out) {
        *a_buflen_out = 4;
    }
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, 4);
    if (l_buf) {
        l_buf[0] = 0xB1;
        l_buf[1] = 0xB2;
        l_buf[2] = 0xB3;
        l_buf[3] = 0xB4;
    }
    return l_buf;
}

static void s_reset_mocks(void)
{
    DAP_MOCK_RESET(dap_enc_key_new_generate);
    DAP_MOCK_RESET(dap_enc_key_delete);
    DAP_MOCK_RESET(dap_enc_ser_priv_key_size);
    DAP_MOCK_RESET(dap_enc_ser_pub_key_size);
    DAP_MOCK_RESET(dap_enc_key_serialize_priv_key);
    DAP_MOCK_RESET(dap_enc_key_serialize_pub_key);
}

static bool s_test_oversized_rejected(void)
{
    s_reset_mocks();

    size_t l_count = (size_t)UINT8_MAX + 1;
    dap_enc_key_type_t *l_types = DAP_NEW_Z_COUNT(dap_enc_key_type_t, l_count);
    if (!l_types) {
        log_it(L_ERROR, "Failed to allocate key types for oversized test");
        return false;
    }
    for (size_t i = 0; i < l_count; i++) {
        l_types[i] = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    }

    dap_enc_key_t l_key = {.type = DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED};
    dap_enc_sig_multisign_key_new_generate(&l_key, l_types, l_count, NULL, 0, 0);

    bool l_ok = true;
    if (l_key._pvt != NULL) {
        log_it(L_ERROR, "Oversized input should not initialize _pvt");
        l_ok = false;
    }
    if (l_key.priv_key_data != NULL || l_key.pub_key_data != NULL ||
        l_key.priv_key_data_size != 0 || l_key.pub_key_data_size != 0) {
        log_it(L_ERROR, "Oversized input should not produce serialized key data");
        l_ok = false;
    }

    DAP_DEL_Z(l_types);
    return l_ok;
}

static bool s_test_boundary_uint8_max_accepted(void)
{
    s_reset_mocks();

    size_t l_count = UINT8_MAX;
    dap_enc_key_type_t *l_types = DAP_NEW_Z_COUNT(dap_enc_key_type_t, l_count);
    if (!l_types) {
        log_it(L_ERROR, "Failed to allocate key types for boundary test");
        return false;
    }
    for (size_t i = 0; i < l_count; i++) {
        l_types[i] = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    }

    dap_enc_key_t l_key = {.type = DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED};
    dap_enc_sig_multisign_key_new_generate(&l_key, l_types, l_count, NULL, 0, 0);

    bool l_ok = true;
    if (l_key._pvt == NULL) {
        log_it(L_ERROR, "Boundary input should produce params");
        l_ok = false;
    }
    if (!l_key.priv_key_data || !l_key.pub_key_data ||
        l_key.priv_key_data_size == 0 || l_key.pub_key_data_size == 0) {
        log_it(L_ERROR, "Boundary input should produce serialized multisign key data");
        l_ok = false;
    }

    dap_enc_sig_multisign_key_delete(&l_key);
    DAP_DEL_Z(l_types);
    return l_ok;
}

int main(void)
{
    if (dap_mock_init() != 0) {
        return 1;
    }

    bool l_ok = true;
    l_ok &= s_test_oversized_rejected();
    l_ok &= s_test_boundary_uint8_max_accepted();

    dap_mock_deinit();
    return l_ok ? 0 : 1;
}
