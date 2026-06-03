/*
   This product is distributed under 2-term BSD-license terms

   Copyright (c) 2023, QApp. All rights reserved.
   Replaced with a thin wrapper around core dap_random_bytes().
*/

#include "randombytes.h"
#include "dap_rand.h"

void randombytes(uint8_t *out, size_t outlen) {
    dap_random_bytes(out, (unsigned int)outlen);
}
