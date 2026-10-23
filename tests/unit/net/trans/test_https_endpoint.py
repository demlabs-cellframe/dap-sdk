"""Real SDK TLS endpoint, TCP and Python TLS client interoperability smoke test.

The small test HTTP handler uses SDK line/input/output helpers, not the full
SDK HTTP router/reactor. No claim of a production HTTPS listener is made.
"""
import ssl
import socket
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include "dap_tls13_endpoint.h"
#include "dap_tls_http_output.h"
#include "dap_http_input.h"
#include "dap_http_line.h"
typedef struct {dap_tls13_endpoint_t endpoint;dap_http_input_t input;dap_http_output_t output;int fd;bool replied;} peer_t;
static ssize_t send_wire(void *arg,const void *p,size_t n) {
 peer_t *s=arg;ssize_t rc=send(s->fd,p,n>97?97:n,0);
 return rc<0 && (errno==EAGAIN || errno==EWOULDBLOCK)?0:rc;
}
static int drained(void *arg) {(void)arg;return 1;}
static int input(void *arg,const void *p,size_t n) {
 peer_t *s=arg;
 if(s->replied || dap_http_input_append(&s->input,p,n))return -1;
 size_t offset=0;
 while(offset<s->input.size) {
   size_t length=0;int rc=dap_http_line_read(s->input.data+offset,s->input.size-offset,4096,&length);
   if(rc<0)return -1;if(!rc)return 0;
   offset+=length;
   if(length==2) {
     const char reply[]="HTTP/1.1 200 OK\r\nContent-Length: 9\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nhttps-ok\n";
     if(dap_http_output_write(&s->output,reply,sizeof(reply)-1)!=(ssize_t)(sizeof(reply)-1))return -1;
     s->replied=true;return 0;
   }
 }
 return 0;
}
int main(int argc,char **argv) {
 assert(argc==3);
 int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 assert(!bind(listener,(void*)&addr,sizeof(addr)));assert(!listen(listener,1));
 socklen_t size=sizeof(addr);assert(!getsockname(listener,(void*)&addr,&size));
 printf("%u\n",ntohs(addr.sin_port));fflush(stdout);
 peer_t s={0};s.fd=accept(listener,NULL,NULL);assert(s.fd>=0);close(listener);
 assert(!fcntl(s.fd,F_SETFL,O_NONBLOCK));
 assert(!dap_http_input_init(&s.input,32768));
 assert(!dap_tls13_endpoint_server_plain(&s.endpoint,argv[1],argv[2],send_wire,&s,input,NULL,&s));
 dap_tls_http_output_t output={.tls=&s.endpoint.channel.tls,.outgoing=&s.endpoint.channel.outgoing,
     .wire_drained=drained,.wire_arg=&s};
 s.output=dap_tls_http_output_bind(&output);
 int complete=0;
 for(unsigned turn=0;turn<2000 && !complete;turn++) {
   struct pollfd fd={.fd=s.fd,.events=POLLIN};poll(&fd,1,5);
   unsigned char buffer[113];ssize_t n=recv(s.fd,buffer,sizeof(buffer),0);
   if(n>0)assert(dap_tls13_channel_input(&s.endpoint.channel,buffer,(size_t)n)>=0);
   else if(n<0)assert(errno==EAGAIN || errno==EWOULDBLOCK);
   assert(dap_tls13_channel_drive(&s.endpoint.channel)>=0);
   if(s.replied) {
     int rc=dap_tls_plain_stream_shutdown(&s.endpoint.channel.tls,&s.endpoint.channel.outgoing,drained,&s);
     assert(rc>=0);complete=rc==1;
   }
 }
 assert(s.replied && complete);
 close(s.fd);dap_http_input_clear(&s.input);dap_tls13_endpoint_clear(&s.endpoint);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    (tmp/'dap_common.h').write_text('#include <stdlib.h>\n#define DAP_NEW_Z(t) ((t*)calloc(1,sizeof(t)))\n#define DAP_DELETE free\n#define log_it(...) ((void)0)\n')
    (tmp/'test.c').write_text(code)
    subprocess.run(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:P-256',
                    '-nodes','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
                    '-days','1','-keyout',str(tmp/'key.pem'),'-out',str(tmp/'cert.pem')],check=True)
    subprocess.run(['cc','-DDAP_OS_UNIX','-I'+str(tmp),'-I'+str(sdk/'crypto/tls/include'),
                    '-I'+str(sdk/'net/trans/tls'),'-I'+str(sdk/'net/server/http_server/http_client/include'),
                    str(tmp/'test.c'),str(sdk/'crypto/tls/os/openssl/dap_tls_openssl.c'),
                    '-lssl','-lcrypto','-o',str(tmp/'test')],check=True)
    server = subprocess.Popen([str(tmp/'test'),str(tmp/'cert.pem'),str(tmp/'key.pem')],stdout=subprocess.PIPE,text=True)
    try:
        port = int(server.stdout.readline().strip())
        context = ssl.create_default_context(cafile=str(tmp/'cert.pem'))
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        with socket.create_connection(('127.0.0.1', port), timeout=15) as raw:
            with context.wrap_socket(raw, server_hostname='localhost') as client:
                client.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                response = b''
                while True:
                    chunk = client.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                assert response.startswith(b'HTTP/1.1 200 OK\r\n')
                assert response.split(b'\r\n\r\n', 1)[1] == b'https-ok\n'
                client.unwrap().close()
        assert server.wait(timeout=15) == 0
    finally:
        if server.poll() is None:
            server.kill()
            server.wait()
print('PASS: Python TLS client over real TCP to SDK HTTPS endpoint, CA/hostname verification')
