"""Linux live reuseport test using the production cBPF instructions."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
source = (sdk / 'io/linux/dap_io_flow_cbpf.c').read_text()
start = source.index('    struct sock_filter l_filter[]')
end = source.index('    struct sock_fprog prog', start)
code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/filter.h>
#include <poll.h>
static void check(int family) {
 unsigned socket_count=2;
'''+source[start:end]+r'''
 int sockets[2], one=1;
 struct sockaddr_storage address={0}; socklen_t len;
 if(family==AF_INET) {
   struct sockaddr_in *a=(void*)&address; a->sin_family=family; a->sin_addr.s_addr=htonl(INADDR_LOOPBACK); len=sizeof(*a);
 } else {
   struct sockaddr_in6 *a=(void*)&address; a->sin6_family=family; a->sin6_addr=in6addr_loopback; len=sizeof(*a);
 }
 for(unsigned i=0;i<2;i++) {
   sockets[i]=socket(family,SOCK_DGRAM|SOCK_NONBLOCK,0); assert(sockets[i]>=0);
   assert(!setsockopt(sockets[i],SOL_SOCKET,SO_REUSEPORT,&one,sizeof(one)));
   assert(!bind(sockets[i],(void*)&address,len));
   if(!i) assert(!getsockname(sockets[i],(void*)&address,&len));
 }
 struct sock_fprog program={sizeof(l_filter)/sizeof(*l_filter),l_filter};
 assert(!setsockopt(sockets[1],SOL_SOCKET,SO_ATTACH_REUSEPORT_CBPF,&program,sizeof(program)));
 for(unsigned attempt=0;attempt<20;attempt++) {
   int client=socket(family,SOCK_DGRAM,0); assert(client>=0);
   for(unsigned size=1;size<1400;size+=37) {
     char data[1400]; memset(data,size,sizeof(data));
     assert(sendto(client,data,size,0,(void*)&address,len)==size);
     struct pollfd p[2]={{sockets[0],POLLIN,0},{sockets[1],POLLIN,0}};
     assert(poll(p,2,1000)==1);
     /* 127.0.0.1 and ::1 both map to index one, not a kernel fallback. */
     assert(!(p[0].revents&POLLIN) && (p[1].revents&POLLIN));
     assert(recv(sockets[1],data,sizeof(data),0)==size);
   }
   close(client);
 }
 close(sockets[0]); close(sockets[1]);
}
int main(void) { check(AF_INET); check(AF_INET6); }
'''
with tempfile.TemporaryDirectory() as directory:
    c = pathlib.Path(directory) / 'test.c'
    binary = pathlib.Path(directory) / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: Linux cBPF IPv4/IPv6 stable owner, varied payloads and repeated connections')
