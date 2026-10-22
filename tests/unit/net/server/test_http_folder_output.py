"""Production file output preserves bytes across short writes and backpressure."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
text = (sdk / 'net/server/http_server/dap_http_folder.c').read_text()
start = text.index('bool dap_http_folder_data_write(dap_http_client_t * cl_ht, void * arg)')
cleanup = text.index('static void s_http_folder_delete(')
code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define DAP_SOCK_SIGNAL_CLOSE 1
typedef struct {int flags;} socket_t;
typedef struct {FILE *fd;size_t position;unsigned char pending[16384];size_t pending_size,pending_offset;} dap_http_file_t;
typedef struct {socket_t *esocket;bool keep_alive;dap_http_file_t file;} dap_http_client_t;
#define DAP_HTTP_FILE(c) (&(c)->file)
static size_t received;
static int blocked;
static ssize_t dap_http_client_write_body(dap_http_client_t *c,const void *data,size_t size) {
    (void)c;
    if(blocked) return 0;
    if(size>37) size=37;
    const unsigned char *p=data;
    for(size_t i=0;i<size;i++) assert(p[i]==(unsigned char)((received+i)%251));
    received+=size; return (ssize_t)size;
}
''' + text[cleanup:text.index('\nvoid dap_http_folder_headers_read(', cleanup)] + text[start:] + r'''
int main(void) {
    const size_t sizes[]={0,13,16384,32771};
    for(size_t k=0;k<sizeof(sizes)/sizeof(*sizes);k++) {
        socket_t socket={0}; dap_http_client_t c={.esocket=&socket};
        c.file.fd=tmpfile(); assert(c.file.fd);
        for(size_t i=0;i<sizes[k];i++) assert(fputc((int)(i%251),c.file.fd)!=EOF);
        rewind(c.file.fd); received=0; blocked=1;
        bool pending=dap_http_folder_data_write(&c,NULL);
        assert(!received && !c.file.position);
        if(sizes[k]) assert(pending && !socket.flags);
        blocked=0;
        for(unsigned turns=0;pending && turns<2000;turns++)
            pending=dap_http_folder_data_write(&c,NULL);
        assert(!pending && !c.file.fd && received==sizes[k] && c.file.position==sizes[k]);
        assert(socket.flags & DAP_SOCK_SIGNAL_CLOSE);
        s_http_folder_delete(&c,NULL); /* Already closed: idempotent. */
    }
    socket_t socket={0}; dap_http_client_t c={.esocket=&socket};
    c.file.fd=tmpfile(); assert(c.file.fd);
    int descriptor=fileno(c.file.fd);
    assert(fputs("pending response",c.file.fd)>=0); rewind(c.file.fd);
    blocked=1;
    assert(dap_http_folder_data_write(&c,NULL));
    assert(c.file.pending_size && c.file.fd);
    s_http_folder_delete(&c,NULL); /* Disconnect while output is blocked. */
    assert(!c.file.fd);
    errno=0; assert(fcntl(descriptor,F_GETFD)==-1 && errno==EBADF);
    s_http_folder_delete(&c,NULL);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / 'test.c'
    exe = pathlib.Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: production file response, bounded chunks, blocked/partial output')
