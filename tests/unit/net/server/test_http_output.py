"""Production HTTP body callback: partial writes, backpressure, terminal failure."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/dap_http_simple.c').read_text()
start = text.index('static bool s_http_client_data_write(dap_http_client_t * a_http_client,')
finished = text.index('static void s_http_resume_buffered(')
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "dap_http_output.h"
#define log_it(...) ((void)0)
typedef struct dap_http_client dap_http_client_t;
typedef struct {size_t reply_sent,request_size,reply_size; char *reply_byte; bool close_after_write; dap_http_client_t *http_client;} dap_http_simple_t;
struct dap_http_client {size_t out_content_length; bool keep_alive; dap_http_simple_t simple; dap_http_output_t output; char *pending_headers; int state_read;};
typedef unsigned long dap_events_socket_uuid_t;
typedef struct {void *context;} dap_worker_t;
typedef struct socket {int flags;size_t buf_in_size;dap_events_socket_uuid_t uuid;dap_worker_t *worker;
    dap_http_client_t *client;
    struct {void (*read_callback)(struct socket *,void *);} callbacks;} dap_events_socket_t;
#define DAP_HTTP_CLIENT(s) ((s)->client)
static dap_worker_t owner;
static dap_events_socket_t *live_socket;
static void (*scheduled)(void *); static void *scheduled_arg;
static unsigned reads;
static dap_worker_t *dap_worker_get_current(void) {return &owner;}
static dap_events_socket_t *dap_context_find(void *ctx,dap_events_socket_uuid_t uuid)
{(void)ctx;return live_socket && live_socket->uuid==uuid?live_socket:NULL;}
static void dap_http_client_read(dap_events_socket_t *s,void *arg) {(void)s;(void)arg;++reads;}
static void dap_worker_exec_callback_on(dap_worker_t *w,void (*cb)(void *),void *arg)
{assert(w==&owner);scheduled=cb;scheduled_arg=arg;}
#define DAP_SOCK_SIGNAL_CLOSE 1
#define DAP_HTTP_CLIENT_STATE_START 1
static int writable,readable;
static int drain_result=1;
static int dap_http_client_output_drained(dap_http_client_t *c) {(void)c;return drain_result;}
static void dap_events_socket_set_writable_unsafe(dap_events_socket_t *s,bool value) {(void)s;writable=value;}
static void dap_events_socket_set_readable_unsafe(dap_events_socket_t *s,bool value) {(void)s;readable=value;}
#define DAP_HTTP_SIMPLE(c) (&(c)->simple)
static ssize_t result;
static char received[16]; static size_t count;
static ssize_t sink(void *arg,const void *data,size_t size) {
    (void)arg;
    if (result>0 && (size_t)result<=size) {
        memcpy(received+count,data,(size_t)result); count+=(size_t)result;
    }
    return result;
}
static ssize_t dap_http_client_write_body(dap_http_client_t *c,const void *p,size_t n) {
    return dap_http_output_write(&c->output,p,n);
}
''' + text[finished:text.index('\nstatic void s_esocket_worker_write_callback',finished)] + text[start:text.index('\n\n\n', text.index('\n}', start))] + r'''
int main(void) {
    dap_http_client_t c={.out_content_length=6,.simple={.reply_byte="abcdef"},.output={.write=sink}};
    result=2; assert(s_http_client_data_write(&c,&c.simple)); assert(c.simple.reply_sent==2);
    result=0; assert(s_http_client_data_write(&c,&c.simple)); assert(c.simple.reply_sent==2);
    result=-1; assert(!s_http_client_data_write(&c,&c.simple)); assert(c.simple.reply_sent==2);
    result=5; assert(!s_http_client_data_write(&c,&c.simple)); assert(c.simple.reply_sent==2);
    result=4; assert(!s_http_client_data_write(&c,&c.simple));
    assert(c.simple.reply_sent==6 && c.simple.close_after_write);
    assert(count==6 && !memcmp(received,"abcdef",6));
    dap_events_socket_t socket={0}; c.simple.http_client=&c;
    c.simple.reply_sent=2;
    s_http_client_write_finished(&socket,&c.simple);
    assert(writable && !readable && !socket.flags && c.simple.reply_sent==2);
    c.simple.reply_sent=6; c.pending_headers="pending"; writable=0;
    s_http_client_write_finished(&socket,&c.simple);
    assert(writable && !socket.flags && c.simple.reply_sent==6);
    c.pending_headers=NULL;
    drain_result=0;
    s_http_client_write_finished(&socket,&c.simple);
    assert(!socket.flags && c.simple.reply_sent==6 && !readable);
    drain_result=1;
    s_http_client_write_finished(&socket,&c.simple);
    assert(socket.flags & DAP_SOCK_SIGNAL_CLOSE);
    socket.flags=0; c.simple.close_after_write=false;
    s_http_client_write_finished(&socket,&c.simple);
    assert(!socket.flags && readable && c.state_read==DAP_HTTP_CLIENT_STATE_START);
    assert(!c.simple.reply_sent);
    c.simple.reply_sent=6;socket.buf_in_size=7;socket.uuid=42;socket.worker=&owner;
    socket.client=&c;socket.callbacks.read_callback=dap_http_client_read;live_socket=&socket;
    s_http_client_write_finished(&socket,&c.simple);
    assert(scheduled && !reads);scheduled(scheduled_arg);assert(reads==1);
    c.simple.reply_sent=6;
    s_http_client_write_finished(&socket,&c.simple);
    live_socket=NULL;scheduled(scheduled_arg);assert(reads==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(sdk / 'net/server/http_server/http_client/include'),
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: HTTP body partial output and backpressure')
