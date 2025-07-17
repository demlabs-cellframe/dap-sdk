#include "../include/python_cellframe_common.h"
int dap_network_init(void) { return 0; }
void dap_network_deinit(void) {}
void* dap_client_new(void) { return (void*)1; }
void dap_client_delete(void* client) {}
int dap_client_connect_to(void* client, const char* addr, int port) { return 0; }
void dap_client_disconnect(void* client) {}
int dap_client_write(void* client, const void* data, size_t size) { return size; }
int dap_client_read(void* client, void* buffer, size_t size) { return 0; }
void* dap_server_new(void) { return (void*)1; }
void dap_server_delete(void* server) {}
int dap_server_listen(void* server, const char* addr, int port) { return 0; }
void dap_server_stop(void* server) {}
