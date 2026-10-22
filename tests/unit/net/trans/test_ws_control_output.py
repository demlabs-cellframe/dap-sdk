"""Production control sender and frame builder with socket write fault injection."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk/'net/trans/websocket/dap_net_trans_websocket_stream.c').read_text()
builder = text.index('int dap_net_trans_websocket_build_frame(')
sender = text.index('static int s_ws_send_control(')
sender = text.index('static int s_ws_send_control(dap_stream_t *a_stream')
close = text.index('int dap_net_trans_websocket_send_close(')
reply = text.index('int dap_net_trans_websocket_reply_close(')
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include "dap_ws_close.h"
#define DAP_WS_OPCODE_CLOSE 8
typedef int dap_ws_close_code_t;
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe64 OSSwapHostToBigInt64
#else
#include <endian.h>
#endif
#define DAP_SOCK_SIGNAL_CLOSE 1
typedef int dap_ws_opcode_t;
typedef struct {int flags;} dap_events_socket_t;
typedef struct {dap_events_socket_t *esocket;} dap_net_trans_websocket_private_t;
typedef struct {dap_events_socket_t *esocket;bool is_client_to_uplink;} dap_stream_t;
static dap_net_trans_websocket_private_t fallback;
static dap_net_trans_websocket_private_t *s_get_private_from_stream(dap_stream_t *s) {(void)s;return &fallback;}
static void randombytes(void *p,size_t n) {memset(p,0x5a,n);}
static void s_ws_mask_unmask(uint8_t *p,size_t n,uint32_t key) {
 const uint8_t *k=(const uint8_t*)&key;for(size_t i=0;i<n;i++)p[i]^=k[i%4];
}
static unsigned calls;
static size_t accepted_limit=1000,wire_size;
static unsigned char wire[139];
static size_t dap_events_socket_write_unsafe(dap_events_socket_t *s,const void *p,size_t n) {
 (void)s;calls++;assert(n<=sizeof(wire));memcpy(wire,p,n);wire_size=n;
 return n<accepted_limit?n:accepted_limit;
}
''' + text[builder:text.index('\n/**',builder)] + text[sender:text.index('\nint dap_net_trans_websocket_send_ping(',sender)] + text[close:text.index('\n/**',close)] + text[reply:text.index('\n/**',reply)] + r'''
int main(void) {
 dap_events_socket_t socket={0};dap_stream_t stream={.esocket=&socket,.is_client_to_uplink=true};
 unsigned char payload[125];memset(payload,'a',sizeof(payload));
 assert(!s_ws_send_control(&stream,9,payload,sizeof(payload)));
 assert(wire_size==131 && wire[0]==0x89 && wire[1]==(0x80|125));
 for(size_t i=0;i<125;i++)assert((wire[6+i]^wire[2+i%4])=='a');
 stream.is_client_to_uplink=false;
 assert(!s_ws_send_control(&stream,10,payload,sizeof(payload)));
 assert(wire_size==127 && wire[0]==0x8a && wire[1]==125 && !memcmp(wire+2,payload,125));
 assert(!s_ws_send_control(&stream,9,NULL,0) && wire_size==2);
 unsigned before=calls;
 assert(s_ws_send_control(&stream,9,NULL,1)==-2);
 assert(s_ws_send_control(&stream,9,payload,126)==-2);
 assert(calls==before);
 accepted_limit=1;assert(s_ws_send_control(&stream,9,payload,1)==-5);
 assert(socket.flags&DAP_SOCK_SIGNAL_CLOSE);
 before=calls;assert(s_ws_send_control(&stream,9,NULL,0)==-3 && calls==before);
 socket.flags=0;accepted_limit=0;
 assert(s_ws_send_control(&stream,10,NULL,0)==-5 && socket.flags);
 socket.flags=0;accepted_limit=1000;stream.esocket=NULL;fallback.esocket=&socket;
 assert(!s_ws_send_control(&stream,10,NULL,0));
 assert(!dap_net_trans_websocket_send_close(&stream,1000,"done"));
 assert(wire_size==8 && wire[0]==0x88 && wire[2]==3 && wire[3]==232);
 assert(!memcmp(wire+4,"done",4));
 char reason[125];memset(reason,'x',124);reason[124]=0;
 before=calls;assert(dap_net_trans_websocket_send_close(&stream,1000,reason)==-2 && calls==before);
 reason[123]=0;assert(!dap_net_trans_websocket_send_close(&stream,1000,reason) && wire_size==127);
 before=calls;
 assert(dap_net_trans_websocket_send_close(&stream,1005,NULL)==-2);
 assert(dap_net_trans_websocket_send_close(&stream,1000,"\xc0\x80")==-2 && calls==before);
 assert(!dap_net_trans_websocket_reply_close(&stream,NULL,0));
 assert(wire_size==2 && wire[0]==0x88 && wire[1]==0);
 const unsigned char peer_close[]={3,233,'b','y','e'};
 assert(!dap_net_trans_websocket_reply_close(&stream,peer_close,sizeof(peer_close)));
 assert(wire_size==7 && !memcmp(wire+2,peer_close,sizeof(peer_close)));
 stream.is_client_to_uplink=true;
 assert(!dap_net_trans_websocket_reply_close(&stream,peer_close,sizeof(peer_close)));
 assert(wire_size==11 && wire[1]==(0x80|5));
 for(size_t i=0;i<sizeof(peer_close);i++)assert((wire[6+i]^wire[2+i%4])==peer_close[i]);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp)/'test.c'
    exe = pathlib.Path(tmp)/'test'
    c.write_text(code)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(sdk/'net/trans/websocket'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: production WS control sender, masking and socket write failures')
