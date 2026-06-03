#include "msrln_priv.h"
#include "dap_rand.h"

#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"

#define LOG_TAG "RANDOM"

CRYPTO_MSRLN_STATUS MSRLN_generate_a(const unsigned char* seed, unsigned int seed_nbytes, unsigned int array_ndigits, uint32_t* a)
{
    // Generation of parameter a using SHAKE128
    unsigned int pos = 0, ctr = 0;
    uint16_t val;
    unsigned int nblocks = 16;
    uint8_t buf[DAP_SHAKE128_RATE * 16];

    uint64_t state[25] = {0};
    dap_hash_shake128_absorb(state, seed, seed_nbytes);
    dap_hash_shake128_squeezeblocks((unsigned char *) buf, nblocks, state);

    while (ctr < array_ndigits) {
        val = (buf[pos] | ((uint16_t) buf[pos + 1] << 8)) & 0x3fff;
        if (val < PARAMETER_Q) {
            a[ctr++] = val;
        }
        pos += 2;
        if (pos > DAP_SHAKE128_RATE * nblocks - 2) {
            nblocks = 1;
            dap_hash_shake128_squeezeblocks((unsigned char *) buf, nblocks, state);
            pos = 0;
        }
    }
    return CRYPTO_MSRLN_SUCCESS;
}

CRYPTO_MSRLN_STATUS MSRLN_get_error(const unsigned char* seed, unsigned int seed_nbytes, unsigned char* nonce, unsigned int nonce_nbytes, unsigned int array_nbytes, unsigned char* stream_array)
{
    UNREFERENCED_PARAMETER(seed);
    UNREFERENCED_PARAMETER(seed_nbytes);
    UNREFERENCED_PARAMETER(nonce);
    UNREFERENCED_PARAMETER(nonce_nbytes);

    dap_random_bytes( stream_array, array_nbytes);

    return CRYPTO_MSRLN_SUCCESS;
}

CRYPTO_MSRLN_STATUS random_bytes(unsigned int nbytes, unsigned char* random_array, RandomBytes RandomBytesFunction)
{ // Output "nbytes" of random values.
  // It makes requests of random values to RandomBytesFunction. If successful, the output is given in "random_array".
  // The caller is responsible for providing the "RandomBytesFunction" function passing random values as octets.

    if (random_array == NULL || RandomBytesFunction == NULL || nbytes == 0) {
        return CRYPTO_MSRLN_ERROR_INVALID_PARAMETER;
    }    
    
    return (RandomBytesFunction)(random_array, nbytes);
}


CRYPTO_MSRLN_STATUS extended_output(const unsigned char* seed, unsigned int seed_nbytes, unsigned int array_ndigits, uint32_t* extended_array, ExtendableOutput ExtendableOutputFunction)
{ // Output "array_ndigits" of values in [0, q-1] using an extendable-output function and a seed of size "seed_nbytes".
  // It makes requests of values to ExtendableOutputFunction. If successful, the output is given in "extended_array".
  // The caller is responsible for providing the "ExtendableOutputFunction" function passing values as 32-bit digits.

    if (seed == NULL || extended_array == NULL || ExtendableOutputFunction == NULL || seed_nbytes == 0 || array_ndigits == 0) {
        return CRYPTO_MSRLN_ERROR_INVALID_PARAMETER;
    }    
    
    return (ExtendableOutputFunction)(seed, seed_nbytes, array_ndigits, extended_array);
}


CRYPTO_MSRLN_STATUS stream_output(const unsigned char* seed, unsigned int seed_nbytes, unsigned char* nonce, unsigned int nonce_nbytes, unsigned int array_nbytes, unsigned char* stream_array, StreamOutput StreamOutputFunction)
{ // Output "array_nbytes" of values using a stream cipher, a seed of size "seed_nbytes" and a nonce of size "nonce_nbytes".  
  // It makes requests of values to StreamOutputFunction. If successful, the output is given in "stream_array".
  // The caller is responsible for providing the "StreamOutputFunction" function passing values as octets.

    if (seed == NULL || stream_array == NULL || StreamOutputFunction == NULL || seed_nbytes == 0 || nonce_nbytes == 0 || array_nbytes == 0) {
        return CRYPTO_MSRLN_ERROR_INVALID_PARAMETER;
    }    
    
    return (StreamOutputFunction)(seed, seed_nbytes, nonce, nonce_nbytes, array_nbytes, stream_array);
}
