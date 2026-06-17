#ifndef _DAP_SHA1_H_
#define _DAP_SHA1_H_

#include <stdint.h>
#include <stddef.h>

#define DAP_SHA1_DIGEST_SIZE 20

#ifdef __cplusplus
extern "C" {
#endif

int dap_sha1(uint8_t a_output[DAP_SHA1_DIGEST_SIZE], const uint8_t *a_input, size_t a_inlen);

#ifdef __cplusplus
}
#endif

#endif /* _DAP_SHA1_H_ */
