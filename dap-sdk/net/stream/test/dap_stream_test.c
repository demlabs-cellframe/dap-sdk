#include "dap_stream_test.h"
#include "dap_client.h"
#include "rand/dap_rand.h"
#include "dap_cli_server.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_stream_test"

#define DAP_STREAM_CH_CHAIN_NET_PKT_TYPE_TEST 0x99

static int s_cli_stream_test(int argc, char **argv, char **a_str_reply, int a_version)
{
    // This function is provided without any error checking due to debug purpose
    const char *l_addr_str = NULL,
               *l_port_str = NULL,
               *l_size_str = NULL,
               *l_count_str = NULL;
    dap_cli_server_cmd_find_option_val(argv, 1, argc, "-addr", &l_addr_str);
    dap_cli_server_cmd_find_option_val(argv, 1, argc, "-port", &l_port_str);
    dap_cli_server_cmd_find_option_val(argv, 1, argc, "-size", &l_size_str);
    dap_cli_server_cmd_find_option_val(argv, 1, argc, "-count", &l_count_str);
    uint16_t l_port = atoi(l_port_str);
    uint32_t l_size = atoi(l_size_str);
    uint32_t l_count = atoi(l_count_str);

    dap_stream_test_run(l_addr_str, l_port, l_size, l_count);

    *a_str_reply = dap_strdup("Succesfully sent all packets (or not, who knows?)");

    return 0;
}

void dap_stream_test_init(void)
{
    dap_cli_server_cmd_add("stream_test", s_cli_stream_test, "Stream testing command",
        "stream_test -addr <IP> - port <port> -size <packet_size> -count <packet_count>"
            "\tSet up stream connection with channel 'N' to host with specified IP and port"
            " and sends '-count' packets with size '-size'. Data hash of each packet is logged");
}

void dap_stream_test_run(const char *a_ip_addr_str, uint16_t a_port, size_t a_data_size, int a_pkt_count)
{
    dap_client_t *l_client = dap_client_new(NULL, NULL, NULL);
    dap_client_set_uplink_unsafe(l_client, a_ip_addr_str, a_port);
    dap_client_set_active_channels_unsafe(l_client, "N");
    l_client->connect_on_demand = true;
    // Handshake & connect
    dap_client_go_stage(l_client, STAGE_STREAM_STREAMING, NULL);
    for (int i = 0; i < a_pkt_count; i++) {
        byte_t *l_test_data = DAP_NEW_SIZE(byte_t, a_data_size);
        randombytes(l_test_data, a_data_size);
        char l_data_hash_str[DAP_CHAIN_HASH_FAST_STR_SIZE];
        dap_hash_fast_t l_data_hash;
        dap_hash_fast(l_test_data, a_data_size, &l_data_hash); \
        dap_chain_hash_fast_to_str(&l_data_hash, l_data_hash_str, DAP_CHAIN_HASH_FAST_STR_SIZE);
        log_it(L_ATT, "Prepare test data packet with size %zu and hash %s", a_data_size, l_data_hash_str);
        dap_client_write_mt(l_client, 'N', DAP_STREAM_CH_CHAIN_NET_PKT_TYPE_TEST, l_test_data, a_data_size);
        DAP_DELETE(l_test_data);
    }
}
