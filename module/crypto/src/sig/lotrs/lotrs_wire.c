/*
 * LoTRS — wire format serialization.
 */

#include "lotrs_wire.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "lotrs_wire"
#include "dap_common.h"
#include "dap_serialize.h"

/* Header schema for dap_serialize. */
static const dap_serialize_field_t s_lotrs_header_fields[] = {
    DAP_SERIALIZE_FIELD_SIMPLE(lotrs_wire_header_t, magic,     DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(lotrs_wire_header_t, version,   DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(lotrs_wire_header_t, params_id, DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(lotrs_wire_header_t, d,         DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(lotrs_wire_header_t, N,         DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(lotrs_wire_header_t, T,         DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(lotrs_wire_header_t, flags,     DAP_SERIALIZE_TYPE_UINT32),
};

DAP_SERIALIZE_SCHEMA_DEFINE(s_lotrs_header_schema,
                            lotrs_wire_header_t,
                            s_lotrs_header_fields);

int lotrs_wire_header_pack(uint8_t *a_buf, size_t a_buf_len,
                           const lotrs_wire_header_t *a_hdr)
{
    if (!a_buf || !a_hdr) return -EINVAL;
    if (a_buf_len < LOTRS_WIRE_HEADER_BYTES) return -ENOMEM;

    dap_serialize_result_t l_res = dap_serialize_to_buffer_raw(
        &s_lotrs_header_schema, a_hdr, a_buf, a_buf_len, NULL);
    return l_res.error_code;
}

int lotrs_wire_header_unpack(lotrs_wire_header_t *a_hdr,
                             const uint8_t *a_buf, size_t a_buf_len)
{
    if (!a_hdr || !a_buf) return -EINVAL;
    if (a_buf_len < LOTRS_WIRE_HEADER_BYTES) return -EINVAL;

    memset(a_hdr, 0, sizeof(*a_hdr));
    dap_serialize_result_t l_res = dap_serialize_from_buffer_raw(
        &s_lotrs_header_schema, a_buf, a_buf_len, a_hdr, NULL);
    return l_res.error_code;
}
