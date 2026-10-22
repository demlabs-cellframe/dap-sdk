"""Server control order and failed reply ownership, using production dispatch."""
import pathlib
import subprocess
import tempfile
sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include "dap_tls13_server_dispatch.h"
static unsigned calls, replies, packets;
static int reject;
static int control(void *arg,const void *p,size_t n,void **out,size_t *size) {
 (void)arg; calls++; *out=malloc(n); assert(*out); memcpy(*out,p,n); *size=n; return 0;
}
static int send_reply(void *arg,uint8_t type,const void *p,size_t n) {
 (void)arg; assert(type==DAP_TLS13_ENC_REPLY || type==DAP_TLS13_CTL_REPLY);
 assert(n==3 && !memcmp(p,"sig",3)); replies++; return reject;
}
static int stream(void *arg,const void *p,size_t n) {
 (void)arg;(void)p;(void)n; packets++;return 0;
}
int main(void) {
 dap_tls13_server_dispatch_t s={.enc=control,.ctl=control,.send=send_reply,.data=stream};
 assert(!dap_tls13_server_dispatch_frame(&s,DAP_TLS13_ENC_REQUEST,"sig",3));
 assert(s.phase==DAP_TLS13_SERVER_CTL);
 assert(!dap_tls13_server_dispatch_frame(&s,DAP_TLS13_CTL_REQUEST,"sig",3));
 assert(s.phase==DAP_TLS13_SERVER_STREAM);
 assert(!dap_tls13_server_dispatch_frame(&s,DAP_TLS13_STREAM,"pkt",3));
 assert(calls==2 && replies==2 && packets==1);
 assert(dap_tls13_server_dispatch_frame(&s,DAP_TLS13_CTL_REQUEST,"sig",3)==-1);
 assert(calls==2);
 s=(dap_tls13_server_dispatch_t){.enc=control,.ctl=control,.send=send_reply,.data=stream};
 reject=1;
 assert(dap_tls13_server_dispatch_frame(&s,DAP_TLS13_ENC_REQUEST,"sig",3)==-1);
 assert(s.phase==DAP_TLS13_SERVER_CLOSED);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=pathlib.Path(directory)
    (tmp/'test.c').write_text(code)
    subprocess.run(['cc','-fsanitize=address,undefined','-I'+str(sdk/'net/trans/tls'),
                    str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: server control order, exact reply bytes, failed enqueue and duplicates')
