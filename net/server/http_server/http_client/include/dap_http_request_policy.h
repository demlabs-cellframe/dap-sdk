/* HTTP request limits and Connection token parsing; no socket ownership. */
#ifndef DAP_HTTP_REQUEST_POLICY_H
#define DAP_HTTP_REQUEST_POLICY_H
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define DAP_HTTP_REQUEST_HEADER_BYTES_MAX (32U * 1024U)
#define DAP_HTTP_REQUEST_HEADER_COUNT_MAX 128U

static inline bool dap_http_token_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || (c && strchr("!#$%&'*+-.^_`|~", c));
}

static inline bool dap_http_token_equals(const char *p, size_t n, const char *word)
{
    if (n != strlen(word)) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)word[i]) return false;
    }
    return true;
}

/* Empty list members are ignored per HTTP list syntax. close is sticky
 * across multiple Connection fields; unknown options preserve the default. */
static inline int dap_http_connection_options(const char *value, bool *close,
                                               bool *keep_alive)
{
    if (!value || !close || !keep_alive) return -1;
    const char *p = value;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        const char *start = p;
        while (dap_http_token_char((unsigned char)*p)) ++p;
        if (p == start) return -1;
        size_t n = (size_t)(p - start);
        while (*p == ' ' || *p == '\t') ++p;
        if (*p && *p != ',') return -1;
        if (dap_http_token_equals(start, n, "close")) *close = true;
        if (dap_http_token_equals(start, n, "keep-alive")) *keep_alive = true;
    }
    return 0;
}
#endif
