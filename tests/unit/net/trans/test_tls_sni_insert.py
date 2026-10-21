"""Exercise production SNI insertion with a nonempty extension suffix."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk / 'net/trans/tls/dap_tls_fingerprint.c').read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define DAP_TLS_FP_SNI_MAX_LEN 253
#define DAP_NEW_SIZE(t,n) ((t*)malloc(n))
typedef struct {
 const uint8_t *clienthello;
 size_t clienthello_size, sni_hostname_offset, sni_hostname_length_offset;
 size_t sni_data_length_offset, extensions_length_offset;
} dap_tls_fp_profile_t;
''' + source[source.index('int dap_tls_fp_build_clienthello('):] + r'''
int main(void) {
 uint8_t tpl[20]={1,0,0,16,0,14,0,0,0,5,0,3,0,0,0,0,0x55,0xaa,0x12,0x34};
 dap_tls_fp_profile_t p={tpl,20,16,14,8,4};
 const char *names[]={"", "vpn.example", "a"};
 for(unsigned i=0;i<3;i++) {
   void *out=NULL; size_t size=0,n=strlen(names[i]);
   assert(!dap_tls_fp_build_clienthello(&p,names[i],&out,&size));
   assert(size==20+n);
   assert(!memcmp((uint8_t*)out+16,names[i],n));
   assert(!memcmp((uint8_t*)out+16+n,tpl+16,4));
   free(out);
 }
 p.sni_hostname_offset=21;
 void *out=NULL; size_t n=0;
 assert(dap_tls_fp_build_clienthello(&p,"a",&out,&n)==-2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: SNI insertion preserves suffix and rejects invalid offsets')
