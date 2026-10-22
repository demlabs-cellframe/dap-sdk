"""Production body-copy section: consumed-byte accounting before dispatch."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/dap_http_simple.c').read_text()
start = text.index('\nvoid s_http_client_data_read(')
end = text.index('    if( l_http_simple->request_size >=', start)
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#define log_it(...) ((void)0)
#define DAP_SOCK_SIGNAL_CLOSE 1
#define DAP_REALLOC realloc
#define dap_min(a,b) ((a)<(b)?(a):(b))
typedef unsigned char byte_t;
typedef struct {char *buf_in;size_t buf_in_size;int flags;} socket_t;
typedef struct {size_t request_size,request_size_max; union {void *request;byte_t *request_byte;};} dap_http_simple_t;
typedef struct {size_t in_content_length;socket_t *esocket;dap_http_simple_t simple;} dap_http_client_t;
#define DAP_HTTP_SIMPLE(c) (&(c)->simple)
static size_t dap_http_client_input_size(dap_http_client_t *c) {return c->esocket->buf_in_size;}
static const void *dap_http_client_input_data(dap_http_client_t *c) {return c->esocket->buf_in;}
static int dap_http_client_input_consume(dap_http_client_t *c,size_t n) {c->esocket->buf_in_size-=n;return 0;}
static void dap_events_socket_set_readable_unsafe(socket_t *s,bool b) {(void)s;(void)b;}
''' + text[start:end] + '\n}\n' + r'''
int main(void) {
    socket_t socket={.buf_in="abcNEXT",.buf_in_size=7};
    dap_http_client_t c={.in_content_length=3,.esocket=&socket};
    c.simple.request=calloc(1,4); c.simple.request_size_max=4;
    size_t consumed=SIZE_MAX;
    s_http_client_data_read(&c,&consumed);
    assert(consumed==3 && c.simple.request_size==3);
    assert(!memcmp(c.simple.request,"abc",3));
    assert(!memcmp(socket.buf_in+consumed,"NEXT",4));
    consumed=SIZE_MAX; s_http_client_data_read(&c,&consumed); assert(!consumed);
    c.simple.request_size=0; socket.buf_in_size=1;
    consumed=SIZE_MAX; s_http_client_data_read(&c,&consumed);
    assert(consumed==1 && c.simple.request_size==1);
    c.simple.request_size=4; consumed=SIZE_MAX;
    s_http_client_data_read(&c,&consumed);
    assert(!consumed && (socket.flags & DAP_SOCK_SIGNAL_CLOSE));
    free(c.simple.request);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: production body-copy section consumption and coalesced tail')
