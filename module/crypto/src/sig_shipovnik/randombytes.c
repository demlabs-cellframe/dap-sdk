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

#include "randombytes.h"

#include <errno.h>
#include <stdlib.h>

#if defined(DAP_OS_WASM) || defined(__EMSCRIPTEN__)
#include <emscripten.h>

void randombytes(uint8_t *out, size_t outlen) {
  EM_ASM({
      var heapBuf = Module.HEAPU8.buffer;
      if (typeof SharedArrayBuffer !== 'undefined' && heapBuf instanceof SharedArrayBuffer) {
          var tmp = new Uint8Array($1);
          crypto.getRandomValues(tmp);
          Module.HEAPU8.set(tmp, $0);
      } else {
          crypto.getRandomValues(new Uint8Array(heapBuf, $0, $1));
      }
  }, out, (int)outlen);
}

#else

#include <fcntl.h>
#include <unistd.h>

void randombytes(uint8_t *out, size_t outlen) {
  static int fd = -1;
  ssize_t ret;

  while (fd == -1) {
    fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1 && errno == EINTR)
      continue;
    else if (fd == -1)
      abort();
  }

  while (outlen > 0) {
    ret = read(fd, out, outlen);
    if (ret == -1 && errno == EINTR)
      continue;
    else if (ret == -1)
      abort();

    out += ret;
    outlen -= ret;
  }
}

#endif /* DAP_OS_WASM */
