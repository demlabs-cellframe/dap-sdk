"""Production completion trampoline must use HTTP context, not TLS socket arg."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/http_client/dap_http_client.c').read_text()
start = text.index('void dap_http_client_write_finished(')
code = r'''
#include <assert.h>
#include <stddef.h>
typedef struct socket {void *owner;struct {void (*read_callback)(struct socket *,void *);} callbacks;} dap_events_socket_t;
static void dap_http_client_read(dap_events_socket_t *s,void *arg) {(void)s;(void)arg;}
typedef struct {void *handler_arg; void (*handler_write_finished)(dap_events_socket_t *,void *);} dap_http_client_t;
#define DAP_HTTP_CLIENT(s) ((dap_http_client_t *)(s)->owner)
static int http_context,tls_context,calls;
static void completed(dap_events_socket_t *s,void *arg) {
    assert(arg==&http_context); calls++; s->owner=NULL;
}
''' + text[start:text.index('\nbool dap_http_client_write_callback',start)] + r'''
int main(void) {
    dap_http_client_t client={.handler_arg=&http_context,.handler_write_finished=completed};
    dap_events_socket_t socket={.owner=&client,.callbacks={.read_callback=dap_http_client_read}};
    dap_http_client_write_finished(&socket,&tls_context);
    assert(calls==1 && !socket.owner);
    dap_http_client_write_finished(&socket,&tls_context);
    dap_http_client_write_finished(NULL,&tls_context);
    assert(calls==1);
    socket.owner=(void *)3; /* Non-HTTP owner must never be dereferenced. */
    socket.callbacks.read_callback=NULL;
    dap_http_client_write_finished(&socket,NULL);
    assert(calls==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: HTTP completion context isolated from transport callback arg')
