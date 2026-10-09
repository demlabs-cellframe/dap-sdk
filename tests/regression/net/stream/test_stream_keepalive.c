/* Compile the production implementation, including its private packet dispatcher. */
#include "../../../../net/stream/stream/dap_stream.c"

/* Unrelated parser branches must never run in this control-packet fixture. */
enum dap_log_level g_dap_log_level = L_CRITICAL;
const char *c_error_sanity_check = "sanity";
const char *c_error_memory_alloc = "allocation";
const uint8_t c_dap_stream_sig[8] = {0xa0, 0x95, 0x96, 0xa9, 0x9e, 0x5c, 0xfb, 0xfa};
void _log_it_tag(enum dap_log_level l, const char *t, const char *f, ...) {}
dap_worker_t *dap_worker_get_current(void) { return NULL; }
dap_events_socket_t *dap_context_find(dap_context_t *c, dap_events_socket_uuid_t u) { abort(); }
void dap_timerfd_reset_unsafe(dap_timerfd_t *t) { abort(); }
size_t dap_events_socket_write_unsafe(dap_events_socket_t *e, const void *d, size_t n) { abort(); }
void dap_events_socket_set_readable_unsafe(dap_events_socket_t *e, bool b) { abort(); }
void dap_events_socket_set_writable_unsafe(dap_events_socket_t *e, bool b) { abort(); }
size_t dap_enc_decode_out_size(dap_enc_key_t *k, size_t n, dap_enc_data_type_t t) { return n; }
size_t dap_stream_pkt_read_unsafe(dap_stream_t *s, dap_stream_pkt_t *p, void *b, size_t n)
{ memcpy(b, p->data, n); return n; }
dap_stream_session_t *dap_stream_session_id_mt(uint32_t id) { abort(); }
int dap_stream_session_open(dap_stream_session_t *s) { abort(); }
dap_stream_ch_t *dap_stream_ch_new(dap_stream_t *s, uint8_t id) { abort(); }
dap_cluster_member_t *dap_cluster_member_add(dap_cluster_t *c, dap_stream_node_addr_t *a, int r, void *i) { abort(); }
int dap_link_manager_stream_add(dap_stream_node_addr_t *a, bool u) { abort(); }
char *dap_strncpy(char *d, const char *s, size_t n) { abort(); }
uint64_t dap_list_length(dap_list_t *l) { return 0; }
static bool accept_channel;
static bool channel_read(dap_stream_ch_t *ch, void *packet) { return accept_channel; }

static dap_stream_t *s_expected;
static unsigned s_writes;
static ssize_t s_result;

static ssize_t s_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    assert(a_stream == s_expected);
    assert(a_size == sizeof(dap_stream_pkt_hdr_t));
    const dap_stream_pkt_hdr_t *l_hdr = a_data;
    assert(l_hdr->type == STREAM_PKT_TYPE_ALIVE && l_hdr->size == 0);
    assert(!memcmp(l_hdr->sig, c_dap_stream_sig, sizeof(c_dap_stream_sig)));
    ++s_writes;
    return s_result;
}

int main(void)
{
    uint8_t buffer[sizeof(dap_stream_pkt_hdr_t) + sizeof(dap_stream_fragment_pkt_t) + 1] = {0};
    dap_stream_pkt_t *packet = (dap_stream_pkt_t *)buffer;
    memcpy(packet->hdr.sig, c_dap_stream_sig, sizeof(c_dap_stream_sig));
    dap_enc_key_t key = {0};
    dap_stream_session_t session = {.key = &key};
    dap_stream_t stream = {.session = &session};
    bool accepted;
    const uint8_t types[] = {STREAM_PKT_TYPE_SERVICE_PACKET, STREAM_PKT_TYPE_DATA_PACKET,
                            STREAM_PKT_TYPE_FRAGMENT_PACKET};
    for (size_t i = 0; i < sizeof(types); i++) {
        packet->hdr.type = types[i];
        packet->hdr.size = 1;
        assert(dap_stream_data_proc_read_ext_validated(&stream, buffer,
            sizeof(dap_stream_pkt_hdr_t) + 1, &accepted) == sizeof(dap_stream_pkt_hdr_t) + 1);
        assert(!accepted);
        packet->hdr.size = 0;
        assert(dap_stream_data_proc_read_ext(&stream, buffer, sizeof(dap_stream_pkt_hdr_t)) == sizeof(dap_stream_pkt_hdr_t));
    }
    packet->hdr.type = STREAM_PKT_TYPE_FRAGMENT_PACKET;
    packet->hdr.size = sizeof(dap_stream_fragment_pkt_t) + 1;
    dap_stream_fragment_pkt_t *fragment = (dap_stream_fragment_pkt_t *)packet->data;
    fragment->size = 1;
    fragment->full_size = 2;
    assert(dap_stream_data_proc_read_ext_validated(&stream, buffer, sizeof(buffer), &accepted) == sizeof(buffer));
    assert(accepted && stream.buf_fragments_size_filled == 1);
    /* Repeated position is not progress and clears the incomplete assembly. */
    dap_stream_data_proc_read_ext_validated(&stream, buffer, sizeof(buffer), &accepted);
    assert(!accepted && !stream.buf_fragments);
    uint8_t data[sizeof(dap_stream_pkt_hdr_t) + sizeof(dap_stream_ch_pkt_hdr_t)] = {0};
    dap_stream_pkt_t *data_pkt = (dap_stream_pkt_t *)data;
    memcpy(data_pkt->hdr.sig, c_dap_stream_sig, sizeof(c_dap_stream_sig));
    data_pkt->hdr.type = STREAM_PKT_TYPE_DATA_PACKET;
    data_pkt->hdr.size = sizeof(dap_stream_ch_pkt_hdr_t);
    dap_stream_ch_pkt_t *ch_pkt = (dap_stream_ch_pkt_t *)data_pkt->data;
    dap_stream_ch_proc_t proc = {.id = 42, .packet_in_callback = channel_read};
    dap_stream_ch_t channel = {.proc = &proc};
    dap_stream_ch_t *channels[] = {&channel};
    stream.channel = channels;
    stream.channel_count = 1;
    ch_pkt->hdr.id = 42;
    for (unsigned i = 0; i < 2; i++) {
        ch_pkt->hdr.seq_id = i + 1;
        accept_channel = i;
        dap_stream_data_proc_read_ext_validated(&stream, data, sizeof(data), &accepted);
        assert(accepted == accept_channel);
    }
    fragment->full_size = 1; /* Complete but too short for a channel header. */
    dap_stream_data_proc_read_ext_validated(&stream, buffer, sizeof(buffer), &accepted);
    assert(!accepted && !stream.buf_fragments);
    dap_net_trans_ops_t l_ops = {.write = s_write};
    dap_net_trans_t l_trans = {.ops = &l_ops};
    dap_events_socket_t l_listener = {0};
    dap_stream_t l_a = {.esocket = &l_listener, .trans = &l_trans};
    dap_stream_t l_b = {.esocket = &l_listener, .trans = &l_trans};
    dap_stream_pkt_t l_pkt = {.hdr = {.type = STREAM_PKT_TYPE_KEEPALIVE}};
    s_result = sizeof(dap_stream_pkt_hdr_t);
    s_expected = &l_a;
    s_stream_proc_pkt_in(&l_a, &l_pkt);
    assert(s_writes == 1 && !l_a.trans_ctx && !l_b.is_active);
    l_pkt.hdr.type = STREAM_PKT_TYPE_ALIVE;
    s_stream_proc_pkt_in(&l_a, &l_pkt);
    assert(s_writes == 1 && !l_a.is_active);
    l_pkt.hdr.type = STREAM_PKT_TYPE_KEEPALIVE;
    s_result = -1;
    s_stream_proc_pkt_in(&l_a, &l_pkt);
    assert(s_writes == 2 && !l_b.is_active);
    s_expected = &l_b;
    s_result = sizeof(dap_stream_pkt_hdr_t);
    s_stream_proc_pkt_in(&l_b, &l_pkt);
    assert(s_writes == 3 && !l_b.trans_ctx && l_b.esocket == l_a.esocket);
    dap_stream_pkt_hdr_t l_alive = {.type = STREAM_PKT_TYPE_ALIVE};
    memcpy(l_alive.sig, c_dap_stream_sig, sizeof(c_dap_stream_sig));
    assert(dap_stream_trans_write_unsafe(&l_b, &l_alive, sizeof(l_alive)) == sizeof(l_alive));
    s_result = -1;
    assert(dap_stream_trans_write_unsafe(&l_b, &l_alive, sizeof(l_alive)) == 0);
    assert(dap_stream_send_unsafe(&l_b, &l_alive, sizeof(l_alive)) == -1);
    assert(s_writes == 6);
    dap_net_trans_t l_no_write = {0};
    dap_net_trans_ctx_t l_ctx = {.trans = &l_trans};
    l_b.trans_ctx = &l_ctx;
    l_b.trans = &l_no_write;
    s_result = sizeof(l_alive);
    assert(dap_stream_send_unsafe(&l_b, &l_alive, sizeof(l_alive)) == sizeof(l_alive));
    assert(dap_stream_trans_write_unsafe(&l_b, &l_alive, sizeof(l_alive)) == sizeof(l_alive));
    l_ctx.trans = NULL;
    l_b.trans = &l_trans;
    assert(dap_stream_send_unsafe(&l_b, &l_alive, sizeof(l_alive)) == sizeof(l_alive));
    l_b.trans_ctx = NULL;
    l_b.trans = NULL;
    assert(dap_stream_trans_write_unsafe(&l_b, &l_alive, sizeof(l_alive)) == 0);
    assert(s_writes == 9);
    puts("stream keepalive regression passed");
    return 0;
}
