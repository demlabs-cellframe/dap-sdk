"""Production ENC_INIT dispatch with system json-c (pkg-config required)."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk / 'net/trans/tls/dap_stream_trans_tls.c').read_text()
start = source.index('    case TLS_PHASE_ENC_INIT_WAIT: {')
end = source.index('    case TLS_PHASE_STREAM_CTL_WAIT:', start)
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <json.h>
#define DAP_SOCK_SIGNAL_CLOSE 1
#define TLS_PHASE_ENC_INIT_WAIT 0
#define log_it(...) ((void)0)
typedef void (*dap_net_trans_handshake_cb_t)(void*,const void*,size_t,int);
typedef struct { char *enc_reply; size_t enc_reply_size; dap_net_trans_handshake_cb_t handshake_cb; } context;
typedef struct { unsigned flags; } socket_t;
static void feed(context *l_ctx, socket_t *a_es, const void *l_unwrapped, size_t l_unwrapped_size) {
 void *l_stream=NULL;
 switch(0) {
''' + source[start:end] + r'''
 }
}
static unsigned calls;
static const char message[]="{\"encrypt_msg\":\"abcdef\",\"node_sign\":\"signed bytes\"}";
static void result(void *s,const void *data,size_t size,int error) {
 (void)s; assert(!error && size==strlen(message));
 assert(!memcmp(data,message,size)); calls++;
}
int main(void) {
 for(size_t split=1;split<strlen(message);split++) {
   context c={.handshake_cb=result}; socket_t es={0}; calls=0;
   feed(&c,&es,message,split); assert(!calls && !es.flags);
   feed(&c,&es,message+split,strlen(message)-split);
   assert(calls==1 && !es.flags && !c.enc_reply && !c.handshake_cb);
 }
 context c={.handshake_cb=result}; socket_t es={0}; calls=0;
 feed(&c,&es,"[]",2); assert(es.flags && !calls); free(c.enc_reply);
 c=(context){.handshake_cb=result}; es=(socket_t){0};
 feed(&c,&es,"{}garbage",9); assert(es.flags && !calls); free(c.enc_reply);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    c.write_text(code)
    # The transport branch is production code; json-c is supplied by the host.
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'json-c'], text=True).split()
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(c), '-o', str(exe), *flags], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: every ENC_INIT JSON split, exact callback bytes, malformed replies')
