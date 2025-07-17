/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2021
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <time.h>

#include "KeccakHash.h"
#include "SimpleFIPS202.h"
#include "dap_uuid.h"
#include "dap_guuid.h"
#include "dap_rand.h"
#include "dap_strfuncs.h"
#include "dap_math_convert.h"

#ifndef __cplusplus
# include <stdatomic.h>
#else
# include <atomic>
# define _Atomic(X) std::atomic< X >
#define atomic_bool _Atomic(bool)
#define atomic_uint _Atomic(uint)
#endif

#define LOG_TAG "dap_uuid"

static atomic_uint_fast32_t s_global_counter = 0;

/**
 * @brief dap_uuid_generate_ui64
 * @details Produce uint64 unique id
 * @return
 */
uint128_t dap_uuid_generate_uint128()
{
    uint32_t l_input[4] ={
        [0] = random_uint32_t(UINT32_MAX),
        [1] = time(NULL),
        [2] = atomic_fetch_add(&s_global_counter, 1),
        [3] = random_uint32_t(UINT32_MAX)
    };
    uint128_t l_output;
    SHAKE128((unsigned char *) &l_output,sizeof (l_output), (unsigned char*) &l_input,sizeof (l_input));
 //   uint64_t *l_output_u64 =(uint64_t*) &l_output;
   // log_it(L_DEBUG,"UUID generated 0x%016X%016X (0x%08X%08X%08X%08X",l_output_u64[0],l_output_u64[1],
   //         l_input[0],l_input[1],l_input[2],l_input[3]);
    return l_output;
}

/**
 * @brief dap_uuid_generate_uint64
 * @return
 */
uint64_t dap_uuid_generate_uint64()
{
    uint16_t l_input[4] = {
        [0] = dap_random_uint16(),
        [1] = time(NULL) & UINT16_MAX,      /*  time(NULL) % UINT16_MAX */
        [2] = (uint16_t) atomic_fetch_add(&s_global_counter, 1),
        [3] = dap_random_uint16()
    };
    uint64_t l_output;
    SHAKE128((unsigned char *) &l_output,sizeof (l_output), (unsigned char*) &l_input,sizeof (l_input));
   // log_it(L_DEBUG,"UUID generated 0x%016X%016X (0x%08X%08X%08X%08X",l_output_u64[0],l_output_u64[1],
   //         l_input[0],l_input[1],l_input[2],l_input[3]);
    return l_output;
}

void dap_uuid_generate_nonce(void *a_nonce, size_t a_nonce_size)
{
    if (!a_nonce || !a_nonce_size)
        return;
    uint32_t l_input[4] ={
        [0] = random_uint32_t(UINT32_MAX),
        [1] = time(NULL),
        [2] = atomic_fetch_add(&s_global_counter, 1),
        [3] = random_uint32_t(UINT32_MAX)
    };
    SHAKE128((unsigned char *)a_nonce, a_nonce_size, (unsigned char *)l_input, sizeof(l_input));
}

dap_guuid_str_t dap_guuid_to_hex_str_(dap_guuid_t a_guuid)
{
    dap_guuid_str_t l_ret;
    return snprintf((char*)&l_ret, sizeof(l_ret), "0x%016" DAP_UINT64_FORMAT_X "%016" DAP_UINT64_FORMAT_X,
        a_guuid.net_id, a_guuid.srv_id), l_ret;
}


dap_guuid_t dap_guuid_from_hex_str(const char *a_hex_str, bool *succsess)
{
    dap_guuid_t ret = { .raw = uint128_0 };
    if (!a_hex_str){
        if (succsess) *succsess = false;
        return ret;
    }
    size_t l_hex_str_len = strlen(a_hex_str);
    if (l_hex_str_len != (16 * 2 + 2) || dap_strncmp(a_hex_str, "0x", 2) || dap_is_hex_string(a_hex_str + 2, l_hex_str_len - 2)) {
        if (succsess) *succsess = false;
        return ret;
    }
    dap_guuid_str_t l_str;
    snprintf((char*)&l_str, sizeof(l_str), "0x%s", a_hex_str + 16 + 2);
    uint64_t l_net_id, l_srv_id;
    if (dap_id_uint64_parse(a_hex_str, &l_net_id) || dap_id_uint64_parse(l_str.s, &l_srv_id))
    {
        if (succsess) *succsess = false;
        return ret;
    }
    if (succsess) *succsess = true;
    return dap_guuid_compose(l_net_id, l_srv_id);
}
