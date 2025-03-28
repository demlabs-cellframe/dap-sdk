/*
 * Authors:
 * Alexey V. Stratulat <alexey.stratulat@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe/cellframe-sdk
 * Copyright  (c) 2017-2020
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "dap_json_rpc_params.h"
#include "dap_json_rpc_response.h"
#include "dap_json_rpc_request.h"
#include "dap_http_simple.h"
#include "uthash.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef void (handler_func_t)(dap_json_rpc_params_t *a_params, dap_json_rpc_response_t *a_response, const char *a_method);

typedef struct dap_json_rpc_request_handler {
    char *name;
    handler_func_t *func;
    UT_hash_handle hh;
} dap_json_rpc_request_handler_t;

int dap_json_rpc_registration_request_handler(const char *a_name, handler_func_t *a_func);
int dap_json_rpc_unregistration_request_handler(const char *a_name);

char * dap_json_rpc_request_handler(const char * a_request,  size_t a_request_size);

#ifdef __cplusplus
}
#endif
