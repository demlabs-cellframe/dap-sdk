#include "dap_enc_dilithium_test.h"
#include "dap_enc_dilithium.h"
#include "sig_dilithium/dilithium_params.h"
#include "rand/dap_rand.h"

#define MLEN 59
#define NTESTS 10000

void Signature_Test()
{


    size_t j;
    int ret;
    size_t mlen, smlen;
    uint8_t b;
    // uint8_t m[MLEN + CRYPTO_BYTES];
    // uint8_t m2[MLEN + CRYPTO_BYTES];
    // uint8_t sm[MLEN + CRYPTO_BYTES];
    // uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    // uint8_t sk[CRYPTO_SECRETKEYBYTES];

    dilithium_private_key_t *sk;
    dilithium_public_key_t *pk;
    dilithium_kind_t kind;
    unsigned char m[MLEN + CRYPTO_BYTES];
    unsigned long long mlen;

    dilithium_signature *sm;

    void *seed;
    size_t seed_size;



    for(int i = 0; i < NTESTS; ++i) {

        //randombytes(m, MLEN);

        //dilithium_crypto_sign_keypair calls randombytes to populate seed
        dilithium_crypto_sign_keypair(pk, sk, kind, seed, seed_size);

        dilithium_crypto_sign( sm, &m, mlen, pk);

        ret = crypto_sign_open(m2, &mlen, sm, smlen, pk);

        if(ret) {
            fprintf(stderr, "Verification failed\n");
            return -1;
        }
        if(smlen != MLEN + CRYPTO_BYTES) {
            fprintf(stderr, "Signed message lengths wrong\n");
            return -1;
        }
        if(mlen != MLEN) {
            fprintf(stderr, "Message lengths wrong\n");
            return -1;
        }
        for(j = 0; j < MLEN; ++j) {
            if(m2[j] != m[j]) {
                fprintf(stderr, "Messages don't match\n");
                return -1;
            }
        }

        randombytes((uint8_t *)&j, sizeof(j));
        do {
            randombytes(&b, 1);
        } while(!b);
        sm[j % (MLEN + CRYPTO_BYTES)] += b;
        ret = crypto_sign_open(m2, &mlen, sm, smlen, pk);
        if(!ret) {
            fprintf(stderr, "Trivial forgeries possible\n");
            return -1;
        }
    }

    printf("CRYPTO_PUBLICKEYBYTES = %d\n", CRYPTO_PUBLICKEYBYTES);
    printf("CRYPTO_SECRETKEYBYTES = %d\n", CRYPTO_SECRETKEYBYTES);
    printf("CRYPTO_BYTES = %d\n", CRYPTO_BYTES);


}


void dap_enc_dilithium_tests_run(int a_times) {
    dap_print_module_name("dap_enc_dilithium");
    char l_msg[120] = {0};
    sprintf(l_msg, "signing and verifying message %d times", a_times);
    benchmark_mgs_time(l_msg, benchmark_test_time(test_encode_decode_base58, a_times));
}
