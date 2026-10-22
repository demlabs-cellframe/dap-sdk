"""Plaintext input ownership, budget, incomplete line and coalesced tail."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include <stdlib.h>
static int fail_allocation;
static void *test_realloc(void *p, size_t n) {return fail_allocation?NULL:realloc(p,n);}
#define realloc test_realloc
#include "dap_http_input.h"
#undef realloc
#include "dap_http_line.h"
int main(void) {
    dap_http_input_t input={0};
    assert(!dap_http_input_init(&input,8192));
    const char *line="GET / HTTP/1.1\r\n";
    for(size_t i=0;i<strlen(line);++i) {
        assert(!dap_http_input_append(&input,line+i,1));
        size_t used=0;
        assert(dap_http_line_read(input.data,input.size,4096,&used)==
               (i+1==strlen(line)?1:0));
    }
    assert(!dap_http_input_append(&input,"NEXT",4));
    assert(!dap_http_input_consume(&input,strlen(line)));
    assert(input.size==4 && !memcmp(input.data,"NEXT",4));
    assert(dap_http_input_consume(&input,5)==-1 && input.size==4);
    unsigned char *previous=input.data;size_t capacity=input.capacity;
    char block[8192]={0};fail_allocation=1;
    assert(dap_http_input_append(&input,block,5000)==-1);
    assert(input.data==previous && input.capacity==capacity && input.size==4);
    assert(!memcmp(input.data,"NEXT",4));fail_allocation=0;
    assert(!dap_http_input_append(&input,block,8188));
    assert(input.size==8192 && input.capacity<=input.limit);
    assert(dap_http_input_append(&input,"x",1)==1 && input.size==8192);
    assert(!dap_http_input_consume(&input,8192));
    assert(!dap_http_input_append(&input,"new",3));
    assert(!memcmp(input.data,"new",3));
    dap_http_input_clear(&input);dap_http_input_clear(&input);
    assert(!input.data && !input.size && !input.limit);
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
print('PASS: bounded plaintext input, split lines, tail preservation and allocation failure')
