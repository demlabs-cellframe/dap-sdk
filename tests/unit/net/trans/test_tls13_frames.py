"""Production TLS13 framing: every split, coalescing, bounds and rejection."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include "dap_tls13_frame.h"
static unsigned calls;
static int consume(void *arg,uint8_t type,const void *data,size_t size) {
 (void)arg; assert(type==DAP_TLS13_CTL_REPLY && size==3 && !memcmp(data,"abc",3));
 calls++; return 0;
}
int main(void) {
 uint8_t wire[18];
 assert(!dap_tls13_frame_header(wire,DAP_TLS13_CTL_REPLY,3));
 memcpy(wire+6,"abc",3); memcpy(wire+9,wire,9);
 for(size_t split=0;split<=18;split++) {
   dap_tls13_frame_reader_t r={0}; calls=0;
   assert(!dap_tls13_frame_feed(&r,wire,split,consume,NULL));
   assert(!dap_tls13_frame_feed(&r,wire+split,18-split,consume,NULL));
   assert(calls==2 && !r.payload && !r.header_size);
   dap_tls13_frame_clear(&r);
 }
 dap_tls13_frame_reader_t r={0}; calls=0;
 for(size_t i=0;i<18;i++) assert(!dap_tls13_frame_feed(&r,wire+i,1,consume,NULL));
 assert(calls==2);
 uint8_t bad[]={1,4,0xff,0xff,0xff,0xff};
 assert(dap_tls13_frame_feed(&r,bad,6,consume,NULL)==-1 && !r.payload);
 assert(dap_tls13_frame_feed(&r,wire,9,consume,NULL)==-1);
 dap_tls13_frame_clear(&r);
 assert(dap_tls13_frame_header(wire,4,DAP_TLS13_FRAME_MAX+1)==-1);
 assert(!dap_tls13_frame_feed(&r,wire,7,consume,NULL));
 dap_tls13_frame_clear(&r); /* cancellation mid-payload */
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    (tmp/'test.c').write_text(code)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(sdk/'net/trans/tls'),str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: TLS13 frame splits/coalescing/bounds/cancellation')
