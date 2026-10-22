"""Production key-ID parser and DAP base64 with system json-c."""
import pathlib
import subprocess
import tempfile
sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include <stdio.h>
#include "dap_tls13_key_id.h"
int main(void) {
 char id[33]; memset(id,'A',sizeof(id)); char encoded[45]={0};
 assert(dap_enc_base64_encode(id,33,encoded,DAP_ENC_DATA_TYPE_B64)==44);
 char reply[128],out[34]; snprintf(reply,sizeof(reply),"{\"encrypt_id\":\"%s\"}",encoded);
 assert(!dap_tls13_key_id_from_reply(reply,strlen(reply),out));
 assert(!memcmp(out,id,33) && out[33]==0);
 for(size_t i=0;i<strlen(reply);i++) {
   memset(out,'X',sizeof(out));
   assert(dap_tls13_key_id_from_reply(reply,i,out)==-1 && out[0]==0);
 }
 strcat(reply,"garbage");
 assert(dap_tls13_key_id_from_reply(reply,strlen(reply),out)==-1 && out[0]==0);
 memset(encoded,'!',44); snprintf(reply,sizeof(reply),"{\"encrypt_id\":\"%s\"}",encoded);
 assert(dap_tls13_key_id_from_reply(reply,strlen(reply),out)==-1);
 assert(dap_tls13_key_id_from_reply("[]",2,out)==-1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    (tmp/'dap_enc_key.h').write_text('#include <stdlib.h>\ntypedef enum { DAP_ENC_DATA_TYPE_B64, DAP_ENC_DATA_TYPE_B64_URLSAFE } dap_enc_data_type_t;\n#define DAP_NEW_SIZE(t,n) ((t*)malloc(n))\n#define DAP_NEW_Z_SIZE(t,n) ((t*)calloc(1,n))\n')
    # Copy the public declaration header so its unrelated key backend includes
    # can be stubbed; base64 implementation itself is compiled unchanged.
    (tmp/'dap_enc_base64.h').write_text((sdk/'crypto/include/dap_enc_base64.h').read_text())
    (tmp/'dap_enc_ks.h').write_text('#define DAP_ENC_KS_KEY_ID_SIZE 33\n')
    (tmp/'test.c').write_text(code)
    flags = subprocess.check_output(['pkg-config','--cflags','--libs','json-c'],text=True).split()
    subprocess.run(['cc','-fsanitize=address,undefined','-I'+str(tmp),'-I'+str(sdk/'net/trans/tls'),
                    str(tmp/'test.c'),str(sdk/'crypto/src/dap_enc_base64.c'),'-o',str(tmp/'test'),*flags,'-lm'],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: key ID round-trip, truncated JSON, invalid base64 and trailing data')
