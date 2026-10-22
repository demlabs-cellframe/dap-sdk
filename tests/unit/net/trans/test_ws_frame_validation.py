"""Production frame parser rejects malformed wire headers before allocation."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk/'net/trans/websocket/dap_net_trans_websocket_stream.c').read_text()
start = source.index('int dap_net_trans_websocket_parse_frame(')
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "dap_ws_frame_role.h"
#include "dap_ws_close.h"
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define be64toh OSSwapBigToHostInt64
#define htobe64 OSSwapHostToBigInt64
#else
#include <endian.h>
#endif
typedef int dap_ws_opcode_t;
static void randombytes(void *p,size_t n) {memset(p,0x5a,n);}
#define DAP_NEW_Z_SIZE(t,n) ((t*)calloc(1,n))
static void s_ws_mask_unmask(uint8_t *p,size_t n,uint32_t key) {
 const uint8_t *k=(const uint8_t*)&key;for(size_t i=0;i<n;i++)p[i]^=k[i%4];
}
''' + source[source.index('int dap_net_trans_websocket_build_frame('):source.index('\n/**',source.index('int dap_net_trans_websocket_build_frame('))] + source[start:source.index('\n/**',start)] + r'''
static int parse(const uint8_t *p,size_t n) {
 uint8_t *copy=malloc(n?n:1),*payload=NULL;memcpy(copy,p,n);
 size_t size=0,total=0;int opcode;bool fin;
 int rc=dap_net_trans_websocket_parse_frame(copy,n,&opcode,&fin,&payload,&size,&total);
 free(copy);free(payload);return rc;
}
int main(void) {
 for(size_t capacity=0;capacity<9;capacity++) {
   uint8_t *small=malloc(capacity?capacity:1);memset(small,0xaa,capacity?capacity:1);
   size_t written=99;
   assert(dap_net_trans_websocket_build_frame(small,capacity,2,true,true,"abc",3,&written)==-2);
   assert(!written);for(size_t i=0;i<capacity;i++)assert(small[i]==0xaa);
   free(small);
 }
 uint8_t built[9];size_t built_size=0;
 assert(!dap_net_trans_websocket_build_frame(built,sizeof(built),2,true,true,"abc",3,&built_size));
 assert(built_size==9 && parse(built,built_size)==0);
 size_t tail=123;
 assert(!dap_ws_tail_size(DAP_WS_RETAINED_DAP_MAX,0,&tail) && tail==DAP_WS_RETAINED_DAP_MAX);
 assert(dap_ws_tail_size(DAP_WS_RETAINED_DAP_MAX+1,0,&tail)==-1 && !tail);
 assert(dap_ws_tail_size(5,6,&tail)==-1 && !tail);
 assert(!dap_ws_tail_size(5,5,&tail) && !tail);
 assert(!dap_ws_tail_size(100,99,&tail) && tail==1);
 size_t message_bytes=0;
 assert(!dap_ws_message_budget(&message_bytes,2,false,DAP_WS_RECEIVE_MESSAGE_MAX-1));
 assert(message_bytes==DAP_WS_RECEIVE_MESSAGE_MAX-1);
 assert(!dap_ws_message_budget(&message_bytes,9,true,125));
 assert(message_bytes==DAP_WS_RECEIVE_MESSAGE_MAX-1);
 assert(dap_ws_message_budget(&message_bytes,0,true,2)==-1);
 assert(message_bytes==DAP_WS_RECEIVE_MESSAGE_MAX-1);
 assert(!dap_ws_message_budget(&message_bytes,0,true,1) && !message_bytes);
 assert(!dap_ws_message_budget(&message_bytes,2,true,5) && !message_bytes);
 const uint8_t budget[]={0x82,127,0,0,0,0,0,16,0,0};
 for(size_t i=0;i<sizeof(budget);i++) assert(dap_ws_frame_budget_check(budget,i,DAP_WS_RECEIVE_FRAME_MAX)==-2);
 assert(!dap_ws_frame_budget_check(budget,sizeof(budget),DAP_WS_RECEIVE_FRAME_MAX));
 assert(dap_ws_frame_budget_check(budget,sizeof(budget),DAP_WS_RECEIVE_FRAME_MAX-1)==-1);
 const uint8_t huge[]={0x82,127,128,0,0,0,0,0,0,0};
 assert(dap_ws_frame_budget_check(huge,sizeof(huge),DAP_WS_RECEIVE_FRAME_MAX)==-1);
 assert(dap_ws_close_valid(NULL,0));
 const uint8_t valid_close[]={3,232,0xf0,0x9f,0x98,0x80};
 assert(dap_ws_close_valid(valid_close,sizeof(valid_close)));
 assert(!dap_ws_close_valid(valid_close,1));
 assert(!dap_ws_close_valid(valid_close,5));
 const uint8_t invalid_close[][6]={{3,237},{3,232,0xc0,0x80},{3,232,0xed,0xa0,0x80},{3,232,0xf4,0x90,0x80,0x80}};
 const size_t close_sizes[]={2,4,5,6};
 for(size_t i=0;i<4;i++) assert(!dap_ws_close_valid(invalid_close[i],close_sizes[i]));
 bool fragmented=false;
 assert(dap_ws_fragment_accept(&fragmented,1,true)==-1 && !fragmented);
 assert(dap_ws_fragment_accept(&fragmented,1,false)==-1 && !fragmented);
 assert(dap_ws_fragment_accept(&fragmented,0,true)==-1 && !fragmented);
 assert(!dap_ws_fragment_accept(&fragmented,2,false) && fragmented);
 assert(!dap_ws_fragment_accept(&fragmented,9,true) && fragmented);
 assert(dap_ws_fragment_accept(&fragmented,2,true)==-1 && fragmented);
 assert(!dap_ws_fragment_accept(&fragmented,0,false) && fragmented);
 assert(!dap_ws_fragment_accept(&fragmented,10,true) && fragmented);
 assert(!dap_ws_fragment_accept(&fragmented,0,true) && !fragmented);
 assert(!dap_ws_fragment_accept(&fragmented,2,true) && !fragmented);
 const uint8_t good[]={0x82,0x83,1,2,3,4,'a'^1,'b'^2,'c'^3};
 assert(dap_ws_frame_role_check(good,1,true)==-2);
 assert(dap_ws_frame_role_check(good,sizeof(good),true)==0);
 assert(dap_ws_frame_role_check(good,sizeof(good),false)==-1);
 const uint8_t unmasked[]={0x82,0};
 assert(dap_ws_frame_role_check(unmasked,2,false)==0);
 assert(dap_ws_frame_role_check(unmasked,2,true)==-1);
 for(size_t n=0;n<sizeof(good);n++)assert(parse(good,n)==-2);
 assert(parse(good,sizeof(good))==0);
 const uint8_t bad[][10]={
 {0xc2,0},{0x83,0},{0x09,0},{0x89,126},{0x88,1,0},
 {0x82,126,0,125},{0x82,127,0,0,0,0,0,0,0,126},
 {0x82,127,128,0,0,0,0,0,0,0}};
 const size_t sizes[]={2,2,2,2,3,4,10,10};
 for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);i++)assert(parse(bad[i],sizes[i])==-1);
 const uint8_t ping[]={0x89,0};assert(parse(ping,sizeof(ping))==0);
 const uint8_t continuation[]={0,0};assert(parse(continuation,sizeof(continuation))==0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c=pathlib.Path(tmp)/'test.c'
    exe=pathlib.Path(tmp)/'test'
    c.write_text(code)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(sdk/'net/trans/websocket'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: production WS frame header validation and split boundaries')
