#pragma once
#include <stddef.h>
#include <sys/types.h>
struct dap_events_socket;
ssize_t dap_net_trans_tls_server_write_unsafe(struct dap_events_socket *a_es,
                                             const void *a_data, size_t a_size);

int dap_net_trans_tls_server_init(void);
void dap_net_trans_tls_server_deinit(void);
