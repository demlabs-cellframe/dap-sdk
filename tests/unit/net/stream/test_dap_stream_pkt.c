#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_stream_pkt.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_enc_key.h"
#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_net_trans_ctx.h"
#include "dap_server.h"

// Mock state
static void *s_last_write_data = NULL;
static size_t s_last_write_size = 0;
static int s_packet_callback_calls = 0;
static int s_notifier_calls = 0;
static int s_esocket_remove_calls = 0;
static int s_shrink_calls = 0;
static size_t s_last_shrink_size = 0;
static int s_trans_close_calls = 0;

#define TEST_STREAM_CH_ID 'Z'

typedef enum test_delete_mode {
    TEST_DELETE_STREAM,
    TEST_DELETE_ESOCKET
} test_delete_mode_t;

// Declare mock
DAP_MOCK_DECLARE(dap_events_socket_write_unsafe);
DAP_MOCK_DECLARE(dap_events_socket_remove_and_delete_unsafe);
DAP_MOCK_DECLARE(dap_events_socket_shrink_buf_in);

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

DAP_MOCK_WRAPPER_CUSTOM(void, dap_events_socket_remove_and_delete_unsafe,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(bool, a_preserve_inheritor)
)
{
    UNUSED(a_preserve_inheritor);
    s_esocket_remove_calls++;
    if (a_es && a_es->callbacks.delete_callback)
        a_es->callbacks.delete_callback(a_es, a_es->callbacks.arg);
}

DAP_MOCK_WRAPPER_CUSTOM(void, dap_events_socket_shrink_buf_in,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(size_t, a_shrink_size)
)
{
    UNUSED(a_es);
    s_shrink_calls++;
    s_last_shrink_size = a_shrink_size;
}

static void s_reset_write_capture(void)
{
    DAP_DEL_Z(s_last_write_data);
    s_last_write_size = 0;
}

static void s_test_stream_drop_session(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->session)
        return;
    dap_stream_session_t *l_session = a_stream->session;
    a_stream->session = NULL;
    if (l_session->key)
        dap_enc_key_delete(l_session->key);
    DAP_DELETE(l_session);
}

static bool s_packet_delete_stream_callback(dap_stream_ch_t *a_ch, void *a_arg)
{
    UNUSED(a_arg);
    s_packet_callback_calls++;
    s_test_stream_drop_session(a_ch->stream);
    dap_stream_delete_unsafe(a_ch->stream);
    return true;
}

static bool s_packet_accept_callback(dap_stream_ch_t *a_ch, void *a_arg)
{
    UNUSED(a_ch);
    UNUSED(a_arg);
    s_packet_callback_calls++;
    return true;
}

static void s_trans_close_reentrant_delete(dap_stream_t *a_stream)
{
    s_trans_close_calls++;
    dap_stream_delete_unsafe(a_stream);
}

static void s_notifier_delete_callback(dap_stream_ch_t *a_ch, uint8_t a_type, const void *a_data,
                                       size_t a_data_size, void *a_arg)
{
    UNUSED(a_type);
    UNUSED(a_data);
    UNUSED(a_data_size);
    s_notifier_calls++;
    s_test_stream_drop_session(a_ch->stream);
    if ((test_delete_mode_t)(uintptr_t)a_arg == TEST_DELETE_ESOCKET)
        dap_events_socket_remove_and_delete_unsafe(a_ch->stream->esocket, false);
    else
        dap_stream_delete_unsafe(a_ch->stream);
}

static dap_stream_t *s_test_stream_new(dap_events_socket_t *a_es,
                                       dap_stream_ch_read_callback_t a_packet_callback,
                                       bool a_with_trans_ctx)
{
    static dap_stream_ch_proc_t s_proc;
    s_proc = (dap_stream_ch_proc_t) {
        .id = TEST_STREAM_CH_ID,
        .packet_in_callback = a_packet_callback
    };

    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    TEST_ASSERT(l_stream != NULL, "stream allocation failed");

    dap_stream_session_t *l_session = DAP_NEW_Z(dap_stream_session_t);
    TEST_ASSERT(l_session != NULL, "session allocation failed");
    l_session->key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SALSA2012, NULL, 0, NULL, 0, 32);
    TEST_ASSERT(l_session->key != NULL, "session key allocation failed");
    l_stream->session = l_session;
    l_stream->client_last_seq_id_packet = (size_t)-1;
    l_stream->esocket = a_es;
    if (a_es) {
        l_stream->esocket_uuid = a_es->uuid;
        a_es->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    }

    if (a_with_trans_ctx) {
        l_stream->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
        TEST_ASSERT(l_stream->trans_ctx != NULL, "trans_ctx allocation failed");
        l_stream->trans_ctx->stream = l_stream;
        a_es->_inheritor = l_stream->trans_ctx;
    }

    dap_stream_ch_t *l_ch = DAP_NEW_Z(dap_stream_ch_t);
    TEST_ASSERT(l_ch != NULL, "channel allocation failed");
    pthread_mutex_init(&l_ch->mutex, NULL);
    l_ch->stream = l_stream;
    l_ch->proc = &s_proc;
    l_ch->ready_to_read = true;

    l_stream->channel = DAP_NEW_Z(dap_stream_ch_t *);
    TEST_ASSERT(l_stream->channel != NULL, "channel array allocation failed");
    l_stream->channel[0] = l_ch;
    l_stream->channel_count = 1;

    return l_stream;
}

static dap_stream_ch_pkt_t *s_make_ch_pkt(const char *a_data, size_t *a_pkt_size)
{
    size_t l_data_size = strlen(a_data) + 1;
    *a_pkt_size = sizeof(dap_stream_ch_pkt_hdr_t) + l_data_size;
    dap_stream_ch_pkt_t *l_ch_pkt = DAP_NEW_Z_SIZE(dap_stream_ch_pkt_t, *a_pkt_size);
    TEST_ASSERT(l_ch_pkt != NULL, "channel packet allocation failed");
    l_ch_pkt->hdr.id = TEST_STREAM_CH_ID;
    l_ch_pkt->hdr.type = STREAM_CH_PKT_TYPE_REQUEST;
    l_ch_pkt->hdr.data_size = (uint32_t)l_data_size;
    memcpy(l_ch_pkt->data, a_data, l_data_size);
    return l_ch_pkt;
}

static void *s_make_stream_pkt(dap_stream_t *a_stream, uint8_t a_type, const void *a_payload,
                               size_t a_payload_size, size_t *a_pkt_size)
{
    s_reset_write_capture();
    size_t l_written = dap_stream_pkt_write_unsafe(a_stream, a_type, a_payload, a_payload_size);
    TEST_ASSERT(l_written == s_last_write_size && l_written > 0, "stream packet write failed");
    void *l_pkt = DAP_DUP_SIZE(s_last_write_data, s_last_write_size);
    TEST_ASSERT(l_pkt != NULL, "stream packet duplicate failed");
    *a_pkt_size = s_last_write_size;
    s_reset_write_capture();
    return l_pkt;
}

static void *s_make_data_stream_pkt(dap_stream_t *a_stream, const char *a_data, size_t *a_pkt_size)
{
    size_t l_ch_pkt_size = 0;
    dap_stream_ch_pkt_t *l_ch_pkt = s_make_ch_pkt(a_data, &l_ch_pkt_size);
    void *l_pkt = s_make_stream_pkt(a_stream, STREAM_PKT_TYPE_DATA_PACKET, l_ch_pkt, l_ch_pkt_size, a_pkt_size);
    DAP_DELETE(l_ch_pkt);
    return l_pkt;
}

static void *s_make_fragment_stream_pkt(dap_stream_t *a_stream, const uint8_t *a_full_pkt,
                                        uint32_t a_full_size, uint32_t a_shift, uint32_t a_size,
                                        size_t *a_pkt_size)
{
    size_t l_fragment_size = sizeof(dap_stream_fragment_pkt_t) + a_size;
    dap_stream_fragment_pkt_t *l_fragment = DAP_NEW_Z_SIZE(dap_stream_fragment_pkt_t, l_fragment_size);
    TEST_ASSERT(l_fragment != NULL, "fragment allocation failed");
    l_fragment->size = a_size;
    l_fragment->mem_shift = a_shift;
    l_fragment->full_size = a_full_size;
    memcpy(l_fragment->data, a_full_pkt + a_shift, a_size);
    void *l_pkt = s_make_stream_pkt(a_stream, STREAM_PKT_TYPE_FRAGMENT_PACKET,
                                    l_fragment, l_fragment_size, a_pkt_size);
    DAP_DELETE(l_fragment);
    return l_pkt;
}

// Test case: Write RAW (no session)
void test_write_raw()
{
    // Create real mock esocket instead of dummy pointer
    dap_events_socket_t l_mock_esocket = {0};
    l_mock_esocket.type = DESCRIPTOR_TYPE_SOCKET_CLIENT; // Stream-oriented, not datagram
    
    dap_stream_t l_stream = {0};
    dap_net_trans_ctx_t l_trans_ctx = {0};
    l_stream.esocket = &l_mock_esocket;
    l_stream.trans_ctx = &l_trans_ctx;
    l_stream.session = NULL;
    l_stream.node.uint64 = 1;
    
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
    // Create real mock esocket instead of dummy pointer
    dap_events_socket_t l_mock_esocket = {0};
    l_mock_esocket.type = DESCRIPTOR_TYPE_SOCKET_CLIENT; // Stream-oriented, not datagram
    
    dap_stream_t l_stream = {0};
    dap_net_trans_ctx_t l_trans_ctx = {0};
    l_stream.esocket = &l_mock_esocket;
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

void test_packet_callback_delete_stream_is_deferred()
{
    s_packet_callback_calls = 0;
    dap_events_socket_t l_mock_esocket = { .type = DESCRIPTOR_TYPE_SOCKET_CLIENT, .uuid = 0x1001 };
    dap_stream_t *l_stream = s_test_stream_new(&l_mock_esocket, s_packet_delete_stream_callback, false);
    size_t l_pkt_size = 0;
    void *l_pkt = s_make_data_stream_pkt(l_stream, "delete from packet callback", &l_pkt_size);

    bool l_delete_requested = false;
    size_t l_processed = dap_stream_data_proc_read_ext_checked(l_stream, l_pkt, l_pkt_size, &l_delete_requested);

    TEST_ASSERT(l_processed == l_pkt_size, "packet callback delete should consume full packet");
    TEST_ASSERT(l_delete_requested, "packet callback delete should be reported");
    TEST_ASSERT(s_packet_callback_calls == 1, "packet callback should be called once");
    DAP_DELETE(l_pkt);
}

void test_legacy_read_delete_returns_before_final_free()
{
    s_packet_callback_calls = 0;
    dap_events_socket_t l_mock_esocket = { .type = DESCRIPTOR_TYPE_SOCKET_CLIENT, .uuid = 0x1007 };
    dap_stream_t *l_stream = s_test_stream_new(&l_mock_esocket, s_packet_delete_stream_callback, false);
    size_t l_pkt_size = 0;
    void *l_pkt = s_make_data_stream_pkt(l_stream, "legacy delete from packet callback", &l_pkt_size);

    size_t l_processed = dap_stream_data_proc_read_ext(l_stream, l_pkt, l_pkt_size);

    TEST_ASSERT(l_processed == l_pkt_size, "legacy packet callback delete should consume full packet");
    TEST_ASSERT(s_packet_callback_calls == 1, "packet callback should be called once");
    TEST_ASSERT(l_stream->delete_deferred, "legacy read should keep stream allocated with deferred delete");
    TEST_ASSERT(!l_stream->delete_in_progress, "legacy read should not start final delete before return");
    TEST_ASSERT(l_stream->packet_proc_depth == 0, "packet guard should be left before legacy return");
    dap_stream_delete_unsafe(l_stream);
    DAP_DELETE(l_pkt);
}

void test_notifier_delete_stream_is_deferred()
{
    s_packet_callback_calls = 0;
    s_notifier_calls = 0;
    dap_stream_ch_notifier_t *l_notifier = DAP_NEW_Z(dap_stream_ch_notifier_t);
    TEST_ASSERT(l_notifier != NULL, "notifier allocation failed");
    *l_notifier = (dap_stream_ch_notifier_t) {
        .callback = s_notifier_delete_callback,
        .arg = (void *)(uintptr_t)TEST_DELETE_STREAM
    };

    dap_events_socket_t l_mock_esocket = { .type = DESCRIPTOR_TYPE_SOCKET_CLIENT, .uuid = 0x1002 };
    dap_stream_t *l_stream = s_test_stream_new(&l_mock_esocket, s_packet_accept_callback, false);
    l_stream->channel[0]->packet_in_notifiers = dap_list_append(NULL, l_notifier);
    size_t l_pkt_size = 0;
    void *l_pkt = s_make_data_stream_pkt(l_stream, "delete from notifier", &l_pkt_size);

    bool l_delete_requested = false;
    size_t l_processed = dap_stream_data_proc_read_ext_checked(l_stream, l_pkt, l_pkt_size, &l_delete_requested);

    TEST_ASSERT(l_processed == l_pkt_size, "notifier delete should consume full packet");
    TEST_ASSERT(l_delete_requested, "notifier delete should be reported");
    TEST_ASSERT(s_packet_callback_calls == 1, "packet callback should be called once before notifier");
    TEST_ASSERT(s_notifier_calls == 1, "notifier should be called once");
    DAP_DELETE(l_pkt);
}

void test_notifier_esocket_delete_uses_stream_guard()
{
    s_packet_callback_calls = 0;
    s_notifier_calls = 0;
    s_esocket_remove_calls = 0;
    dap_stream_ch_notifier_t *l_notifier = DAP_NEW_Z(dap_stream_ch_notifier_t);
    TEST_ASSERT(l_notifier != NULL, "notifier allocation failed");
    *l_notifier = (dap_stream_ch_notifier_t) {
        .callback = s_notifier_delete_callback,
        .arg = (void *)(uintptr_t)TEST_DELETE_ESOCKET
    };

    dap_server_t l_server = {0};
    dap_stream_add_proc_udp(&l_server);
    dap_events_socket_t l_mock_esocket = { .type = DESCRIPTOR_TYPE_SOCKET_CLIENT, .uuid = 0x1003 };
    l_mock_esocket.callbacks = l_server.client_callbacks;
    dap_stream_t *l_stream = s_test_stream_new(&l_mock_esocket, s_packet_accept_callback, true);
    l_stream->channel[0]->packet_in_notifiers = dap_list_append(NULL, l_notifier);
    size_t l_pkt_size = 0;
    void *l_pkt = s_make_data_stream_pkt(l_stream, "delete esocket from notifier", &l_pkt_size);

    bool l_delete_requested = false;
    size_t l_processed = dap_stream_data_proc_read_ext_checked(l_stream, l_pkt, l_pkt_size, &l_delete_requested);

    TEST_ASSERT(l_processed == l_pkt_size, "esocket delete should consume full packet");
    TEST_ASSERT(l_delete_requested, "esocket delete should be reported");
    TEST_ASSERT(s_packet_callback_calls == 1, "packet callback should be called once before esocket delete");
    TEST_ASSERT(s_notifier_calls == 1, "notifier should be called once");
    TEST_ASSERT(s_esocket_remove_calls == 1, "esocket remove callback should be called once");
    TEST_ASSERT(l_mock_esocket._inheritor == NULL, "esocket inheritor should be detached by delete callback");
    DAP_DELETE(l_pkt);
}

void test_fragmented_packet_callback_delete_stream_is_deferred()
{
    s_packet_callback_calls = 0;
    dap_events_socket_t l_mock_esocket = { .type = DESCRIPTOR_TYPE_SOCKET_CLIENT, .uuid = 0x1004 };
    dap_stream_t *l_stream = s_test_stream_new(&l_mock_esocket, s_packet_delete_stream_callback, false);

    size_t l_ch_pkt_size = 0;
    dap_stream_ch_pkt_t *l_ch_pkt = s_make_ch_pkt("delete from fragmented packet", &l_ch_pkt_size);
    uint32_t l_first_size = (uint32_t)(l_ch_pkt_size / 2);
    uint32_t l_second_size = (uint32_t)(l_ch_pkt_size - l_first_size);
    size_t l_pkt1_size = 0, l_pkt2_size = 0;
    void *l_pkt1 = s_make_fragment_stream_pkt(l_stream, (const uint8_t*)l_ch_pkt,
                                              (uint32_t)l_ch_pkt_size, 0, l_first_size, &l_pkt1_size);
    void *l_pkt2 = s_make_fragment_stream_pkt(l_stream, (const uint8_t*)l_ch_pkt,
                                              (uint32_t)l_ch_pkt_size, l_first_size, l_second_size, &l_pkt2_size);
    DAP_DELETE(l_ch_pkt);

    bool l_delete_requested = false;
    size_t l_processed1 = dap_stream_data_proc_read_ext_checked(l_stream, l_pkt1, l_pkt1_size, &l_delete_requested);
    TEST_ASSERT(l_processed1 == l_pkt1_size, "first fragment should be consumed");
    TEST_ASSERT(!l_delete_requested, "first fragment should not delete stream");
    TEST_ASSERT(s_packet_callback_calls == 0, "packet callback should not run before reassembly completes");
    DAP_DELETE(l_pkt1);

    l_delete_requested = false;
    size_t l_processed2 = dap_stream_data_proc_read_ext_checked(l_stream, l_pkt2, l_pkt2_size, &l_delete_requested);
    TEST_ASSERT(l_processed2 == l_pkt2_size, "second fragment should be consumed");
    TEST_ASSERT(l_delete_requested, "fragmented callback delete should be reported");
    TEST_ASSERT(s_packet_callback_calls == 1, "packet callback should run once after reassembly");
    DAP_DELETE(l_pkt2);
}

void test_delete_reentrant_close_is_idempotent()
{
    s_trans_close_calls = 0;
    dap_events_socket_t l_mock_esocket = { .type = DESCRIPTOR_TYPE_SOCKET_CLIENT, .uuid = 0x1005 };
    dap_stream_t *l_stream = s_test_stream_new(&l_mock_esocket, s_packet_accept_callback, false);
    s_test_stream_drop_session(l_stream);

    dap_net_trans_ops_t l_ops = {
        .close = s_trans_close_reentrant_delete
    };
    dap_net_trans_t l_trans = {
        .ops = &l_ops
    };
    l_stream->trans = &l_trans;

    dap_stream_delete_unsafe(l_stream);

    TEST_ASSERT(s_trans_close_calls == 1, "trans close should be called once despite reentrant delete");
}

void test_esocket_data_read_delete_skips_shrink()
{
    s_packet_callback_calls = 0;
    s_notifier_calls = 0;
    s_esocket_remove_calls = 0;
    s_shrink_calls = 0;
    s_last_shrink_size = 0;

    dap_stream_ch_notifier_t *l_notifier = DAP_NEW_Z(dap_stream_ch_notifier_t);
    TEST_ASSERT(l_notifier != NULL, "notifier allocation failed");
    *l_notifier = (dap_stream_ch_notifier_t) {
        .callback = s_notifier_delete_callback,
        .arg = (void *)(uintptr_t)TEST_DELETE_ESOCKET
    };

    dap_server_t l_server = {0};
    dap_stream_add_proc_udp(&l_server);
    dap_events_socket_t l_mock_esocket = { .type = DESCRIPTOR_TYPE_SOCKET_CLIENT, .uuid = 0x1006 };
    l_mock_esocket.callbacks = l_server.client_callbacks;
    dap_stream_t *l_stream = s_test_stream_new(&l_mock_esocket, s_packet_accept_callback, true);
    l_stream->channel[0]->packet_in_notifiers = dap_list_append(NULL, l_notifier);
    size_t l_pkt_size = 0;
    void *l_pkt = s_make_data_stream_pkt(l_stream, "delete esocket during read", &l_pkt_size);
    l_mock_esocket.buf_in = l_pkt;
    l_mock_esocket.buf_in_size = l_pkt_size;

    int l_ret = -1;
    l_mock_esocket.callbacks.read_callback(&l_mock_esocket, &l_ret);

    TEST_ASSERT(l_ret == (int)l_pkt_size, "esocket read should report consumed packet");
    TEST_ASSERT(s_packet_callback_calls == 1, "packet callback should be called once before esocket delete");
    TEST_ASSERT(s_notifier_calls == 1, "notifier should be called once");
    TEST_ASSERT(s_esocket_remove_calls == 1, "esocket remove callback should be called once");
    TEST_ASSERT(s_shrink_calls == 0, "delete during read must skip input buffer shrink");
    TEST_ASSERT(s_last_shrink_size == 0, "no shrink size should be recorded");
    TEST_ASSERT(l_mock_esocket._inheritor == NULL, "esocket inheritor should be detached by delete callback");
    DAP_DELETE(l_pkt);
}

int main(int argc, char **argv) {
    UNUSED(argc); UNUSED(argv);
    TEST_SUITE_START("test_dap_stream_pkt");
    TEST_RUN(test_write_raw);
    TEST_RUN(test_write_encrypted);
    TEST_RUN(test_read_raw);
    TEST_RUN(test_packet_callback_delete_stream_is_deferred);
    TEST_RUN(test_legacy_read_delete_returns_before_final_free);
    TEST_RUN(test_notifier_delete_stream_is_deferred);
    TEST_RUN(test_notifier_esocket_delete_uses_stream_guard);
    TEST_RUN(test_fragmented_packet_callback_delete_stream_is_deferred);
    TEST_RUN(test_delete_reentrant_close_is_idempotent);
    TEST_RUN(test_esocket_data_read_delete_skips_shrink);
    s_reset_write_capture();
    TEST_SUITE_END();
    return 0;
}
