"""Production WS status line: bounded input, valid reason phrase, no NUL comparison."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include <stdlib.h>
#include "dap_ws_upgrade_status.h"
static int check(const char *s,size_t n) {
    void *p=malloc(n?n:1);memcpy(p,s,n);
    int rc=dap_ws_upgrade_status_valid(p,n);free(p);return rc;
}
int main(void) {
    const char *s="HTTP/1.1 101 Switching Protocols\r\n";
    for(size_t n=0;n<strlen(s);++n) assert(!check(s,n));
    assert(check(s,strlen(s)));
    s="HTTP/1.1 101 \r\nUpgrade: websocket\r\n\r\n";assert(check(s,strlen(s)));
    const char *bad[]={"HTTP/1.0 101 Switching Protocols\r\n",
        "HTTP/1.1 1010 Wrong\r\n","HTTP/1.1 200 OK\r\n",
        "HTTP/1.1 101\r\n","HTTP/1.1 101 Bad\n","HTTP/1.1 101 Bad\rX"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) assert(!check(bad[i],strlen(bad[i])));
    const char nul[]="HTTP/1.1 101 \0hidden\r\n";assert(!check(nul,sizeof(nul)-1));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(sdk / 'net/trans/websocket'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: production WS Upgrade status line')
