"""Compile the production request-line parser with exact-size input buffers."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/http_client/dap_http_client.c').read_text()
start = text.index('static int s_http_start_line_parse(')
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include "dap_http_request_policy.h"
typedef struct {
    char action[32], url_path[1024], in_query_string[1024];
    uint32_t action_len, url_path_len, in_query_string_len;
    bool http_version_11;
} dap_http_client_t;
''' + text[start:text.index('\n/**', start)] + r'''
static int parse(dap_http_client_t *c, const char *s, size_t n) {
    char *p=malloc(n?n:1); memcpy(p,s,n);
    int rc=s_http_start_line_parse(c,p,n); free(p); return rc;
}
int main(void) {
    dap_http_client_t c={0};
    const char *good="POST /enc_init/x?q=one?two HTTP/1.1\r\n";
    for(size_t i=0;i<strlen(good);++i) assert(parse(&c,good,i)<0);
    assert(parse(&c,good,strlen(good))==0);
    assert(!strcmp(c.action,"POST") && !strcmp(c.url_path,"/enc_init/x"));
    assert(!strcmp(c.in_query_string,"q=one?two"));
    good="GET / HTTP/1.0\r\n";
    assert(parse(&c,good,strlen(good))==0 && !c.in_query_string_len && !c.in_query_string[0]);
    const char *bad[]={"GET / HTTP/1.\r\n", "GET / HTTP/1.9\r\n",
        "GET / HTTP/1.1 extra\r\n", " GET / HTTP/1.1\r\n",
        "GET  / HTTP/1.1\r\n", "GET /\t HTTP/1.1\r\n", "GET /#x HTTP/1.1\r\n",
        "GET http://example/a HTTP/1.1\r\n", "GET / HTTP/1.1\n", "\r\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        dap_http_client_t before=c;
        assert(parse(&c,bad[i],strlen(bad[i]))<0);
        assert(!memcmp(&c,&before,sizeof(c)));
    }
    const char embedded[]="GET /a\0b HTTP/1.1\r\n";
    assert(parse(&c,embedded,sizeof(embedded)-1)<0);
    char large[1100]; memset(large,'a',sizeof(large));
    memcpy(large,"GET /",5); memcpy(large+sizeof(large)-11," HTTP/1.1\r\n",11);
    assert(parse(&c,large,sizeof(large))<0);
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
print('PASS: production HTTP request-line bounds and routing fields')
