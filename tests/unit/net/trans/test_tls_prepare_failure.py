"""Production prepare: failed connect closes unpublished socket, not owner."""
import pathlib
import subprocess
import tempfile
sdk = pathlib.Path(__file__).resolve().parents[4]
source=(sdk/'net/trans/tls/dap_stream_trans_tls.c').read_text()
start=source.index('static int s_tls_stage_prepare(', source.index('/*  Configuration'))
end=source.index('\nstatic ssize_t s_tls_write(',start)
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#define UNUSED(x) ((void)(x))
#define log_it(...) ((void)0)
#define DAP_SOCK_CONNECTING 1
#define DAP_SOCK_READY_TO_WRITE 2
#define DAP_SOCK_READY_TO_READ 4
typedef int dap_events_socket_callbacks_t;
typedef int dap_net_trans_t;
typedef int dap_stream_node_addr_t;
typedef struct { int socket; void *_inheritor; unsigned flags; void *worker; struct { void *arg; } callbacks; } dap_events_socket_t;
typedef struct { dap_net_trans_t *trans; } dap_stream_t;
typedef struct { const char *host; unsigned short port; dap_events_socket_callbacks_t *callbacks;
 void *client_ctx,*worker,*node_addr; bool authorized; } dap_net_stage_prepare_params_t;
typedef struct { int error_code; dap_events_socket_t *esocket; dap_stream_t *stream; } dap_net_stage_prepare_result_t;
static struct { const char *sni_hostname; } s_config;
static int deleted,published,resolve_fail,connect_ok,removed;
static dap_events_socket_t es;
static dap_events_socket_t *dap_events_socket_create_resolved(const char *h,int p,int t,int proto,void *cb,bool *failed) {
 (void)h;(void)p;(void)t;(void)proto;(void)cb;
 *failed=resolve_fail;es=(dap_events_socket_t){.socket=-1}; return resolve_fail?NULL:&es;
}
static void dap_events_socket_connect(dap_events_socket_t *s,int *error){(void)s;*error=connect_ok?0:ECONNREFUSED;}
static void dap_events_socket_delete_unsafe(dap_events_socket_t *s,bool preserve){assert(s==&es && preserve && s->_inheritor==(void*)42);deleted++;}
static void dap_events_socket_remove_and_delete_unsafe(dap_events_socket_t *s,bool preserve){assert(s->worker && !s->callbacks.arg);removed++;dap_events_socket_delete_unsafe(s,preserve);}
static void dap_worker_add_events_socket(void *w,dap_events_socket_t *s){s->worker=w;published++;}
static dap_stream_t *dap_stream_new_es_client(dap_events_socket_t *s,dap_stream_node_addr_t *n,bool a){(void)s;(void)n;(void)a;return NULL;}
''' + source[start:end] + r'''
int main(void) {
 dap_net_stage_prepare_params_t p={.host="127.0.0.1",.port=8085,.client_ctx=(void*)42,.worker=(void*)1};
 dap_net_stage_prepare_result_t r={.stream=(void*)1,.esocket=(void*)1};
 assert(s_tls_stage_prepare(NULL,&p,&r)==-1);
 assert(r.error_code==ECONNREFUSED && !r.stream && !r.esocket && deleted==1 && !published);
 resolve_fail=1;
 assert(s_tls_stage_prepare(NULL,&p,&r)==-1 && deleted==1 && !published);
 resolve_fail=0;connect_ok=1;
 assert(s_tls_stage_prepare(NULL,&p,&r)==-1);
 assert(deleted==2 && removed==1 && published==1 && !r.stream && !r.esocket);
 p.worker=NULL;
 assert(s_tls_stage_prepare(NULL,&p,&r)==-1);
 assert(deleted==3 && removed==1 && published==1 && !r.stream && !r.esocket);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=pathlib.Path(directory);(tmp/'test.c').write_text(code)
    subprocess.run(['cc','-fsanitize=address,undefined',str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: TLS prepare connect/resolve failures leave no socket or stream result')
