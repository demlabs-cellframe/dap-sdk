#!/usr/bin/env python3
"""Compile the production frame parser and test TCP segmentation with ASan/UBSan.

Standalone regression test; requires Python 3 and a C compiler (CC or cc).
No SDK mocks or replacement parser are used.
"""
import os
from pathlib import Path
import subprocess
import tempfile

sdk = Path(__file__).resolve().parents[4]
source = (sdk / "net/trans/websocket/dap_net_trans_websocket_stream.c").read_text()
start = source.index("int dap_net_trans_websocket_parse_frame(")
parser = source[start:source.index("\n/**", start)]
support = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define be64toh OSSwapBigToHostInt64
#else
#include <endian.h>
#endif
#define DAP_NEW_Z_SIZE(t,n) ((t *)calloc(1,n))
typedef int dap_ws_opcode_t;
static void s_ws_mask_unmask(uint8_t *p, size_t n, uint32_t key) {
    for (size_t i=0; i<n; ++i) p[i] ^= ((uint8_t *)&key)[i%4];
}
'''
tests = r'''
static void check(const uint8_t *frame, size_t n, size_t payload_size, int expected) {
    int opcode=0; bool fin=false; uint8_t *payload=NULL; size_t size=0, total=0;
    int rc=dap_net_trans_websocket_parse_frame(frame,n,&opcode,&fin,&payload,&size,&total);
    assert(rc==expected);
    if (!rc) {
        assert(opcode==2 && fin && size==payload_size && total==n);
        for(size_t i=0;i<size;i++) assert(payload[i]==(uint8_t)i);
    }
    free(payload);
}
int main(void) {
    const size_t sizes[]={0,1,125,126,1024,65535,65536};
    for(size_t k=0;k<sizeof(sizes)/sizeof(*sizes);k++) {
        for(int masked=0;masked<2;masked++) {
            size_t len=sizes[k], h=len<126?2:len<65536?4:10;
            size_t n=h+(masked?4:0)+len;
            uint8_t *frame=calloc(1,n);
            frame[0]=0x82;
            frame[1]=(masked?0x80:0)|(len<126?len:len<65536?126:127);
            if(h>2) for(size_t i=2;i<h;i++) frame[i]=(uint64_t)len>>(8*(h-1-i));
            const uint8_t mask[]={0x12,0x34,0x56,0x78};
            if(masked) { memcpy(frame+h,mask,4); h+=4; }
            for(size_t i=0;i<len;i++) frame[h+i]=(uint8_t)i^(masked?mask[i%4]:0);
            /* Every possible first-read boundary, including a one-byte header. */
            for(size_t split=0;split<n;split++) check(frame,split,len,-2);
            check(frame,n,len,0);
            /* Coalesced frame followed by the first byte of the next frame. */
            uint8_t *pair=malloc(n+1); memcpy(pair,frame,n); pair[n]=0x82;
            check(pair,n,len,0); check(pair+n,1,len,-2);
            free(pair); free(frame);
        }
    }
    uint8_t invalid[]={0x82,127,0x80,0,0,0,0,0,0,0};
    check(invalid,sizeof(invalid),0,-1);
    puts("PASS: masked/unmasked frames, all TCP split boundaries, coalesced tail, invalid length");
}
'''
with tempfile.TemporaryDirectory(prefix="ws-tcp-splits-") as tmp:
    c = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    c.write_text(support + parser + tests)
    subprocess.run([os.environ.get("CC", "cc"), "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
