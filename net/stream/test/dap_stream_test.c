#include "dap_client.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_test"

#define DAP_STREAM_CH_CHAIN_NET_PKT_TYPE_TEST 0x99

void dap_stream_test_run(const char *a_ip_addr_str, uint16_t a_port, size_t a_data_size, int a_pkt_count)
{
    byte_t *l_test_data = DAP_NEW_SIZE(byte_t, a_data_size);
    randombytes(l_test_data, a_data_size);
    char *l_data_hash_str;
    dap_get_data_hash_str_static(l_test_data, a_data_size, l_data_hash_str);
    log_it(L_ATT, "Prepare test data packet with hash %s", l_data_hash_str);
    dap_client_t *l_client = dap_client_new(NULL, NULL, NULL);
    dap_client_set_uplink_unsafe(l_client, a_ip_addr_str, a_port);
    dap_client_set_active_channels_unsafe(l_client, "N");
    l_client->connect_on_demand = true;
    // Handshake & connect
    dap_client_go_stage(l_client, STAGE_STREAM_STREAMING, NULL);
    for (int i = 0; i < a_pkt_count; i++)
        dap_client_write_mt(l_client, 'N', DAP_STREAM_CH_CHAIN_NET_PKT_TYPE_TEST, l_test_data, a_data_size);
}
