/*
 * Authors:
 * Alexey V. Stratulat <alexey.stratulat@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe/cellframe-sdk
 * Copyright  (c) 2017-2020
 * All rights reserved.

 This file is part of DAP (Deus Applications Prototypes) the open source project

    DAP (Deus Applicaions Prototypes) is free software: you can redistribute it and/or modify
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

#include "dap_http_simple.h"
#include "dap_json_rpc_errors.h"
#include "json.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef enum dap_json_rpc_response_type_result{
    TYPE_RESPONSE_NULL,
    TYPE_RESPONSE_STRING,
    TYPE_RESPONSE_INTEGER,
    TYPE_RESPONSE_DOUBLE,
    TYPE_RESPONSE_BOOLEAN,
    TYPE_RESPONSE_JSON
}dap_json_rpc_response_type_result_t;

typedef struct dap_json_rpc_response_JSON
{
    json_object *obj_result;
    json_object *obj_error;
    dap_json_rpc_error_JSON_t *struct_error;
    json_object *obj_id;
}dap_json_rpc_request_JSON_t;

void dap_json_rpc_request_JSON_free(dap_json_rpc_request_JSON_t *l_request_JSON);

typedef struct dap_json_rpc_response
{
    dap_json_rpc_response_type_result_t type;
    union {
        char* result_string;
        int64_t result_int;
        double result_double;
        bool result_boolean;
        json_object *result_json_object;
    };
    dap_json_rpc_error_t* error;
    uint64_t id;
}dap_json_rpc_response_t;

/**
 * Create a new JSON-RPC response structure.
 *
 * This function allocates memory for a new `dap_json_rpc_response_t` structure,
 * initializes its fields, and assigns the provided values. The caller is RESPONSIBLE 
 * FOR managing the MEMORY of the returned response structure or use dap_json_rpc_response_free.
 *
 * @param result A pointer to the result data that corresponds to the response type.
 * @param type The response type indicating the format of the result data: TYPE_RESPONSE_NULL,
 *                                                                         TYPE_RESPONSE_STRING,
 *                                                                         TYPE_RESPONSE_INTEGER,
 *                                                                         TYPE_RESPONSE_DOUBLE,
 *                                                                         TYPE_RESPONSE_BOOLEAN,
 *                                                                         TYPE_RESPONSE_JSON
 * @param id The unique identifier associated with the REQUEST ID.
 * @return A pointer to the newly created `dap_json_rpc_response_t` structure. Don't forget about dap_json_rpc_response_free.
 *         Return NULL in case of memory allocation failure, an unsupported response type,
 *         or if `TYPE_RESPONSE_NULL` is specified as the response type.
 */
dap_json_rpc_response_t* dap_json_rpc_response_create(void * result, dap_json_rpc_response_type_result_t type, int64_t id);

/**
 * Free the resources associated with a JSON-RPC response structure.
 * @param response A pointer to the JSON-RPC response structure to be freed.
 */
void dap_json_rpc_response_free(dap_json_rpc_response_t *a_response);

/**
 * Convert a dap_json_rpc_response_t structure to a JSON string representation.
 *
 * @param response A pointer to the dap_json_rpc_response_t.
 * @return A DYNAMICALLY allocated string containing the JSON-formatted representation
 *         of the response structure.
 *         Returns NULL if the provided response pointer is NULL or 
 *         if memory allocation fails during conversion.
 */
char* dap_json_rpc_response_to_string(const dap_json_rpc_response_t* response);

/**
 * Convert a JSON string representation to a dap_json_rpc_response_t structure.
 *
 * @param json_string The JSON-formatted string to be converted.
 * @return A pointer to a DYNAMICALLY allocated dap_json_rpc_response_t structure
 *         created from the parsed JSON string.
 *         Returns NULL if the JSON parsing fails or memory allocation fails
 *         during structure creation.
 */
dap_json_rpc_response_t* dap_json_rpc_response_from_string(const char* json_string);

/**
 * Prints the result of a JSON-RPC response to the standard output.
 *
 * This function takes a dap_json_rpc_response_t structure as input and prints the result
 * contained in it to the standard output. The response type determines the format of the
 * printed result. The function handles different types of response data, including strings,
 * integers, doubles, booleans, and JSON objects.
 *
 * @param response The JSON-RPC response structure to be printed.
 * @return Returns 0 on success. 
 *         -1 indicates an empty response,
 *         -2 indicates an issue with the JSON object inside the response.
 */
int dap_json_rpc_response_printf_result(dap_json_rpc_response_t* response);


void dap_json_rpc_response_send(dap_json_rpc_response_t *a_response, dap_http_simple_t *a_client);
dap_json_rpc_response_t *dap_json_rpc_response_from_json(char *a_data_json);


#ifdef __cplusplus
}
#endif
