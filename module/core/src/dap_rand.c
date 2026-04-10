/*
 * Cryptographically secure random number generation with buffered urandom.
 *
 * Amortizes syscall overhead by reading large chunks from /dev/urandom
 * and serving small requests from an internal buffer.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_rand.h"
#include <string.h>
#include <stdlib.h>

#if defined(DAP_OS_WASM)
#include <emscripten.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>

#define RAND_BUF_SIZE 4096

static int     s_urandom_fd = -1;
static uint8_t s_buf[RAND_BUF_SIZE];
static unsigned s_buf_pos = RAND_BUF_SIZE;

static inline void s_delay(unsigned int count)
{
    while (count--) {}
}

static void s_read_urandom_raw(void *a_buf, unsigned a_nbytes)
{
    if (__builtin_expect(s_urandom_fd == -1, 0)) {
        do {
            s_urandom_fd = open("/dev/urandom", O_RDONLY);
            if (s_urandom_fd == -1)
                s_delay(0xFFFFF);
        } while (s_urandom_fd == -1);
    }
    int n = (int)a_nbytes;
    for (int i = 0; i < n; ) {
        int r = read(s_urandom_fd, (char *)a_buf + i, n - i);
        if (r >= 0)
            i += r;
        else
            s_delay(0xFFFF);
    }
}
#endif

int dap_random_bytes(void *a_buf, unsigned int a_nbytes)
{
#if defined(DAP_OS_WASM)
    EM_ASM({
        var buf = new Uint8Array(Module.HEAPU8.buffer, $0, $1);
        crypto.getRandomValues(buf);
    }, a_buf, a_nbytes);
    return 0;
#elif defined(_WIN32)
    HCRYPTPROV p;
    if (CryptAcquireContext(&p, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) == FALSE)
        return 1;
    if (CryptGenRandom(p, a_nbytes, (BYTE *)a_buf) == FALSE) {
        CryptReleaseContext(p, 0);
        return 1;
    }
    CryptReleaseContext(p, 0);
    return 0;
#else
    if (a_nbytes >= RAND_BUF_SIZE) {
        s_read_urandom_raw(a_buf, a_nbytes);
        return 0;
    }
    uint8_t *dst = (uint8_t *)a_buf;
    unsigned rem = a_nbytes;
    while (rem > 0) {
        if (__builtin_expect(s_buf_pos >= RAND_BUF_SIZE, 0)) {
            s_read_urandom_raw(s_buf, RAND_BUF_SIZE);
            s_buf_pos = 0;
        }
        unsigned avail = RAND_BUF_SIZE - s_buf_pos;
        unsigned chunk = rem < avail ? rem : avail;
        memcpy(dst, s_buf + s_buf_pos, chunk);
        s_buf_pos += chunk;
        dst += chunk;
        rem -= chunk;
    }
    return 0;
#endif
}

uint32_t dap_random_uint32(uint32_t a_max)
{
    uint32_t ret;
    dap_random_bytes(&ret, sizeof(ret));
    ret %= a_max;
    return ret;
}

uint8_t dap_random_byte(void)
{
    uint8_t ret;
    dap_random_bytes(&ret, 1);
    return ret;
}

uint16_t dap_random_uint16(void)
{
    uint16_t ret;
    dap_random_bytes(&ret, sizeof(ret));
    return ret;
}
