/*
 * DNS tunnel wire codec.
 *
 * The codec owns no memory. All output is written to caller-provided buffers.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define DAP_DNS_TUNNEL_FRAME_MAGIC 0x444E5431U
#define DAP_DNS_TUNNEL_FRAME_VERSION 1U
#define DAP_DNS_TUNNEL_FRAME_HEADER_SIZE 44U
#define DAP_DNS_TUNNEL_EDNS_UDP_SIZE 1232U
#define DAP_DNS_TUNNEL_DEFAULT_SUFFIX "vpn.invalid"

#define DAP_DNS_TUNNEL_MSG_HANDSHAKE 1U
#define DAP_DNS_TUNNEL_MSG_DATA 2U
#define DAP_DNS_TUNNEL_MSG_POLL 3U
#define DAP_DNS_TUNNEL_MSG_CLOSE 4U

/* Payload travels in an EDNS0 option instead of the QNAME labels */
#define DAP_DNS_TUNNEL_FLAG_EDNS_PAYLOAD 0x0001U
#define DAP_DNS_TUNNEL_EDNS_OPTION_CODE 0xFDE9U

typedef enum dap_dns_tunnel_wire_error {
    DAP_DNS_TUNNEL_WIRE_OK = 0,
    DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT = -1,
    DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS = -2,
    DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT = -3,
    DAP_DNS_TUNNEL_WIRE_ERROR_CRC = -4,
    DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY = -5
} dap_dns_tunnel_wire_error_t;

typedef struct dap_dns_tunnel_frame {
    uint8_t type;
    uint16_t flags;
    uint64_t client_nonce;
    uint64_t session_cookie;
    uint32_t message_id;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint32_t ack_message_id;
    uint16_t ack_fragment_index;
    uint16_t payload_length;
    const uint8_t *payload;
} dap_dns_tunnel_frame_t;

int dap_dns_tunnel_frame_encode(const dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_output, size_t *a_output_size);
int dap_dns_tunnel_frame_decode(const uint8_t *a_data, size_t a_data_size,
        dap_dns_tunnel_frame_t *a_frame, uint8_t *a_payload,
        size_t *a_payload_size);

int dap_dns_tunnel_wire_build_query(uint16_t a_transaction_id,
        const char *a_suffix, const dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_output, size_t *a_output_size);
int dap_dns_tunnel_wire_parse_query(const uint8_t *a_packet,
        size_t a_packet_size, const char *a_suffix,
        uint16_t *a_transaction_id, dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_payload, size_t *a_payload_size);

int dap_dns_tunnel_wire_build_response(const uint8_t *a_query,
        size_t a_query_size, const char *a_suffix,
        const dap_dns_tunnel_frame_t *a_frame, uint8_t *a_output,
        size_t *a_output_size);
int dap_dns_tunnel_wire_parse_response(const uint8_t *a_packet,
        size_t a_packet_size, const char *a_suffix,
        uint16_t *a_transaction_id, dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_payload, size_t *a_payload_size);

int dap_dns_tunnel_wire_payload_budget(const char *a_suffix,
        size_t *a_query_payload, size_t *a_response_payload);
int dap_dns_tunnel_wire_payload_budget_ext(const char *a_suffix,
        size_t *a_query_qname_payload, size_t *a_query_edns_payload,
        size_t *a_response_payload);
