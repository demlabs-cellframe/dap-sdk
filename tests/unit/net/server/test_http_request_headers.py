"""Production common/server header parsers with minimal header storage stubs."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
common = (sdk / 'net/common/http/src/dap_http_header.c').read_text()
server = (sdk / 'net/server/http_server/http_client/dap_http_header_server.c').read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "dap_http_request_policy.h"
#define debug_if(...) ((void)0)
#define dap_min(a,b) ((a)<(b)?(a):(b))
#define DAP_HTTP$SZ_FIELD_NAME 256
#define DAP_HTTP$SZ_FIELD_VALUE 1024
enum { HTTP_FLD$K_CONNECTION, HTTP_FLD$K_CONTENT_TYPE, HTTP_FLD$K_CONTENT_LEN,
       HTTP_FLD$K_COOKIE, HTTP_FLD$K_EOL };
typedef struct header { char name[256]; struct header *next; } dap_http_header_t;
typedef struct {
    dap_http_header_t *in_headers;
    size_t in_content_length;
    bool keep_alive;
    bool in_connection_close;
    size_t in_header_bytes, in_header_count;
    char in_content_type[256], in_cookie[1024];
} dap_http_client_t;
static dap_http_header_t *dap_http_header_find(dap_http_header_t *h, const char *name) {
    for (; h; h=h->next) if (!strcasecmp(h->name, name)) return h;
    return NULL;
}
static dap_http_header_t *dap_http_header_add(dap_http_header_t **h, const char *name, const char *value) {
    (void)value;
    dap_http_header_t *p=calloc(1,sizeof(*p)); assert(p);
    strcpy(p->name,name); p->next=*h; *h=p; return p;
}
static void clear(dap_http_client_t *c) {
    while(c->in_headers) { dap_http_header_t *h=c->in_headers; c->in_headers=h->next; free(h); }
    memset(c,0,sizeof(*c));
}
'''
code += server[server.index('#define $STRINI'):server.index('#undef  $STRINI')] + '\n'
code += common[common.index('int dap_http_header_parse_line('):common.index('\n/**', common.index('int dap_http_header_parse_line('))]
code += server[server.index('int dap_http_header_server_parse('):server.index('\n/**', server.index('int dap_http_header_server_parse('))]
code += r'''
static int parse(dap_http_client_t *c, const char *s) {
    size_t n=strlen(s); char *p=malloc(n); memcpy(p,s,n);
    int rc=dap_http_header_server_parse(c,p,n); free(p); return rc;
}
int main(void) {
    dap_http_client_t c={0};
    assert(parse(&c,"cOnTeNt-LeNgTh: 42\r\n")==0 && c.in_content_length==42);
    assert(parse(&c,"Content-Length: 42\r\n")==-1);
    assert(c.in_content_length==42); clear(&c);
    const char *bad[]={"Content-Length: -1\r\n", "Content-Length: +1\r\n",
        "Content-Length: 1x\r\n", "Content-Length: \r\n", "Content-Length: 1, 1\r\n",
        "Content-Length: 999999999999999999999999999999\r\n",
        "transfer-encoding: chunked\r\n", "Transfer-Encoding: identity\r\n",
        "Content-Length : 1\r\n", " Content-Length: 1\r\n",
        "X: a\rb\r\n", "X: a\nb\r\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        assert(parse(&c,bad[i])==-1); assert(!c.in_headers); clear(&c);
    }
    assert(parse(&c,"Content-Length: 0\r\n")==0);
    assert(parse(&c,"Transfer-Encoding: chunked\r\n")==-1); clear(&c);
    char maximum[128]; snprintf(maximum,sizeof(maximum),"Content-Length: %zu\r\n",SIZE_MAX);
    assert(parse(&c,maximum)==-1); clear(&c);
    c.keep_alive=true;
    assert(parse(&c,"Connection: Upgrade\r\n")==0 && c.keep_alive);
    assert(parse(&c,"Connection: keep-alive, CLOSE\r\n")==0 && !c.keep_alive);
    assert(parse(&c,"Connection: Keep-Alive\r\n")==0 && !c.keep_alive); clear(&c);
    assert(parse(&c,"Connection: upgrade, keep-alive\r\n")==0 && c.keep_alive); clear(&c);
    assert(parse(&c,"Connection: keep-alive garbage\r\n")==-1); clear(&c);
    assert(parse(&c,"Connection: xclose\r\n")==0 && !c.in_connection_close); clear(&c);
    for(size_t i=0;i<DAP_HTTP_REQUEST_HEADER_COUNT_MAX;++i)
        assert(parse(&c,"X: a\r\n")==0);
    assert(parse(&c,"\r\n")==1);
    assert(parse(&c,"X: a\r\n")==-1); clear(&c);
    char large[1000]; memset(large,'a',sizeof(large));
    memcpy(large,"X: ",3); memcpy(large+sizeof(large)-3,"\r\n",3);
    for(size_t i=0;i<32;++i) assert(parse(&c,large)==0);
    assert(parse(&c,large)==-1); clear(&c);
    c.in_header_bytes=DAP_HTTP_REQUEST_HEADER_BYTES_MAX-2;
    assert(parse(&c,"\r\n")==1); clear(&c);
    c.in_header_bytes=DAP_HTTP_REQUEST_HEADER_BYTES_MAX-1;
    assert(parse(&c,"\r\n")==-1); clear(&c);
    assert(parse(&c,"content-type: application/octet-stream\r\n")==0);
    assert(!strcmp(c.in_content_type,"application/octet-stream")); clear(&c);
    char name[8],value[8];
    assert(dap_http_header_parse_line("X: abc",6,name,0,value,sizeof(value))==-1);
    assert(dap_http_header_parse_line("LongName: x",11,name,sizeof(name),value,sizeof(value))==-1);
    assert(dap_http_header_parse_line("X: 12345678",11,name,sizeof(name),value,sizeof(value))==-1);
    assert(dap_http_header_parse_line("X: a\0b",6,name,sizeof(name),value,sizeof(value))==-1);
    const char *ows="X: \t a \t\r\n";
    assert(dap_http_header_parse_line(ows,strlen(ows),name,sizeof(name),value,sizeof(value))==0);
    assert(!strcmp(value,"a"));
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
print('PASS: production HTTP request headers, lengths, case and malformed input')
