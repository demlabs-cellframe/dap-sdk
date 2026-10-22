/* Borrowed plaintext output interface. The sink copies accepted bytes before
 * returning; zero means backpressure, negative means terminal failure.
 * Scheduling writable/flush/close belongs to the connection owner. */
#ifndef DAP_HTTP_OUTPUT_H
#define DAP_HTTP_OUTPUT_H
#include <stddef.h>
#include <sys/types.h>

typedef ssize_t (*dap_http_output_write_fn)(void *, const void *, size_t);
/* 1: all previously accepted bytes left every local output queue;
 * 0: pending; -1: fatal. This is not a TCP ACK/delivery guarantee.
 * The owner schedules another completion notification after progress. */
typedef int (*dap_http_output_drained_fn)(void *);
typedef struct dap_http_output {
    dap_http_output_write_fn write;
    void *arg;
    dap_http_output_drained_fn drained;
} dap_http_output_t;

static inline ssize_t dap_http_output_write(const dap_http_output_t *output,
                                            const void *data, size_t size)
{
    if (!output || !output->write || (!data && size)) return -1;
    if (!size) return 0;
    ssize_t accepted = output->write(output->arg, data, size);
    return accepted < 0 || (size_t)accepted > size ? -1 : accepted;
}
#endif
