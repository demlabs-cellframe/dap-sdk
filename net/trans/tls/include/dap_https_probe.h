#pragma once
#include <stdint.h>
#include "dap_server.h"
/* Experimental synchronous HTTPS listener. Start before accepting traffic;
 * keep it alive until workers/connections have stopped. No VPN routes. */
dap_server_t *dap_https_probe_start(const char *address, uint16_t port,
                                   const char *certificate, const char *private_key);
/* Call once before events deinit. Existing
 * connections retire through their bounded deadlines; router lives to last ref. */
void dap_https_probe_stop(dap_server_t *server);
