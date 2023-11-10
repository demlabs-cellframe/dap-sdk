#include "dap_common.h"

int dap_crc64_init();

uint64_t crc64_update(uint64_t a_crc, const uint8_t *a_ptr, const size_t a_count);

uint64_t crc64(const uint8_t *a_ptr, const size_t a_count);
