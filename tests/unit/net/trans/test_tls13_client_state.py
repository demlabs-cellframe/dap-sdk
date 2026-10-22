"""Production dispatch: phase ordering, reentrant transitions and duplicates."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include "dap_tls13_client_state.h"
static unsigned enc,ctl,packets;
static int on_enc(void *arg,const void *data,size_t size) {
 dap_tls13_client_state_t *s=arg;
 assert(s->phase==DAP_TLS13_ENC_DONE && !s->enc_reply);
 assert(size==3 && !memcmp(data,"sig",3)); enc++;
 s->phase=DAP_TLS13_WAIT_CTL; return 0;
}
static int on_ctl(void *arg,const void *data,size_t size) {
 dap_tls13_client_state_t *s=arg; (void)data;(void)size;
 assert(s->phase==DAP_TLS13_STREAMING && !s->ctl_reply); ctl++; return 0;
}
static int on_data(void *arg,const void *data,size_t size) {
 (void)arg;(void)data;(void)size; packets++; return 0;
}
int main(void) {
 dap_tls13_client_state_t s={.phase=DAP_TLS13_WAIT_ENC,.enc_reply=on_enc,
   .ctl_reply=on_ctl,.stream_data=on_data}; s.arg=&s;
 assert(!dap_tls13_client_dispatch(&s,DAP_TLS13_ENC_REPLY,"sig",3));
 assert(s.phase==DAP_TLS13_WAIT_CTL && enc==1);
 assert(!dap_tls13_client_dispatch(&s,DAP_TLS13_CTL_REPLY,"key",3));
 assert(!dap_tls13_client_dispatch(&s,DAP_TLS13_STREAM,"pkt",3));
 assert(ctl==1 && packets==1);
 assert(dap_tls13_client_dispatch(&s,DAP_TLS13_CTL_REPLY,"key",3)==-1);
 assert(s.phase==DAP_TLS13_CLOSED && ctl==1);
 assert(dap_tls13_client_dispatch(&s,DAP_TLS13_STREAM,"pkt",3)==-1 && packets==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    (tmp/'test.c').write_text(code)
    subprocess.run(['cc','-fsanitize=address,undefined','-I'+str(sdk/'net/trans/tls'),
                    str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: TLS13 client phases, exact bytes, reentrant callback and duplicate rejection')
