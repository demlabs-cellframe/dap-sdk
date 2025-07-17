#pragma once

#include "dap_common.h"

void dap_stream_test_init(void);
void dap_stream_test_run(const char *a_ip_addr_str, uint16_t a_port, size_t a_data_size, int a_pkt_count);

// Static function prototype
static int s_cli_stream_test(int argc, char **argv, char **a_str_reply, int a_version);
