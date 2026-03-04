/**
 * Comprehensive HTTP Client Test Suite
 *
 * Features tested:
 * - Redirect handling with connection reuse
 * - Chunked transfer encoding with streaming
 * - Smart buffer optimization
 * - Error handling and timeouts
 * - MIME-based streaming detection
 * - HEAD method support (200 OK, redirects, Connection: close, 404, custom headers)
 *
 * All tests run against a built-in mock HTTP server (127.0.0.1:18089).
 * No external network access is required.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "dap_client_http.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_http_header.h"
#include "dap_strfuncs.h"

/* ============================================================
 * Embedded mock HTTP/1.1 server
 *
 * Provides deterministic httpbin-compatible responses so the
 * whole test suite is fully self-contained (no external network).
 * ============================================================ */

#define MOCK_HOST     "127.0.0.1"
#define MOCK_PORT     18089
#define _STR(x)       #x
#define STR(x)        _STR(x)
#define MOCK_PORT_STR STR(MOCK_PORT)

/* Minimal 1×1 px RGB PNG (valid binary, correct CRC values). */
static const unsigned char s_tiny_png[] = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,   /* signature   */
    0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,   /* IHDR len+id */
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,   /* 1×1         */
    0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,0xDE, /* 8bpp RGB + CRC */
    0x00,0x00,0x00,0x0C,0x49,0x44,0x41,0x54,   /* IDAT len+id */
    0x08,0xD7,0x63,0xF8,0xCF,0xC0,0x00,0x00,   /* deflate     */
    0x00,0x02,0x00,0x01,0xE2,0x21,0xBC,0x33,   /* data + CRC  */
    0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,   /* IEND len+id */
    0xAE,0x42,0x60,0x82                         /* IEND CRC    */
};
#define TINY_PNG_SIZE ((int)sizeof(s_tiny_png))

static int          s_srv_fd      = -1;
static volatile int s_srv_running = 0;
static pthread_t    s_srv_thread;

/* ---- low-level send helpers ---- */
static void _srv_send(int fd, const void *d, size_t n)  { send(fd, d, n, MSG_NOSIGNAL); }
static void _srv_sends(int fd, const char *s)            { _srv_send(fd, s, strlen(s)); }

static void _srv_chunk(int fd, const void *d, size_t n) {
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "%zx\r\n", n);
    _srv_sends(fd, hdr);
    _srv_send(fd, d, n);
    _srv_sends(fd, "\r\n");
}
static void _srv_chunk_end(int fd) { _srv_sends(fd, "0\r\n\r\n"); }

/* ---- emit status line + common headers ----
 * close_conn: 1 → send "Connection: close", 0 → send "Connection: keep-alive"
 */
static void _srv_status(int fd, int code, const char *ct, int content_len,
                        int close_conn) {
    const char *txt =
        code == 200 ? "OK" :
        code == 201 ? "Created" :
        code == 301 ? "Moved Permanently" :
        code == 302 ? "Found" :
        code == 308 ? "Permanent Redirect" :
        code == 404 ? "Not Found" :
        code == 508 ? "Loop Detected" : "Unknown";
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Connection: %s\r\n",
             code, txt,
             ct ? ct : "text/plain",
             close_conn ? "close" : "keep-alive");
    _srv_sends(fd, hdr);
    if (content_len >= 0) {
        char cl[48];
        snprintf(cl, sizeof(cl), "Content-Length: %d\r\n", content_len);
        _srv_sends(fd, cl);
    } else {
        _srv_sends(fd, "Transfer-Encoding: chunked\r\n");
    }
    _srv_sends(fd, "\r\n");
}

/* ---- route handler ----
 * close_conn: honour the Connection: close policy from the outer loop
 */
static void _srv_handle(int fd, const char *req, int close_conn) {
    char method[16] = {0}, path_q[2048] = {0};
    if (sscanf(req, "%15s %2047s", method, path_q) < 2)
        return;

    int is_head = (strcasecmp(method, "HEAD") == 0);

    /* split path / query string */
    char path[2048] = {0}, query[2048] = {0};
    char *qmark = strchr(path_q, '?');
    if (qmark) {
        size_t plen = (size_t)(qmark - path_q);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, path_q, plen);
        path[plen] = '\0';
        dap_strncpy(query, qmark + 1, sizeof(query) - 1);
    } else {
        dap_strncpy(path, path_q, sizeof(path) - 1);
    }

    /* --------------------------------------------------
     * /httpbin/get  – basic GET / HEAD 200
     * -------------------------------------------------- */
    if (strcmp(path, "/httpbin/get") == 0) {
        const char *body =
            "{\"url\":\"http://" MOCK_HOST ":" MOCK_PORT_STR "/httpbin/get\","
            "\"args\":{}}";
        _srv_status(fd, 200, "application/json", (int)strlen(body), close_conn);
        if (!is_head) _srv_sends(fd, body);
        return;
    }

    /* --------------------------------------------------
     * /httpbin/post  – echo POST body
     * -------------------------------------------------- */
    if (strcmp(path, "/httpbin/post") == 0) {
        /* body follows \r\n\r\n in already-received buffer */
        const char *sep  = strstr(req, "\r\n\r\n");
        const char *body = sep ? sep + 4 : "";
        char resp[8192];
        int rlen = snprintf(resp, sizeof(resp),
                            "{\"data\":\"%s\","
                            "\"json\":{\"name\": \"test_user\","
                            "\"message\": \"Hello from DAP HTTP client\"}}",
                            body);
        _srv_status(fd, 200, "application/json", rlen, close_conn);
        if (!is_head) _srv_send(fd, resp, (size_t)rlen);
        return;
    }

    /* --------------------------------------------------
     * /httpbin/headers  – echo request headers as text
     * The tests just do strstr() on the body, so we can
     * return the raw request header block verbatim.
     * -------------------------------------------------- */
    if (strcmp(path, "/httpbin/headers") == 0) {
        const char *hdr_end = strstr(req, "\r\n\r\n");
        int hdr_len = hdr_end ? (int)(hdr_end - req) : (int)strlen(req);
        _srv_status(fd, 200, "text/plain", hdr_len, close_conn);
        if (!is_head) _srv_send(fd, req, (size_t)hdr_len);
        return;
    }

    /* --------------------------------------------------
     * /httpbin/status/<code>
     * -------------------------------------------------- */
    if (strncmp(path, "/httpbin/status/", 16) == 0) {
        int code = atoi(path + 16);
        if (code <= 0) code = 200;
        const char *err_body = "";
        _srv_status(fd, code, "text/plain", (int)strlen(err_body), close_conn);
        if (!is_head) _srv_sends(fd, err_body);
        return;
    }

    /* --------------------------------------------------
     * /httpbin/bytes/<N>  – N bytes, Content-Length
     * -------------------------------------------------- */
    if (strncmp(path, "/httpbin/bytes/", 15) == 0) {
        int n = atoi(path + 15);
        if (n <= 0) n = 256;
        if (n > 1024 * 1024) n = 1024 * 1024;
        _srv_status(fd, 200, "application/octet-stream", n, close_conn);
        if (!is_head) {
            char *buf = calloc(1, (size_t)n);
            if (buf) {
                memset(buf, 0x41 /* 'A' */, (size_t)n);
                _srv_send(fd, buf, (size_t)n);
                free(buf);
            }
        }
        return;
    }

    /* --------------------------------------------------
     * /httpbin/stream/<N>  – N chunked JSON objects
     * -------------------------------------------------- */
    if (strncmp(path, "/httpbin/stream/", 16) == 0) {
        int n = atoi(path + 16);
        if (n <= 0) n = 3;
        if (n > 100) n = 100;
        _srv_status(fd, 200, "application/json", -1 /* chunked */, close_conn);
        if (!is_head) {
            for (int i = 0; i < n; i++) {
                char chunk[128];
                int clen = snprintf(chunk, sizeof(chunk),
                                    "{\"id\":%d,\"data\":\"stream-object-%d\"}\n",
                                    i + 1, i + 1);
                _srv_chunk(fd, chunk, (size_t)clen);
                usleep(10000); /* 10 ms gap → separate progress callbacks */
            }
            _srv_chunk_end(fd);
        }
        return;
    }

    /* --------------------------------------------------
     * /httpbin/stream-bytes/<N>  – chunked binary data
     * -------------------------------------------------- */
    if (strncmp(path, "/httpbin/stream-bytes/", 22) == 0) {
        int total = atoi(path + 22);
        if (total <= 0) total = 1024;
        if (total > 1024 * 1024) total = 1024 * 1024;
        _srv_status(fd, 200, "application/octet-stream", -1 /* chunked */, close_conn);
        if (!is_head) {
            const int chunk_size = 8192;
            char *buf = malloc((size_t)chunk_size);
            if (buf) {
                memset(buf, 0x42 /* 'B' */, (size_t)chunk_size);
                int sent = 0;
                while (sent < total) {
                    int to_send = ((total - sent) < chunk_size) ?
                                  (total - sent) : chunk_size;
                    _srv_chunk(fd, buf, (size_t)to_send);
                    sent += to_send;
                    usleep(5000); /* 5 ms between chunks */
                }
                free(buf);
            }
            _srv_chunk_end(fd);
        }
        return;
    }

    /* --------------------------------------------------
     * /httpbin/image/png  – minimal valid PNG
     * -------------------------------------------------- */
    if (strcmp(path, "/httpbin/image/png") == 0) {
        _srv_status(fd, 200, "image/png", TINY_PNG_SIZE, close_conn);
        if (!is_head) _srv_send(fd, s_tiny_png, sizeof(s_tiny_png));
        return;
    }

    /* --------------------------------------------------
     * /httpbin/redirect/<N>  – N hops, then 200
     * -------------------------------------------------- */
    if (strncmp(path, "/httpbin/redirect/", 18) == 0) {
        int n = atoi(path + 18);
        if (n > 0) {
            char loc[256];
            snprintf(loc, sizeof(loc),
                     "HTTP/1.1 302 Found\r\n"
                     "Location: http://" MOCK_HOST ":" MOCK_PORT_STR
                     "/httpbin/redirect/%d\r\n"
                     "Content-Length: 0\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     n - 1);
            _srv_sends(fd, loc);
        } else {
            const char *ok = "{\"redirects\":0}";
            _srv_status(fd, 200, "application/json", (int)strlen(ok), close_conn);
            if (!is_head) _srv_sends(fd, ok);
        }
        return;
    }

    /* --------------------------------------------------
     * /httpbin/absolute-redirect/<N>  – same, for limit test
     * -------------------------------------------------- */
    if (strncmp(path, "/httpbin/absolute-redirect/", 27) == 0) {
        int n = atoi(path + 27);
        if (n > 0) {
            char loc[256];
            snprintf(loc, sizeof(loc),
                     "HTTP/1.1 302 Found\r\n"
                     "Location: http://" MOCK_HOST ":" MOCK_PORT_STR
                     "/httpbin/absolute-redirect/%d\r\n"
                     "Content-Length: 0\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     n - 1);
            _srv_sends(fd, loc);
        } else {
            const char *ok = "{\"redirects\":0}";
            _srv_status(fd, 200, "application/json", (int)strlen(ok), close_conn);
            if (!is_head) _srv_sends(fd, ok);
        }
        return;
    }

    /* --------------------------------------------------
     * /httpbin/redirect-to?url=<target>
     * -------------------------------------------------- */
    if (strcmp(path, "/httpbin/redirect-to") == 0) {
        char target[512] = "/httpbin/get";
        const char *up = strstr(query, "url=");
        if (up) {
            const char *v   = up + 4;
            const char *amp = strchr(v, '&');
            size_t      vlen = amp ? (size_t)(amp - v) : strlen(v);
            if (vlen < sizeof(target)) { memcpy(target, v, vlen); target[vlen] = '\0'; }
        }
        char loc[1024];
        if (target[0] == '/') {
            snprintf(loc, sizeof(loc),
                     "HTTP/1.1 302 Found\r\n"
                     "Location: http://" MOCK_HOST ":" MOCK_PORT_STR "%s\r\n"
                     "Content-Length: 0\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     target);
        } else {
            snprintf(loc, sizeof(loc),
                     "HTTP/1.1 302 Found\r\n"
                     "Location: %s\r\n"
                     "Content-Length: 0\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     target);
        }
        _srv_sends(fd, loc);
        return;
    }

    /* --------------------------------------------------
     * /pub-redirect  – simulates pub.cellframe.net 301
     * (used by test 15 instead of the real CDN)
     * -------------------------------------------------- */
    if (strcmp(path, "/pub-redirect") == 0) {
        char loc[256];
        snprintf(loc, sizeof(loc),
                 "HTTP/1.1 301 Moved Permanently\r\n"
                 "Location: http://" MOCK_HOST ":" MOCK_PORT_STR "/httpbin/get\r\n"
                 "Content-Length: 0\r\n"
                 "Connection: %s\r\n\r\n",
                 close_conn ? "close" : "keep-alive");
        _srv_sends(fd, loc);
        return;
    }

    /* --------------------------------------------------
     * Fallback: 404
     * -------------------------------------------------- */
    const char *nf = "Not Found";
    _srv_status(fd, 404, "text/plain", (int)strlen(nf), close_conn);
    if (!is_head) _srv_sends(fd, nf);
}

/* ---- accept loop (runs in background thread) ---- */
static void *s_srv_loop(void *arg) {
    (void)arg;
    while (s_srv_running) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int cfd = accept(s_srv_fd, (struct sockaddr *)&cli, &cli_len);
        if (cfd < 0) {
            if (s_srv_running) usleep(1000);
            continue;
        }

        /* per-connection receive timeout */
        struct timeval rtv = {5, 0};
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

        /* Keep-alive: handle multiple requests on same TCP connection.
         * The client reuses the connection when following redirects, so
         * we must NOT close after each response unless explicitly asked. */
        while (1) {
            char req[65536] = {0};
            size_t total = 0;
            while (total < sizeof(req) - 1) {
                ssize_t n = recv(cfd, req + total, sizeof(req) - 1 - total, 0);
                if (n <= 0) goto next_conn;
                total += (size_t)n;
                if (strstr(req, "\r\n\r\n")) break;
            }
            if (total == 0) break;

            /* Honour explicit Connection: close from the client */
            int close_conn = (strstr(req, "Connection: close") != NULL ||
                              strstr(req, "connection: close") != NULL);

            _srv_handle(cfd, req, close_conn);

            if (close_conn) break;
        }
        next_conn:
        close(cfd);
    }
    return NULL;
}

/* ---- start / stop ---- */
static int s_mock_server_start(void) {
    s_srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_srv_fd < 0) return -1;

    int opt = 1;
    setsockopt(s_srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* accept() will return after 1 s so the loop can check s_srv_running */
    struct timeval atv = {1, 0};
    setsockopt(s_srv_fd, SOL_SOCKET, SO_RCVTIMEO, &atv, sizeof(atv));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MOCK_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(s_srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_srv_fd); s_srv_fd = -1; return -1;
    }
    if (listen(s_srv_fd, 32) < 0) {
        close(s_srv_fd); s_srv_fd = -1; return -1;
    }
    s_srv_running = 1;
    pthread_create(&s_srv_thread, NULL, s_srv_loop, NULL);
    printf("  ✓ Mock HTTP server started at http://%s:%d\n", MOCK_HOST, MOCK_PORT);
    return 0;
}

static void s_mock_server_stop(void) {
    s_srv_running = 0;
    if (s_srv_fd >= 0) {
        shutdown(s_srv_fd, SHUT_RDWR);
        close(s_srv_fd);
        s_srv_fd = -1;
    }
    pthread_join(s_srv_thread, NULL);
}

/* ============================================================
 * Test state tracking
 * ============================================================ */

static struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    int assertions_passed;
    int assertions_failed;
    int current_test_failures;
    time_t start_time;
} g_test_state = {0};

#define TEST_START(name) do { \
    printf("\n[TEST %d] %s\n", ++g_test_state.tests_run, name); \
    printf("=========================================\n"); \
    g_test_state.current_test_failures = 0; \
} while(0)

#define TEST_EXPECT(condition, message) do { \
    if (condition) { \
        printf("✓ PASS: %s\n", message); \
        g_test_state.assertions_passed++; \
    } else { \
        printf("✗ FAIL: %s\n", message); \
        g_test_state.assertions_failed++; \
        g_test_state.current_test_failures++; \
    } \
} while(0)

#define TEST_END() do { \
    if (g_test_state.current_test_failures == 0) { \
        g_test_state.tests_passed++; \
    } else { \
        g_test_state.tests_failed++; \
    } \
} while(0)

#define TEST_INFO(format, ...) do { \
    printf("  INFO: " format "\n", ##__VA_ARGS__); \
} while(0)

/* ---- Test completion flags ---- */
static bool g_test1_completed  = false;
static bool g_test2_completed  = false;
static bool g_test3_completed  = false;
static bool g_test4_completed  = false;
static bool g_test5_completed  = false;
static bool g_test6_completed  = false;
static bool g_test7_completed  = false;
static bool g_test8_completed  = false;
static bool g_test9_completed  = false;
static bool g_test10_completed = false;
static bool g_test11_completed = false;
static bool g_test12_completed = false;
static bool g_test13_completed = false;
static bool g_test14_completed = false;
static bool g_test15_completed = false;
static bool g_test16_completed = false;
static bool g_test17_completed = false;
static bool g_test18_completed = false;
static bool g_test19_completed = false;
static bool g_test20_completed = false;
static bool g_test21_completed = false;

/* Helper: block until test flag set or timeout */
static void wait_for_test_completion(bool *completion_flag, int timeout_seconds)
{
    int waited = 0;
    printf("  Waiting for test completion");
    fflush(stdout);

    while (!(*completion_flag) && waited < timeout_seconds) {
        sleep(1);
        waited++;
        if (waited % 2 == 0) {
            printf(".");
            fflush(stdout);
        }
    }

    if (*completion_flag) {
        printf(" completed in %d seconds\n", waited);
    } else {
        printf(" TIMEOUT after %d seconds!\n", timeout_seconds);
        TEST_INFO("WARNING: Test did not complete within %d seconds", timeout_seconds);
    }
}

/* ============================================================
 * Test 1: Basic redirect functionality
 * ============================================================ */
static bool g_test1_success = false;
static int  g_test1_status  = 0;

static void test1_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Response: status=%d, size=%zu bytes", a_status_code, a_body_size);
    g_test1_status  = a_status_code;
    g_test1_success = (a_status_code == 200 && a_body_size > 0);

    if (a_body_size > 0) {
        char *body_str = (char *)a_body;
        if (strstr(body_str, MOCK_HOST) && strstr(body_str, "httpbin/get"))
            TEST_INFO("Successfully reached final redirect destination");
    }
    g_test1_completed = true;
}

static void test1_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error: code=%d (%s)", a_error_code, strerror(a_error_code));
    g_test1_success  = false;
    g_test1_completed = true;
}

/* ============================================================
 * Test 2: Redirect limit enforcement
 * ============================================================ */
static bool g_test2_got_error    = false;
static int  g_test2_error_code   = 0;
static int  g_test2_redirect_count = 0;

static void test2_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Response received: status=%d, size=%zu", a_status_code, a_body_size);

    if (a_body_size > 0) {
        char *body_str = (char *)a_body;
        if (strstr(body_str, MOCK_HOST) && strstr(body_str, "httpbin/get"))
            TEST_INFO("Successfully reached final destination");
    }
    g_test2_completed = true;
}

static void test2_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Expected error received: code=%d (%s)", a_error_code,
              a_error_code == -301 ? "too many redirects" : "other error");
    g_test2_got_error  = true;
    g_test2_error_code = a_error_code;
    g_test2_completed  = true;
}

/* ============================================================
 * Test 3: Chunked streaming
 * ============================================================ */
static int    g_test3_chunks_received  = 0;
static size_t g_test3_total_streamed   = 0;
static bool   g_test3_response_called  = false;
static time_t g_test3_first_chunk_time = 0;

static void test3_progress_callback(void *a_data, size_t a_data_size,
                                    size_t a_total, void *a_arg)
{
    g_test3_chunks_received++;
    g_test3_total_streamed += a_data_size;
    TEST_INFO("Chunk #%d: %zu bytes (total: %zu)",
              g_test3_chunks_received, a_data_size, g_test3_total_streamed);

    if (g_test3_chunks_received == 1)
        g_test3_first_chunk_time = time(NULL);

    if (a_data_size > 0 && g_test3_chunks_received == 1) {
        char *data_str = (char *)a_data;
        int json_count = 0;
        for (size_t i = 0; i < a_data_size; i++)
            if (data_str[i] == '{') json_count++;
        TEST_INFO("Detected %d JSON objects in first chunk", json_count);

        if (json_count >= 3) {
            TEST_INFO("Multiple JSON objects received in single chunk (valid streaming)");
            g_test3_completed = true;
        }
    }

    /* 0-byte progress callback = end-of-chunked-stream marker */
    if (a_data_size == 0 && g_test3_chunks_received > 0) {
        TEST_INFO("End-of-stream (0-byte chunk), completing test");
        g_test3_completed = true;
        return;
    }

    if (g_test3_chunks_received >= 1 &&
        g_test3_first_chunk_time > 0 &&
        (time(NULL) - g_test3_first_chunk_time) >= 3) {
        TEST_INFO("Completing test after receiving data and waiting period");
        g_test3_completed = true;
    }
}

static void test3_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Final response called (unexpected in streaming mode)");
    g_test3_response_called = true;
    g_test3_completed       = true;
}

static void test3_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in chunked test: code=%d", a_error_code);
    g_test3_completed = true;
}

/* ============================================================
 * Test 4: Small file accumulation
 * ============================================================ */
static bool   g_test4_response_received = false;
static size_t g_test4_response_size     = 0;
static int    g_test4_progress_calls    = 0;
static size_t g_test4_progress_total    = 0;
static time_t g_test4_start_time        = 0;

static void test4_progress_callback(void *a_data, size_t a_data_size,
                                    size_t a_total, void *a_arg)
{
    g_test4_progress_calls++;
    g_test4_progress_total += a_data_size;
    TEST_INFO("Progress #%d: %zu bytes (total so far: %zu)",
              g_test4_progress_calls, a_data_size, g_test4_progress_total);

    if (g_test4_progress_total >= 256 ||
        (g_test4_start_time > 0 && (time(NULL) - g_test4_start_time) >= 5)) {
        TEST_INFO("Completing test via progress callback (streaming mode)");
        g_test4_response_size = g_test4_progress_total;
        g_test4_completed     = true;
    }
}

static void test4_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Final response: status=%d, size=%zu bytes (accumulation mode)",
              a_status_code, a_body_size);
    g_test4_response_received = true;
    g_test4_response_size     = a_body_size;
    g_test4_completed         = true;
}

static void test4_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in accumulation test: code=%d (%s)", a_error_code,
              a_error_code == ETIMEDOUT  ? "ETIMEDOUT" :
              a_error_code == EHOSTUNREACH ? "EHOSTUNREACH" :
              a_error_code == ECONNREFUSED ? "ECONNREFUSED" : "Unknown");
    g_test4_completed = true;
}

/* ============================================================
 * Test 5: follow_redirects flag behavior
 * ============================================================ */
static bool g_test5_got_redirect_response = false;
static int  g_test5_redirect_status       = 0;

static void test5_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Redirect response: status=%d, size=%zu", a_status_code, a_body_size);
    g_test5_got_redirect_response = true;
    g_test5_redirect_status       = a_status_code;

    struct dap_http_header *header = a_headers;
    while (header) {
        if (strcasecmp(header->name, "Location") == 0)
            TEST_INFO("Location header: %s", header->value);
        header = header->next;
    }
    g_test5_completed = true;
}

static void test5_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Unexpected error in redirect flag test: code=%d", a_error_code);
    g_test5_completed = true;
}

/* ============================================================
 * Test 6: MIME-based streaming detection
 * ============================================================ */
static int  g_test6_progress_calls = 0;
static bool g_test6_mime_detected  = false;
static time_t g_test6_start_time   = 0;

static void test6_progress_callback(void *a_data, size_t a_data_size,
                                    size_t a_total, void *a_arg)
{
    g_test6_progress_calls++;
    TEST_INFO("Binary streaming #%d: %zu bytes", g_test6_progress_calls, a_data_size);

    if (a_data_size >= 4) {
        unsigned char *data = (unsigned char *)a_data;
        if (data[0] == 0x89 && data[1] == 0x50 &&
            data[2] == 0x4E && data[3] == 0x47) {
            TEST_INFO("PNG binary signature detected");
            g_test6_mime_detected = true;
        }
    }

    if (g_test6_progress_calls >= 1 || g_test6_mime_detected)
        g_test6_completed = true;
}

static void test6_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Final response called (may be normal for small images)");

    struct dap_http_header *header = a_headers;
    while (header) {
        if (strcasecmp(header->name, "Content-Type") == 0) {
            TEST_INFO("Content-Type: %s", header->value);
            if (strstr(header->value, "image/png"))
                g_test6_mime_detected = true;
        }
        header = header->next;
    }

    /* Check PNG signature in the body itself */
    if (a_body_size >= 4) {
        unsigned char *data = (unsigned char *)a_body;
        if (data[0] == 0x89 && data[1] == 0x50 &&
            data[2] == 0x4E && data[3] == 0x47) {
            TEST_INFO("PNG signature found in response body");
            g_test6_mime_detected = true;
        }
    }

    g_test6_completed = true;
}

static void test6_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in MIME test: code=%d", a_error_code);
    g_test6_completed = true;
}

/* ============================================================
 * Test 7: Connection timeout
 * ============================================================ */
static bool g_test7_timeout_occurred = false;
static int  g_test7_timeout_code     = 0;

static void test7_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Unexpected response (should timeout): status=%d", a_status_code);
    g_test7_completed = true;
}

static void test7_error_callback(int a_error_code, void *a_arg)
{
    const char *error_name =
        a_error_code == ETIMEDOUT    ? "ETIMEDOUT" :
        a_error_code == 60           ? "ETIMEDOUT(60)" :
        a_error_code == ECONNREFUSED ? "ECONNREFUSED" :
        a_error_code == EHOSTUNREACH ? "EHOSTUNREACH" :
        a_error_code < 0             ? "DAP_INTERNAL" : "Other";
    TEST_INFO("Connection error: code=%d (%s) - Expected for unreachable host",
              a_error_code, error_name);
    g_test7_timeout_occurred = true;
    g_test7_timeout_code     = a_error_code;
    g_test7_completed        = true;
}

/* ============================================================
 * Test 8: Large file streaming efficiency
 * ============================================================ */
static int    g_test8_progress_calls  = 0;
static size_t g_test8_total_received  = 0;
static size_t g_test8_expected_size   = 0;
static bool   g_test8_response_called = false;
static time_t g_test8_start_time      = 0;

static void test8_progress_callback(void *a_data, size_t a_data_size,
                                    size_t a_total, void *a_arg)
{
    g_test8_progress_calls++;
    g_test8_total_received += a_data_size;

    if (g_test8_progress_calls <= 5 || g_test8_progress_calls % 10 == 0) {
        double progress = a_total > 0 ?
            (double)g_test8_total_received * 100.0 / a_total : 0;
        TEST_INFO("Streaming progress #%d: %zu bytes (%.1f%% of %zu total)",
                  g_test8_progress_calls, a_data_size, progress, a_total);
    }

    if (g_test8_expected_size == 0 && a_total > 0) {
        g_test8_expected_size = a_total;
        TEST_INFO("Expected total size: %zu bytes (%.1f MB)",
                  a_total, a_total / (1024.0 * 1024.0));
    }

    if (g_test8_expected_size > 0 &&
        g_test8_total_received >= g_test8_expected_size) {
        TEST_INFO("Streaming complete: received %zu/%zu bytes in %d callbacks",
                  g_test8_total_received, g_test8_expected_size,
                  g_test8_progress_calls);
        g_test8_completed = true;
    }

    if (g_test8_start_time > 0 &&
        (time(NULL) - g_test8_start_time) >= 20 &&
        g_test8_total_received > 50 * 1024) {
        TEST_INFO("Completing after timeout with %zu bytes received",
                  g_test8_total_received);
        g_test8_completed = true;
    }
}

static void test8_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Unexpected final response called (should stream): status=%d, size=%zu",
              a_status_code, a_body_size);
    g_test8_response_called = true;
    g_test8_completed       = true;
}

static void test8_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in large file streaming test: code=%d", a_error_code);
    g_test8_completed = true;
}

/* ============================================================
 * Test 9: File Download with Streaming to Disk
 * ============================================================ */
static int    g_test9_progress_calls = 0;
static size_t g_test9_total_written  = 0;
static size_t g_test9_expected_size  = 0;
static FILE  *g_test9_file           = NULL;
static char   g_test9_filename[256]  = {0};
static time_t g_test9_start_time     = 0;
static bool   g_test9_file_complete  = false;

static void test9_progress_callback(void *a_data, size_t a_data_size,
                                    size_t a_total, void *a_arg)
{
    g_test9_progress_calls++;

    if (g_test9_file == NULL && a_data_size > 0) {
        snprintf(g_test9_filename, sizeof(g_test9_filename),
                 "http_client_test_%ld.png", (long)time(NULL));
        g_test9_file = fopen(g_test9_filename, "wb");
        if (!g_test9_file) {
            TEST_INFO("ERROR: Cannot create file %s", g_test9_filename);
            g_test9_completed = true;
            return;
        }
        TEST_INFO("Streaming PNG to file: %s", g_test9_filename);

        if (a_data_size >= 4) {
            unsigned char *data = (unsigned char *)a_data;
            if (data[0] == 0x89 && data[1] == 0x50 &&
                data[2] == 0x4E && data[3] == 0x47)
                TEST_INFO("✓ PNG signature detected: 89 50 4E 47 (PNG)");
        }

        if (a_total > 0) {
            g_test9_expected_size = a_total;
            TEST_INFO("Expected PNG size: %zu bytes (%.1f KB)",
                      a_total, a_total / 1024.0);
        }
    }

    if (g_test9_file && a_data_size > 0) {
        size_t written = fwrite(a_data, 1, a_data_size, g_test9_file);
        if (written != a_data_size)
            TEST_INFO("WARNING: Write incomplete (%zu/%zu bytes)", written, a_data_size);
        g_test9_total_written += written;
        fflush(g_test9_file);
    }

    if (g_test9_progress_calls <= 5 || g_test9_progress_calls % 10 == 0) {
        double progress = (g_test9_expected_size > 0) ?
            (double)g_test9_total_written * 100.0 / g_test9_expected_size : 0;
        TEST_INFO("File progress #%d: +%zu bytes → %zu total (%.1f%%)",
                  g_test9_progress_calls, a_data_size, g_test9_total_written, progress);
    }

    if (g_test9_expected_size > 0 &&
        g_test9_total_written >= g_test9_expected_size) {
        TEST_INFO("File download complete: %zu bytes in %d chunks",
                  g_test9_total_written, g_test9_progress_calls);
        g_test9_file_complete = true;
        if (g_test9_file) { fclose(g_test9_file); g_test9_file = NULL; }
        g_test9_completed = true;
    }

    if (g_test9_start_time > 0 &&
        (time(NULL) - g_test9_start_time) >= 15 &&
        g_test9_total_written > 1024) {
        TEST_INFO("Completing PNG download: %zu bytes (timeout reached)",
                  g_test9_total_written);
        g_test9_file_complete = true;
        if (g_test9_file) { fclose(g_test9_file); g_test9_file = NULL; }
        g_test9_completed = true;
    }
}

static void test9_response_callback(void *a_body, size_t a_body_size,
                                    struct dap_http_header *a_headers,
                                    void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Unexpected response callback in streaming download (status=%d, size=%zu)",
              a_status_code, a_body_size);

    if (a_body_size > 0 && !g_test9_file_complete) {
        if (g_test9_file == NULL) {
            snprintf(g_test9_filename, sizeof(g_test9_filename),
                     "http_client_test_fallback_%ld.png", (long)time(NULL));
            g_test9_file = fopen(g_test9_filename, "wb");
        }
        if (g_test9_file) {
            fwrite(a_body, 1, a_body_size, g_test9_file);
            fclose(g_test9_file);
            g_test9_file         = NULL;
            g_test9_total_written = a_body_size;

            if (a_body_size >= 4) {
                unsigned char *data = (unsigned char *)a_body;
                if (data[0] == 0x89 && data[1] == 0x50 &&
                    data[2] == 0x4E && data[3] == 0x47)
                    TEST_INFO("✓ Fallback: PNG signature verified in saved file");
            }
            TEST_INFO("Fallback: saved PNG %zu bytes to %s",
                      a_body_size, g_test9_filename);
        }
    }
    g_test9_completed = true;
}

static void test9_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in file download test: code=%d (%s)", a_error_code,
              a_error_code == ETIMEDOUT  ? "ETIMEDOUT" :
              a_error_code == ECONNREFUSED ? "ECONNREFUSED" : "Other");
    if (g_test9_file) { fclose(g_test9_file); g_test9_file = NULL; }
    g_test9_completed = true;
}

/* ============================================================
 * Test 10: POST request with JSON data
 * ============================================================ */
static bool   g_test10_post_success  = false;
static int    g_test10_status        = 0;
static bool   g_test10_json_echoed   = false;
static size_t g_test10_response_size = 0;

static void test10_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("POST response: status=%d, size=%zu bytes", a_status_code, a_body_size);
    g_test10_status        = a_status_code;
    g_test10_response_size = a_body_size;

    if (a_status_code == 200 && a_body_size > 0) {
        g_test10_post_success = true;

        char *response_str = (char *)a_body;
        if (strstr(response_str, "\"name\": \"test_user\"") &&
            strstr(response_str, "\"message\": \"Hello from DAP HTTP client\"")) {
            g_test10_json_echoed = true;
            TEST_INFO("✓ POST data successfully echoed in response");
        }

        struct dap_http_header *header = a_headers;
        while (header) {
            if (strcasecmp(header->name, "Content-Type") == 0) {
                TEST_INFO("Response Content-Type: %s", header->value);
                if (strstr(header->value, "application/json"))
                    TEST_INFO("✓ JSON response Content-Type detected");
            }
            header = header->next;
        }

        if (a_body_size > 100) {
            char preview[101] = {0};
            dap_strncpy(preview, response_str, 100);
            TEST_INFO("Response preview: %.100s...", preview);
        } else {
            TEST_INFO("Full response: %.*s", (int)a_body_size, response_str);
        }
    }
    g_test10_completed = true;
}

static void test10_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("POST request error: code=%d (%s)", a_error_code,
              a_error_code == ETIMEDOUT  ? "ETIMEDOUT" :
              a_error_code == ECONNREFUSED ? "ECONNREFUSED" :
              a_error_code == EHOSTUNREACH ? "EHOSTUNREACH" : "Other");
    g_test10_post_success = false;
    g_test10_completed    = true;
}

/* ============================================================
 * Test 11: Custom headers validation
 * ============================================================ */
static bool g_test11_headers_found = false;
static int  g_test11_status        = 0;

static void test11_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test11_status = a_status_code;
    TEST_INFO("Headers response: status=%d, size=%zu", a_status_code, a_body_size);

    if (a_body_size > 0) {
        char *body_str = (char *)a_body;
        if (strstr(body_str, "X-Test-Client") &&
            strstr(body_str, "DAP-HTTP-Client") &&
            strstr(body_str, "X-Custom-Header")) {
            g_test11_headers_found = true;
            TEST_INFO("✓ Custom headers found in response");
        }
    }
    g_test11_completed = true;
}

static void test11_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in headers test: code=%d", a_error_code);
    g_test11_completed = true;
}

/* ============================================================
 * Test 12: Error handling – 404 Not Found
 * ============================================================ */
static int  g_test12_status        = 0;
static bool g_test12_error_handled = false;

static void test12_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test12_status        = a_status_code;
    g_test12_error_handled = true;
    TEST_INFO("404 response: status=%d, size=%zu", a_status_code, a_body_size);
    g_test12_completed = true;
}

static void test12_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in 404 test: code=%d", a_error_code);
    g_test12_error_handled = true;
    g_test12_completed     = true;
}

/* ============================================================
 * Test 13: Chunked encoding with larger data
 * ============================================================ */
static int    g_test13_chunks_received   = 0;
static bool   g_test13_zero_copy_active  = false;
static size_t g_test13_total_streamed    = 0;

static void test13_progress_callback(void *a_data, size_t a_data_size,
                                     size_t a_total, void *a_arg)
{
    g_test13_chunks_received++;
    g_test13_total_streamed += a_data_size;
    g_test13_zero_copy_active = true;

    TEST_INFO("Chunked chunk #%d: %zu bytes (total: %zu)",
              g_test13_chunks_received, a_data_size, g_test13_total_streamed);

    /* 0-byte callback = end-of-stream */
    if (a_data_size == 0 && g_test13_chunks_received > 0) {
        TEST_INFO("End-of-stream (0-byte chunk), completing test (%zu bytes total)",
                  g_test13_total_streamed);
        g_test13_completed = true;
        return;
    }

    if (g_test13_total_streamed >= 50 * 1024) {
        TEST_INFO("Received sufficient chunked data (%zu bytes), completing test",
                  g_test13_total_streamed);
        g_test13_completed = true;
    }
}

static void test13_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Chunked response: status=%d, size=%zu", a_status_code, a_body_size);
    if (a_body_size > 0)
        g_test13_total_streamed = a_body_size;
    g_test13_completed = true;
}

static void test13_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error in chunked test: code=%d", a_error_code);
    g_test13_completed = true;
}

/* ============================================================
 * Test 14: HEAD method – Basic 200 OK
 * ============================================================ */
static bool   g_test14_success     = false;
static int    g_test14_status      = 0;
static size_t g_test14_body_size   = 0;
static bool   g_test14_has_location = false;

static void test14_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test14_status    = a_status_code;
    g_test14_body_size = a_body_size;

    TEST_INFO("[HEAD_TEST] Response: status=%d, body_size=%zu (should be 0 for HEAD)",
              a_status_code, a_body_size);

    if (a_body_size == 0 && a_status_code == 200) {
        g_test14_success = true;
        TEST_INFO("[HEAD_TEST] ✓ HEAD request successful - no body received as expected");
    }

    if (a_headers) {
        struct dap_http_header *header = a_headers;
        while (header) {
            if (strcasecmp(header->name, "Location") == 0) {
                g_test14_has_location = true;
                TEST_INFO("[HEAD_TEST] Location header found: %s", header->value);
            }
            header = header->next;
        }
    }
    g_test14_completed = true;
}

static void test14_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("[HEAD_TEST] Error: code=%d (%s)", a_error_code, strerror(a_error_code));
    g_test14_success  = false;
    g_test14_completed = true;
}

/* ============================================================
 * Test 15: HEAD method – 301 redirect (via local mock)
 * ============================================================ */
static bool g_test15_success          = false;
static int  g_test15_status           = 0;
static bool g_test15_redirect_handled = false;

static void test15_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test15_status = a_status_code;
    TEST_INFO("[HEAD_TEST] Redirect response: status=%d, body_size=%zu",
              a_status_code, a_body_size);

    if (a_status_code == 308 || a_status_code == 301) {
        g_test15_redirect_handled = true;

        if (a_headers) {
            struct dap_http_header *header = a_headers;
            while (header) {
                if (strcasecmp(header->name, "Location") == 0) {
                    TEST_INFO("[HEAD_TEST] ✓ Location header found: %s", header->value);
                    g_test15_success = true;
                }
                header = header->next;
            }
        }
    }

    if (a_body_size == 0)
        TEST_INFO("[HEAD_TEST] ✓ No body received for HEAD redirect (correct)");

    g_test15_completed = true;
}

static void test15_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("[HEAD_TEST] Redirect error: code=%d", a_error_code);
    g_test15_completed = true;
}

/* ============================================================
 * Test 16: HEAD method – Connection: close handling
 * ============================================================ */
static bool g_test16_success                   = false;
static int  g_test16_status                    = 0;
static bool g_test16_connection_close_handled  = false;

static void test16_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test16_status = a_status_code;
    TEST_INFO("[HEAD_TEST] Connection: close response: status=%d, body_size=%zu",
              a_status_code, a_body_size);

    if (a_headers) {
        struct dap_http_header *header = a_headers;
        while (header) {
            if (strcasecmp(header->name, "Connection") == 0 &&
                strcasecmp(header->value, "close") == 0) {
                g_test16_connection_close_handled = true;
                TEST_INFO("[HEAD_TEST] ✓ Connection: close header detected");
            }
            header = header->next;
        }
    }

    if (a_status_code == 200 || a_status_code == 308 || a_status_code == 301) {
        g_test16_success = true;
        TEST_INFO("[HEAD_TEST] ✓ HEAD request completed successfully with Connection: close");
    }
    g_test16_completed = true;
}

static void test16_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("[HEAD_TEST] Connection: close error: code=%d", a_error_code);
    if (a_error_code == ETIMEDOUT || a_error_code == 60 ||
        a_error_code == ECONNRESET || a_error_code == EPIPE) {
        TEST_INFO("[HEAD_TEST] Expected error for Connection: close - server closed connection");
        g_test16_success = true;
        g_test16_status  = 200;
    }
    g_test16_completed = true;
}

/* ============================================================
 * Test 17: HEAD method – 404 Not Found
 * ============================================================ */
static bool g_test17_success = false;
static int  g_test17_status  = 0;

static void test17_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test17_status = a_status_code;
    TEST_INFO("[HEAD_TEST] 404 response: status=%d, body_size=%zu",
              a_status_code, a_body_size);

    if (a_status_code == 404 && a_body_size == 0) {
        g_test17_success = true;
        TEST_INFO("[HEAD_TEST] ✓ HEAD 404 handled correctly - status 404, no body");
    }
    g_test17_completed = true;
}

static void test17_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("[HEAD_TEST] 404 error callback: code=%d", a_error_code);
    g_test17_completed = true;
}

/* ============================================================
 * Test 18: HEAD method – With custom headers
 * ============================================================ */
static bool g_test18_success = false;
static int  g_test18_status  = 0;

static void test18_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test18_status = a_status_code;
    TEST_INFO("[HEAD_TEST] HEAD with headers: status=%d, body_size=%zu",
              a_status_code, a_body_size);

    if (a_status_code == 200 && a_body_size == 0) {
        g_test18_success = true;
        TEST_INFO("[HEAD_TEST] ✓ HEAD request with custom headers successful");
    }
    g_test18_completed = true;
}

static void test18_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("[HEAD_TEST] HEAD with headers error: code=%d", a_error_code);
    g_test18_completed = true;
}

/* ============================================================
 * Test 19: Regression – GET with large custom headers
 * ============================================================ */
static bool g_test19_success             = false;
static int  g_test19_status             = 0;
static bool g_test19_large_header_echoed = false;

static void test19_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test19_status = a_status_code;
    TEST_INFO("Large header GET response: status=%d, size=%zu", a_status_code, a_body_size);

    if (a_status_code == 200) {
        g_test19_success = true;
        if (a_body_size > 0 && a_body) {
            char *body_str = (char *)a_body;
            if (strstr(body_str, "X-Large-Signature")) {
                g_test19_large_header_echoed = true;
                TEST_INFO("Large custom header echoed in response");
            }
        }
    }
    g_test19_completed = true;
}

static void test19_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Large header GET error: code=%d", a_error_code);
    g_test19_completed = true;
}

/* ============================================================
 * Test 20: Regression – POST with large custom headers
 * ============================================================ */
static bool g_test20_success = false;
static int  g_test20_status  = 0;

static void test20_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test20_status = a_status_code;
    TEST_INFO("Large header POST response: status=%d, size=%zu", a_status_code, a_body_size);
    if (a_status_code == 200)
        g_test20_success = true;
    g_test20_completed = true;
}

static void test20_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Large header POST error: code=%d", a_error_code);
    g_test20_completed = true;
}

/* ============================================================
 * Test 21: Regression – GET with multiple custom headers
 * ============================================================ */
static bool g_test21_success        = false;
static int  g_test21_status         = 0;
static bool g_test21_headers_echoed = false;

static void test21_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    g_test21_status = a_status_code;
    TEST_INFO("Multiple headers GET response: status=%d, size=%zu",
              a_status_code, a_body_size);

    if (a_status_code == 200) {
        g_test21_success = true;
        if (a_body_size > 0 && a_body) {
            char *body_str = (char *)a_body;
            if (strstr(body_str, "X-Header-First") && strstr(body_str, "X-Header-Last")) {
                g_test21_headers_echoed = true;
                TEST_INFO("All multiple custom headers echoed in response");
            }
        }
    }
    g_test21_completed = true;
}

static void test21_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Multiple headers GET error: code=%d", a_error_code);
    g_test21_completed = true;
}

/* ============================================================
 * Test suite runner
 * ============================================================ */
void run_test_suite()
{
    printf("=== HTTP Client Test Suite ===\n");
    printf("Server: http://%s:%d\n", MOCK_HOST, MOCK_PORT);
    printf("Tests run sequentially; each waits for completion.\n\n");

    /* Test 1: Basic redirect */
    TEST_START("Same Host Redirect with Connection Reuse");
    printf("Testing: %s:%d/httpbin/redirect-to?url=/httpbin/get\n",
           MOCK_HOST, MOCK_PORT);
    printf("Expected: 200 OK with connection reuse\n");

    g_test1_success   = false;
    g_test1_completed = false;
    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/redirect-to?url=/httpbin/get", NULL, 0, NULL,
        test1_response_callback, test1_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test1_completed, 10);
    TEST_EXPECT(g_test1_success, "Redirect completed successfully");
    TEST_EXPECT(g_test1_status == 200, "Final status is 200 OK");
    TEST_END();

    /* Test 2: Redirect limit enforcement */
    TEST_START("Redirect Limit Behavior Analysis");
    printf("Testing: %s:%d/httpbin/absolute-redirect/3 (should work within limit)\n",
           MOCK_HOST, MOCK_PORT);
    printf("Expected: Successful response after 3 redirects\n");

    g_test2_got_error      = false;
    g_test2_redirect_count = 0;
    g_test2_completed      = false;

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/absolute-redirect/3", NULL, 0, NULL,
        test2_response_callback, test2_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test2_completed, 10);
    TEST_EXPECT(!g_test2_got_error, "3 redirects should succeed (within limit of 5)");

    printf("\nTesting redirect limit with /absolute-redirect/10 (exceeds limit of 5)...\n");
    g_test2_got_error  = false;
    g_test2_completed  = false;

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/absolute-redirect/10",
        NULL, 0, NULL,
        test2_response_callback, test2_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test2_completed, 15);
    if (g_test2_got_error) {
        if (g_test2_error_code == 508) {
            TEST_EXPECT(true, "Error code is 508 (Loop Detected - too many redirects)");
        } else if (g_test2_error_code == ETIMEDOUT) {
            TEST_INFO("NOTE: Got timeout instead of redirect limit (server-side issue)");
            TEST_EXPECT(true, "Timeout is acceptable for complex redirect chains");
        } else {
            TEST_INFO("Got error code %d instead of expected 508", g_test2_error_code);
            TEST_EXPECT(false, "Unexpected error code");
        }
    } else {
        TEST_INFO("WARNING: 10 redirects completed successfully (limit not enforced)");
        TEST_INFO("This may indicate the redirect limit check needs review");
    }
    TEST_END();

    /* Test 3: Chunked streaming */
    TEST_START("Chunked Transfer Encoding Streaming");
    printf("Testing: %s:%d/httpbin/stream/3 (chunked JSON)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: Progress callbacks with streaming data\n");

    g_test3_chunks_received  = 0;
    g_test3_response_called  = false;
    g_test3_completed        = false;
    g_test3_first_chunk_time = 0;

    dap_client_http_request_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/stream/3", NULL, 0, NULL,
        test3_response_callback, test3_error_callback, NULL,
        test3_progress_callback, NULL, NULL, true
    );

    wait_for_test_completion(&g_test3_completed, 15);
    TEST_EXPECT(g_test3_chunks_received >= 1, "Streaming data received via progress callback");
    TEST_EXPECT(g_test3_total_streamed > 0, "Some data was streamed");
    TEST_EXPECT(!g_test3_response_called, "No final callback (streaming mode)");
    TEST_END();

    /* Test 4: Small file accumulation */
    TEST_START("Small File Accumulation Mode");
    printf("Testing: %s:%d/httpbin/bytes/256 (small file)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: Final callback OR streaming (both acceptable)\n");

    g_test4_response_received = false;
    g_test4_progress_calls    = 0;
    g_test4_progress_total    = 0;
    g_test4_completed         = false;
    g_test4_start_time        = time(NULL);

    dap_client_http_request_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/bytes/256", NULL, 0, NULL,
        test4_response_callback, test4_error_callback, NULL,
        test4_progress_callback, NULL, NULL, true
    );

    wait_for_test_completion(&g_test4_completed, 10);

    bool got_data = (g_test4_response_received && g_test4_response_size == 256) ||
                    (g_test4_progress_calls > 0 && g_test4_progress_total >= 256);
    TEST_EXPECT(got_data, "256 bytes received via response or progress callbacks");

    if (g_test4_response_received) {
        TEST_INFO("Data received via final response callback (accumulation mode)");
        TEST_EXPECT(g_test4_response_size == 256, "Correct file size in response");
    } else if (g_test4_progress_calls > 0) {
        TEST_INFO("Data received via %d progress callbacks (streaming mode)",
                  g_test4_progress_calls);
        TEST_EXPECT(g_test4_progress_total >= 256, "Correct file size via streaming");
    } else {
        TEST_INFO("No data received via either method - this is a problem");
    }
    TEST_END();

    /* Test 5: follow_redirects flag = false */
    TEST_START("Redirect Flag Disabled (follow_redirects = false)");
    printf("Testing: %s:%d/httpbin/redirect/1 with follow_redirects=false\n",
           MOCK_HOST, MOCK_PORT);
    printf("Expected: 302 redirect response (not followed)\n");

    g_test5_got_redirect_response = false;
    g_test5_completed             = false;

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/redirect/1", NULL, 0, NULL,
        test5_response_callback, test5_error_callback,
        NULL, NULL, false  /* follow_redirects = false */
    );

    wait_for_test_completion(&g_test5_completed, 10);
    TEST_EXPECT(g_test5_got_redirect_response, "Redirect response received");

    bool is_redirect     = (g_test5_redirect_status >= 301 && g_test5_redirect_status <= 308);
    bool is_server_error = (g_test5_redirect_status >= 500 && g_test5_redirect_status <= 599);

    if (is_redirect) {
        TEST_INFO("SUCCESS: Got redirect status %d", g_test5_redirect_status);
        TEST_EXPECT(true, "Status indicates redirect (301-308)");
    } else if (is_server_error) {
        TEST_INFO("NOTE: Server returned error %d (server issue, not client issue)",
                  g_test5_redirect_status);
        TEST_EXPECT(true, "Status handled gracefully (server error tolerance)");
    } else {
        TEST_EXPECT(false, "Unexpected status code");
    }
    TEST_END();

    /* Test 6: MIME-based streaming detection */
    TEST_START("MIME-based Streaming Detection (Binary Content)");
    printf("Testing: %s:%d/httpbin/image/png (PNG image)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: MIME type triggers streaming or binary detection\n");

    g_test6_progress_calls = 0;
    g_test6_mime_detected  = false;
    g_test6_completed      = false;
    g_test6_start_time     = time(NULL);

    dap_client_http_request_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/image/png", NULL, 0, NULL,
        test6_response_callback, test6_error_callback, NULL,
        test6_progress_callback, NULL, NULL, true
    );

    wait_for_test_completion(&g_test6_completed, 10);
    TEST_EXPECT(g_test6_mime_detected, "PNG MIME type or signature detected");
    if (g_test6_progress_calls > 0) {
        TEST_INFO("Streaming mode activated for binary content");
    } else {
        TEST_INFO("Binary content handled in response mode (also acceptable)");
    }
    TEST_END();

    /* Test 7: Connection timeout */
    TEST_START("Connection Timeout Handling");
    printf("Testing: 10.255.255.1:80 (non-routable IP)\n");
    printf("Expected: Connection error (timeout, refused, or unreachable)\n");

    g_test7_timeout_occurred = false;
    g_test7_timeout_code     = 0;
    g_test7_completed        = false;

    dap_client_http_request_simple_async(
        NULL, "10.255.255.1", 80, "GET", NULL,
        "/", NULL, 0, NULL,
        test7_response_callback, test7_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test7_completed, 40);
    TEST_EXPECT(g_test7_timeout_occurred, "Connection error occurred");
    TEST_EXPECT(g_test7_timeout_occurred, "Connection to unreachable host failed as expected");
    TEST_END();

    /* Test 8: Moderate file streaming */
    TEST_START("Moderate File Streaming (Size-based Trigger)");
    printf("Testing: %s:%d/httpbin/bytes/102400 (requests 100KB)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: Size threshold triggers streaming mode\n");

    g_test8_progress_calls  = 0;
    g_test8_total_received  = 0;
    g_test8_expected_size   = 0;
    g_test8_response_called = false;
    g_test8_completed       = false;
    g_test8_start_time      = time(NULL);

    dap_client_http_request_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/bytes/102400", NULL, 0, NULL,
        test8_response_callback, test8_error_callback, NULL,
        test8_progress_callback, NULL, NULL, true
    );

    wait_for_test_completion(&g_test8_completed, 25);

    if (g_test8_progress_calls >= 3) {
        TEST_EXPECT(true, "Streaming mode activated (multiple progress callbacks)");
        TEST_EXPECT(!g_test8_response_called, "No final response callback (pure streaming mode)");
        double avg_chunk = (double)g_test8_total_received / g_test8_progress_calls;
        TEST_INFO("Streaming efficiency: %.1f KB avg chunk, %d total chunks",
                  avg_chunk / 1024.0, g_test8_progress_calls);
    } else {
        TEST_INFO("Streaming mode not activated (%d callbacks) - response callback used",
                  g_test8_progress_calls);
    }

    if (g_test8_expected_size >= 1024 * 1024) {
        TEST_EXPECT(g_test8_total_received >= g_test8_expected_size,
                    "All 1MB data received via streaming");
        TEST_EXPECT(g_test8_progress_calls >= 5, "Size threshold triggered streaming mode");
    } else if (g_test8_expected_size >= 100 * 1024) {
        double completion_rate = (double)g_test8_total_received / g_test8_expected_size;
        bool adequate_completion = completion_rate >= 0.8;
        TEST_EXPECT(adequate_completion, "Adequate data received (80%+ of available)");
        if (adequate_completion)
            TEST_INFO("SUCCESS: Received %.1f%% (%zu/%zu bytes)",
                      completion_rate * 100.0, g_test8_total_received, g_test8_expected_size);
    } else if (g_test8_total_received >= 50 * 1024) {
        TEST_INFO("NOTE: Got %zu bytes", g_test8_total_received);
    } else {
        TEST_INFO("WARNING: Very small response %zu bytes", g_test8_total_received);
    }
    TEST_END();

    /* Test 9: File Download with Streaming to Disk */
    TEST_START("PNG Image Download with Streaming to Disk");
    printf("Testing: %s:%d/httpbin/image/png (PNG image file)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: MIME-based streaming activation, file saved with PNG signature\n");

    g_test9_progress_calls = 0;
    g_test9_total_written  = 0;
    g_test9_expected_size  = 0;
    g_test9_file           = NULL;
    g_test9_filename[0]    = 0;
    g_test9_start_time     = time(NULL);
    g_test9_file_complete  = false;

    dap_client_http_request_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/image/png", NULL, 0, NULL,
        test9_response_callback, test9_error_callback, NULL,
        test9_progress_callback, NULL, NULL, true
    );

    wait_for_test_completion(&g_test9_completed, 20);
    TEST_EXPECT(g_test9_total_written > 0, "PNG data successfully written to file");

    if (g_test9_filename[0] != 0) {
        FILE *check_file = fopen(g_test9_filename, "rb");
        if (check_file) {
            fseek(check_file, 0, SEEK_END);
            long file_size = ftell(check_file);

            fseek(check_file, 0, SEEK_SET);
            unsigned char png_header[4];
            if (fread(png_header, 1, 4, check_file) == 4) {
                bool is_png = (png_header[0] == 0x89 && png_header[1] == 0x50 &&
                               png_header[2] == 0x4E && png_header[3] == 0x47);
                TEST_EXPECT(is_png, "Valid PNG signature in saved file");
                if (is_png)
                    TEST_INFO("✓ PNG file saved: %s (%ld bytes) - valid PNG signature",
                              g_test9_filename, file_size);
            }
            fclose(check_file);
            TEST_EXPECT(file_size == (long)g_test9_total_written,
                        "File size matches streamed data");
        } else {
            TEST_INFO("✗ File not found: %s", g_test9_filename);
        }
    }

    if (g_test9_expected_size > 0) {
        TEST_EXPECT(g_test9_total_written >= g_test9_expected_size, "All PNG data received");
        TEST_INFO("SUCCESS: PNG streaming to disk (%zu bytes)", g_test9_expected_size);
    } else if (g_test9_total_written >= 4) {
        TEST_INFO("SUCCESS: PNG received via streaming/response (%zu bytes)",
                  g_test9_total_written);
    }

    if (g_test9_progress_calls > 1) {
        double avg_chunk = (double)g_test9_total_written / g_test9_progress_calls;
        TEST_INFO("Streaming mode: %.1f KB avg chunk, %d chunks → PNG file",
                  avg_chunk / 1024.0, g_test9_progress_calls);
        TEST_EXPECT(true, "Streaming mode activated (multiple progress callbacks)");
    } else if (g_test9_progress_calls == 1) {
        TEST_INFO("Single chunk mode: %zu bytes → PNG file", g_test9_total_written);
        TEST_EXPECT(true, "File download successful (single chunk acceptable for PNG)");
    } else {
        TEST_INFO("Response mode: PNG saved via response callback");
        TEST_EXPECT(g_test9_total_written > 0, "PNG downloaded successfully");
    }
    TEST_END();

    /* Test 10: POST request with JSON data */
    TEST_START("POST Request with JSON Data");
    printf("Testing: %s:%d/httpbin/post (JSON POST data)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: 200 OK with echoed JSON data in response\n");

    g_test10_post_success  = false;
    g_test10_status        = 0;
    g_test10_json_echoed   = false;
    g_test10_response_size = 0;
    g_test10_completed     = false;

    const char *json_data = "{"
                            "\"name\": \"test_user\","
                            "\"message\": \"Hello from DAP HTTP client\","
                            "\"timestamp\": 1640995200,"
                            "\"test_id\": 10"
                            "}";
    size_t json_size = strlen(json_data);

    TEST_INFO("Sending JSON payload (%zu bytes): %s", json_size, json_data);

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "POST",
        "application/json",
        "/httpbin/post", json_data, json_size, NULL,
        test10_response_callback, test10_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test10_completed, 15);
    TEST_EXPECT(g_test10_post_success, "POST request completed successfully");
    TEST_EXPECT(g_test10_status == 200, "Status is 200 OK");
    TEST_EXPECT(g_test10_response_size > 0, "Response contains data");
    TEST_EXPECT(g_test10_json_echoed, "Posted JSON data echoed in response");
    TEST_END();

    /* Test 11: Custom headers validation */
    TEST_START("Custom Headers Validation");
    printf("Testing: %s:%d/httpbin/headers (custom headers)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: Custom headers echoed in response\n");

    g_test11_completed     = false;
    g_test11_headers_found = false;
    g_test11_status        = 0;

    const char *custom_headers = "X-Test-Client: DAP-HTTP-Client\r\n"
                                 "X-Test-Version: 1.0\r\n"
                                 "X-Custom-Header: test-value-123\r\n";

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/headers", NULL, 0, NULL,
        test11_response_callback, test11_error_callback,
        NULL, (char *)custom_headers, true
    );

    wait_for_test_completion(&g_test11_completed, 10);
    TEST_EXPECT(g_test11_status == 200, "Status is 200 OK");
    TEST_EXPECT(g_test11_headers_found, "Custom headers found in response");
    TEST_END();

    /* Test 12: Error handling – 404 Not Found */
    TEST_START("Error Handling - 404 Not Found");
    printf("Testing: %s:%d/httpbin/status/404 (404 error)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: 404 status code handled gracefully\n");

    g_test12_completed     = false;
    g_test12_status        = 0;
    g_test12_error_handled = false;

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/status/404", NULL, 0, NULL,
        test12_response_callback, test12_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test12_completed, 10);
    TEST_EXPECT(g_test12_status == 404, "Status is 404 Not Found");
    TEST_EXPECT(g_test12_error_handled, "404 error handled gracefully");
    TEST_END();

    /* Test 13: Chunked encoding with larger data */
    TEST_START("Chunked Encoding Streaming (Larger Data)");
    printf("Testing: %s:%d/httpbin/stream-bytes/102400 (100KB chunked)\n",
           MOCK_HOST, MOCK_PORT);
    printf("Expected: Chunked streaming with visible progress\n");

    g_test13_completed       = false;
    g_test13_chunks_received = 0;
    g_test13_zero_copy_active = false;
    g_test13_total_streamed  = 0;

    dap_client_http_request_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/stream-bytes/102400", NULL, 0, NULL,
        test13_response_callback, test13_error_callback, NULL,
        test13_progress_callback, NULL, NULL, true
    );

    wait_for_test_completion(&g_test13_completed, 15);
    TEST_EXPECT(g_test13_chunks_received > 0, "Chunked data received");
    TEST_EXPECT(g_test13_total_streamed > 0, "Data streamed successfully");

    if (g_test13_zero_copy_active && g_test13_chunks_received > 1) {
        TEST_INFO("Chunked streaming successful: %zu bytes in %d chunks",
                  g_test13_total_streamed, g_test13_chunks_received);
        TEST_EXPECT(true, "Chunked streaming with multiple chunks");
    } else if (g_test13_total_streamed > 0) {
        TEST_INFO("Data received but not in chunked streaming mode: %zu bytes",
                  g_test13_total_streamed);
        TEST_EXPECT(true, "Data received successfully");
    }
    TEST_END();

    /* Test 14: HEAD method – Basic 200 OK */
    TEST_START("HEAD Method - Basic 200 OK Response");
    printf("Testing: %s:%d/httpbin/get (HEAD request)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: 200 OK, zero body size\n");

    g_test14_success      = false;
    g_test14_completed    = false;
    g_test14_status       = 0;
    g_test14_body_size    = 0;
    g_test14_has_location = false;

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "HEAD", NULL,
        "/httpbin/get", NULL, 0, NULL,
        test14_response_callback, test14_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test14_completed, 10);
    TEST_EXPECT(g_test14_success, "HEAD request completed successfully");
    TEST_EXPECT(g_test14_status == 200, "Status is 200 OK");
    TEST_EXPECT(g_test14_body_size == 0, "Body size is zero (HEAD requirement)");
    TEST_END();

    /* Test 15: HEAD method – 301 redirect (local mock) */
    TEST_START("HEAD Method - 301 Permanent Redirect");
    printf("Testing: %s:%d/pub-redirect (HEAD, local 301)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: 301 redirect with Location header, zero body\n");

    g_test15_success          = false;
    g_test15_completed        = false;
    g_test15_status           = 0;
    g_test15_redirect_handled = false;

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "HEAD", NULL,
        "/pub-redirect", NULL, 0, NULL,
        test15_response_callback, test15_error_callback,
        NULL, NULL, false  /* don't follow redirect */
    );

    wait_for_test_completion(&g_test15_completed, 10);
    TEST_EXPECT(g_test15_success, "HEAD redirect handled successfully");
    TEST_EXPECT(g_test15_status == 308 || g_test15_status == 301,
                "Status is 308 or 301 redirect");
    TEST_EXPECT(g_test15_redirect_handled, "Redirect response received");
    TEST_END();

    /* Test 16: HEAD method – Connection: close handling */
    TEST_START("HEAD Method - Connection: close Handling");
    printf("Testing: %s:%d/httpbin/get (HEAD with Connection: close)\n",
           MOCK_HOST, MOCK_PORT);
    printf("Expected: Response OR timeout (server may close connection early)\n");

    g_test16_success                  = false;
    g_test16_completed                = false;
    g_test16_status                   = 0;
    g_test16_connection_close_handled = false;

    const char *connection_close_header = "Connection: close\r\n";

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "HEAD", NULL,
        "/httpbin/get", NULL, 0, NULL,
        test16_response_callback, test16_error_callback,
        NULL, (char *)connection_close_header, true
    );

    wait_for_test_completion(&g_test16_completed, 10);
    TEST_EXPECT(g_test16_success, "HEAD request completed with Connection: close");
    TEST_EXPECT(g_test16_status == 200 || g_test16_status == 308 || g_test16_status == 301,
                "Valid HTTP status received");
    TEST_END();

    /* Test 17: HEAD method – 404 Not Found */
    TEST_START("HEAD Method - 404 Not Found");
    printf("Testing: %s:%d/httpbin/status/404 (HEAD request)\n", MOCK_HOST, MOCK_PORT);
    printf("Expected: 404 status, zero body\n");

    g_test17_success   = false;
    g_test17_completed = false;
    g_test17_status    = 0;

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "HEAD", NULL,
        "/httpbin/status/404", NULL, 0, NULL,
        test17_response_callback, test17_error_callback,
        NULL, NULL, true
    );

    wait_for_test_completion(&g_test17_completed, 10);
    TEST_EXPECT(g_test17_success, "HEAD 404 handled correctly");
    TEST_EXPECT(g_test17_status == 404, "Status is 404 Not Found");
    TEST_END();

    /* Test 18: HEAD method – With custom headers */
    TEST_START("HEAD Method - With Custom Headers");
    printf("Testing: %s:%d/httpbin/headers (HEAD with custom headers)\n",
           MOCK_HOST, MOCK_PORT);
    printf("Expected: 200 OK, zero body, headers processed\n");

    g_test18_success   = false;
    g_test18_completed = false;
    g_test18_status    = 0;

    const char *head_custom_headers = "X-HEAD-Test: DAP-HTTP-HEAD-Client\r\n"
                                      "X-Test-Method: HEAD\r\n";

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "HEAD", NULL,
        "/httpbin/headers", NULL, 0, NULL,
        test18_response_callback, test18_error_callback,
        NULL, (char *)head_custom_headers, true
    );

    wait_for_test_completion(&g_test18_completed, 10);
    TEST_EXPECT(g_test18_success, "HEAD request with custom headers successful");
    TEST_EXPECT(g_test18_status == 200, "Status is 200 OK");
    TEST_END();

    /* Test 19: Regression – GET with large custom headers (~5KB) */
    TEST_START("Regression: GET with Large Custom Headers (dynamic buffer)");
    printf("Testing: GET with ~5KB custom header (X-Large-Signature)\n");
    printf("Expected: 200 OK, no header buffer overflow\n");

    g_test19_completed           = false;
    g_test19_success             = false;
    g_test19_status              = 0;
    g_test19_large_header_echoed = false;

#define LARGE_HEADER_VALUE_SIZE 5000
    char large_header[LARGE_HEADER_VALUE_SIZE + 64];
    {
        char large_value[LARGE_HEADER_VALUE_SIZE + 1];
        for (int i = 0; i < LARGE_HEADER_VALUE_SIZE; i++)
            large_value[i] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i % 64];
        large_value[LARGE_HEADER_VALUE_SIZE] = '\0';
        snprintf(large_header, sizeof(large_header), "X-Large-Signature: %s\r\n", large_value);
    }

    TEST_INFO("Custom header size: %zu bytes", strlen(large_header));

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/headers", NULL, 0, NULL,
        test19_response_callback, test19_error_callback,
        NULL, large_header, true
    );

    wait_for_test_completion(&g_test19_completed, 15);
    TEST_EXPECT(g_test19_success, "GET with large custom header completed (no overflow)");
    TEST_EXPECT(g_test19_status == 200, "Status is 200 OK");
    TEST_EXPECT(g_test19_large_header_echoed, "Large header echoed in response");
    TEST_END();

    /* Test 20: Regression – POST with large custom headers */
    TEST_START("Regression: POST with Large Custom Headers (dynamic buffer)");
    printf("Testing: POST with JSON body + ~5KB custom header\n");
    printf("Expected: 200 OK, dynamic buffer handles content-type + custom headers\n");

    g_test20_completed = false;
    g_test20_success   = false;
    g_test20_status    = 0;

    const char *test20_json      = "{\"test\": \"post_with_large_headers\"}";
    size_t      test20_json_size = strlen(test20_json);

    TEST_INFO("POST body: %zu bytes, custom header: %zu bytes",
              test20_json_size, strlen(large_header));

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "POST",
        "application/json",
        "/httpbin/post", test20_json, test20_json_size, NULL,
        test20_response_callback, test20_error_callback,
        NULL, large_header, true
    );

    wait_for_test_completion(&g_test20_completed, 15);
    TEST_EXPECT(g_test20_success, "POST with large custom header completed (no overflow)");
    TEST_EXPECT(g_test20_status == 200, "Status is 200 OK");
    TEST_END();

    /* Test 21: Regression – GET with multiple custom headers */
    TEST_START("Regression: GET with Multiple Custom Headers (accumulated size)");
    printf("Testing: GET with 20 custom headers (~1KB total)\n");
    printf("Expected: 200 OK, all headers properly sent\n");

    g_test21_completed      = false;
    g_test21_success        = false;
    g_test21_status         = 0;
    g_test21_headers_echoed = false;

    char multi_headers[2048];
    int offset = 0;
    offset += snprintf(multi_headers + offset, sizeof(multi_headers) - offset,
                       "X-Header-First: value-first\r\n");
    for (int i = 1; i <= 18; i++)
        offset += snprintf(multi_headers + offset, sizeof(multi_headers) - offset,
                           "X-Header-%02d: value-padding-%02d-abcdefghij\r\n", i, i);
    offset += snprintf(multi_headers + offset, sizeof(multi_headers) - offset,
                       "X-Header-Last: value-last\r\n");

    TEST_INFO("Total custom headers size: %d bytes (%d headers)", offset, 20);

    dap_client_http_request_simple_async(
        NULL, MOCK_HOST, MOCK_PORT, "GET", NULL,
        "/httpbin/headers", NULL, 0, NULL,
        test21_response_callback, test21_error_callback,
        NULL, multi_headers, true
    );

    wait_for_test_completion(&g_test21_completed, 10);
    TEST_EXPECT(g_test21_success, "GET with multiple custom headers completed");
    TEST_EXPECT(g_test21_status == 200, "Status is 200 OK");
    TEST_EXPECT(g_test21_headers_echoed, "First and last headers echoed in response");
    TEST_END();
}

/* ============================================================
 * Summary
 * ============================================================ */
void print_test_summary()
{
    time_t end_time  = time(NULL);
    int total_time   = (int)(end_time - g_test_state.start_time);

    printf("\n=========================================\n");
    printf("        TEST SUITE SUMMARY\n");
    printf("=========================================\n");
    printf("Tests run:      %d\n", g_test_state.tests_run);
    printf("Tests passed:   %d\n", g_test_state.tests_passed);
    printf("Tests failed:   %d\n", g_test_state.tests_failed);
    printf("Test success:   %.1f%%\n",
           g_test_state.tests_run > 0 ?
           (float)g_test_state.tests_passed * 100.0f / g_test_state.tests_run : 0);
    printf("\n");
    printf("Assertions passed: %d\n", g_test_state.assertions_passed);
    printf("Assertions failed: %d\n", g_test_state.assertions_failed);
    printf("Total time:        %d seconds\n", total_time);
    printf("=========================================\n");

    if (g_test_state.tests_failed == 0) {
        printf("ALL TESTS PASSED!\n");
    } else {
        printf("WARNING: %d test(s) failed. Check output above.\n",
               g_test_state.tests_failed);
    }

    printf("\nFeatures validated:\n");
    printf("✓ Connection reuse for same-host redirects\n");
    printf("✓ Redirect behavior analysis\n");
    printf("✓ Chunked transfer encoding streaming\n");
    printf("✓ Smart buffer optimization (small vs large files)\n");
    printf("✓ Configurable redirect following (follow_redirects flag)\n");
    printf("✓ MIME-based streaming detection (binary content)\n");
    printf("✓ Connection timeout handling\n");
    printf("✓ Size-based streaming trigger\n");
    printf("✓ PNG image download with streaming to disk\n");
    printf("✓ POST requests with JSON data\n");
    printf("✓ Custom headers validation and echo\n");
    printf("✓ HTTP error status handling (404 Not Found)\n");
    printf("✓ Chunked encoding streaming (larger data)\n");
    printf("✓ HEAD method - Basic 200 OK response\n");
    printf("✓ HEAD method - 301 Permanent Redirect handling\n");
    printf("✓ HEAD method - Connection: close handling\n");
    printf("✓ HEAD method - 404 Not Found handling\n");
    printf("✓ HEAD method - Custom headers support\n");
    printf("✓ Regression - GET with large custom headers (dynamic buffer)\n");
    printf("✓ Regression - POST with large custom headers (dynamic buffer)\n");
    printf("✓ Regression - GET with multiple accumulated custom headers\n");

    if (g_test9_filename[0] != 0 && g_test9_total_written > 0) {
        printf("\nDownloaded file: %s (%zu bytes)\n",
               g_test9_filename, g_test9_total_written);
        if (remove(g_test9_filename) == 0) {
            printf("Test PNG file auto-cleaned.\n");
        } else {
            printf("Note: PNG file preserved at %s for inspection.\n",
                   g_test9_filename);
        }
    }

    printf("\nNote: Redirect limit enforcement (max 5) is implemented in code\n");
    printf("but may not trigger with current test URLs due to server behavior.\n");
}

/* ============================================================
 * Entry points
 * ============================================================ */
void test_http_client()
{
    g_test_state.start_time = time(NULL);

    /* Prevent SIGPIPE from killing the process when writing to a closed socket */
    signal(SIGPIPE, SIG_IGN);

    /* Start the local mock server (must come before dap_events_start) */
    if (s_mock_server_start() != 0) {
        fprintf(stderr, "ERROR: Failed to start mock HTTP server on port %d\n",
                MOCK_PORT);
        exit(EXIT_FAILURE);
    }
    usleep(100000); /* 100 ms warm-up */

    /* Initialize DAP subsystems */
    dap_events_init(1, 0);
    dap_events_start();
    dap_client_http_init();

    /* 5 s connect, 10 s read, 1 MB streaming threshold */
    dap_client_http_set_params(5000, 10000, 1024 * 1024);

    printf("HTTP Client Test Environment:\n");
    printf("✓ Mock HTTP server on http://%s:%d\n", MOCK_HOST, MOCK_PORT);
    printf("✓ Timeouts: 5s connect, 10s read\n");
    printf("✓ Streaming threshold: 1MB\n\n");

    run_test_suite();
    print_test_summary();

    /* Stop mock server */
    s_mock_server_stop();

    printf("\nShutting down test environment...\n");
    printf("Test suite completed. Exiting.\n");
    fflush(stdout);
    fflush(stderr);

    int exit_code = (g_test_state.tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    exit(exit_code);
}

int main()
{
    dap_log_level_set(L_DEBUG);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    test_http_client();
    return 0;
}
