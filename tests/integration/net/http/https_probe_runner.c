/* Standalone production reactor/listener runner. No network mocks. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_http_server.h"
#include "dap_https_probe.h"
#include "../../../../net/trans/websocket/dap_ws_accept.h"
#include "../../../../net/trans/tls/dap_wss_dap.h"
#include "../../../../net/trans/tls/dap_wss_server_session.h"
#include "../../../../net/trans/tls/dap_wss_client_session.h"
#include "../../../../net/trans/tls/dap_wss_client_upgrade.h"

static unsigned upgrade_ready, upgrade_tail;
static int s_upgrade_write(void *arg,const void *data,size_t size)
{
    (void)arg;
    return size>20 && !memcmp(data,"GET /vpn HTTP/1.1\r\n",19) ? 0 : -1;
}
static int s_upgrade_ready(void *arg) {(void)arg; ++upgrade_ready; return 0;}
static int s_upgrade_tail(void *arg,const void *data,size_t size)
{
    (void)arg;
    if (upgrade_ready!=1 || size!=4 || memcmp(data,"tail",4)) return -1;
    ++upgrade_tail;return 0;
}
static int s_test_upgrade(void)
{
    dap_wss_client_upgrade_t u={0};
    const char reply[]="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\ntail";
    int rc=dap_wss_client_upgrade_start(&u,"localhost","/vpn","dGhlIHNhbXBsZSBub25jZQ==",s_upgrade_write,NULL);
    for (size_t i=0; !rc && i<sizeof(reply)-6; ++i)
        rc=dap_wss_client_upgrade_feed(&u,reply+i,1,s_upgrade_ready,s_upgrade_tail,NULL);
    if (!rc) rc=dap_wss_client_upgrade_feed(&u,reply+sizeof(reply)-6,5,s_upgrade_ready,s_upgrade_tail,NULL);
    if (!rc && (upgrade_ready!=1 || upgrade_tail!=1)) rc=-1;
    dap_wss_client_upgrade_clear(&u);
    return rc;
}

static unsigned s_control_calls, s_data_calls, s_reply_calls;
static int s_control(void *arg, const void *data, size_t size, void **out, size_t *length)
{
    (void)arg;
    if (size != 4 || memcmp(data, "test", 4)) return -1;
    *out = malloc(4); if (!*out) return -1;
    memcpy(*out, "test", 4); *length = 4; ++s_control_calls; return 0;
}
static int s_data(void *arg, const void *data, size_t size)
{
    (void)arg;
    if (size != 4 || memcmp(data, "test", 4)) return -1;
    ++s_data_calls; return 0;
}
static int s_reply(void *arg, const void *data, size_t size)
{
    (void)arg;
    if (size != 4 || memcmp(data, "test", 4)) return -1;
    ++s_reply_calls; return 0;
}
static int s_codec_wire(void *arg, const void *data, size_t size)
{
    for (size_t i=0; i<size; ++i)
        if (dap_wss_dap_input(arg, (const char *)data+i, 1)) return -1;
    return 0;
}
static int s_test_wss_session(void)
{
    dap_wss_client_session_t client={0}; dap_wss_server_session_t server={0};
    int rc=dap_wss_client_session_init(&client,s_reply,s_reply,s_reply,NULL,s_codec_wire,&server.wire);
    if (!rc) rc=dap_wss_server_session_init(&server,s_control,s_control,s_data,NULL,s_codec_wire,&client.wire);
    if (!rc && !dap_wss_server_session_write(&server,"test",4)) rc=-1;
    if (!rc && !dap_wss_client_session_send(&client,DAP_TLS13_STREAM,"test",4)) rc=-1;
    if (!rc) rc=dap_wss_client_session_send(&client,DAP_TLS13_ENC_REQUEST,"test",4);
    if (!rc && client.state.phase != DAP_TLS13_ENC_DONE) rc=-1;
    if (!rc) rc=dap_wss_client_session_send(&client,DAP_TLS13_CTL_REQUEST,"test",4);
    if (!rc && client.state.phase != DAP_TLS13_STREAMING) rc=-1;
    if (!rc) rc=dap_wss_client_session_send(&client,DAP_TLS13_STREAM,"test",4);
    if (!rc) rc=dap_wss_server_session_write(&server,"test",4);
    if (!rc && (s_control_calls!=2 || s_data_calls!=1 || s_reply_calls!=3)) rc=-1;
    dap_wss_client_session_clear(&client); dap_wss_server_session_clear(&server);
    return rc;
}

typedef struct { dap_wss_dap_t codec; unsigned delivered; } test_peer_t;
static int s_test_dispatch(void *arg, uint8_t type, const void *data, size_t size)
{
    test_peer_t *p = arg;
    if (type != DAP_TLS13_STREAM || size != 4 || memcmp(data, "test", 4)) return -1;
    ++p->delivered;
    return 0;
}
static int s_test_wire(void *arg, const void *data, size_t size)
{
    test_peer_t *p = arg;
    for (size_t i = 0; i < size; ++i)
        if (dap_wss_dap_input(&p->codec, (const char *)data + i, 1)) return -1;
    return 0;
}
static int s_test_wss_dap(void)
{
    test_peer_t client = {0}, server = {0};
    int rc = dap_wss_dap_init(&client.codec, false, s_test_dispatch, &client, s_test_wire, &server);
    if (!rc) rc = dap_wss_dap_init(&server.codec, true, s_test_dispatch, &server, s_test_wire, &client);
    if (!rc) rc = dap_wss_dap_send(&client.codec, DAP_TLS13_STREAM, "test", 4);
    if (!rc) rc = dap_wss_dap_send(&server.codec, DAP_TLS13_STREAM, "test", 4);
    if (!rc && (client.delivered != 1 || server.delivered != 1)) rc = -1;
    dap_wss_dap_clear(&client.codec); dap_wss_dap_clear(&server.codec);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    if (dap_common_init("https_probe_runner", NULL)) return 3;
    if (s_test_wss_dap()) return 10;
    if (s_test_wss_session()) return 11;
    if (s_test_upgrade()) return 12;
    char accept[29];
    if (dap_ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept)) ||
        strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")) return 9;
    if (dap_config_init(argv[1])) return 4;
    if (dap_events_init(2, 30000)) return 5;
    if (dap_events_start()) return 6;
    if (dap_http_init()) return 7;
    dap_server_t *server = dap_https_probe_start("127.0.0.1", (uint16_t)atoi(argv[4]), argv[2], argv[3]);
    if (!server) return 8;
    puts("HTTPS_READY"); fflush(stdout);
    /* The test driver closes stdin after its requests complete. */
    getchar();
    dap_https_probe_stop(server);
    puts("HTTPS_STOPPED"); fflush(stdout);
    /* Keep workers alive while the driver checks accepted connections. */
    getchar();
    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();
    dap_config_deinit();
    return 0;
}
