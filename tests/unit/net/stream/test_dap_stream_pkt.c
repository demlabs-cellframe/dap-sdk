#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_stream_pkt.h"
#include "dap_stream.h"
#include "dap_enc_key.h"
#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_net_trans_ctx.h"

// Mock state
static void *s_last_write_data = NULL;
static size_t s_last_write_size = 0;

// Declare mock
DAP_MOCK_DECLARE(dap_events_socket_write_unsafe);

// Mock for dap_events_socket_write_unsafe
DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_events_socket_write_unsafe,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(const void*, a_data),
    PARAM(size_t, a_data_size)
)
{
    UNUSED(a_es);
    if (s_last_write_data) DAP_DELETE(s_last_write_data);
    s_last_write_data = DAP_DUP_SIZE(a_data, a_data_size);
    s_last_write_size = a_data_size;
    return a_data_size;
}

// Test case: Write RAW (no session)
void test_write_raw()
{
    dap_stream_t l_stream = {0};
    dap_net_trans_ctx_t l_trans_ctx = {0};
    l_trans_ctx.esocket = (dap_events_socket_t*)0x12345; // Dummy address
    l_stream.trans_ctx = &l_trans_ctx;
    l_stream.session = NULL;
    l_stream.node.uint64 = 1; // Dummy node addr
    
    char l_data[] = "Hello World";
    size_t l_data_size = strlen(l_data) + 1;
    
    dap_stream_pkt_write_unsafe(&l_stream, 'A', l_data, l_data_size);
    
    TEST_ASSERT(s_last_write_size == sizeof(dap_stream_pkt_hdr_t) + l_data_size, "Size mismatch");
    
    dap_stream_pkt_hdr_t *l_hdr = (dap_stream_pkt_hdr_t*)s_last_write_data;
    TEST_ASSERT(l_hdr->size == l_data_size, "Header size mismatch");
    TEST_ASSERT(memcmp(s_last_write_data + sizeof(dap_stream_pkt_hdr_t), l_data, l_data_size) == 0, "Data mismatch");
}

// Test case: Write Encrypted
void test_write_encrypted()
{
    dap_stream_t l_stream = {0};
    dap_net_trans_ctx_t l_trans_ctx = {0};
    l_trans_ctx.esocket = (dap_events_socket_t*)0x12345;
    l_stream.trans_ctx = &l_trans_ctx;
    dap_stream_session_t l_session = {0};
    dap_enc_key_t *l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SALSA2012, NULL, 0, NULL, 0, 32);
    l_session.key = l_key;
    l_stream.session = &l_session;
    l_stream.node.uint64 = 1;

    char l_data[] = "Secret Data";
    size_t l_data_size = strlen(l_data) + 1;

    dap_stream_pkt_write_unsafe(&l_stream, 'B', l_data, l_data_size);

    dap_stream_pkt_hdr_t *l_hdr = (dap_stream_pkt_hdr_t*)s_last_write_data;
    // Decrypt to verify
    
    char l_dec_buf[1024];
    size_t l_dec_size = l_key->dec_na(l_key, s_last_write_data + sizeof(dap_stream_pkt_hdr_t), l_hdr->size, l_dec_buf, sizeof(l_dec_buf));
    
    TEST_ASSERT(l_dec_size == l_data_size, "Decrypted size mismatch");
    TEST_ASSERT(memcmp(l_dec_buf, l_data, l_data_size) == 0, "Decrypted data mismatch");

    dap_enc_key_delete(l_key);
}

// Test case: Read RAW
void test_read_raw()
{
    dap_stream_t l_stream = {0};
    l_stream.session = NULL;
    
    char l_data[] = "Incoming Raw";
    size_t l_data_size = strlen(l_data) + 1;
    
    // Construct packet
    size_t l_pkt_size = sizeof(dap_stream_pkt_hdr_t) + l_data_size;
    dap_stream_pkt_t *l_pkt = DAP_NEW_SIZE(dap_stream_pkt_t, l_pkt_size);
    l_pkt->hdr.size = l_data_size;
    memcpy(l_pkt->data, l_data, l_data_size);
    
    char l_out_buf[1024];
    size_t l_read = dap_stream_pkt_read_unsafe(&l_stream, l_pkt, l_out_buf, sizeof(l_out_buf));
    
    TEST_ASSERT(l_read == l_data_size, "Read size mismatch");
    TEST_ASSERT(memcmp(l_out_buf, l_data, l_data_size) == 0, "Read data mismatch");
    
    DAP_DELETE(l_pkt);
}

int main(int argc, char **argv) {
    UNUSED(argc); UNUSED(argv);
    TEST_SUITE_START("test_dap_stream_pkt");
    TEST_RUN(test_write_raw);
    TEST_RUN(test_write_encrypted);
    TEST_RUN(test_read_raw);
    TEST_SUITE_END();
    return 0;
}