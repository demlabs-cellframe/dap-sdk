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
#include <errno.h>
#include <pthread.h>

#define RAND_BUF_SIZE 4096

/*
 * Thread-safety model (CR-D22 fix):
 *
 * The buffered path uses a process-wide refill buffer. Without a mutex,
 * two threads could both pass the refill check, both read the same bytes
 * from `s_buf` and hand identical "random" output to each separate
 * consumer (HOTS nonces, key seeds, challenge polynomials, …). That is a
 * catastrophic failure of a CSPRNG contract, so every access to the
 * shared buffer and lazy fd initialisation must be serialised.
 *
 *  * s_urandom_fd is installed exactly once via pthread_once.
 *  * All reads/writes to s_buf / s_buf_pos happen under s_buf_mtx.
 *  * Large requests bypass the buffer and go straight to
 *    s_read_urandom_locked(), which just calls read() on the fd —
 *    parallel read() from a character device is safe on Linux and BSD.
 */

static int     s_urandom_fd = -1;
static uint8_t s_buf[RAND_BUF_SIZE];
static unsigned s_buf_pos = RAND_BUF_SIZE;

static pthread_once_t  s_fd_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t s_buf_mtx = PTHREAD_MUTEX_INITIALIZER;

static inline void s_delay(unsigned int count)
{
    while (count--) {}
}

static void s_urandom_open_once(void)
{
    while (s_urandom_fd == -1) {
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd != -1) {
            s_urandom_fd = fd;
            break;
        }
        if (errno != EINTR) {
            // Fail-fast: without /dev/urandom we cannot produce secure
            // randomness. Returning 0-filled buffers to callers would be
            // worse than retrying.
            s_delay(0xFFFFF);
        }
    }
}

static void s_read_urandom_fd(void *a_buf, unsigned a_nbytes)
{
    pthread_once(&s_fd_once, s_urandom_open_once);
    int n = (int)a_nbytes;
    for (int i = 0; i < n; ) {
        int r = read(s_urandom_fd, (char *)a_buf + i, n - i);
        if (r > 0) {
            i += r;
        } else if (r == 0) {
            // EOF on /dev/urandom is unexpected — spin with a short delay
            // to allow the kernel to replenish the entropy pool.
            s_delay(0xFFFF);
        } else {
            if (errno == EINTR)
                continue;
            s_delay(0xFFFF);
        }
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
    if (a_nbytes == 0)
        return 0;
    if (a_nbytes >= RAND_BUF_SIZE) {
        // Large requests bypass the buffer entirely: lock-free parallel
        // read() on /dev/urandom is well-defined on Linux and *BSD.
        s_read_urandom_fd(a_buf, a_nbytes);
        return 0;
    }
    uint8_t *dst = (uint8_t *)a_buf;
    unsigned rem = a_nbytes;
    pthread_mutex_lock(&s_buf_mtx);
    while (rem > 0) {
        if (__builtin_expect(s_buf_pos >= RAND_BUF_SIZE, 0)) {
            s_read_urandom_fd(s_buf, RAND_BUF_SIZE);
            s_buf_pos = 0;
        }
        unsigned avail = RAND_BUF_SIZE - s_buf_pos;
        unsigned chunk = rem < avail ? rem : avail;
        memcpy(dst, s_buf + s_buf_pos, chunk);
        // Zero-out already-consumed bytes so a later heap/stack leak of
        // s_buf cannot reveal recently produced randomness.
        memset(s_buf + s_buf_pos, 0, chunk);
        s_buf_pos += chunk;
        dst += chunk;
        rem -= chunk;
    }
    pthread_mutex_unlock(&s_buf_mtx);
    return 0;
#endif
}

uint32_t dap_random_uint32(uint32_t a_max)
{
    if (a_max == 0)
        return 0;
    // Unbiased rejection sampling: avoid modulo bias that is non-negligible
    // when 2^32 is not a multiple of a_max. Biased choice of leaf indices /
    // ring positions would otherwise leak the signer identity on the
    // challenge side.
    //
    // threshold = 2^32 mod a_max  — smallest number of samples we must reject
    // (0 when a_max is a power of two; handled by the loop naturally).
    uint32_t threshold = (uint32_t)(-a_max) % a_max;
    uint32_t ret;
    do {
        dap_random_bytes(&ret, sizeof(ret));
    } while (ret < threshold);
    return ret % a_max;
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
