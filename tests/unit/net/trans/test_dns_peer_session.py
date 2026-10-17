#!/usr/bin/env python3
"""Run production DNS endpoint/session predicates with ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile

sdk = Path(__file__).resolve().parents[4]
text = (sdk / 'net/trans/dns/dap_net_trans_dns_stream.c').read_text()
start = text.index('static bool s_dns_same_peer(')
end = text.index('static void s_dns_client_read_cb(', start)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
enum { DAP_DNS_TUNNEL_MSG_POLL=1, DAP_DNS_TUNNEL_MSG_HANDSHAKE=2, DATA=3 };
typedef struct { uint64_t client_nonce, session_cookie; bool handshake_done; void *handshake_cb; } dns_client_priv_t;
typedef struct { uint64_t client_nonce, session_cookie; int type; } dap_dns_tunnel_frame_t;
'''
main = r'''
int main(void) {
    struct sockaddr_storage a={0}, b={0};
    struct sockaddr_in *v4=(void *)&a;
    v4->sin_family=AF_INET; v4->sin_port=htons(53); v4->sin_addr.s_addr=htonl(0x7f000001);
    b=a; assert(s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    ((struct sockaddr_in *)&b)->sin_zero[0]=99;
    assert(s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    ((struct sockaddr_in *)&b)->sin_port=htons(54);
    assert(!s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    b=a; ((struct sockaddr_in *)&b)->sin_addr.s_addr++;
    assert(!s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    assert(!s_dns_same_peer(&a,1,&a,sizeof(a)));
    memset(&a,0,sizeof(a)); struct sockaddr_in6 *v6=(void *)&a;
    v6->sin6_family=AF_INET6; v6->sin6_port=htons(53); v6->sin6_scope_id=2;
    b=a; assert(s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    ((struct sockaddr_in6 *)&b)->sin6_flowinfo=1;
    assert(s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    ((struct sockaddr_in6 *)&b)->sin6_scope_id=3;
    assert(!s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    b=a; ((struct sockaddr_in6 *)&b)->sin6_addr.s6_addr[15]=1;
    assert(!s_dns_same_peer(&a,sizeof(a),&b,sizeof(b)));
    dns_client_priv_t c={.client_nonce=42,.handshake_cb=&a};
    dap_dns_tunnel_frame_t f={.client_nonce=42,.type=DAP_DNS_TUNNEL_MSG_POLL};
    assert(s_dns_response_session_matches(&c,&f));
    f.client_nonce=41; assert(!s_dns_response_session_matches(&c,&f)); f.client_nonce=42;
    f.session_cookie=99; assert(!s_dns_response_session_matches(&c,&f));
    f.type=DAP_DNS_TUNNEL_MSG_HANDSHAKE; assert(s_dns_response_session_matches(&c,&f));
    f.type=DATA; assert(!s_dns_response_session_matches(&c,&f));
    c.session_cookie=99; c.handshake_done=true; c.handshake_cb=NULL;
    assert(s_dns_response_session_matches(&c,&f));
    f.session_cookie=100; assert(!s_dns_response_session_matches(&c,&f));
    f.session_cookie=0; assert(!s_dns_response_session_matches(&c,&f));
    c.session_cookie=0; assert(!s_dns_response_session_matches(&c,&f));
    puts("PASS: DNS IPv4/IPv6 peer, nonce, bootstrap and established cookie validation");
}
'''
with tempfile.TemporaryDirectory(prefix='dns-peer-') as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(prefix + text[start:end] + main)
    subprocess.run([os.environ.get('CC', 'cc'), '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
