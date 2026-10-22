"""Production WS continuation checks socket lifetime and protocol ownership."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/trans/websocket/dap_net_trans_websocket_server.c').read_text()
start = text.index('static void s_ws_resume_buffered(')
code = r'''
#include <assert.h>
#include <stdlib.h>
#define DAP_SOCK_SIGNAL_CLOSE 1
typedef unsigned long dap_events_socket_uuid_t;
typedef struct socket {int flags;size_t buf_in_size;struct {void (*read_callback)(struct socket *,void *);} callbacks;} dap_events_socket_t;
typedef struct {void *context;} dap_worker_t;
static dap_worker_t worker;
static dap_events_socket_t *live;
static unsigned reads;
static dap_worker_t *dap_worker_get_current(void) {return &worker;}
static dap_events_socket_t *dap_context_find(void *ctx,dap_events_socket_uuid_t uuid) {(void)ctx;assert(uuid==42);return live;}
static void s_ws_server_esocket_read(dap_events_socket_t *s,void *arg) {(void)s;(void)arg;++reads;}
''' + text[start:text.index('\nstatic int s_switch_to_websocket_protocol(',start)] + r'''
static void resume(void) {dap_events_socket_uuid_t *p=malloc(sizeof(*p));assert(p);*p=42;s_ws_resume_buffered(p);}
int main(void) {
 dap_events_socket_t socket={.buf_in_size=10,.callbacks={.read_callback=s_ws_server_esocket_read}};
 live=&socket;resume();assert(reads==1);
 live=NULL;resume();assert(reads==1);
 live=&socket;socket.flags=DAP_SOCK_SIGNAL_CLOSE;resume();assert(reads==1);
 socket.flags=0;socket.callbacks.read_callback=NULL;resume();assert(reads==1);
 socket.callbacks.read_callback=s_ws_server_esocket_read;socket.buf_in_size=0;resume();assert(reads==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: WS continuation ownership, disconnect and buffered tail')
