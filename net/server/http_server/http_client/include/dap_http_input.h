/* Owner-worker plaintext storage. Independent of sockets and TLS.
 * Consumers borrow data until the next append/consume/clear; async handlers
 * must copy their request before returning. No implicit dropping on overflow. */
#ifndef DAP_HTTP_INPUT_H
#define DAP_HTTP_INPUT_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct dap_http_input {
    unsigned char *data;
    size_t size, capacity, limit;
} dap_http_input_t;

/* Initialize a zeroed, unowned buffer with a caller-selected memory budget. */
static inline int dap_http_input_init(dap_http_input_t *input, size_t limit)
{
    if (!input || input->data || input->size || input->capacity || !limit) return -1;
    input->limit = limit;
    return 0;
}

/* 0 accepted, 1 budget exhausted (nothing accepted), -1 invalid/OOM.
 * Source must not alias input storage. The caller retains responsibility for
 * blocked bytes; a TLS consumer that cannot retain them must fail closed. */
static inline int dap_http_input_append(dap_http_input_t *input, const void *data, size_t size)
{
    if (!input || !input->limit || (!data && size) || input->size > input->limit)
        return -1;
    if (size > input->limit - input->size) return 1;
    if (!size) return 0;
    size_t needed = input->size + size;
    if (needed > input->capacity) {
        size_t capacity = input->capacity ? input->capacity :
                          (input->limit < 4096 ? input->limit : 4096);
        while (capacity < needed) {
            capacity = capacity > input->limit / 2 ? input->limit : capacity * 2;
        }
        unsigned char *new_data = realloc(input->data, capacity);
        if (!new_data) return -1;
        input->data = new_data;
        input->capacity = capacity;
    }
    memcpy(input->data + input->size, data, size);
    input->size = needed;
    return 0;
}

static inline int dap_http_input_consume(dap_http_input_t *input, size_t consumed)
{
    if (!input || consumed > input->size) return -1;
    if (!consumed) return 0;
    input->size -= consumed;
    if (input->size) memmove(input->data, input->data + consumed, input->size);
    return 0;
}

static inline void dap_http_input_clear(dap_http_input_t *input)
{
    if (!input) return;
    free(input->data);
    memset(input, 0, sizeof(*input));
}
#endif
