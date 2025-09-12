#include "dap_rand.h"
#include "dap_enc_base64.h"
#include "dap_common.h"

#include <stdlib.h>
#include <string.h>

#define LOG_TAG "dap_rand"
//#define SHISHUA_TARGET 0    // SHISHUA_TARGET_SCALAR
#include "shishua.h"

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <pthread.h>
    static int s_urandom_fd = -1;
    static pthread_once_t s_urandom_init = PTHREAD_ONCE_INIT;
    
    static void init_urandom_fd(void) {
        s_urandom_fd = open("/dev/urandom", O_RDONLY);
    }
    
    // Security fix: add cleanup function for file descriptor
    static void cleanup_urandom_fd(void) {
        if (s_urandom_fd != -1) {
            close(s_urandom_fd);
            s_urandom_fd = -1;
        }
    }
    
    // Register cleanup function to be called at exit
    __attribute__((constructor))
    static void register_cleanup(void) {
        atexit(cleanup_urandom_fd);
    }
#endif

#define passed 0 
#define failed 1

int randombytes(void* random_array, unsigned int nbytes)
{ // Generation of "nbytes" of random values
    
#if defined(_WIN32)
    HCRYPTPROV p;

    if (CryptAcquireContext(&p, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) == FALSE) {
      return failed;
    }

    if (CryptGenRandom(p, nbytes, (BYTE*)random_array) == FALSE) {
      CryptReleaseContext(p, 0);
      return failed;
    }

    CryptReleaseContext(p, 0);
#else
    pthread_once(&s_urandom_init, init_urandom_fd);
    
    if (s_urandom_fd == -1) {
        return failed;
    }
    
    int bytes_read = 0;
    while (bytes_read < (int)nbytes) {
        int r = read(s_urandom_fd, (char*)random_array + bytes_read, 
                    nbytes - bytes_read);
        if (r > 0) {
            bytes_read += r;
        } else if (r == 0) {
            // EOF on /dev/urandom should never happen, this is a critical error
            log_it(L_CRITICAL, "Unexpected EOF on /dev/urandom");
            return failed;
        } else if (errno != EINTR) {
            // Any error other than EINTR is critical for crypto security
            log_it(L_CRITICAL, "Critical error reading from /dev/urandom: %s", strerror(errno));
            return failed;
        }
        // Only EINTR continues the loop
    }
#endif
    return passed;
}

int randombase64(void*random_array, unsigned int size)
{
    // Early parameter validation
    if (!random_array || size == 0) return failed;
    
    // Special handling for small sizes (1-4 chars + null-terminator)
    if (size <= 5) {
        // Generate one full base64 block (4 chars) and truncate
        uint8_t l_binary_data[3];
        if (randombytes(l_binary_data, 3) != 0)
            return failed;
            
        char l_temp_base64[5];  // 4 chars + null terminator
        size_t l_encoded_size = dap_enc_base64_encode(l_binary_data, 3, 
                                                      l_temp_base64, DAP_ENC_DATA_TYPE_B64);
        
        // Copy only what fits in user buffer (including null-terminator)
        unsigned int l_copy_size = dap_min(l_encoded_size, size - 1);
        memcpy(random_array, l_temp_base64, l_copy_size);
        ((char*)random_array)[l_copy_size] = '\0';
        return passed;
    }
    
    // Normal handling for larger sizes
    // Calculate binary bytes that will give us ≤ size-1 base64 chars (reserve space for null-terminator)
    unsigned int l_max_chars = size - 1;  // Reserve 1 byte for null-terminator  
    unsigned int l_binary_bytes = (l_max_chars / 4) * 3;  // Complete 4-char blocks only
    
    // Allocate temporary buffer for binary data
    uint8_t *l_binary_data = DAP_NEW_Z_SIZE(uint8_t, l_binary_bytes);
    if (!l_binary_data)
        return failed;
    
    // Generate random binary data
    if (randombytes(l_binary_data, l_binary_bytes) != 0) {
        DAP_DELETE(l_binary_data);
        return failed;
    }
    
    // Encode directly to user buffer (safe - will never overflow)
    size_t l_encoded_size = dap_enc_base64_encode(l_binary_data, l_binary_bytes, 
                                                  random_array, DAP_ENC_DATA_TYPE_B64);
    DAP_DELETE(l_binary_data);
    
    ((char*)random_array)[dap_min(l_encoded_size, size - 1)] = '\0';
    
    return passed;
}

/*** Custom uint256 pseudo-random generator section ***/

#define DAP_SHISHUA_BUFF_SIZE 4

static prng_state s_shishua_state = {0};
static uint256_t s_shishua_out[DAP_SHISHUA_BUFF_SIZE] __attribute__((aligned(128)));
static atomic_uint_fast8_t s_shishua_idx = 0;

// Set the seed for pseudo-random generator with uint256 format
void dap_pseudo_random_seed(uint256_t a_seed)
{
    uint64_t l_seed[4] = {a_seed._hi.a, a_seed._hi.b, a_seed._lo.a, a_seed._lo.b};
    prng_init(&s_shishua_state, l_seed);
    s_shishua_idx = 0;
}

// Get a next pseudo-random number in 0..a_rand_max range inclusive
uint256_t dap_pseudo_random_get(uint256_t a_rand_max, uint256_t *a_raw_result)
{
    uint256_t l_tmp, l_ret, l_rand_ceil;
    atomic_uint_fast8_t l_prev_idx = atomic_fetch_add(&s_shishua_idx, 1);
    int l_buf_pos = l_prev_idx % DAP_SHISHUA_BUFF_SIZE;
    if (l_buf_pos == 0)
        prng_gen(&s_shishua_state, (uint8_t *)s_shishua_out, DAP_SHISHUA_BUFF_SIZE * sizeof(uint256_t));
    if (IS_ZERO_256(a_rand_max))
        return uint256_0;
    uint256_t l_out_raw = s_shishua_out[l_buf_pos];
    if (a_raw_result)
        *a_raw_result = l_out_raw;
    if (EQUAL_256(a_rand_max, uint256_max))
        return l_out_raw;
    SUM_256_256(a_rand_max, uint256_1, &l_rand_ceil);
    divmod_impl_256(l_out_raw, l_rand_ceil, &l_tmp, &l_ret);
    return l_ret;
}

// Cleanup function for proper resource management
void dap_rand_cleanup(void) {
#if !defined(_WIN32)
    if (s_urandom_fd != -1) {
        close(s_urandom_fd);
        s_urandom_fd = -1;
    }
#endif
}
