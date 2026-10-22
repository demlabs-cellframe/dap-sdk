"""Production pending-header drain: exact bytes across short/blocked writes."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/http_client/dap_http_client.c').read_text()
start = text.index('static int s_http_flush_headers(')
code = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dap_http_response_buffer.h"
typedef struct {char *pending_headers; size_t pending_headers_size,pending_headers_offset;} dap_http_client_t;
static ssize_t allowance;
static char wire[128]; static size_t written;
static ssize_t dap_http_client_write_body(dap_http_client_t *c,const void *p,size_t n) {
    (void)c;
    if(allowance<=0) return allowance;
    if(n>(size_t)allowance) n=(size_t)allowance;
    memcpy(wire+written,p,n); written+=n; return (ssize_t)n;
}
''' + text[start:text.index('\nvoid dap_http_client_write(', start)] + r'''
int main(void) {
    char tiny[4]; size_t used=0;
    assert(dap_http_response_append(tiny,sizeof(tiny),&used,"%s","abc")==0 && used==3);
    assert(dap_http_response_append(tiny,sizeof(tiny),&used,"x")==-1 && used==3);
    dap_http_client_t c={.pending_headers=malloc(128)};
    assert(c.pending_headers);
    assert(!dap_http_response_append(c.pending_headers,128,&c.pending_headers_size,
        "HTTP/1.1 %u %s\r\n",200U,"OK"));
    assert(!dap_http_response_append(c.pending_headers,128,&c.pending_headers_size,
        "Content-Length: 0\r\n\r\n"));
    size_t total=c.pending_headers_size;
    allowance=3; assert(s_http_flush_headers(&c)==0 && c.pending_headers_offset==3);
    allowance=0; assert(s_http_flush_headers(&c)==0 && c.pending_headers_offset==3);
    allowance=-1; assert(s_http_flush_headers(&c)==-1 && c.pending_headers_offset==3);
    allowance=2; while(c.pending_headers) assert(s_http_flush_headers(&c)>=0);
    assert(written==total && !c.pending_headers_offset && !c.pending_headers_size);
    assert(!memcmp(wire,"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",total));
    assert(s_http_flush_headers(&c)==1 && written==total);
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
print('PASS: bounded response serialization and production header drain')
