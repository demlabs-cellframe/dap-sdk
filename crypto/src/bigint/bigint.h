#ifndef BIGINT_H
#define BIGINT_H
#endif // BIGINT_H
#include <stdint.h>

struct dap_bigint {
    uint64_t header;
    uint64_t* body;
};
typedef struct dap_bigint dap_bigint_t;
