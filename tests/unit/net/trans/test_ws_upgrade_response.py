"""Production Upgrade validator plus production shared HTTP parser."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk / 'net/common/http/src/dap_http_header.c').read_text()
start = source.index('int dap_http_header_parse_line(')
code = '#include <assert.h>\n#include "dap_ws_upgrade_request.h"\n'
code += source[start:source.index('\n/**', start)]
code += r'''
int main(void) {
 dap_http_header_t hs[6]={0};
 const char *names[]={"Host","Sec-WebSocket-Key","Sec-WebSocket-Version","Upgrade","Connection"};
 const char *values[]={"example.org:443","dGhlIHNhbXBsZSBub25jZQ==","13","websocket","Upgrade"};
 for(size_t i=0;i<5;i++){strcpy(hs[i].name,names[i]);strcpy(hs[i].value,values[i]);hs[i].next=i<4?&hs[i+1]:NULL;}
 assert(dap_ws_request_headers_valid(hs,1,0));
 assert(!dap_ws_request_headers_valid(hs,0,0));
 assert(!dap_ws_request_headers_valid(hs,1,1));
 assert(!dap_ws_request_headers_valid(hs+1,1,0));
 hs[4].next=&hs[5];strcpy(hs[5].name,"hOsT");strcpy(hs[5].value,"other");
 assert(!dap_ws_request_headers_valid(hs,1,0));
 strcpy(hs[5].name,"Transfer-Encoding");strcpy(hs[5].value,"chunked");
 assert(!dap_ws_request_headers_valid(hs,1,0));
 strcpy(hs[5].name,"Content-Length");strcpy(hs[5].value,"0");
 assert(dap_ws_request_headers_valid(hs,1,0));
 strcpy(hs[5].value,"1");assert(!dap_ws_request_headers_valid(hs,1,0));
 hs[4].next=NULL;strcpy(hs[0].value,"bad host");
 assert(!dap_ws_request_headers_valid(hs,1,0));
 const char *key="dGhlIHNhbXBsZSBub25jZQ==";
 assert(dap_ws_request_fields_valid("GET","WebSocket","keep-alive, Upgrade",key));
 assert(!dap_ws_request_fields_valid("POST","websocket","Upgrade",key));
 assert(!dap_ws_request_fields_valid("GET","notwebsocket","Upgrade",key));
 assert(!dap_ws_request_fields_valid("GET","websocket","NotUpgrade",key));
 assert(!dap_ws_request_fields_valid("GET","websocket","Upgrade, close",key));
 assert(!dap_ws_request_key_valid("dGhlIHNhbXBsZSBub25jZR=="));
 assert(!dap_ws_request_key_valid("dGhlIHNhbXBsZSBub25jZQ="));
 assert(!dap_ws_request_key_valid("!!!!!!!!!!!!!!!!!!!!!!=="));
 char huge[DAP_WS_UPGRADE_HEADERS_MAX+32];memset(huge,'x',sizeof(huge));
 size_t used=123;
 assert(dap_ws_upgrade_header_size(huge,DAP_WS_UPGRADE_HEADERS_MAX-1,&used)==0 && !used);
 assert(dap_ws_upgrade_header_size(huge,DAP_WS_UPGRADE_HEADERS_MAX,&used)==-1 && !used);
 memcpy(huge+DAP_WS_UPGRADE_HEADERS_MAX-4,"\r\n\r\n",4);
 assert(dap_ws_upgrade_header_size(huge,sizeof(huge),&used)==1 && used==DAP_WS_UPGRADE_HEADERS_MAX);
 memcpy(huge,"\r\n\r\n",4);
 assert(dap_ws_upgrade_header_size(huge,sizeof(huge),&used)==1 && used==4);
 const char *s="HTTP/1.1 101 Switching Protocols\r\nUpgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: expected\r\n\r\n";
 assert(dap_ws_upgrade_response_valid(s,strlen(s),"expected",NULL));
 for(size_t n=0;n<strlen(s);++n) assert(!dap_ws_upgrade_response_valid(s,n,"expected",NULL));
 assert(!dap_ws_upgrade_response_valid(s,strlen(s),"wrong",NULL));
 const char *headers[]={
 "Sec-WebSocket-Accept: expected\r\n",
 "Sec-WebSocket-Extensions: permessage-deflate\r\n",
 "Sec-WebSocket-Protocol: test\r\n",
 "Bad Name: value\r\n"};
 char buffer[1024];
 for(size_t i=0;i<sizeof(headers)/sizeof(*headers);++i) {
   size_t n=strlen(s)-2;memcpy(buffer,s,n);strcpy(buffer+n,headers[i]);strcat(buffer,"\r\n");
   assert(!dap_ws_upgrade_response_valid(buffer,strlen(buffer),"expected",NULL));
 }
 size_t n=strlen(s)-2;memcpy(buffer,s,n);
 strcpy(buffer+n,"Sec-WebSocket-Protocol: test\r\n\r\n");
 assert(dap_ws_upgrade_response_valid(buffer,strlen(buffer),"expected","other, test"));
 assert(!dap_ws_upgrade_response_valid(buffer,strlen(buffer),"expected","Test"));
 assert(!dap_ws_upgrade_response_valid(buffer,strlen(buffer),"expected","testing"));
 s="HTTP/1.1 101 OK\r\nSec-WebSocket-Accept: expected\r\n\r\n";
 assert(!dap_ws_upgrade_response_valid(s,strlen(s),"expected",NULL));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I'+str(sdk/'net/trans/websocket'), '-I'+str(sdk/'net/common/http/include'),
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: production WS Upgrade headers, accept binding and negotiation')
