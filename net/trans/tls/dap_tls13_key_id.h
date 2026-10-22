/* Parse only the server-generated ENC reply. No trust decisions here. */
#ifndef DAP_TLS13_KEY_ID_H
#define DAP_TLS13_KEY_ID_H
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <json-c/json.h>
#include "dap_enc_base64.h"
#include "dap_enc_ks.h"

static int dap_tls13_key_id_from_reply(const void *data, size_t size,
                                      char out[DAP_ENC_KS_KEY_ID_SIZE + 1])
{
    if (!out) return -1;
    memset(out, 0, DAP_ENC_KS_KEY_ID_SIZE + 1);
    if (!data || !size || size > INT_MAX) return -1;
    struct json_tokener *tok = json_tokener_new();
    if (!tok) return -1;
    json_object *root = json_tokener_parse_ex(tok, data, (int)size), *id = NULL;
    bool valid = json_tokener_get_error(tok) == json_tokener_success && root &&
        json_object_is_type(root, json_type_object) &&
        json_object_object_get_ex(root, "encrypt_id", &id) && json_object_is_type(id, json_type_string);
    size_t end = json_tokener_get_parse_end(tok);
    const unsigned char *bytes = data;
    while (end < size && (bytes[end] == ' ' || bytes[end] == '\n' || bytes[end] == '\r' || bytes[end] == '\t')) ++end;
    valid = valid && end == size;
    if (valid) {
        const char *encoded = json_object_get_string(id);
        size_t length = (size_t)json_object_get_string_len(id);
        /* Exact encoded length bounds allocation and excludes embedded NUL. */
        valid = length == ((DAP_ENC_KS_KEY_ID_SIZE + 2) / 3) * 4 && strlen(encoded) == length;
        for (size_t i = 0; valid && i < length; ++i) {
            unsigned char c = (unsigned char)encoded[i];
            valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '+' || c == '/';
        }
        if (valid) {
            char decoded[DAP_ENC_KS_KEY_ID_SIZE + 4] = {0};
            size_t n = dap_enc_base64_decode(encoded, length, decoded, DAP_ENC_DATA_TYPE_B64);
            valid = n == DAP_ENC_KS_KEY_ID_SIZE;
            if (valid) memcpy(out, decoded, n);
        }
    }
    out[DAP_ENC_KS_KEY_ID_SIZE] = '\0';
    json_object_put(root);
    json_tokener_free(tok);
    return valid ? 0 : -1;
}
#endif
