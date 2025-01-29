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

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#include <time.h>
#endif

#include "dap_common.h"
#include <pthread.h>
#include "dap_sign.h"

#include "include/dap_http_server.h"
#include "dap_http_client.h"
#include "include/dap_http_simple.h"

#include "dap_enc.h"
#include "include/dap_enc_ks.h"
#include "dap_enc_key.h"
#include "dap_enc_iaes.h"
#include "include/dap_enc_http.h"
#include "dap_enc_base64.h"
#include "dap_enc_msrln.h"
#include "http_status_code.h"
#include "dap_http_ban_list_client.h"
#include "json.h"
#include "dap_http_ban_list_client.h"
#include "dap_cert.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_enc_http"

dap_stream_node_addr_t dap_stream_node_addr_from_sign(dap_sign_t *a_sign);

static dap_enc_acl_callback_t s_acl_callback = NULL;

int enc_http_init()
{
    dap_http_ban_list_client_init();
    return 0;
}

void enc_http_deinit()
{

}

static void _enc_http_write_reply(struct dap_http_simple *a_cl_st,
                                  const char* a_encrypt_id, int a_id_len,
                                  const char* a_encrypt_msg,int a_msg_len,
                                  const char* a_node_sign,  int a_sign_len)
{
    struct json_object *l_jobj = json_object_new_object();
    json_object_object_add(l_jobj, "encrypt_id", json_object_new_string_len(a_encrypt_id, a_id_len));
    json_object_object_add(l_jobj, "encrypt_msg", json_object_new_string_len(a_encrypt_msg, a_msg_len));
    if (a_node_sign)
        json_object_object_add(l_jobj, "node_sign", json_object_new_string_len(a_node_sign, a_sign_len));
    json_object_object_add(l_jobj, "dap_protocol_version", json_object_new_int(DAP_PROTOCOL_VERSION));
    const char* l_json_str = json_object_to_json_string(l_jobj);
    dap_http_simple_reply(a_cl_st, (void*) l_json_str, (size_t) strlen(l_json_str));
    json_object_put(l_jobj);
}

void dap_enc_http_json_response_format_enable(bool);

void dap_enc_http_set_acl_callback(dap_enc_acl_callback_t a_callback)
{
    s_acl_callback = a_callback;
}

/**
 * @brief enc_http_proc Enc http interface
 * @param cl_st HTTP Simple client instance
 * @param arg Pointer to bool with okay status (true if everything is ok, by default)
 */
void enc_http_proc(struct dap_http_simple *cl_st, void * arg)
{
    log_it(L_DEBUG,"Proc enc http request");
    http_status_code_t * return_code = (http_status_code_t*)arg;

    if(!strcmp(cl_st->http_client->url_path,"gd4y5yh78w42aaagh")) {
        dap_enc_key_type_t l_pkey_exchange_type =DAP_ENC_KEY_TYPE_MSRLN ;
        dap_enc_key_type_t l_enc_block_type = DAP_ENC_KEY_TYPE_IAES;
        size_t l_pkey_exchange_size = MSRLN_PKA_BYTES;
        size_t l_block_key_size=32;
        int l_protocol_version = 0;
        size_t l_sign_count = 0;
        sscanf(cl_st->http_client->in_query_string, "enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,block_key_size=%zu,protocol_version=%d,sign_count=%zu",
                                      &l_enc_block_type,&l_pkey_exchange_type,&l_pkey_exchange_size,&l_block_key_size, &l_protocol_version, &l_sign_count);

        log_it(L_DEBUG, "Stream encryption: %s\t public key exchange: %s",dap_enc_get_type_name(l_enc_block_type),
               dap_enc_get_type_name(l_pkey_exchange_type));
        size_t l_decode_len = DAP_ENC_BASE64_DECODE_SIZE(cl_st->request_size);
        uint8_t alice_msg[l_decode_len + 1];
        l_decode_len = dap_enc_base64_decode(cl_st->request, cl_st->request_size, alice_msg, DAP_ENC_DATA_TYPE_B64);
        alice_msg[l_decode_len] = '\0';
        dap_chain_hash_fast_t l_sign_hash = {0};
        if (!l_protocol_version && !l_sign_count) {
            if (l_decode_len > l_pkey_exchange_size + sizeof(dap_sign_hdr_t)) {
                l_sign_count = 1;
            } else if (l_decode_len != l_pkey_exchange_size) {
                /* No sign inside */
                log_it(L_WARNING, "Wrong message size, without a valid sign must be = %zu", l_pkey_exchange_size);
                *return_code = Http_Status_BadRequest;
                return;
            }
        }

        /* Verify all signs */
        dap_sign_t *l_sign = NULL;
        size_t l_bias = l_pkey_exchange_size;
        size_t l_sign_validated_count = 0;
        for(; l_sign_validated_count < l_sign_count && l_bias < l_decode_len; ++l_sign_validated_count) {
            l_sign = (dap_sign_t *)&alice_msg[l_bias];
            int l_verify_ret = dap_sign_verify_all(l_sign, l_decode_len - l_bias, alice_msg, l_pkey_exchange_size);
            if (l_verify_ret) {
                log_it(L_ERROR, "Can't authorize, sign verification didn't pass (err %d)", l_verify_ret);
                *return_code = Http_Status_Unauthorized;
                return;
            }
            l_bias += dap_sign_get_size(l_sign);
            dap_stream_node_addr_t l_client_pkey_node_addr = dap_stream_node_addr_from_sign(l_sign);
            const char *l_client_node_addr_str = dap_stream_node_addr_to_str_static(l_client_pkey_node_addr);
            if (dap_http_ban_list_client_check(l_client_node_addr_str, NULL, NULL)) {
                log_it(L_ERROR, "Client %s is banned.", l_client_node_addr_str);
                *return_code = Http_Status_Forbidden;
                return;
            }
        }
        if (l_sign_validated_count != l_sign_count) {
            log_it(L_ERROR, "Can't authorize all %zu signs", l_sign_count);
            *return_code = Http_Status_Unauthorized;
            return;
        }

        dap_enc_key_t* l_pkey_exchange_key = dap_enc_key_new(l_pkey_exchange_type);
        if(! l_pkey_exchange_key){
            log_it(L_WARNING, "Wrong http_enc request. Can't init PKey exchange with type %s", dap_enc_get_type_name(l_pkey_exchange_type) );
            *return_code = Http_Status_BadRequest;
            return;
        }
        if(l_pkey_exchange_key->gen_bob_shared_key) {
            l_pkey_exchange_key->pub_key_data_size = l_pkey_exchange_key->gen_bob_shared_key(l_pkey_exchange_key, alice_msg, l_pkey_exchange_size,
                    &l_pkey_exchange_key->pub_key_data);
        }

        dap_enc_ks_key_t *l_enc_key_ks = dap_enc_ks_new();
        dap_return_if_pass(!l_enc_key_ks);
        if (s_acl_callback) {
            l_enc_key_ks->acl_list = s_acl_callback(&l_sign_hash);
        } else {
            log_it(L_DEBUG, "Callback for ACL is not set, pass anauthorized");
        }
    
        char    *encrypt_msg = DAP_NEW_Z_SIZE(char, DAP_ENC_BASE64_ENCODE_SIZE(l_pkey_exchange_key->pub_key_data_size) + 1),
                encrypt_id[DAP_ENC_BASE64_ENCODE_SIZE(DAP_ENC_KS_KEY_ID_SIZE) + 1] = { '\0' };
        int l_enc_msg_len = (int)dap_enc_base64_encode( l_pkey_exchange_key->pub_key_data,
                                                        l_pkey_exchange_key->pub_key_data_size,
                                                        encrypt_msg, DAP_ENC_DATA_TYPE_B64);

        l_enc_key_ks->key = dap_enc_key_new_generate(l_enc_block_type,
                                               l_pkey_exchange_key->priv_key_data, // shared key
                                               l_pkey_exchange_key->priv_key_data_size,
                                               l_enc_key_ks->id, DAP_ENC_KS_KEY_ID_SIZE, l_block_key_size);
        
        dap_enc_ks_save_in_storage(l_enc_key_ks);
        int l_enc_id_len = (int)dap_enc_base64_encode(l_enc_key_ks->id, sizeof (l_enc_key_ks->id), 
                                                      encrypt_id, DAP_ENC_DATA_TYPE_B64),
            l_node_msg_len = 0;

        // save verified node addr and generate own sign
        char* l_node_sign_msg = NULL;
        if (l_protocol_version && l_sign_count) {
            l_enc_key_ks->node_addr = dap_stream_node_addr_from_sign(l_sign);

            dap_cert_t *l_node_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
            dap_sign_t *l_node_sign = dap_sign_create(l_node_cert->enc_key,l_pkey_exchange_key->pub_key_data, l_pkey_exchange_key->pub_key_data_size, DAP_SIGN_HASH_TYPE_DEFAULT);
            if (!l_node_sign) {
                dap_enc_key_delete(l_pkey_exchange_key);
                DAP_DELETE(encrypt_msg);
                *return_code = Http_Status_InternalServerError;
                return;
            }
            size_t l_node_sign_size = dap_sign_get_size(l_node_sign);
            size_t l_node_sign_size_new = DAP_ENC_BASE64_ENCODE_SIZE(l_node_sign_size) + 1;

            l_node_sign_msg = DAP_NEW_Z_SIZE(char, l_node_sign_size_new);
            if (!l_node_sign_msg) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                dap_enc_key_delete(l_pkey_exchange_key);
                *return_code = Http_Status_InternalServerError;
                DAP_DELETE(encrypt_msg);
                DAP_DELETE(l_node_sign);
                return;
            }
            l_node_msg_len = (int)dap_enc_base64_encode(l_node_sign, l_node_sign_size, l_node_sign_msg, DAP_ENC_DATA_TYPE_B64);
            DAP_DELETE(l_node_sign);
        }

        _enc_http_write_reply(cl_st, encrypt_id, l_enc_id_len, encrypt_msg, l_enc_msg_len, l_node_sign_msg, l_node_msg_len);
        DAP_DELETE(encrypt_msg);
        dap_enc_key_delete(l_pkey_exchange_key);
        DAP_DEL_Z(l_node_sign_msg);

        *return_code = Http_Status_OK;

    } else{
        log_it(L_ERROR,"Wrong path '%s' in the request to enc_http module",cl_st->http_client->url_path);
        *return_code = Http_Status_NotFound;
        return;
    }
}

/**
 * @brief enc_http_add_proc
 * @param sh
 * @param url
 */
void enc_http_add_proc(struct dap_http_server* sh, const char * url)
{
    dap_http_simple_proc_add(sh,url,140000,enc_http_proc);
}

/**
 * @brief enc_http_request_decode
 * @param a_http_simple
 * @return
 */
enc_http_delegate_t *enc_http_request_decode(struct dap_http_simple *a_http_simple)
{

    dap_enc_key_t * l_key= dap_enc_ks_find_http(a_http_simple->http_client);
    if(l_key){
        enc_http_delegate_t * dg = DAP_NEW_Z_RET_VAL_IF_FAIL(enc_http_delegate_t, NULL);
        dg->key=l_key;
        dg->http=a_http_simple->http_client;
       // dg->isOk=true;

        strncpy(dg->action,(char *)a_http_simple->http_client->action,sizeof(dg->action) - 1);
        dg->action[sizeof(dg->action) - 1] = '\0';
        if(a_http_simple->http_client->in_cookie[0])
            dg->cookie=strdup(a_http_simple->http_client->in_cookie);

        if(a_http_simple->request_size){
            size_t l_dg_request_size_max = a_http_simple->request_size;
            dg->request= DAP_NEW_SIZE( void , l_dg_request_size_max+1);
            if (!dg->request) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                DAP_DEL_Z(dg);
                return NULL;
            }
            dg->request_size=dap_enc_decode(l_key, a_http_simple->request, a_http_simple->request_size,dg->request,
                                            l_dg_request_size_max, DAP_ENC_DATA_TYPE_RAW);
            dg->request_str[dg->request_size] = 0;
            // log_it(L_DEBUG,"Request after decode '%s'",dg->request_str);
            // log_it(L_DEBUG,"Request before decode: '%s' after decode '%s'",cl_st->request_str,dg->request_str);
        }

        dap_enc_data_type_t l_enc_type;
        int protocol_version = 21; //TODO: Get protocol version
        if(protocol_version >= 21  )
            l_enc_type = DAP_ENC_DATA_TYPE_B64_URLSAFE;
        else
            l_enc_type = DAP_ENC_DATA_TYPE_B64;

        size_t l_url_path_size_max = strlen(a_http_simple->http_client->url_path);
        if(l_url_path_size_max){
            dg->url_path= DAP_NEW_SIZE(char,l_url_path_size_max+1);
            if (!dg->url_path) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                DAP_DEL_Z(dg->request);
                DAP_DEL_Z(dg);
                return NULL;
            }
            dg->url_path_size=dap_enc_decode(l_key, a_http_simple->http_client->url_path,l_url_path_size_max,dg->url_path, l_url_path_size_max, l_enc_type);
            dg->url_path[dg->url_path_size] = 0;
            log_it(L_DEBUG,"URL path after decode '%s'",dg->url_path );
            // log_it(L_DEBUG,"URL path before decode: '%s' after decode '%s'",cl_st->http->url_path,dg->url_path );
        }

        size_t l_in_query_size=strlen(a_http_simple->http_client->in_query_string);

        if(l_in_query_size){
            dg->in_query= DAP_NEW_SIZE(char, l_in_query_size+1);
            if (!dg->in_query) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                DAP_DEL_Z(dg->request);
                DAP_DEL_Z(dg->url_path);
                DAP_DEL_Z(dg);
                return NULL;
            }
            dg->in_query_size=dap_enc_decode(l_key, a_http_simple->http_client->in_query_string,l_in_query_size,dg->in_query,l_in_query_size,  l_enc_type);
            dg->in_query[dg->in_query_size] = 0;
            log_it(L_DEBUG,"Query string after decode '%s'",dg->in_query);
        }
        dg->response = calloc(1,a_http_simple->reply_size_max+1);
        if (!dg->response) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            DAP_DEL_Z(dg->in_query);
            DAP_DEL_Z(dg->request);
            DAP_DEL_Z(dg->url_path);
            DAP_DEL_Z(dg);
            return NULL;
        }
        dg->response_size_max=a_http_simple->reply_size_max;

        return dg;
    }else{
        log_it(L_WARNING,"No Key was found in the request");
        return NULL;
    }
}

/**
 * @brief enc_http_reply_encode
 * @param a_http_simple
 * @param a_http_delegate
 */
void enc_http_reply_encode(struct dap_http_simple *a_http_simple,enc_http_delegate_t * a_http_delegate)
{
    if (!a_http_delegate->response)
        return;

    if(a_http_simple->reply)
        DAP_DELETE(a_http_simple->reply);

    size_t l_reply_size_max = dap_enc_code_out_size(a_http_delegate->key,
                                                      a_http_delegate->response_size,
                                                      DAP_ENC_DATA_TYPE_RAW);

    a_http_simple->reply = DAP_NEW_SIZE(void,l_reply_size_max);
    a_http_simple->reply_size = dap_enc_code( a_http_delegate->key,
                                              a_http_delegate->response, a_http_delegate->response_size,
                                              a_http_simple->reply, l_reply_size_max,
                                              DAP_ENC_DATA_TYPE_RAW);
}

void enc_http_delegate_delete(enc_http_delegate_t * dg)
{
    DAP_DEL_MULTY(dg->cookie, dg->in_query, dg->request, dg->response, dg->url_path, dg);
}

size_t enc_http_reply(enc_http_delegate_t *a_http_delegate, const void *a_data, size_t a_data_size)
{
    size_t wb = a_data_size > a_http_delegate->response_size_max - a_http_delegate->response_size
            ? a_http_delegate->response_size_max - a_http_delegate->response_size
            : a_data_size;
    memcpy(a_http_delegate->response + a_http_delegate->response_size, a_data, wb);
    a_http_delegate->response_size += wb;
    return wb;
}

size_t enc_http_reply_f(enc_http_delegate_t *a_http_delegate, const char *a_data, ...)
{
    va_list ap, ap_copy;
    va_start(ap, a_data);
    va_copy(ap_copy, ap);
    int mem_size = vsnprintf(NULL, 0, a_data, ap) + 1;
    va_end(ap);
    char *l_buf = DAP_NEW_SIZE(char, mem_size);
    if (!l_buf) {
        va_end(ap_copy);
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return 0;
    }
    vsnprintf(l_buf, mem_size, a_data, ap_copy);
    va_end(ap_copy);
    size_t l_ret = enc_http_reply(a_http_delegate, l_buf, mem_size);
    DAP_DELETE(l_buf);
    return l_ret;
}

