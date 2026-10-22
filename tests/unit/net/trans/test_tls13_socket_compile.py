"""Compile socket adapter with a minimal SDK socket contract (not a live test)."""
import pathlib
import subprocess
import tempfile
sdk = pathlib.Path(__file__).resolve().parents[4]
stub = r'''
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t dap_events_socket_uuid_t;
typedef struct dap_worker dap_worker_t;
typedef struct dap_events_socket dap_events_socket_t;
struct dap_events_socket {
 dap_events_socket_uuid_t uuid; dap_worker_t *worker;
 unsigned flags; size_t buf_out_size,buf_in_size; void *buf_in;
 struct { void *arg; void (*read_callback)(dap_events_socket_t*,void*);
 bool (*write_callback)(dap_events_socket_t*,void*);
 void (*delete_callback)(dap_events_socket_t*,void*);
 void (*write_finished_callback)(dap_events_socket_t*,void*);
 void (*error_callback)(dap_events_socket_t*,int); } callbacks;
};
#define DAP_SOCK_SIGNAL_CLOSE 1
void dap_events_socket_set_writable_unsafe(dap_events_socket_t*,bool);
size_t dap_events_socket_write_unsafe(dap_events_socket_t*,const void*,size_t);
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=pathlib.Path(directory)
    (tmp/'dap_events_socket.h').write_text(stub)
    (tmp/'dap_worker.h').write_text('#pragma once\n#include "dap_events_socket.h"\nstruct dap_worker { void *context; };\ndap_worker_t *dap_worker_get_current(void);\nvoid dap_worker_exec_callback_on(dap_worker_t*,void (*)(void*),void*);\n')
    (tmp/'dap_context.h').write_text('#pragma once\n#include "dap_events_socket.h"\ndap_events_socket_t *dap_context_find(void*,dap_events_socket_uuid_t);\n')
    (tmp/'test.c').write_text('#include "dap_tls13_connection.h"\nint bind_test(dap_tls13_socket_t *s,dap_events_socket_t *es,dap_tls13_channel_t *c){return dap_tls13_socket_bind(s,es,c);}\n')
    subprocess.run(['cc','-std=c11','-DDAP_OS_UNIX','-Werror=incompatible-pointer-types',
                    '-I'+str(tmp),'-I'+str(sdk/'crypto/tls/include'),'-I'+str(sdk/'net/trans/tls'),
                    '-fsyntax-only',str(tmp/'test.c')],check=True)
print('PASS: TLS13 socket adapter syntax and callback signatures (stub contract)')
