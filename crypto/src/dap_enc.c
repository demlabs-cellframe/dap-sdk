/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "dap_enc.h"
#include "dap_enc_base64.h"
#include "dap_enc_key.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_cert.h"
#include "dap_crc64.h"
#include "dap_sign.h"

#define LOG_TAG "dap_enc"

/**
 * @brief dap_enc_init if you want to use crypto functions from dap-sdk call that function
 * 
 * @return int 
 */

static bool s_debug_more = false;

int dap_enc_init()
{
    dap_enc_key_init();
    dap_cert_init();
    dap_crc64_init();
    s_debug_more = g_config ? dap_config_get_item_bool_default(g_config, "crypto", "debug_more", false) : false;
    dap_sign_init(DAP_SIGN_HASH_TYPE_SHA3);
    return 0;
}

/**
 * @brief dap_enc_deinit
 */
void dap_enc_deinit()
{
    dap_cert_deinit();
}

bool dap_enc_debug_more()
{
    return s_debug_more;
}

/**
 * @brief dap_enc_code_out_size
 * @param a_key
 * @param a_buf_in_size
 * @return min buffer size for input in encode function
 */
size_t dap_enc_code_out_size(dap_enc_key_t* a_key, const size_t a_buf_in_size, dap_enc_data_type_t type)
{
    size_t l_size = dap_enc_key_get_enc_size(a_key->type, a_buf_in_size);
    return type == DAP_ENC_DATA_TYPE_RAW ? l_size : DAP_ENC_BASE64_ENCODE_SIZE(l_size);
}

/**
 * @brief dap_enc_decode_out_size
 * @param a_key
 * @param a_buf_in_size
 * @return min buffer size for input in decode function
 */
size_t dap_enc_decode_out_size(dap_enc_key_t* a_key, const size_t a_buf_in_size, dap_enc_data_type_t type)
{
    size_t l_size = type == DAP_ENC_DATA_TYPE_RAW ? a_buf_in_size : DAP_ENC_BASE64_DECODE_SIZE(a_buf_in_size);
    return l_size ? dap_enc_key_get_dec_size(a_key->type, l_size) : 0;
}

/**
 * @brief Encode data with key
 * @param a_key Private key
 * @param a_buf  Input buffer
 * @param a_buf_size Input buffer size
 * @param a_buf_out Output buffer
 * @param a_buf_out_size_max
 * @return bytes actualy written in the output buffer
 */
size_t dap_enc_code(dap_enc_key_t *a_key,
                    const void *a_buf_in,  const size_t a_buf_size,
                          void *a_buf_out, const size_t a_buf_out_size_max,
                    dap_enc_data_type_t a_data_type_out)
{
    dap_return_val_if_fail_err(a_key && a_key->enc_na && a_buf_in && a_buf_out, 0, "Invalid params");
    size_t l_ret = dap_enc_code_out_size(a_key, a_buf_size, a_data_type_out);
    if ( !l_ret || l_ret > a_buf_out_size_max )
        return log_it(L_ERROR, "Insufficient out buffer size: %zu < %zu", a_buf_out_size_max, l_ret), 0;
    switch ( a_data_type_out ) {
    case DAP_ENC_DATA_TYPE_RAW:
        l_ret = a_key->enc_na(a_key, a_buf_in, a_buf_size, a_buf_out, a_buf_out_size_max);
        break;
    case DAP_ENC_DATA_TYPE_B64:
    case DAP_ENC_DATA_TYPE_B64_URLSAFE: {
        char *l_tmp_buf = DAP_NEW_Z_SIZE(char, l_ret);
        size_t l_tmp_size = a_key->enc_na(a_key, a_buf_in, a_buf_size, l_tmp_buf, l_ret);
        l_ret = dap_enc_base64_encode(l_tmp_buf, l_tmp_size, a_buf_out, a_data_type_out);
        DAP_DELETE(l_tmp_buf);
    } break;
    default:
        log_it(L_ERROR, "Unknown enc type %d", (int)a_data_type_out);
        l_ret = 0;
    }
    return l_ret;
}

/**
 * @brief Decode data with key
 * @param key_public Public key
 * @param buf  Input buffer
 * @param buf_size Input buffer size
 * @param buf_out Output buffer
 * @param buf_out_max Maximum size of output buffer
 * @return bytes actualy written in the output buffer
 */
size_t dap_enc_decode(dap_enc_key_t *a_key,
                      const void *a_buf_in,  const size_t a_buf_in_size,
                            void *a_buf_out, const size_t a_buf_out_size_max,
                      dap_enc_data_type_t a_data_type_in)
{
    dap_return_val_if_fail_err(a_key && a_key->enc_na && a_buf_in && a_buf_out, 0, "Invalid params");
    size_t l_ret = dap_enc_decode_out_size(a_key, a_buf_in_size, a_data_type_in);
    if ( !l_ret || l_ret > a_buf_out_size_max )
        return log_it(L_ERROR, "Insufficient out buffer size: %zu < %zu", a_buf_out_size_max, l_ret), 0;
    switch ( a_data_type_in ) {
    case DAP_ENC_DATA_TYPE_RAW:
        l_ret = a_key->dec_na(a_key, a_buf_in, a_buf_in_size, a_buf_out, a_buf_out_size_max);
        break;
    case DAP_ENC_DATA_TYPE_B64:
    case DAP_ENC_DATA_TYPE_B64_URLSAFE: {
        char *l_tmp_buf = DAP_NEW_Z_SIZE(char, DAP_ENC_BASE64_DECODE_SIZE(a_buf_in_size));
        size_t l_tmp_size = dap_enc_base64_decode(a_buf_in, a_buf_in_size, l_tmp_buf, a_data_type_in);
        l_ret = a_key->dec_na(a_key, l_tmp_buf, l_tmp_size, a_buf_out, a_buf_out_size_max);
        DAP_DELETE(l_tmp_buf);
    } break;
    default:
        log_it(L_ERROR, "Unknown enc type %d", (int)a_data_type_in);
        l_ret = 0;
    }
    return l_ret;
}
