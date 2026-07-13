/**
 * @file dap_stream_ctl_handler.c
 * @brief Transport-independent stream control handler
 *
 * Extracted from s_stream_ctl_proc — no HTTP dependency.
 * Uses dap_trans_request_t for request/reply.
 */

#include <string.h>
#include <stdlib.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_stream_ctl_handler.h"
#include "dap_stream_ctl.h"
#include "dap_stream_session.h"
#include "dap_enc_key.h"
#include "dap_enc_ks.h"
#include "dap_enc_base64.h"
#include "json.h"

#define LOG_TAG "dap_stream_ctl_handler"

/* Default encryption parameters (matches s_socket_forward_key in stream_ctl.c) */
#define DEFAULT_ENC_TYPE     DAP_ENC_KEY_TYPE_OAES
#define DEFAULT_ENC_KEY_SIZE 32

int dap_stream_ctl_handler_process(dap_trans_request_t *a_req)
{
    if (!a_req) return -1;

    const char *l_url_path = trans_get_url_path(a_req);
    if (!l_url_path) {
        trans_set_status(a_req, 400);
        return -1;
    }

    /* Parse URL params: channels=RS,enc_type=6,enc_key_size=32,enc_headers=0 */
    char l_channels[sizeof(((dap_stream_session_t *)0)->active_channels)] = {0};
    dap_enc_key_type_t l_enc_type = DEFAULT_ENC_TYPE;
    size_t l_enc_key_size = DEFAULT_ENC_KEY_SIZE;
    int l_enc_headers = 0;
    bool l_is_legacy = true;

    /* url_path contains the params: "channels=RS,enc_type=6,enc_key_size=32,enc_headers=0" */
    char *l_url_copy = DAP_NEW_SIZE(char, strlen(l_url_path) + 1);
    if (!l_url_copy) { trans_set_status(a_req, 500); return -1; }
    memcpy(l_url_copy, l_url_path, strlen(l_url_path) + 1);

    char *l_tok_tmp = NULL;
    char *l_tok = strtok_r(l_url_copy, ",", &l_tok_tmp);
    while (l_tok) {
        char *l_name = l_tok;
        char *l_value = strchr(l_tok, '=');
        if (l_value && l_value != l_name) {
            *l_value++ = '\0';
            if (strcmp(l_name, "channels") == 0) {
                if (strlen(l_value) >= sizeof(l_channels)) {
                    log_it(L_WARNING, "stream_ctl: channels param too long");
                    DAP_DELETE(l_url_copy);
                    trans_set_status(a_req, 400);
                    return -1;
                }
                dap_strncpy(l_channels, l_value, sizeof(l_channels));
            } else if (strcmp(l_name, "enc_type") == 0) {
                l_enc_type = atoi(l_value);
                l_is_legacy = false;
            } else if (strcmp(l_name, "enc_key_size") == 0) {
                l_enc_key_size = (size_t)atoi(l_value);
                l_is_legacy = false;
            } else if (strcmp(l_name, "enc_headers") == 0) {
                l_enc_headers = atoi(l_value);
            }
        }
        l_tok = strtok_r(NULL, ",", &l_tok_tmp);
    }
    DAP_DELETE(l_url_copy);

    if (l_is_legacy) {
        l_enc_type = DAP_ENC_KEY_TYPE_OAES;
    }

    /* Look up KeyID for ACL check */
    const char *l_key_id = trans_get_key_id(a_req);
    dap_enc_ks_key_t *l_ks_key = NULL;
    if (l_key_id && l_key_id[0]) {
        l_ks_key = dap_enc_ks_find(l_key_id);
        if (!l_ks_key) {
            log_it(L_WARNING, "stream_ctl: KeyID '%s' not found", l_key_id);
            trans_set_status(a_req, 400);
            return -2;
        }
    }

    /* Create stream session */
    dap_stream_session_t *l_session = dap_stream_session_pure_new();
    if (!l_session) {
        trans_set_status(a_req, 500);
        return -3;
    }

    memcpy(l_session->active_channels, l_channels, sizeof(l_session->active_channels));

    /* Generate session key */
    char l_key_str[KEX_KEY_STR_SIZE + 1];
    dap_random_string_fill(l_key_str, KEX_KEY_STR_SIZE);
    l_key_str[KEX_KEY_STR_SIZE] = '\0';  /* dap_random_string_fill does NOT NUL-terminate */

    l_session->key = dap_enc_key_new_generate(l_enc_type, l_key_str, KEX_KEY_STR_SIZE,
                                               NULL, 0, l_enc_key_size);
    if (!l_session->key) {
        log_it(L_ERROR, "stream_ctl: failed to generate session key");
        trans_set_status(a_req, 500);
        return -4;
    }

    /* Set ACL if KeyID was provided */
    if (l_ks_key) {
        l_session->acl = l_ks_key->acl_list;
        l_session->node = l_ks_key->node_addr;
        log_it(L_INFO, "stream_ctl: session %u node_addr=" NODE_ADDR_FP_STR,
               l_session->id, NODE_ADDR_FP_ARGS_S(l_ks_key->node_addr));
    }

    /* Build response: "session_id key_str protocol_version enc_type enc_headers" */
    if (l_is_legacy) {
        trans_reply_f(a_req, "%u %s", l_session->id, l_key_str);
    } else {
        trans_reply_f(a_req, "%u %s %u %d %d",
                      l_session->id, l_key_str,
                      DAP_PROTOCOL_VERSION, l_enc_type, l_enc_headers);
    }

    log_it(L_INFO, "stream_ctl: session %u initialized (channels=%s, enc=%d)",
           l_session->id, l_channels, l_enc_type);

    trans_set_status(a_req, 200);
    return 0;
}
