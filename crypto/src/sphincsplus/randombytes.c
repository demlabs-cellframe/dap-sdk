/*
This code was taken from the SPHINCS reference implementation and is public domain.
*/

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
    #include <threads.h>
    static _Thread_local int fd = -1;
#endif

#include "randombytes.h"


void randombytes(unsigned char *x, unsigned long long xlen)
{
#if defined(_WIN32)
    HCRYPTPROV p;
    if (CryptAcquireContext(&p, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) == FALSE) {
      return;
    }
    if (CryptGenRandom(p, xlen, (BYTE*)x) == FALSE) {
      return;
    }
    CryptReleaseContext(p, 0);
#else
    unsigned long long i;
    if (fd == -1) {
        for (;;) {
            fd = open("/dev/urandom", O_RDONLY);
            if (fd != -1) {
                break;
            }
            sleep(1);
        }
    }
    while (xlen > 0) {
        if (xlen < 1048576) {
            i = xlen;
        }
        else {
            i = 1048576;
        }

        i = (unsigned long long)read(fd, x, i);
        if (i < 1) {
            sleep(1);
            continue;
        }

        x += i;
        xlen -= i;
    }
#endif
}
