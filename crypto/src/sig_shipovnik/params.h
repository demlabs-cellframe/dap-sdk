/*
   This product is distributed under 2-term BSD-license terms

   Copyright (c) 2023, QApp. All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met: 

   1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer. 
   2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution. 

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
   ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <stdint.h>

// main params

#define N_shipovnik 2896
#define K_shipovnik 1448
#define W_shipovnik 318
#define DELTA_shipovnik 219

#define GOST512_OUTPUT_BYTES 64

#define H_PRIME_SIZE 262088
// H' bit matrix [k x (n-k)] (rowwise)
extern const uint8_t H_PRIME[H_PRIME_SIZE];

// derived params

#define SHIPOVNIK_PUBLICKEYBYTES ((N_shipovnik - K_shipovnik) / 8)
#define SHIPOVNIK_SECRETKEYBYTES (N_shipovnik / 8)

#define CS_BYTES (DELTA_shipovnik * 3 * GOST512_OUTPUT_BYTES)
#define SIGMA_BIT_WIDTH 12
#define SIGMA_BYTES (N_shipovnik * sizeof(uint16_t))
#define SIGMA_PACKED_BYTES (SIGMA_BIT_WIDTH * N_shipovnik / 8)

#define SHIPOVNIK_SIGBYTES                                                           \
  (CS_BYTES + DELTA_shipovnik * (SIGMA_PACKED_BYTES + SHIPOVNIK_SECRETKEYBYTES))
