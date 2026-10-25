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

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    if (dap_common_init("https_probe_runner", NULL)) return 3;
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
