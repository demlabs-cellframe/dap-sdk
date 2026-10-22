"""Execute production rejection callback: handled, terminal HTTP error."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/trans/websocket/dap_net_trans_websocket_server.c').read_text()
start = text.index('static int s_websocket_reject_upgrade(')
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define DAP_HTTP_CLIENT_STATE_NONE 0
typedef struct {uint16_t reply_status_code;char reply_reason_phrase[64];bool keep_alive,out_connection_close;int state_read;void *esocket;} dap_http_client_t;
static int readable=1,writes;
static void dap_events_socket_set_readable_unsafe(void *s,bool value) {(void)s;readable=value;}
static void dap_http_client_write(dap_http_client_t *c) {
 assert(c->state_read==0 && !c->keep_alive && c->out_connection_close);++writes;
}
''' + text[start:text.index('\nint dap_net_trans_websocket_try_upgrade', start)] + r'''
int main(void) {
 uint16_t statuses[]={400,426,500,503};
 for(unsigned i=0;i<4;i++) {
   dap_http_client_t c={.keep_alive=true,.state_read=1,.reply_reason_phrase="old"};
   assert(s_websocket_reject_upgrade(&c,statuses[i])==0);
   assert(c.reply_status_code==statuses[i] && !readable && !c.reply_reason_phrase[0]);
 }
 assert(writes==4);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: production Upgrade rejection response state')
