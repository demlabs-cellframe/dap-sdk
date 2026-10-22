"""Production CTL handler: ENC identity/ACL propagation, invalid key rejection.

Session store and crypto allocation are stubs; not a cryptographic/payment test.
"""
import pathlib
import subprocess
import tempfile
sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk/'net/stream/stream/dap_stream_ctl_handler.c').read_text()
code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "dap_trans_request.h"
#define DAP_NEW_SIZE(t,n) ((t*)malloc(n))
#define DAP_DELETE free
#define log_it(...) ((void)0)
#define dap_strncpy strncpy
#define KEX_KEY_STR_SIZE 32
#define DAP_PROTOCOL_VERSION 26
#define DAP_ENC_KEY_TYPE_OAES 1
typedef int dap_enc_key_type_t;
typedef struct { uint64_t uint64; } node_t;
typedef struct { unsigned id; char active_channels[16]; void *key; void *acl; node_t node; } dap_stream_session_t;
typedef struct { void *acl_list; node_t node_addr; } dap_enc_ks_key_t;
static int creates, cipher, key_stub, acl_stub;
static size_t key_size;
static dap_stream_session_t session;
static dap_enc_ks_key_t identity={.acl_list=&acl_stub,.node_addr={12345}};
static dap_enc_ks_key_t *dap_enc_ks_find(const char *id) { return !strcmp(id,"bound-key")?&identity:NULL; }
static dap_stream_session_t *dap_stream_session_pure_new(void) { creates++; memset(&session,0,sizeof(session)); session.id=42; return &session; }
static void dap_random_string_fill(char *p,size_t n) { memset(p,'A',n); }
static void *dap_enc_key_new_generate(int type,const void *k,size_t n,const void *seed,size_t sn,size_t size) {
 (void)k;(void)n;(void)seed;(void)sn; cipher=type;key_size=size;return &key_stub;
}
int trans_reply_f(dap_trans_request_t *r,const char *fmt,...) {
 va_list ap;va_start(ap,fmt); char b[256];int n=vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);
 assert(n>0 && n<(int)sizeof(b));r->reply=malloc(n+1);memcpy(r->reply,b,n+1);r->reply_size=n;return n;
}
''' + source[source.index('/* Default encryption parameters'):]+r'''
int main(void) {
 dap_trans_request_t r={.url_path="channels=RS,enc_type=6,enc_key_size=32,enc_headers=0",.key_id="bound-key"};
 assert(!dap_stream_ctl_handler_process(&r));
 assert(r.status_code==200 && creates==1 && cipher==6 && key_size==32);
 assert(session.acl==identity.acl_list && session.node.uint64==12345);
 assert(!strcmp(session.active_channels,"RS"));free(r.reply);
 r=(dap_trans_request_t){.url_path="channels=RS,enc_type=6",.key_id="wrong-key"};
 assert(dap_stream_ctl_handler_process(&r)==-2 && r.status_code==400);
 assert(creates==1 && !r.reply);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=pathlib.Path(directory)
    (tmp/'test.c').write_text(code)
    subprocess.run(['cc','-fsanitize=address,undefined','-I'+str(sdk/'core/include'),
                    str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: CTL preserves ENC identity/ACL and rejects unknown KeyID before session allocation')
