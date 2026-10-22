"""Production CTL builder: exact wire request and short-write rejection."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk / 'net/trans/tls/dap_stream_trans_tls.c').read_text()
start = source.index('static int s_tls_send_session_request(')
end = source.index('\nstatic int s_tls_session_create(', start)
code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
typedef struct { void *trans_ctx; } dap_stream_t;
typedef struct { const char *channels; int enc_type; size_t enc_key_size; bool enc_headers; } dap_net_session_params_t;
#define DAP_CLIENT_PROTOCOL_VERSION 26
#define DAP_UPLINK_PATH_STREAM_CTL "stream_ctl"
#define DAP_DELETE free
#define log_it(...) ((void)0)
#define debug_if(...) ((void)0)
static char *dap_strdup_printf(const char *fmt,...) {
 va_list ap; va_start(ap,fmt); char *p=NULL; int rc=vasprintf(&p,fmt,ap); va_end(ap); return rc<0?NULL:p;
}
static int short_write;
static char wire[4096];
static ssize_t dap_stream_trans_write_unsafe(dap_stream_t *s,const void *data,size_t size) {
 (void)s; assert(size<sizeof(wire)); memcpy(wire,data,size); wire[size]=0;
 return short_write ? (ssize_t)size-1 : (ssize_t)size;
}
''' + source[start:end] + r'''
int main(void) {
 dap_stream_t s={.trans_ctx=(void*)1};
 dap_net_session_params_t p={"RS",6,32,false};
 assert(s_tls_send_session_request(&s,&p)==0);
 assert(!strcmp(wire,"POST /stream_ctl/channels=RS,enc_type=6,enc_key_size=32,enc_headers=0 HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/text\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\n26"));
 short_write=1; assert(s_tls_send_session_request(&s,&p)==-3);
 char large[2048]; memset(large,'S',sizeof(large)-1);large[sizeof(large)-1]=0;
 p.channels=large; assert(s_tls_send_session_request(&s,&p)==-3);
 p.channels=NULL; assert(s_tls_send_session_request(&s,&p)==-1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    (tmp/'test.c').write_text(code)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: CTL wire bytes, short writes, oversized URL, invalid parameters')
