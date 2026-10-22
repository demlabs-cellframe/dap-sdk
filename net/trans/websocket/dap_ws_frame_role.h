#ifndef DAP_WS_FRAME_ROLE_H
#define DAP_WS_FRAME_ROLE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define DAP_WS_RECEIVE_FRAME_MAX (1024U * 1024U)
#define DAP_WS_RECEIVE_MESSAGE_MAX (16U * 1024U * 1024U)
#define DAP_WS_RETAINED_DAP_MAX (16U * 1024U * 1024U)

static inline int dap_ws_tail_size(size_t input, size_t consumed, size_t *remaining)
{
    if (!remaining) return -1;
    *remaining = 0;
    if (consumed > input || input - consumed > DAP_WS_RETAINED_DAP_MAX) return -1;
    *remaining = input - consumed;
    return 0;
}

/* Count payload across complete data fragments, excluding control frames.
 * Invoke after sequence validation; reject before delivering this payload. */
static inline int dap_ws_message_budget(size_t *used, unsigned opcode,
                                       bool fin, size_t size)
{
    if (!used) return -1;
    if (opcode == 8 || opcode == 9 || opcode == 10) return 0;
    size_t previous = opcode == 0 ? *used : 0;
    if (previous > DAP_WS_RECEIVE_MESSAGE_MAX ||
        size > DAP_WS_RECEIVE_MESSAGE_MAX - previous) return -1;
    *used = fin ? 0 : previous + size;
    return 0;
}

/* Inspect only the declared length; reject over-budget frames before the
 * complete payload is buffered. Structural validation remains in parser. */
static inline int dap_ws_frame_budget_check(const void *data, size_t size, size_t limit)
{
    if (!data || !limit) return -1;
    if (size < 2) return -2;
    const uint8_t *p = data;
    uint64_t length = p[1] & 127;
    size_t bytes = length == 126 ? 2 : length == 127 ? 8 : 0;
    if (bytes) {
        if (size < 2 + bytes) return -2;
        length = 0;
        for (size_t i = 0; i < bytes; ++i) length = (length << 8) | p[2+i];
    }
    return length > limit ? -1 : 0;
}
/* Match parse_frame status: 0 valid, -2 incomplete, -1 protocol error. */
static inline int dap_ws_frame_role_check(const void *data, size_t size, bool server_receiver)
{
    if (!data) return -1;
    if (size < 2) return -2;
    const uint8_t *bytes = data;
    return ((bytes[1] & 0x80) != 0) == server_receiver ? 0 : -1;
}
/* Invoke only after a complete, structurally valid frame. Do not advance
 * state on partial input; control frames may interleave fragmented data. */
static inline int dap_ws_fragment_accept(bool *fragmented, unsigned opcode, bool fin)
{
    if (!fragmented) return -1;
    if (opcode == 8 || opcode == 9 || opcode == 10) return 0;
    if (opcode == 0) {
        if (!*fragmented) return -1;
        if (fin) *fragmented = false;
        return 0;
    }
    /* This carrier transports binary DAP bytes, not WebSocket text messages.
     * Reject text instead of silently bypassing its UTF-8 requirements. */
    if (opcode != 2 || *fragmented) return -1;
    *fragmented = !fin;
    return 0;
}
#endif
