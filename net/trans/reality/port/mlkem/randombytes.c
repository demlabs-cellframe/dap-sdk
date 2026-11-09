#define OPENSSL_SUPPRESS_DEPRECATED
#include "randombytes.h"
#include <openssl/rand.h>
#include <stdlib.h>

void randombytes(uint8_t *out, size_t outlen)
{
    if (RAND_bytes(out, (int)outlen) != 1) abort(); /* CSPRNG failure is fatal */
}
