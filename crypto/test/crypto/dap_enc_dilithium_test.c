#include "dap_enc_dilithium_test.h"
#include "dap_enc_dilithium.h"
//#include "ringct20/ringct20_params.h"
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

    dilithium_private_key *sk;
    dilithium_public_key *pk;
    dilithium_kind_t kind;
    unsigned char m[MLEN + CRYPTO_BYTES];
    unsigned long long mlen;



    for(int i = 0; i < NTESTS; ++i) {

        randombytes(m, MLEN);

        dilithium_crypto_sign_keypair(pk, sk, kind, const void * seed, size_t seed_size);
        dilithium_crypto_sign( dilithium_signature_t *sig, &m, mlen, pk);

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
