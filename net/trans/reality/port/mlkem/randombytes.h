#ifndef REALITY_PORT_RANDOMBYTES_H
#define REALITY_PORT_RANDOMBYTES_H
#include <stdint.h>
#include <stddef.h>
/* PQClean hook -- backed by the platform CSPRNG (see randombytes.c) */
void randombytes(uint8_t *out, size_t outlen);
#endif
