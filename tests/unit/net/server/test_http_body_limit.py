"""Execute production headers callback: reject before allocation, preserve NONE."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/dap_http_simple.c').read_text()
start = text.index('static void s_http_client_headers_read( dap_http_client_t *a_http_client,')
end = text.index('\nvoid s_http_client_data_read(', start)
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#define UNUSED_ARG
#define L_ERROR 1
#define log_it(...) ((void)0)
#define Http_Status_ServiceUnavailable 503
#define DAP_HTTP_CLIENT_STATE_NONE 0
typedef struct {size_t request_size_max;} route;
typedef struct {void *request; size_t request_size_max;} dap_http_simple_t;
typedef struct {
    size_t in_content_length;
    int reply_status_code, keep_alive, out_connection_close, state_read;
    void *esocket;
    route *proc;
    dap_http_simple_t simple;
} dap_http_client_t;
#define DAP_HTTP_SIMPLE(c) (&(c)->simple)
#define DAP_HTTP_SIMPLE_URL_PROC(p) (p)
static int allocations, writes, readable, fail_alloc;
static void *allocate(size_t n) {++allocations; return fail_alloc?NULL:calloc(1,n);}
#define DAP_NEW_Z_SIZE(t,n) allocate(n)
static void dap_events_socket_set_readable_unsafe(void *p, bool value) {(void)p; readable=value;}
static void dap_http_client_write(dap_http_client_t *c) {(void)c; ++writes;}
static void *s_http_proc_pool;
static int s_proc_pool_task, s_proc_pool_complete;
static void s_write_response_busy(dap_http_simple_t *p) {(void)p;}
static void s_write_data_to_socket(dap_http_simple_t *p) {(void)p;}
static int dap_thread_pool_submit(void *p, int a, void *b, int d, void *e)
{(void)p;(void)a;(void)b;(void)d;(void)e; return 0;}
''' + text[start:end] + r'''
static dap_http_client_t run(route *r, size_t length) {
    dap_http_client_t c={.in_content_length=length,.keep_alive=1,.state_read=1,.proc=r};
    allocations=writes=0; readable=1;
    s_http_client_headers_read(&c,NULL); return c;
}
int main(void) {
    route r={.request_size_max=8};
    dap_http_client_t c=run(&r,9);
    assert(!allocations && writes==1 && !readable && c.reply_status_code==413);
    assert(!c.state_read && !c.keep_alive && c.out_connection_close);
    c=run(&r,8); assert(allocations==1 && !writes && c.simple.request_size_max==9);
    free(c.simple.request);
    fail_alloc=1; c=run(&r,8);
    assert(writes==1 && !readable && !c.state_read && c.reply_status_code==503);
    r.request_size_max=SIZE_MAX; c=run(&r,SIZE_MAX);
    assert(!allocations && c.reply_status_code==413);
    r.request_size_max=0; c=run(&r,1); assert(!allocations && c.reply_status_code==413);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: production HTTP body admission and allocation failure')
