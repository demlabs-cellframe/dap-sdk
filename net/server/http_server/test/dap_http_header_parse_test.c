/*
 * Strict Content-Length parsing regression (Oct-2026 security review).
 *
 * The old parser fed the header value to atoi() into a size_t: "-1" wrapped
 * to SIZE_MAX and huge values sized the request-body allocation. The parser
 * must now reject negatives, trailing garbage and anything above
 * DAP_HTTP_IN_CONTENT_LENGTH_MAX, marking the request via
 * in_content_length_bad for the 413 path.
 */
#include <stdio.h>
#include <string.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_http_client.h"
#include "dap_http_header_server.h"

#define LOG_TAG "dap_http_header_parse_test"

static int s_failures = 0;

#define ASSERT_CASE(expr, name) { \
    if (expr) { dap_pass_msg(name); } \
    else { s_failures++; dap_fail(name); } \
}

static void parse_one(dap_http_client_t *a_cl, const char *a_line)
{
    memset(a_cl, 0, sizeof(*a_cl));
    dap_http_header_parse(a_cl, a_line, strlen(a_line));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    dap_log_set_external_output(LOGGER_OUTPUT_STDERR, NULL);
    dap_http_client_t l_cl;

    parse_one(&l_cl, "Content-Length: 123");
    ASSERT_CASE(l_cl.in_content_length == 123 && !l_cl.in_content_length_bad,
                "plain Content-Length parsed");

    parse_one(&l_cl, "Content-Length: 0");
    ASSERT_CASE(l_cl.in_content_length == 0 && !l_cl.in_content_length_bad,
                "zero Content-Length is valid");

    parse_one(&l_cl, "Content-Length: -1");
    ASSERT_CASE(l_cl.in_content_length_bad && l_cl.in_content_length == 0,
                "negative Content-Length rejected");

    parse_one(&l_cl, "Content-Length: 99999999999999999999");
    ASSERT_CASE(l_cl.in_content_length_bad, "overflowing Content-Length rejected");

    parse_one(&l_cl, "Content-Length: 20000000");
    ASSERT_CASE(l_cl.in_content_length_bad, "over-cap Content-Length (20MB) rejected");

    parse_one(&l_cl, "Content-Length: 1024x");
    ASSERT_CASE(l_cl.in_content_length_bad, "trailing-garbage Content-Length rejected");

    parse_one(&l_cl, "Content-Length: 16777216"); // exactly the 16MB cap: allowed
    ASSERT_CASE(!l_cl.in_content_length_bad && l_cl.in_content_length == 16777216,
                "exactly 16MB is the allowed maximum");

    parse_one(&l_cl, "Content-Length: 16777217"); // one byte over the cap
    ASSERT_CASE(l_cl.in_content_length_bad, "16MB+1 rejected");

    return s_failures ? -1 : 0;
}
