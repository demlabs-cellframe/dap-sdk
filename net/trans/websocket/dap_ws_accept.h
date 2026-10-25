#ifndef DAP_WS_ACCEPT_H
#define DAP_WS_ACCEPT_H
#include "dap_hash.h"
#include "dap_enc_base64.h"
#include <string.h>

/* RFC 6455 section 4.2.2: SHA-1(key text + GUID), not the DAP fast hash. */
static inline int dap_ws_accept_key(const char *key, char *out, size_t capacity)
{
    if (!key || !out || capacity < 29 || strlen(key) != 24) return -1;
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char input[60], digest[20];
    memcpy(input, key, 24);
    memcpy(input + 24, guid, 36);
    if (dap_hash_sha1(digest, input, sizeof(input))) return -1;
    size_t n = dap_enc_base64_encode(digest, sizeof(digest), out, DAP_ENC_DATA_TYPE_B64);
    if (n != 28) return -1;
    out[n] = '\0';
    return 0;
}
#endif
