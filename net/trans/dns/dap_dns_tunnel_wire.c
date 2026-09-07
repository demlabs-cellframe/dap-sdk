#include "dap_dns_tunnel_wire.h"

#include <stdbool.h>
#include <string.h>

#define DNS_HEADER_SIZE 12U
#define DNS_TYPE_TXT 16U
#define DNS_TYPE_OPT 41U
#define DNS_CLASS_IN 1U
#define DNS_FLAG_RD 0x0100U
#define DNS_FLAG_QR 0x8000U
#define DNS_FLAG_RA 0x0080U
#define DNS_MAX_NAME_WIRE 255U
#define DNS_MAX_NAME_TEXT 253U
#define DNS_MAX_LABEL 63U
#define DNS_OPT_SIZE 11U

static uint16_t s_read_u16(const uint8_t *a_data)
{
    return (uint16_t)((uint16_t)a_data[0] << 8 | a_data[1]);
}

static uint32_t s_read_u32(const uint8_t *a_data)
{
    return (uint32_t)a_data[0] << 24 | (uint32_t)a_data[1] << 16 |
            (uint32_t)a_data[2] << 8 | a_data[3];
}

static uint64_t s_read_u64(const uint8_t *a_data)
{
    return (uint64_t)s_read_u32(a_data) << 32 | s_read_u32(a_data + 4);
}

static void s_write_u16(uint8_t *a_data, uint16_t a_value)
{
    a_data[0] = (uint8_t)(a_value >> 8);
    a_data[1] = (uint8_t)a_value;
}

static void s_write_u32(uint8_t *a_data, uint32_t a_value)
{
    a_data[0] = (uint8_t)(a_value >> 24);
    a_data[1] = (uint8_t)(a_value >> 16);
    a_data[2] = (uint8_t)(a_value >> 8);
    a_data[3] = (uint8_t)a_value;
}

static void s_write_u64(uint8_t *a_data, uint64_t a_value)
{
    s_write_u32(a_data, (uint32_t)(a_value >> 32));
    s_write_u32(a_data + 4, (uint32_t)a_value);
}

static uint32_t s_frame_crc(const uint8_t *a_header, const uint8_t *a_payload,
        size_t a_payload_size)
{
    uint32_t l_crc = UINT32_MAX;
    for(size_t i = 0; i < DAP_DNS_TUNNEL_FRAME_HEADER_SIZE - 4; ++i) {
        l_crc ^= a_header[i];
        for(unsigned l_bit = 0; l_bit < 8; ++l_bit)
            l_crc = l_crc >> 1 ^ ((0U - (l_crc & 1U)) & 0xEDB88320U);
    }
    for(size_t i = 0; i < a_payload_size; ++i) {
        l_crc ^= a_payload[i];
        for(unsigned l_bit = 0; l_bit < 8; ++l_bit)
            l_crc = l_crc >> 1 ^ ((0U - (l_crc & 1U)) & 0xEDB88320U);
    }
    return ~l_crc;
}

static int s_suffix_to_wire(const char *a_suffix, uint8_t *a_wire,
        size_t *a_wire_size, size_t *a_text_size)
{
    if(!a_suffix || !a_wire_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    size_t l_length = strlen(a_suffix);
    if(!l_length)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    if(a_suffix[l_length - 1] == '.')
        --l_length;
    if(!l_length || l_length > DNS_MAX_NAME_TEXT)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;

    size_t l_output = 0;
    size_t l_label_start = 0;
    while(l_label_start < l_length) {
        size_t l_label_end = l_label_start;
        while(l_label_end < l_length && a_suffix[l_label_end] != '.')
            ++l_label_end;
        size_t l_label_size = l_label_end - l_label_start;
        if(!l_label_size || l_label_size > DNS_MAX_LABEL)
            return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
        if(l_output + l_label_size + 1 >= DNS_MAX_NAME_WIRE)
            return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
        if(a_wire) {
            a_wire[l_output] = (uint8_t)l_label_size;
            for(size_t i = 0; i < l_label_size; ++i) {
                uint8_t l_char = (uint8_t)a_suffix[l_label_start + i];
                if(l_char <= 0x20 || l_char >= 0x7F)
                    return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
                a_wire[l_output + 1 + i] =
                        l_char >= 'A' && l_char <= 'Z' ? l_char + ('a' - 'A') : l_char;
            }
        } else {
            for(size_t i = 0; i < l_label_size; ++i) {
                uint8_t l_char = (uint8_t)a_suffix[l_label_start + i];
                if(l_char <= 0x20 || l_char >= 0x7F)
                    return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
            }
        }
        l_output += l_label_size + 1;
        l_label_start = l_label_end + 1;
    }
    if(a_wire)
        a_wire[l_output] = 0;
    *a_wire_size = l_output + 1;
    if(a_text_size)
        *a_text_size = l_length;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

static size_t s_base32_size(size_t a_size)
{
    return (a_size * 8 + 4) / 5;
}

static int s_base32_encode(const uint8_t *a_data, size_t a_data_size,
        char *a_output, size_t a_output_size)
{
    static const char s_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    size_t l_required = s_base32_size(a_data_size);
    if(a_output_size < l_required)
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    uint32_t l_bits = 0;
    unsigned l_bit_count = 0;
    size_t l_output = 0;
    for(size_t i = 0; i < a_data_size; ++i) {
        l_bits = l_bits << 8 | a_data[i];
        l_bit_count += 8;
        while(l_bit_count >= 5) {
            l_bit_count -= 5;
            a_output[l_output++] = s_alphabet[(l_bits >> l_bit_count) & 31U];
        }
    }
    if(l_bit_count)
        a_output[l_output++] = s_alphabet[(l_bits << (5 - l_bit_count)) & 31U];
    return l_output == l_required ? DAP_DNS_TUNNEL_WIRE_OK :
            DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
}

static int s_base32_value(uint8_t a_char)
{
    if(a_char >= 'a' && a_char <= 'z')
        a_char -= 'a' - 'A';
    if(a_char >= 'A' && a_char <= 'Z')
        return a_char - 'A';
    if(a_char >= '2' && a_char <= '7')
        return a_char - '2' + 26;
    return -1;
}

static int s_base32_decode(const uint8_t *a_data, size_t a_data_size,
        uint8_t *a_output, size_t *a_output_size)
{
    if(!a_output_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    size_t l_required = a_data_size * 5 / 8;
    if(!a_output || *a_output_size < l_required) {
        *a_output_size = l_required;
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    }
    uint32_t l_bits = 0;
    unsigned l_bit_count = 0;
    size_t l_output = 0;
    for(size_t i = 0; i < a_data_size; ++i) {
        int l_value = s_base32_value(a_data[i]);
        if(l_value < 0)
            return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
        l_bits = l_bits << 5 | (uint32_t)l_value;
        l_bit_count += 5;
        if(l_bit_count >= 8) {
            l_bit_count -= 8;
            a_output[l_output++] = (uint8_t)(l_bits >> l_bit_count);
        }
    }
    if(l_bit_count && (l_bits & ((1U << l_bit_count) - 1U)))
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    *a_output_size = l_output;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

static bool s_qname_fits(size_t a_frame_size, size_t a_suffix_text_size)
{
    size_t l_chars = s_base32_size(a_frame_size);
    size_t l_labels = (l_chars + DNS_MAX_LABEL - 1) / DNS_MAX_LABEL;
    if(!l_labels)
        return false;
    size_t l_text_size = l_chars + l_labels + a_suffix_text_size;
    return l_text_size <= DNS_MAX_NAME_TEXT;
}

static int s_build_qname(const uint8_t *a_frame, size_t a_frame_size,
        const char *a_suffix, uint8_t *a_output, size_t *a_output_size)
{
    uint8_t l_suffix[DNS_MAX_NAME_WIRE];
    size_t l_suffix_size = sizeof(l_suffix);
    size_t l_suffix_text_size = 0;
    int l_result = s_suffix_to_wire(a_suffix, l_suffix, &l_suffix_size,
            &l_suffix_text_size);
    if(l_result)
        return l_result;
    if(!s_qname_fits(a_frame_size, l_suffix_text_size))
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;

    char l_encoded[DNS_MAX_NAME_TEXT];
    size_t l_encoded_size = s_base32_size(a_frame_size);
    l_result = s_base32_encode(a_frame, a_frame_size, l_encoded,
            sizeof(l_encoded));
    if(l_result)
        return l_result;
    size_t l_required = l_encoded_size +
            (l_encoded_size + DNS_MAX_LABEL - 1) / DNS_MAX_LABEL +
            l_suffix_size;
    if(!a_output_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    if(!a_output || *a_output_size < l_required) {
        *a_output_size = l_required;
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    }

    size_t l_input = 0;
    size_t l_output = 0;
    while(l_input < l_encoded_size) {
        size_t l_label_size = l_encoded_size - l_input;
        if(l_label_size > DNS_MAX_LABEL)
            l_label_size = DNS_MAX_LABEL;
        a_output[l_output++] = (uint8_t)l_label_size;
        memcpy(a_output + l_output, l_encoded + l_input, l_label_size);
        l_output += l_label_size;
        l_input += l_label_size;
    }
    memcpy(a_output + l_output, l_suffix, l_suffix_size);
    *a_output_size = l_output + l_suffix_size;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

static int s_decode_name(const uint8_t *a_packet, size_t a_packet_size,
        size_t a_offset, uint8_t *a_name, size_t *a_name_size,
        size_t *a_consumed)
{
    if(!a_packet || !a_name || !a_name_size || !a_consumed ||
            a_offset >= a_packet_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    size_t l_position = a_offset;
    size_t l_output = 0;
    size_t l_consumed = 0;
    bool l_jumped = false;
    size_t l_jumps = 0;
    while(true) {
        if(l_position >= a_packet_size)
            return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
        uint8_t l_length = a_packet[l_position];
        if((l_length & 0xC0U) == 0xC0U) {
            if(l_position + 1 >= a_packet_size)
                return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
            size_t l_pointer = (size_t)(l_length & 0x3FU) << 8 |
                    a_packet[l_position + 1];
            if(l_pointer >= l_position || ++l_jumps > DNS_MAX_NAME_WIRE)
                return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
            if(!l_jumped)
                l_consumed += 2;
            l_position = l_pointer;
            l_jumped = true;
            continue;
        }
        if(l_length & 0xC0U)
            return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
        if(!l_jumped)
            ++l_consumed;
        ++l_position;
        if(!l_length) {
            if(l_output >= *a_name_size)
                return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
            a_name[l_output++] = 0;
            *a_name_size = l_output;
            *a_consumed = l_consumed;
            return DAP_DNS_TUNNEL_WIRE_OK;
        }
        if(l_length > DNS_MAX_LABEL || l_position + l_length > a_packet_size ||
                l_output + l_length + 1 >= DNS_MAX_NAME_WIRE)
            return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
        if(!l_jumped)
            l_consumed += l_length;
        a_name[l_output++] = l_length;
        for(size_t i = 0; i < l_length; ++i) {
            uint8_t l_char = a_packet[l_position + i];
            a_name[l_output++] = l_char >= 'A' && l_char <= 'Z' ?
                    l_char + ('a' - 'A') : l_char;
        }
        l_position += l_length;
    }
}

static int s_qname_decode_frame(const uint8_t *a_name, size_t a_name_size,
        const char *a_suffix, uint8_t *a_frame, size_t *a_frame_size)
{
    uint8_t l_suffix[DNS_MAX_NAME_WIRE];
    size_t l_suffix_size = sizeof(l_suffix);
    int l_result = s_suffix_to_wire(a_suffix, l_suffix, &l_suffix_size, NULL);
    if(l_result)
        return l_result;
    if(a_name_size <= l_suffix_size ||
            memcmp(a_name + a_name_size - l_suffix_size, l_suffix,
                    l_suffix_size))
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    size_t l_data_size = a_name_size - l_suffix_size;
    uint8_t l_encoded[DNS_MAX_NAME_TEXT];
    size_t l_encoded_size = 0;
    size_t l_position = 0;
    while(l_position < l_data_size) {
        uint8_t l_length = a_name[l_position++];
        if(!l_length || l_length > DNS_MAX_LABEL ||
                l_position + l_length > l_data_size ||
                l_encoded_size + l_length > sizeof(l_encoded))
            return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
        memcpy(l_encoded + l_encoded_size, a_name + l_position, l_length);
        l_encoded_size += l_length;
        l_position += l_length;
    }
    return s_base32_decode(l_encoded, l_encoded_size, a_frame, a_frame_size);
}

static int s_parse_question(const uint8_t *a_packet, size_t a_packet_size,
        const char *a_suffix, uint8_t *a_name, size_t *a_name_size,
        size_t *a_question_end, uint8_t *a_frame, size_t *a_frame_size)
{
    size_t l_consumed = 0;
    int l_result = s_decode_name(a_packet, a_packet_size, DNS_HEADER_SIZE,
            a_name, a_name_size, &l_consumed);
    if(l_result)
        return l_result;
    size_t l_end = DNS_HEADER_SIZE + l_consumed;
    if(l_end + 4 > a_packet_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
    if(s_read_u16(a_packet + l_end) != DNS_TYPE_TXT ||
            s_read_u16(a_packet + l_end + 2) != DNS_CLASS_IN)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    if(a_frame) {
        l_result = s_qname_decode_frame(a_name, *a_name_size, a_suffix,
                a_frame, a_frame_size);
        if(l_result)
            return l_result;
    } else {
        uint8_t l_dummy[DNS_MAX_NAME_WIRE];
        size_t l_dummy_size = sizeof(l_dummy);
        l_result = s_qname_decode_frame(a_name, *a_name_size, a_suffix,
                l_dummy, &l_dummy_size);
        if(l_result)
            return l_result;
    }
    *a_question_end = l_end + 4;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

int dap_dns_tunnel_frame_encode(const dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_output, size_t *a_output_size)
{
    if(!a_frame || !a_output_size || (a_frame->payload_length && !a_frame->payload))
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    if(!a_frame->fragment_count ||
            a_frame->fragment_index >= a_frame->fragment_count)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    size_t l_required = DAP_DNS_TUNNEL_FRAME_HEADER_SIZE +
            a_frame->payload_length;
    if(!a_output || *a_output_size < l_required) {
        *a_output_size = l_required;
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    }
    s_write_u32(a_output, DAP_DNS_TUNNEL_FRAME_MAGIC);
    a_output[4] = DAP_DNS_TUNNEL_FRAME_VERSION;
    a_output[5] = a_frame->type;
    s_write_u16(a_output + 6, a_frame->flags);
    s_write_u64(a_output + 8, a_frame->client_nonce);
    s_write_u64(a_output + 16, a_frame->session_cookie);
    s_write_u32(a_output + 24, a_frame->message_id);
    s_write_u16(a_output + 28, a_frame->fragment_index);
    s_write_u16(a_output + 30, a_frame->fragment_count);
    s_write_u32(a_output + 32, a_frame->ack_message_id);
    s_write_u16(a_output + 36, a_frame->ack_fragment_index);
    s_write_u16(a_output + 38, a_frame->payload_length);
    if(a_frame->payload_length)
        memcpy(a_output + DAP_DNS_TUNNEL_FRAME_HEADER_SIZE, a_frame->payload,
                a_frame->payload_length);
    s_write_u32(a_output + 40, s_frame_crc(a_output,
            a_output + DAP_DNS_TUNNEL_FRAME_HEADER_SIZE,
            a_frame->payload_length));
    *a_output_size = l_required;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

int dap_dns_tunnel_frame_decode(const uint8_t *a_data, size_t a_data_size,
        dap_dns_tunnel_frame_t *a_frame, uint8_t *a_payload,
        size_t *a_payload_size)
{
    if(!a_data || !a_frame || !a_payload_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    if(a_data_size < DAP_DNS_TUNNEL_FRAME_HEADER_SIZE)
        return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
    if(s_read_u32(a_data) != DAP_DNS_TUNNEL_FRAME_MAGIC ||
            a_data[4] != DAP_DNS_TUNNEL_FRAME_VERSION)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    uint16_t l_payload_size = s_read_u16(a_data + 38);
    if(a_data_size != DAP_DNS_TUNNEL_FRAME_HEADER_SIZE + l_payload_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
    if(s_read_u32(a_data + 40) != s_frame_crc(a_data,
            a_data + DAP_DNS_TUNNEL_FRAME_HEADER_SIZE, l_payload_size))
        return DAP_DNS_TUNNEL_WIRE_ERROR_CRC;
    uint16_t l_fragment_index = s_read_u16(a_data + 28);
    uint16_t l_fragment_count = s_read_u16(a_data + 30);
    if(!l_fragment_count || l_fragment_index >= l_fragment_count)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    if(l_payload_size && (!a_payload || *a_payload_size < l_payload_size)) {
        *a_payload_size = l_payload_size;
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    }
    if(l_payload_size)
        memcpy(a_payload, a_data + DAP_DNS_TUNNEL_FRAME_HEADER_SIZE,
                l_payload_size);
    a_frame->type = a_data[5];
    a_frame->flags = s_read_u16(a_data + 6);
    a_frame->client_nonce = s_read_u64(a_data + 8);
    a_frame->session_cookie = s_read_u64(a_data + 16);
    a_frame->message_id = s_read_u32(a_data + 24);
    a_frame->fragment_index = l_fragment_index;
    a_frame->fragment_count = l_fragment_count;
    a_frame->ack_message_id = s_read_u32(a_data + 32);
    a_frame->ack_fragment_index = s_read_u16(a_data + 36);
    a_frame->payload_length = l_payload_size;
    a_frame->payload = l_payload_size ? a_payload : NULL;
    *a_payload_size = l_payload_size;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

int dap_dns_tunnel_wire_build_query(uint16_t a_transaction_id,
        const char *a_suffix, const dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_output, size_t *a_output_size)
{
    if(!a_output_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    uint8_t l_frame[DNS_MAX_NAME_WIRE];
    size_t l_frame_size = sizeof(l_frame);
    int l_result = dap_dns_tunnel_frame_encode(a_frame, l_frame, &l_frame_size);
    if(l_result)
        return l_result;
    uint8_t l_qname[DNS_MAX_NAME_WIRE];
    size_t l_qname_size = sizeof(l_qname);
    l_result = s_build_qname(l_frame, l_frame_size, a_suffix, l_qname,
            &l_qname_size);
    if(l_result)
        return l_result;
    size_t l_required = DNS_HEADER_SIZE + l_qname_size + 4 + DNS_OPT_SIZE;
    if(!a_output || *a_output_size < l_required) {
        *a_output_size = l_required;
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    }
    memset(a_output, 0, DNS_HEADER_SIZE);
    s_write_u16(a_output, a_transaction_id);
    s_write_u16(a_output + 2, DNS_FLAG_RD);
    s_write_u16(a_output + 4, 1);
    s_write_u16(a_output + 10, 1);
    size_t l_offset = DNS_HEADER_SIZE;
    memcpy(a_output + l_offset, l_qname, l_qname_size);
    l_offset += l_qname_size;
    s_write_u16(a_output + l_offset, DNS_TYPE_TXT);
    s_write_u16(a_output + l_offset + 2, DNS_CLASS_IN);
    l_offset += 4;
    a_output[l_offset++] = 0;
    s_write_u16(a_output + l_offset, DNS_TYPE_OPT);
    s_write_u16(a_output + l_offset + 2, DAP_DNS_TUNNEL_EDNS_UDP_SIZE);
    s_write_u32(a_output + l_offset + 4, 0);
    s_write_u16(a_output + l_offset + 8, 0);
    *a_output_size = l_required;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

int dap_dns_tunnel_wire_parse_query(const uint8_t *a_packet,
        size_t a_packet_size, const char *a_suffix,
        uint16_t *a_transaction_id, dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_payload, size_t *a_payload_size)
{
    if(!a_packet || !a_transaction_id || !a_frame || !a_payload_size ||
            a_packet_size < DNS_HEADER_SIZE)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    if(s_read_u16(a_packet + 2) & DNS_FLAG_QR ||
            s_read_u16(a_packet + 4) != 1 ||
            s_read_u16(a_packet + 6) || s_read_u16(a_packet + 8))
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    uint8_t l_name[DNS_MAX_NAME_WIRE];
    size_t l_name_size = sizeof(l_name);
    uint8_t l_frame[DNS_MAX_NAME_WIRE];
    size_t l_frame_size = sizeof(l_frame);
    size_t l_question_end = 0;
    int l_result = s_parse_question(a_packet, a_packet_size, a_suffix,
            l_name, &l_name_size, &l_question_end, l_frame, &l_frame_size);
    if(l_result)
        return l_result;

    uint16_t l_additional = s_read_u16(a_packet + 10);
    if(l_additional != 1 || l_question_end + DNS_OPT_SIZE != a_packet_size)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    size_t l_offset = l_question_end;
    if(a_packet[l_offset] || s_read_u16(a_packet + l_offset + 1) != DNS_TYPE_OPT ||
            s_read_u16(a_packet + l_offset + 3) != DAP_DNS_TUNNEL_EDNS_UDP_SIZE ||
            s_read_u32(a_packet + l_offset + 5) ||
            s_read_u16(a_packet + l_offset + 9))
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    l_result = dap_dns_tunnel_frame_decode(l_frame, l_frame_size, a_frame,
            a_payload, a_payload_size);
    if(!l_result)
        *a_transaction_id = s_read_u16(a_packet);
    return l_result;
}

int dap_dns_tunnel_wire_build_response(const uint8_t *a_query,
        size_t a_query_size, const char *a_suffix,
        const dap_dns_tunnel_frame_t *a_frame, uint8_t *a_output,
        size_t *a_output_size)
{
    if(!a_query || !a_output_size || a_query_size < DNS_HEADER_SIZE)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    if(s_read_u16(a_query + 2) & DNS_FLAG_QR ||
            s_read_u16(a_query + 4) != 1)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    uint8_t l_name[DNS_MAX_NAME_WIRE];
    size_t l_name_size = sizeof(l_name);
    size_t l_question_end = 0;
    int l_result = s_parse_question(a_query, a_query_size, a_suffix,
            l_name, &l_name_size, &l_question_end, NULL, NULL);
    if(l_result)
        return l_result;

    uint8_t l_frame[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_frame_size = sizeof(l_frame);
    l_result = dap_dns_tunnel_frame_encode(a_frame, l_frame, &l_frame_size);
    if(l_result)
        return l_result;
    size_t l_txt_strings = (l_frame_size + 254) / 255;
    size_t l_rdata_size = l_frame_size + l_txt_strings;
    size_t l_question_size = l_name_size + 4;
    size_t l_required = DNS_HEADER_SIZE + l_question_size + 12 + l_rdata_size;
    if(l_rdata_size > UINT16_MAX || l_required > DAP_DNS_TUNNEL_EDNS_UDP_SIZE)
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    if(!a_output || *a_output_size < l_required) {
        *a_output_size = l_required;
        return DAP_DNS_TUNNEL_WIRE_ERROR_CAPACITY;
    }
    memset(a_output, 0, DNS_HEADER_SIZE);
    s_write_u16(a_output, s_read_u16(a_query));
    s_write_u16(a_output + 2, DNS_FLAG_QR | DNS_FLAG_RA |
            (s_read_u16(a_query + 2) & DNS_FLAG_RD));
    s_write_u16(a_output + 4, 1);
    s_write_u16(a_output + 6, 1);
    size_t l_offset = DNS_HEADER_SIZE;
    memcpy(a_output + l_offset, l_name, l_name_size);
    l_offset += l_name_size;
    s_write_u16(a_output + l_offset, DNS_TYPE_TXT);
    s_write_u16(a_output + l_offset + 2, DNS_CLASS_IN);
    l_offset += 4;
    a_output[l_offset++] = 0xC0;
    a_output[l_offset++] = DNS_HEADER_SIZE;
    s_write_u16(a_output + l_offset, DNS_TYPE_TXT);
    s_write_u16(a_output + l_offset + 2, DNS_CLASS_IN);
    s_write_u32(a_output + l_offset + 4, 0);
    s_write_u16(a_output + l_offset + 8, (uint16_t)l_rdata_size);
    l_offset += 10;
    size_t l_frame_offset = 0;
    while(l_frame_offset < l_frame_size) {
        size_t l_chunk_size = l_frame_size - l_frame_offset;
        if(l_chunk_size > 255)
            l_chunk_size = 255;
        a_output[l_offset++] = (uint8_t)l_chunk_size;
        memcpy(a_output + l_offset, l_frame + l_frame_offset, l_chunk_size);
        l_offset += l_chunk_size;
        l_frame_offset += l_chunk_size;
    }
    *a_output_size = l_offset;
    return DAP_DNS_TUNNEL_WIRE_OK;
}

int dap_dns_tunnel_wire_parse_response(const uint8_t *a_packet,
        size_t a_packet_size, const char *a_suffix,
        uint16_t *a_transaction_id, dap_dns_tunnel_frame_t *a_frame,
        uint8_t *a_payload, size_t *a_payload_size)
{
    if(!a_packet || !a_transaction_id || !a_frame || !a_payload_size ||
            a_packet_size < DNS_HEADER_SIZE)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    uint16_t l_flags = s_read_u16(a_packet + 2);
    uint16_t l_answers = s_read_u16(a_packet + 6);
    if(!(l_flags & DNS_FLAG_QR) || (l_flags & 0x000FU) ||
            s_read_u16(a_packet + 4) != 1 || !l_answers)
        return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
    uint8_t l_question_name[DNS_MAX_NAME_WIRE];
    size_t l_question_name_size = sizeof(l_question_name);
    size_t l_offset = 0;
    int l_result = s_parse_question(a_packet, a_packet_size, a_suffix,
            l_question_name, &l_question_name_size, &l_offset, NULL, NULL);
    if(l_result)
        return l_result;

    for(uint16_t i = 0; i < l_answers; ++i) {
        uint8_t l_answer_name[DNS_MAX_NAME_WIRE];
        size_t l_answer_name_size = sizeof(l_answer_name);
        size_t l_consumed = 0;
        l_result = s_decode_name(a_packet, a_packet_size, l_offset,
                l_answer_name, &l_answer_name_size, &l_consumed);
        if(l_result)
            return l_result;
        l_offset += l_consumed;
        if(l_offset + 10 > a_packet_size)
            return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
        uint16_t l_type = s_read_u16(a_packet + l_offset);
        uint16_t l_class = s_read_u16(a_packet + l_offset + 2);
        uint16_t l_rdata_size = s_read_u16(a_packet + l_offset + 8);
        l_offset += 10;
        if(l_offset + l_rdata_size > a_packet_size)
            return DAP_DNS_TUNNEL_WIRE_ERROR_BOUNDS;
        if(l_type == DNS_TYPE_TXT && l_class == DNS_CLASS_IN &&
                l_answer_name_size == l_question_name_size &&
                !memcmp(l_answer_name, l_question_name, l_question_name_size)) {
            uint8_t l_encoded[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
            size_t l_encoded_size = 0;
            size_t l_rdata_end = l_offset + l_rdata_size;
            while(l_offset < l_rdata_end) {
                size_t l_chunk_size = a_packet[l_offset++];
                if(!l_chunk_size || l_offset + l_chunk_size > l_rdata_end ||
                        l_encoded_size + l_chunk_size > sizeof(l_encoded))
                    return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
                memcpy(l_encoded + l_encoded_size, a_packet + l_offset,
                        l_chunk_size);
                l_encoded_size += l_chunk_size;
                l_offset += l_chunk_size;
            }
            l_result = dap_dns_tunnel_frame_decode(l_encoded, l_encoded_size,
                    a_frame, a_payload, a_payload_size);
            if(!l_result)
                *a_transaction_id = s_read_u16(a_packet);
            return l_result;
        }
        l_offset += l_rdata_size;
    }
    return DAP_DNS_TUNNEL_WIRE_ERROR_FORMAT;
}

int dap_dns_tunnel_wire_payload_budget(const char *a_suffix,
        size_t *a_query_payload, size_t *a_response_payload)
{
    if(!a_query_payload || !a_response_payload)
        return DAP_DNS_TUNNEL_WIRE_ERROR_ARGUMENT;
    size_t l_suffix_size = DNS_MAX_NAME_WIRE;
    size_t l_suffix_text_size = 0;
    int l_result = s_suffix_to_wire(a_suffix, NULL, &l_suffix_size,
            &l_suffix_text_size);
    if(l_result)
        return l_result;

    size_t l_query_frame = DAP_DNS_TUNNEL_FRAME_HEADER_SIZE;
    while(s_qname_fits(l_query_frame + 1, l_suffix_text_size))
        ++l_query_frame;
    *a_query_payload = l_query_frame - DAP_DNS_TUNNEL_FRAME_HEADER_SIZE;

    size_t l_question_size = DNS_MAX_NAME_WIRE + 4;
    size_t l_response_frame = DAP_DNS_TUNNEL_FRAME_HEADER_SIZE;
    while(true) {
        size_t l_next = l_response_frame + 1;
        size_t l_txt_strings = (l_next + 254) / 255;
        if(DNS_HEADER_SIZE + l_question_size + 12 + l_next +
                l_txt_strings > DAP_DNS_TUNNEL_EDNS_UDP_SIZE)
            break;
        l_response_frame = l_next;
    }
    *a_response_payload = l_response_frame -
            DAP_DNS_TUNNEL_FRAME_HEADER_SIZE;
    return DAP_DNS_TUNNEL_WIRE_OK;
}
