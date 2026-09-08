#include "dap_dns_tunnel_wire.h"

#include <stdio.h>
#include <string.h>

#define TEST_SUFFIX "vpn.example"
#define CHECK(a_condition) do { \
    if(!(a_condition)) { \
        fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #a_condition); \
        return 1; \
    } \
} while(0)

static dap_dns_tunnel_frame_t s_frame(const uint8_t *a_payload,
        uint16_t a_payload_size)
{
    dap_dns_tunnel_frame_t l_frame = {
        .type = 3,
        .flags = 0x1200,
        .client_nonce = UINT64_C(0x0102030405060708),
        .session_cookie = UINT64_C(0x8877665544332211),
        .message_id = 0xA1B2C3D4,
        .fragment_index = 1,
        .fragment_count = 3,
        .ack_message_id = 0x10203040,
        .ack_fragment_index = 2,
        .payload_length = a_payload_size,
        .payload = a_payload
    };
    return l_frame;
}

static int s_check_frame(const dap_dns_tunnel_frame_t *a_frame,
        const uint8_t *a_payload, size_t a_payload_size)
{
    CHECK(a_frame->type == 3);
    CHECK((a_frame->flags & ~DAP_DNS_TUNNEL_FLAG_EDNS_PAYLOAD) == 0x1200);
    CHECK(a_frame->client_nonce == UINT64_C(0x0102030405060708));
    CHECK(a_frame->session_cookie == UINT64_C(0x8877665544332211));
    CHECK(a_frame->message_id == 0xA1B2C3D4);
    CHECK(a_frame->fragment_index == 1);
    CHECK(a_frame->fragment_count == 3);
    CHECK(a_frame->ack_message_id == 0x10203040);
    CHECK(a_frame->ack_fragment_index == 2);
    CHECK(a_frame->payload_length == a_payload_size);
    CHECK(!memcmp(a_frame->payload, a_payload, a_payload_size));
    return 0;
}

static int s_test_frame_codec(void)
{
    static const uint8_t s_payload[] = { 0x00, 0x01, 0x7F, 0x80, 0xFF };
    dap_dns_tunnel_frame_t l_frame = s_frame(s_payload, sizeof(s_payload));
    uint8_t l_encoded[128];
    size_t l_encoded_size = sizeof(l_encoded);
    CHECK(dap_dns_tunnel_frame_encode(&l_frame, l_encoded,
            &l_encoded_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_encoded_size == DAP_DNS_TUNNEL_FRAME_HEADER_SIZE +
            sizeof(s_payload));
    CHECK(l_encoded[0] == 'D' && l_encoded[1] == 'N' &&
            l_encoded[2] == 'T' && l_encoded[3] == '1');

    uint8_t l_payload[32];
    size_t l_payload_size = sizeof(l_payload);
    dap_dns_tunnel_frame_t l_decoded;
    CHECK(dap_dns_tunnel_frame_decode(l_encoded, l_encoded_size, &l_decoded,
            l_payload, &l_payload_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(!s_check_frame(&l_decoded, s_payload, sizeof(s_payload)));

    l_encoded[l_encoded_size - 1] ^= 1;
    l_payload_size = sizeof(l_payload);
    CHECK(dap_dns_tunnel_frame_decode(l_encoded, l_encoded_size, &l_decoded,
            l_payload, &l_payload_size) == DAP_DNS_TUNNEL_WIRE_ERROR_CRC);
    CHECK(dap_dns_tunnel_frame_decode(l_encoded,
            DAP_DNS_TUNNEL_FRAME_HEADER_SIZE - 1, &l_decoded, l_payload,
            &l_payload_size) == DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS);
    return 0;
}

static int s_question_end(const uint8_t *a_packet, size_t a_packet_size,
        size_t *a_text_size)
{
    size_t l_offset = 12;
    size_t l_text = 0;
    while(l_offset < a_packet_size && a_packet[l_offset]) {
        uint8_t l_label_size = a_packet[l_offset++];
        CHECK(l_label_size <= 63);
        CHECK(l_offset + l_label_size <= a_packet_size);
        l_text += l_label_size + (l_text ? 1 : 0);
        l_offset += l_label_size;
    }
    CHECK(l_offset < a_packet_size);
    CHECK(l_text <= 253);
    *a_text_size = l_text;
    return (int)(l_offset + 1 + 4);
}

static int s_test_query_codec(uint8_t *a_query, size_t *a_query_size)
{
    uint8_t l_input[64];
    for(size_t i = 0; i < sizeof(l_input); ++i)
        l_input[i] = (uint8_t)(i * 7);
    dap_dns_tunnel_frame_t l_frame = s_frame(l_input, sizeof(l_input));
    CHECK(dap_dns_tunnel_wire_build_query(0xCAFE, TEST_SUFFIX, &l_frame,
            a_query, a_query_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(a_query[2] == 0x01 && a_query[3] == 0x00);
    CHECK(a_query[4] == 0x00 && a_query[5] == 0x01);
    CHECK(a_query[10] == 0x00 && a_query[11] == 0x01);

    size_t l_text_size = 0;
    int l_question_end = s_question_end(a_query, *a_query_size, &l_text_size);
    CHECK(l_question_end > 0);
    CHECK((size_t)l_question_end + 11 == *a_query_size);
    CHECK(a_query[l_question_end] == 0);
    CHECK(a_query[l_question_end + 1] == 0);
    CHECK(a_query[l_question_end + 2] == 41);
    CHECK(a_query[l_question_end + 3] == 0x04);
    CHECK(a_query[l_question_end + 4] == 0xD0);

    uint8_t l_output[128];
    size_t l_output_size = sizeof(l_output);
    uint16_t l_transaction_id = 0;
    dap_dns_tunnel_frame_t l_decoded;
    CHECK(dap_dns_tunnel_wire_parse_query(a_query, *a_query_size,
            TEST_SUFFIX, &l_transaction_id, &l_decoded, l_output,
            &l_output_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_transaction_id == 0xCAFE);
    CHECK(!s_check_frame(&l_decoded, l_input, sizeof(l_input)));

    uint8_t l_bad_query[512];
    memcpy(l_bad_query, a_query, *a_query_size);
    l_bad_query[l_question_end + 4] = 0xD1;
    l_output_size = sizeof(l_output);
    CHECK(dap_dns_tunnel_wire_parse_query(l_bad_query, *a_query_size,
            TEST_SUFFIX, &l_transaction_id, &l_decoded, l_output,
            &l_output_size) == DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT);
    return 0;
}

static int s_test_response_codec(const uint8_t *a_query, size_t a_query_size)
{
    uint8_t l_input[300];
    for(size_t i = 0; i < sizeof(l_input); ++i)
        l_input[i] = (uint8_t)(255 - i);
    dap_dns_tunnel_frame_t l_frame = s_frame(l_input, sizeof(l_input));
    uint8_t l_response[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_response_size = sizeof(l_response);
    CHECK(dap_dns_tunnel_wire_build_response(a_query, a_query_size,
            TEST_SUFFIX, &l_frame, l_response,
            &l_response_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_response_size <= DAP_DNS_TUNNEL_EDNS_UDP_SIZE);

    uint8_t l_output[400];
    size_t l_output_size = sizeof(l_output);
    uint16_t l_transaction_id = 0;
    dap_dns_tunnel_frame_t l_decoded;
    CHECK(dap_dns_tunnel_wire_parse_response(l_response, l_response_size,
            TEST_SUFFIX, &l_transaction_id, &l_decoded, l_output,
            &l_output_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_transaction_id == 0xCAFE);
    CHECK(!s_check_frame(&l_decoded, l_input, sizeof(l_input)));

    size_t l_text_size = 0;
    int l_answer_offset = s_question_end(l_response, l_response_size,
            &l_text_size);
    CHECK(l_answer_offset > 0);
    uint8_t l_bad_response[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    memcpy(l_bad_response, l_response, l_response_size);
    l_bad_response[l_answer_offset] = 0xC0 |
            ((unsigned)l_answer_offset >> 8 & 0x3F);
    l_bad_response[l_answer_offset + 1] = (uint8_t)l_answer_offset;
    l_output_size = sizeof(l_output);
    CHECK(dap_dns_tunnel_wire_parse_response(l_bad_response, l_response_size,
            TEST_SUFFIX, &l_transaction_id, &l_decoded, l_output,
            &l_output_size) == DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT);
    return 0;
}

static int s_test_budgets(void)
{
    size_t l_query_payload = 0;
    size_t l_response_payload = 0;
    CHECK(dap_dns_tunnel_wire_payload_budget(TEST_SUFFIX, &l_query_payload,
            &l_response_payload) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_query_payload > 0);
    CHECK(l_response_payload > l_query_payload);
    CHECK(dap_dns_tunnel_wire_payload_budget("bad..suffix", &l_query_payload,
            &l_response_payload) == DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT);

    size_t l_qname_payload = 0;
    size_t l_edns_payload = 0;
    CHECK(dap_dns_tunnel_wire_payload_budget_ext(TEST_SUFFIX, &l_qname_payload,
            &l_edns_payload, &l_response_payload) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_edns_payload > l_qname_payload);
    CHECK(l_edns_payload > l_response_payload);
    return 0;
}

static int s_test_edns_payload(void)
{
    size_t l_qname_payload = 0;
    size_t l_edns_payload = 0;
    size_t l_response_payload = 0;
    CHECK(dap_dns_tunnel_wire_payload_budget_ext(TEST_SUFFIX, &l_qname_payload,
            &l_edns_payload, &l_response_payload) == DAP_DNS_TUNNEL_WIRE_OK);

    uint8_t l_input[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_input_size = l_edns_payload;
    for(size_t i = 0; i < l_input_size; ++i)
        l_input[i] = (uint8_t)(i * 13 + 5);
    dap_dns_tunnel_frame_t l_frame = s_frame(l_input, (uint16_t)l_input_size);
    uint8_t l_query[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_query_size = sizeof(l_query);
    CHECK(dap_dns_tunnel_wire_build_query(0x1234, TEST_SUFFIX, &l_frame,
            l_query, &l_query_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_query_size <= DAP_DNS_TUNNEL_EDNS_UDP_SIZE);

    uint8_t l_output[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_output_size = sizeof(l_output);
    uint16_t l_transaction_id = 0;
    dap_dns_tunnel_frame_t l_decoded;
    CHECK(dap_dns_tunnel_wire_parse_query(l_query, l_query_size, TEST_SUFFIX,
            &l_transaction_id, &l_decoded, l_output,
            &l_output_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_transaction_id == 0x1234);
    CHECK(l_decoded.flags & DAP_DNS_TUNNEL_FLAG_EDNS_PAYLOAD);
    CHECK(l_decoded.payload_length == l_input_size);
    CHECK(!memcmp(l_decoded.payload, l_input, l_input_size));

    /* option payload must stay consistent with the length in the QNAME header */
    l_query[l_query_size - 1] ^= 0xFF;
    l_output_size = sizeof(l_output);
    CHECK(dap_dns_tunnel_wire_parse_query(l_query, l_query_size, TEST_SUFFIX,
            &l_transaction_id, &l_decoded, l_output,
            &l_output_size) == DAP_DNS_TUNNEL_WIRE_ERROR_CRC);
    return 0;
}

static int s_test_poll_and_suffix(void)
{
    dap_dns_tunnel_frame_t l_frame = {
        .type = DAP_DNS_TUNNEL_MSG_POLL,
        .client_nonce = 1,
        .session_cookie = 2,
        .message_id = 3,
        .fragment_index = 0,
        .fragment_count = 1
    };
    uint8_t l_query[512];
    size_t l_query_size = sizeof(l_query);
    CHECK(dap_dns_tunnel_wire_build_query(1, DAP_DNS_TUNNEL_DEFAULT_SUFFIX,
            &l_frame, l_query, &l_query_size) == DAP_DNS_TUNNEL_WIRE_OK);
    uint8_t l_payload[8];
    size_t l_payload_size = sizeof(l_payload);
    uint16_t l_txid = 0;
    dap_dns_tunnel_frame_t l_decoded;
    CHECK(dap_dns_tunnel_wire_parse_query(l_query, l_query_size,
            DAP_DNS_TUNNEL_DEFAULT_SUFFIX, &l_txid, &l_decoded, l_payload,
            &l_payload_size) == DAP_DNS_TUNNEL_WIRE_OK);
    CHECK(l_decoded.type == DAP_DNS_TUNNEL_MSG_POLL);
    CHECK(dap_dns_tunnel_wire_parse_query(l_query, l_query_size, "other.invalid",
            &l_txid, &l_decoded, l_payload, &l_payload_size) ==
            DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT);
    uint8_t l_truncated[16];
    memcpy(l_truncated, l_query, sizeof(l_truncated));
    l_payload_size = sizeof(l_payload);
    CHECK(dap_dns_tunnel_wire_parse_query(l_truncated, sizeof(l_truncated),
            DAP_DNS_TUNNEL_DEFAULT_SUFFIX, &l_txid, &l_decoded, l_payload,
            &l_payload_size) == DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS);
    return 0;
}

int main(void)
{
    uint8_t l_query[512];
    size_t l_query_size = sizeof(l_query);
    CHECK(!s_test_frame_codec());
    CHECK(!s_test_query_codec(l_query, &l_query_size));
    CHECK(!s_test_response_codec(l_query, l_query_size));
    CHECK(!s_test_budgets());
    CHECK(!s_test_edns_payload());
    CHECK(!s_test_poll_and_suffix());
    printf("DNS tunnel wire tests passed\n");
    return 0;
}
