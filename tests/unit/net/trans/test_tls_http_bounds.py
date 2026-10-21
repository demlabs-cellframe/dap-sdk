"""Production TLS control parser: truncation, malformed lengths, non-NUL input."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk / 'net/trans/tls/dap_net_trans_tls_server.c').read_text()
start = source.index('static int s_parse_http_post(')
end = source.index('\n/* ------------------------------------------------------------------ */', start)
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define DAP_NEW_SIZE(t,n) ((t*)malloc(n))
''' + source[start:end] + r'''
static int parse(const char *s, size_t n) {
 char *input=malloc(n?n:1); memcpy(input,s,n);
 char *url=NULL,*query=NULL,*body=NULL; size_t size=0;
 int rc=s_parse_http_post(input,n,&url,&query,&body,&size);
 free(input); free(url); free(query); free(body); return rc;
}
int main(void) {
 const char *s="POST /enc_init?a=b HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc";
 for(size_t i=0;i<strlen(s);i++) assert(parse(s,i)==1);
 assert(parse(s,strlen(s))==0);
 const char *bad[]={
 "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
 "POST / HTTP/1.1\r\nContent-Length: 9999999999999999999999\r\n\r\n",
 "POST / HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
 "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
 "POST / HTTP/1.1\r\nContent-Length: 0x10\r\n\r\n"};
 for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(parse(bad[i],strlen(bad[i]))==-1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: TLS HTTP parser prefix boundaries and malformed lengths')
