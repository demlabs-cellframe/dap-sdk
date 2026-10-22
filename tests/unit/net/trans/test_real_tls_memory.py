"""Real production OpenSSL backend: TLS 1.3 over fragmented memory BIO I/O."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
test = r'''
#include <assert.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include "dap_tls.h"
#include "dap_tls_reactor.h"
#include "dap_tls13_channel.h"
#include "dap_tls13_endpoint.h"
static unsigned frames;
static int frame_received(void *arg,uint8_t type,const void *data,size_t size) {
 (void)arg; assert(type==DAP_TLS13_CTL_REPLY && size==12);
 assert(!memcmp(data,"opaque reply",12)); frames++; return 0;
}
typedef struct { dap_tls_session_t *peer; int blocked; } sink_t;
static ssize_t wire_send(void *arg, const void *data, size_t size) {
 sink_t *sink=arg;
 if(sink->blocked) return 0;
 size_t n=size>17?17:size;
 return dap_tls_openssl_session_feed(sink->peer,data,n);
}
static void transfer(dap_tls_session_t *from, dap_tls_session_t *to) {
 unsigned char b[17]; ssize_t n;
 while((n=dap_tls_openssl_session_drain(from,b,sizeof(b)))>0)
   assert(dap_tls_openssl_session_feed(to,b,n)==n);
 assert(n==0);
}
int main(int argc,char **argv) {
 assert(argc==3);
 FILE *f=fopen(argv[1],"r"); assert(f);
 X509 *cert=PEM_read_X509(f,NULL,NULL,NULL); fclose(f); assert(cert);
 unsigned char *der=NULL,pin[32]; unsigned int n=0;
 int len=i2d_X509_PUBKEY(X509_get_X509_PUBKEY(cert),&der);
 assert(len>0 && EVP_Digest(der,len,pin,&n,EVP_sha256(),NULL) && n==32);
 OPENSSL_free(der); X509_free(cert);
 char pin_hex[65];
 for(unsigned i=0;i<32;i++) sprintf(pin_hex+2*i,"%02x",pin[i]);
 {
   dap_tls13_endpoint_t client={0},server={0};
   sink_t to_server={0},to_client={0};
   assert(dap_tls13_endpoint_client(&client,"bad",NULL,wire_send,&to_server,frame_received,NULL)==-1);
   assert(!client.context && !client.channel.tls.session);
   assert(!dap_tls13_endpoint_server(&server,argv[1],argv[2],wire_send,&to_client,frame_received,NULL));
   assert(!dap_tls13_endpoint_client(&client,pin_hex,NULL,wire_send,&to_server,frame_received,NULL));
   to_server.peer=server.channel.tls.session; to_client.peer=client.channel.tls.session;
   for(unsigned i=0;i<1000 && !(client.channel.tls.ready && server.channel.tls.ready);i++) {
     assert(dap_tls13_channel_drive(&client.channel)>=0);
     assert(dap_tls13_channel_drive(&server.channel)>=0);
   }
   assert(client.channel.tls.ready && server.channel.tls.ready);
   while(dap_tls_reactor_flush(&client.channel.tls)==0) {}
   while(dap_tls_reactor_flush(&server.channel.tls)==0) {}
   assert(!dap_tls13_channel_send(&client.channel,DAP_TLS13_CTL_REPLY,"opaque reply",12));
   for(unsigned i=0;i<1000 && !frames;i++) assert(dap_tls13_channel_drive(&server.channel)>=0);
   assert(frames==1); frames=0;
   dap_tls13_endpoint_clear(&client); dap_tls13_endpoint_clear(&server);
   assert(!client.context && !server.context);
 }
 for(int wrong=0;wrong<2;wrong++) {
   dap_tls_context_t *cc=dap_tls_context_new_client(),*sc=dap_tls_context_new_server(argv[1],argv[2]);
   assert(cc && sc);
   assert(!dap_tls_context_set_min_version(cc,0x0304));
   assert(!dap_tls_context_set_min_version(sc,0x0304));
   dap_tls_session_t *c=dap_tls_session_new(cc),*s=dap_tls_session_new(sc);
   assert(c && s);
   assert(!dap_tls_openssl_session_set_memory_io(c));
   assert(!dap_tls_openssl_session_set_memory_io(s));
   unsigned char expected[32]; memcpy(expected,pin,32); expected[0]^=wrong;
   assert(!dap_tls_openssl_session_set_spki_pin(c,expected));
   assert(dap_tls_session_write(c,"secret",6)<0);
   sink_t client_sink={s,0}, server_sink={c,0};
   dap_tls_reactor_t client_handshake={.session=c,.send=wire_send,.send_arg=&client_sink};
   dap_tls_reactor_t server_handshake={.session=s,.send=wire_send,.send_arg=&server_sink,.server=true};
   int cr=0,sr=0;
   for(int i=0;i<1000 && cr>=0 && !(cr==1 && sr==1);i++) {
     cr=dap_tls_reactor_handshake(&client_handshake);
     if(cr<0) break;
     sr=dap_tls_reactor_handshake(&server_handshake);
     assert(sr>=0);
   }
   if(wrong) {
     assert(cr==-1 && dap_tls_session_write(c,"secret",6)<0);
   } else {
     assert(cr==1 && sr==1 && !strcmp(dap_tls_session_get_version(c),"TLSv1.3"));
     while(dap_tls_reactor_flush(&client_handshake)==0) {}
     while(dap_tls_reactor_flush(&server_handshake)==0) {}
     sink_t sink={s,1};
     dap_tls_reactor_t reactor={.session=c,.send=wire_send,.send_arg=&sink,.ready=true};
     assert(dap_tls_reactor_write(&reactor,"signed DAP payment bytes",24)==24);
     assert(reactor.size>reactor.offset);
     assert(dap_tls_reactor_write(&reactor,"next",4)==0);
     sink.blocked=0;
     while(dap_tls_reactor_flush(&reactor)==0) {}
     assert(!reactor.failed);
     assert(dap_tls_reactor_handshake(&reactor)==1);
     assert(dap_tls_reactor_feed(&reactor,NULL,0)==0);
     char out[64];
     sink_t back={c,0};
     dap_tls_reactor_t server={.session=s,.send=wire_send,.send_arg=&back,.ready=true,.server=true};
     assert(dap_tls_reactor_read(&server,out,sizeof(out))==24);
     assert(!memcmp(out,"signed DAP payment bytes",24));
     assert(dap_tls_reactor_read(&server,out,sizeof(out))==0 && !server.peer_closed);
     dap_tls_plain_queue_t queue={0};
     sink.blocked=1;
     assert(!dap_tls_plain_queue_push(&queue,"first",5));
     assert(!dap_tls_plain_queue_push(&queue,"second",6));
     assert(dap_tls_plain_queue_pump(&queue,&reactor)==0);
     assert(queue.head && queue.allocated);
     assert(dap_tls_plain_queue_push(&queue,"x",DAP_TLS_PLAIN_QUEUE_LIMIT)==-1);
     sink.blocked=0;
     int drained=0;
     for(int i=0;i<1000 && !drained;i++) {
       int rc=dap_tls_plain_queue_pump(&queue,&reactor);
       assert(rc>=0); drained=rc==1;
     }
     assert(drained && !queue.head && !queue.allocated);
     size_t got=0;
     while(got<11) {
       ssize_t n=dap_tls_reactor_read(&server,out+got,sizeof(out)-got);
       assert(n>0); got+=(size_t)n;
     }
     assert(!memcmp(out,"firstsecond",11));
     assert(!dap_tls_plain_queue_push(&queue,"cancel",6));
     dap_tls_plain_queue_clear(&queue);
     assert(!queue.head && !queue.tail && !queue.allocated);
     dap_tls13_channel_t channel_client={.tls=reactor,.dispatch=frame_received};
     dap_tls13_channel_t channel_server={.tls=server,.dispatch=frame_received};
     assert(!dap_tls13_channel_send(&channel_client,DAP_TLS13_CTL_REPLY,"opaque reply",12));
     for(int i=0;i<1000 && !frames;i++) {
       assert(dap_tls13_channel_drive(&channel_client)>=0);
       assert(dap_tls13_channel_input(&channel_server,NULL,0)>=0);
     }
     assert(frames==1);
     /* Hand back session ownership for shutdown checks below. */
     reactor=channel_client.tls; server=channel_server.tls;
     channel_client.tls.session=NULL; channel_server.tls.session=NULL;
     dap_tls13_channel_clear(&channel_client); dap_tls13_channel_clear(&channel_server);
     int closed=0;
     for(int i=0;i<100 && !closed;i++) {
       int a=dap_tls_reactor_shutdown(&reactor);
       int b=dap_tls_reactor_shutdown(&server);
       assert(a>=0 && b>=0);
       closed=a==1 && b==1;
     }
     assert(closed);
     assert(dap_tls_reactor_write(&reactor,"late",4)==-1);
   }
   dap_tls_session_free(c); dap_tls_session_free(s);
   dap_tls_context_free(cc); dap_tls_context_free(sc);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    (tmp/'dap_common.h').write_text('#include <stdlib.h>\n#define DAP_NEW_Z(t) ((t*)calloc(1,sizeof(t)))\n#define DAP_DELETE free\n#define log_it(...) ((void)0)\n')
    (tmp/'test.c').write_text(test)
    subprocess.run(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:P-256',
                    '-nodes','-subj','/CN=test','-days','1','-keyout',str(tmp/'key.pem'),
                    '-out',str(tmp/'cert.pem')],check=True)
    subprocess.run(['cc','-DDAP_OS_UNIX','-I'+str(tmp),'-I'+str(sdk/'crypto/tls/include'),
                    '-I'+str(sdk/'net/trans/tls'),
                    str(tmp/'test.c'),str(sdk/'crypto/tls/os/openssl/dap_tls_openssl.c'),
                    '-lssl','-lcrypto','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test'),str(tmp/'cert.pem'),str(tmp/'key.pem')],check=True)
print('PASS: real TLS 1.3, fragmented BIO, correct/wrong SPKI, application data gated')
