"""Production input accessors isolate plaintext from socket ciphertext."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/http_client/dap_http_client.c').read_text()
start = text.index('const unsigned char *dap_http_client_input_data(')
code = r'''
#include <assert.h>
#include <stdbool.h>
#include "dap_http_input.h"
typedef struct {unsigned char *buf_in;size_t buf_in_size;} socket_t;
typedef struct {bool use_plaintext_input;dap_http_input_t plaintext_input;socket_t *esocket;} dap_http_client_t;
static void dap_events_socket_shrink_buf_in(socket_t *s,size_t n) {
    s->buf_in_size-=n;memmove(s->buf_in,s->buf_in+n,s->buf_in_size);
}
''' + text[start:text.index('\nstatic char  *z_basename',start)] + r'''
int main(void) {
    unsigned char wire[]="ciphertext";
    socket_t socket={wire,10};dap_http_client_t c={.esocket=&socket};
    assert(dap_http_client_input_size(&c)==10);
    assert(!dap_http_input_init(&c.plaintext_input,64));
    assert(!dap_http_input_append(&c.plaintext_input,"GET /",5));
    c.use_plaintext_input=true;
    assert(dap_http_client_input_size(&c)==5);
    assert(!memcmp(dap_http_client_input_data(&c),"GET /",5));
    assert(!dap_http_client_input_consume(&c,4));
    assert(dap_http_client_input_size(&c)==1);
    assert(socket.buf_in_size==10 && !memcmp(wire,"ciphertext",10));
    assert(dap_http_client_input_consume(&c,2)==-1);
    c.use_plaintext_input=false;
    assert(!dap_http_client_input_consume(&c,6));
    assert(socket.buf_in_size==4 && !memcmp(wire,"text",4));
    assert(c.plaintext_input.size==1 && *c.plaintext_input.data=='/');
    dap_http_input_clear(&c.plaintext_input);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(sdk / 'net/server/http_server/http_client/include'),
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: production HTTP input accessors preserve ciphertext/plaintext isolation')
