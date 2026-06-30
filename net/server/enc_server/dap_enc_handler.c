/**
 * @file dap_enc_handler.c
 * @brief Transport-independent encryption handshake handler
 *
 * Extracted from enc_http_proc — no HTTP dependency.
 * Uses dap_trans_request_t for request/reply.
 */

#include <string.h>
#include "dap_common.h"
#include "dap_enc_handler.h"
#include "dap_enc_server.h"
#include "dap_enc_base64.h"
#include "json.h"

#define LOG_TAG "dap_enc_handler"

int dap_enc_handler_process(dap_trans_request_t *a_req)
{
    if (!a_req) return -1;

    /* Parse query string: enc_type=X,pkey_exchange_type=X,... */
    dap_enc_server_request_t l_request = {0};
    const char *l_query = trans_get_query(a_req);
    if (!l_query || dap_enc_server_parse_query(l_query, &l_request) != 0) {
        log_it(L_ERROR, "enc_handler: failed to parse query string");
        trans_set_status(a_req, 400);
        return -1;
    }

    /* Decode base64 body (alice public key) */
    size_t l_body_len = 0;
    const void *l_body = trans_get_body(a_req, &l_body_len);
    if (!l_body || l_body_len == 0) {
        log_it(L_ERROR, "enc_handler: empty request body");
        trans_set_status(a_req, 400);
        return -2;
    }

    size_t l_decode_len = DAP_ENC_BASE64_DECODE_SIZE(l_body_len);
    uint8_t *l_alice_msg = DAP_NEW_Z_SIZE(uint8_t, l_decode_len + 1);
    if (!l_alice_msg) {
        trans_set_status(a_req, 500);
        return -3;
    }

    l_decode_len = dap_enc_base64_decode(l_body, l_body_len,
                                          l_alice_msg, DAP_ENC_DATA_TYPE_B64);
    l_alice_msg[l_decode_len] = '\0';

    l_request.alice_msg = l_alice_msg;
    l_request.alice_msg_size = l_decode_len;

    /* Process encryption handshake */
    dap_enc_server_response_t *l_response = NULL;
    int l_ret = dap_enc_server_process_request(&l_request, &l_response);
    DAP_DELETE(l_alice_msg);

    if (l_ret != 0 || !l_response || !l_response->success) {
        log_it(L_ERROR, "enc_handler: handshake failed: %s",
               l_response && l_response->error_message ? l_response->error_message : "unknown");
        int l_status = 400;
        if (l_response) {
            if (l_response->error_code == -5) l_status = 401;  /* signature fail */
            if (l_response->error_code == -6) l_status = 403;  /* client banned */
            dap_enc_server_response_free(l_response);
        }
        trans_set_status(a_req, l_status);
        return -4;
    }

    /* Build JSON response */
    struct json_object *l_jobj = json_object_new_object();
    json_object_object_add(l_jobj, "encrypt_id",
        json_object_new_string_len(l_response->encrypt_id, (int)l_response->encrypt_id_len));
    json_object_object_add(l_jobj, "encrypt_msg",
        json_object_new_string_len(l_response->encrypt_msg, (int)l_response->encrypt_msg_len));
    if (l_response->node_sign_msg && l_response->node_sign_msg_len > 0) {
        json_object_object_add(l_jobj, "node_sign",
            json_object_new_string_len(l_response->node_sign_msg, (int)l_response->node_sign_msg_len));
    }
    json_object_object_add(l_jobj, "dap_protocol_version",
        json_object_new_int(DAP_PROTOCOL_VERSION));

    const char *l_json_str = json_object_to_json_string(l_jobj);
    trans_reply(a_req, l_json_str, strlen(l_json_str));
    json_object_put(l_jobj);

    dap_enc_server_response_free(l_response);
    trans_set_status(a_req, 200);
    return 0;
}
