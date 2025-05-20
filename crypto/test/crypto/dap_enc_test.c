#include <unistd.h>
#include "dap_common.h"
#include "dap_enc_test.h"
#include "dap_test.h"
#include "rand/dap_rand.h"
#include "dap_sign.h"
#include "dap_enc.h"
#include "dap_enc_chipmunk_test.h"

#define LOG_TAG "dap_crypto_enc_tests"
#define DAP_CHAIN_ATOM_MAX_SIZE (256 * 1024) // 256 KB

const dap_enc_key_type_t c_key_type_arr[] = {
        DAP_ENC_KEY_TYPE_SIG_TESLA,
        DAP_ENC_KEY_TYPE_SIG_BLISS,
        DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
        DAP_ENC_KEY_TYPE_SIG_FALCON,
        DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS,
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK,
#ifdef DAP_ECDSA
        DAP_ENC_KEY_TYPE_SIG_ECDSA,
        DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM
#endif
#ifdef DAP_SHIPOVNIK
        DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK
#endif
        };
const size_t c_keys_count = sizeof(c_key_type_arr) / sizeof(dap_enc_key_type_t);

int get_cur_time_msec();

#define TEST_SER_FILE_NAME "keystorage.txt"
void test_encypt_decrypt(int count_steps, const dap_enc_key_type_t key_type, const int cipher_key_size)
{
    dap_print_module_name(dap_enc_get_type_name(key_type));
    const int max_source_size = 10000;
    int time_beg = get_cur_time_msec();



    for(int i = 0; i < count_steps; i++) {
        size_t source_size = 0;
        const size_t seed_size = 16;
        uint8_t seed[seed_size];

        const size_t kex_size = 32;
        uint8_t kex[kex_size];
        randombytes(seed, seed_size);
        randombytes(kex, kex_size);

        dap_enc_key_t* key = dap_enc_key_new_generate(key_type, kex, kex_size, seed, seed_size, cipher_key_size);
        source_size = 256;//1 + random_uint32_t(max_source_size);

        uint8_t *source = DAP_NEW_SIZE(uint8_t, source_size);

        randombytes(source, source_size);//randombase64(source, source_size);
        uint8_t * buf_encrypted = NULL;
        uint8_t * buf_decrypted = NULL;


        size_t encrypted_size = key->enc(key, source, source_size, (void**) &buf_encrypted);
        size_t result_size = key->dec(key, buf_encrypted, encrypted_size, (void**) &buf_decrypted);

        dap_assert_PIF(source_size == result_size, "Check result decode size");

        dap_assert_PIF(memcmp(source, buf_decrypted, source_size) == 0,
                "Check source and encode->decode data");

        DAP_DEL_MULTY(source, buf_encrypted, buf_decrypted);
        dap_enc_key_delete(key);
    }
    int time_end = get_cur_time_msec();
    char pass_msg_buf[256];
    snprintf(pass_msg_buf, sizeof(pass_msg_buf), "Encode and decode      %d times T = %f (%f per once)", count_steps, (time_end - time_beg)/1000.0,(time_end - time_beg)/1000.0/count_steps);
    dap_pass_msg(pass_msg_buf);

}

void test_encypt_decrypt_fast(int count_steps, const dap_enc_key_type_t key_type, const int cipher_key_size)
{
    const int max_source_size = 10000;
    dap_print_module_name(dap_enc_get_type_name(key_type));
    char buf_encrypt_out[max_source_size+128];
    char buf_decrypt_out[max_source_size+32];
    int time_beg = get_cur_time_msec();


    size_t seed_size = 16;
    uint8_t seed[seed_size];

    size_t kex_size = 32;
    uint8_t kex[kex_size];

    randombytes(seed, seed_size);
    randombytes(kex, kex_size);

    dap_enc_key_t* key = dap_enc_key_new_generate(key_type, kex, kex_size, seed, seed_size, cipher_key_size);

    size_t source_size = 0;

    for(int i = 0; i < count_steps; i++) {
        source_size = 1 + random_uint32_t(max_source_size);
//        printf("ss = %d\n", source_size);fflush(stdout);

        uint8_t *source = DAP_NEW_SIZE(uint8_t,source_size + 0);
        randombytes(source, source_size);//randombase64(source, source_size);


        size_t encrypted_size = key->enc_na(key, source, source_size, buf_encrypt_out, max_source_size+128);

        size_t result_size = key->dec_na(key, buf_encrypt_out, encrypted_size, buf_decrypt_out, max_source_size+32);



        dap_assert_PIF(source_size == result_size, "Check result decode size");

        dap_assert_PIF(memcmp(source, buf_decrypt_out, source_size) == 0,
                "Check source and encode->decode data");
        DAP_DELETE(source);
    }

    dap_enc_key_delete(key);
    int time_end = get_cur_time_msec();
    char pass_msg_buf[256];
    snprintf(pass_msg_buf, sizeof(pass_msg_buf), "Encode and decode fast %d times T = %f (%f per once)", count_steps, (time_end - time_beg)/1000.0,(time_end - time_beg)/1000.0/count_steps);
    dap_pass_msg(pass_msg_buf);
}


static void _encrypt_decrypt(enum dap_enc_key_type key_type,
                             enum dap_enc_data_type data_type,
                             size_t count_steps)
{
    size_t source_size = 1;
    const int MAX_SEED_SIZE = 100;
    uint8_t seed[MAX_SEED_SIZE];
    for (size_t i = 0; i < count_steps; i++) {
        source_size = 1 + random_uint32_t(2000);

        const char *kex_data = "123";
        size_t kex_size = strlen(kex_data);
        const size_t seed_size = 1 + random_uint32_t(MAX_SEED_SIZE-1);

        randombytes(seed, seed_size);
//        printf("i = %d ss = %d, ss=%d\n",i, source_size,seed_size );fflush(stdout);
        uint8_t *source = DAP_NEW_SIZE(uint8_t, source_size);
//        printf(".");fflush(stdout);
        randombytes(source, source_size);
//        printf(".");fflush(stdout);
        dap_enc_key_t* key = dap_enc_key_new_generate(key_type, kex_data, kex_size, seed, seed_size, 0);
//        printf(".");fflush(stdout);

        size_t encrypt_buff_size = dap_enc_code_out_size(key, source_size, data_type);
        uint8_t *encrypt_result = DAP_NEW_SIZE(uint8_t, encrypt_buff_size);
//        printf(".");fflush(stdout);
        size_t encrypted_size = dap_enc_code(key, source,
                                             source_size,
                                             encrypt_result,
                                             encrypt_buff_size,
                                             data_type);
//        printf(".");fflush(stdout);
        size_t min_decode_buff_size = dap_enc_decode_out_size(key, encrypt_buff_size, data_type);
//        printf(".");fflush(stdout);
        uint8_t *decode_result = DAP_NEW_SIZE(uint8_t, min_decode_buff_size);
//        printf(".");fflush(stdout);
        size_t out_size = dap_enc_decode(key,
                                         encrypt_result,
                                         encrypted_size,
                                         decode_result,
                                         min_decode_buff_size,
                                         data_type);
//        printf("source_size = %d, out_size = %d, min_decode_buff_size = %d, encrypt_buff_size = %d, encrypted_size = %d\n",
//               source_size, out_size,min_decode_buff_size, encrypt_buff_size, encrypted_size);
//        printf("%.2x%.2x\n", source[0], source[1]);
//        printf(".");fflush(stdout);

        dap_assert_PIF(source_size == out_size, "Check result decode size");

//        printf(".");fflush(stdout);
        dap_assert_PIF(memcmp(source, decode_result, source_size) == 0, "Check source and encode->decode data");
//        printf(".");fflush(stdout);
//#ifdef xxxxx



//#endif
        DAP_DEL_MULTY(decode_result, encrypt_result, source);
        dap_enc_key_delete(key);
    }
}

void test_encode_decode_raw(size_t count_steps)
{
    _encrypt_decrypt(DAP_ENC_KEY_TYPE_IAES, DAP_ENC_DATA_TYPE_RAW, count_steps);
    dap_pass_msg("Encode->decode raw");
}

void test_encode_decode_raw_b64(size_t count_steps)
{
    _encrypt_decrypt(DAP_ENC_KEY_TYPE_IAES, DAP_ENC_DATA_TYPE_B64, count_steps);
    dap_pass_msg("Encode->decode raw base64");
}

void test_encode_decode_raw_b64_url_safe(size_t count_steps)
{
    _encrypt_decrypt(DAP_ENC_KEY_TYPE_IAES, DAP_ENC_DATA_TYPE_B64_URLSAFE, count_steps);
    dap_pass_msg("Encode->decode raw base64 url safe");
}

void dap_init_test_case()
{
    srand((uint32_t) time(NULL));
    dap_enc_key_init();
}

void dap_cleanup_test_case()
{
    dap_enc_key_deinit();
}

static void _write_key_in_file(void* key, size_t key_size,
                               const char* file_name)
{
    FILE *f = fopen(file_name, "wb");
    dap_assert(f, "Create file");
    fwrite(key, key_size, 1, f);
    fclose(f);
}

void* _read_key_from_file(const char* file_name, size_t key_size)
{
    FILE *f = fopen(file_name, "rb");
    dap_assert(f, "Open key file");
    void* resut_key = calloc(1, key_size);//sizeof(dap_enc_key_serialize_t)
    fread(resut_key, key_size, 1, f);// sizeof(dap_enc_key_serialize_t)
    fclose(f);
    return resut_key;
}

static void test_key_generate_by_seed(dap_enc_key_type_t a_key_type)
{
    const size_t seed_size = 1 + random_uint32_t( 1000);
    uint8_t seed[seed_size];
    randombytes(seed, seed_size);

    size_t
        l_priv_key_data_size_1 = 0,
        l_priv_key_data_size_2 = 0,
        l_priv_key_data_size_3 = 0,
        l_priv_key_data_size_4 = 0,
        l_priv_key_data_size_5 = 0,
        l_pub_key_data_size_1 = 0,
        l_pub_key_data_size_2 = 0,
        l_pub_key_data_size_3 = 0,
        l_pub_key_data_size_4 = 0,
        l_pub_key_data_size_5 = 0;

    dap_enc_key_t* l_key_1 = dap_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);
    dap_enc_key_t* l_key_2 = dap_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);
    dap_enc_key_t* l_key_3 = dap_enc_key_new_generate(a_key_type, NULL, 0, NULL, seed_size, 0);
    dap_enc_key_t* l_key_4 = dap_enc_key_new_generate(a_key_type, NULL, 0, seed, 0, 0);
    dap_enc_key_t* l_key_5 = dap_enc_key_new_generate(a_key_type, NULL, 0, NULL, 0, 0);


    uint8_t *l_priv_key_data_1 = dap_enc_key_serialize_priv_key(l_key_1, &l_priv_key_data_size_1);
    uint8_t *l_priv_key_data_2 = dap_enc_key_serialize_priv_key(l_key_2, &l_priv_key_data_size_2);
    uint8_t *l_priv_key_data_3 = dap_enc_key_serialize_priv_key(l_key_3, &l_priv_key_data_size_3);
    uint8_t *l_priv_key_data_4 = dap_enc_key_serialize_priv_key(l_key_4, &l_priv_key_data_size_4);
    uint8_t *l_priv_key_data_5 = dap_enc_key_serialize_priv_key(l_key_5, &l_priv_key_data_size_5);

    uint8_t *l_pub_key_data_1 = dap_enc_key_serialize_pub_key(l_key_1, &l_pub_key_data_size_1);
    uint8_t *l_pub_key_data_2 = dap_enc_key_serialize_pub_key(l_key_2, &l_pub_key_data_size_2);
    uint8_t *l_pub_key_data_3 = dap_enc_key_serialize_pub_key(l_key_3, &l_pub_key_data_size_3);
    uint8_t *l_pub_key_data_4 = dap_enc_key_serialize_pub_key(l_key_4, &l_pub_key_data_size_4);
    uint8_t *l_pub_key_data_5 = dap_enc_key_serialize_pub_key(l_key_5, &l_pub_key_data_size_5);

    dap_assert_PIF(l_priv_key_data_size_1 && l_pub_key_data_size_1 &&
                    l_priv_key_data_size_2 && l_pub_key_data_size_2 &&
                    l_priv_key_data_size_3 && l_pub_key_data_size_3 &&
                    l_priv_key_data_size_4 && l_pub_key_data_size_4 &&
                    l_priv_key_data_size_5 && l_pub_key_data_size_5 &&

                    l_priv_key_data_1 && l_pub_key_data_1 &&
                    l_priv_key_data_2 && l_pub_key_data_2 &&
                    l_priv_key_data_3 && l_pub_key_data_3 &&
                    l_priv_key_data_4 && l_pub_key_data_4 &&
                    l_priv_key_data_5 && l_pub_key_data_5,
                    "Priv and pub data serialisation");

    dap_assert_PIF(l_priv_key_data_size_1 == l_priv_key_data_size_2, "Equal priv_key_data_size");
    dap_assert_PIF(l_priv_key_data_size_1 == l_priv_key_data_size_3, "Equal priv_key_data_size");
    dap_assert_PIF(l_priv_key_data_size_1 == l_priv_key_data_size_4, "Equal priv_key_data_size");
    dap_assert_PIF(l_priv_key_data_size_1 == l_priv_key_data_size_5, "Equal priv_key_data_size");

    dap_assert_PIF(l_pub_key_data_size_1 == l_pub_key_data_size_2, "Equal pub_key_data_size");
    dap_assert_PIF(l_pub_key_data_size_1 == l_pub_key_data_size_3, "Equal pub_key_data_size");
    dap_assert_PIF(l_pub_key_data_size_1 == l_pub_key_data_size_4, "Equal pub_key_data_size");
    dap_assert_PIF(l_pub_key_data_size_1 == l_pub_key_data_size_5, "Equal pub_key_data_size");

    dap_assert_PIF(!memcmp(l_priv_key_data_1, l_priv_key_data_2, l_priv_key_data_size_1), "Equal priv_key_data with same seed");
    dap_assert_PIF(memcmp(l_priv_key_data_1, l_priv_key_data_3, l_priv_key_data_size_1), "Different priv_key_data with not same seed");
    dap_assert_PIF(memcmp(l_priv_key_data_1, l_priv_key_data_4, l_priv_key_data_size_1), "Different priv_key_data with not same seed");
    dap_assert_PIF(memcmp(l_priv_key_data_1, l_priv_key_data_5, l_priv_key_data_size_1), "Different priv_key_data with not same seed");

    dap_assert_PIF(memcmp(l_priv_key_data_3, l_priv_key_data_4, l_priv_key_data_size_1), "Different priv_key_data without seed");
    dap_assert_PIF(memcmp(l_priv_key_data_3, l_priv_key_data_5, l_priv_key_data_size_1), "Different priv_key_data without seed");
    dap_assert_PIF(memcmp(l_priv_key_data_4, l_priv_key_data_5, l_priv_key_data_size_1), "Different priv_key_data without seed");

    dap_assert_PIF(!memcmp(l_pub_key_data_1, l_pub_key_data_2, l_pub_key_data_size_1), "Equal pub_key_data with same seed");
    dap_assert_PIF(memcmp(l_pub_key_data_1, l_pub_key_data_3, l_pub_key_data_size_1), "Different pub_key_data with not same seed");
    dap_assert_PIF(memcmp(l_pub_key_data_1, l_pub_key_data_4, l_pub_key_data_size_1), "Different pub_key_data with not same seed");
    dap_assert_PIF(memcmp(l_pub_key_data_1, l_pub_key_data_5, l_pub_key_data_size_1), "Different pub_key_data with not same seed");

    dap_assert_PIF(memcmp(l_pub_key_data_3, l_pub_key_data_4, l_pub_key_data_size_1), "Different pub_key_data without seed");
    dap_assert_PIF(memcmp(l_pub_key_data_3, l_pub_key_data_5, l_pub_key_data_size_1), "Different pub_key_data without seed");
    dap_assert_PIF(memcmp(l_pub_key_data_4, l_pub_key_data_5, l_pub_key_data_size_1), "Different pub_key_data without seed");

    dap_enc_key_delete(l_key_1);
    dap_enc_key_delete(l_key_2);
    dap_enc_key_delete(l_key_3);
    dap_enc_key_delete(l_key_4);
    dap_enc_key_delete(l_key_5);

    DAP_DEL_MULTY(  l_priv_key_data_1, l_pub_key_data_1,
                    l_priv_key_data_2, l_pub_key_data_2,
                    l_priv_key_data_3, l_pub_key_data_3,
                    l_priv_key_data_4, l_pub_key_data_4,
                    l_priv_key_data_5, l_pub_key_data_5);
    dap_assert(true, s_key_type_to_str(a_key_type));
}

/**
 * @key_type may be DAP_ENC_KEY_TYPE_IAES, DAP_ENC_KEY_TYPE_OAES
 */
static void test_serialize_deserialize(dap_enc_key_type_t key_type, bool enc_test)
{
    const char *kex_data = "1234567890123456789012345678901234567890";//"123";
    size_t kex_size = strlen(kex_data);
    const size_t seed_size = 1 + random_uint32_t( 1000);
    uint8_t seed[seed_size];

    randombytes(seed, seed_size);

//  for key_type==DAP_ENC_KEY_TYPE_OAES must be: key_size=[16|24|32] and kex_size>=key_size
    dap_enc_key_t* key = dap_enc_key_new_generate(key_type, kex_data, kex_size, seed, seed_size, 32);
    size_t l_buflen = 0;
    uint8_t *l_ser_key = dap_enc_key_serialize(key, &l_buflen);
    _write_key_in_file(l_ser_key, l_buflen, TEST_SER_FILE_NAME);
    uint8_t *l_deser_key = _read_key_from_file(TEST_SER_FILE_NAME, l_buflen);
    dap_assert(!memcmp(l_ser_key, l_deser_key, l_buflen),
               "dap_enc_key_serialize_t equals");

    dap_enc_key_t *key3 = dap_enc_key_deserialize(l_deser_key, l_buflen);
    dap_assert(key3, "Key deserialize done");
    dap_enc_key_t *key2 = dap_enc_key_dup(key3);
    dap_assert(key2, "Key dup done");

    dap_assert(key->type == key2->type, "Key type");
    dap_assert(key->last_used_timestamp == key2->last_used_timestamp,
               "Last used timestamp");
    dap_assert(key->priv_key_data_size == key2->priv_key_data_size, "Priv key data size");
    dap_assert(key->pub_key_data_size == key2->pub_key_data_size, "Pub key data size");

    size_t l_ser_skey_len_1 = 0, l_ser_pkey_len_1 = 0, l_ser_skey_len_2 = 0, l_ser_pkey_len_2 = 0;
    uint8_t *l_ser_skey_1 = dap_enc_key_serialize_priv_key(key, &l_ser_skey_len_1);
    uint8_t *l_ser_skey_2 = dap_enc_key_serialize_priv_key(key2, &l_ser_skey_len_2);
    dap_assert(l_ser_skey_len_1 == l_ser_skey_len_2, "Priv key data size");
    dap_assert(!memcmp(l_ser_skey_1, l_ser_skey_2, l_ser_skey_len_1), "Priv key data");

    uint8_t *l_ser_pkey_1 = dap_enc_key_serialize_pub_key(key, &l_ser_pkey_len_1);
    uint8_t *l_ser_pkey_2 = dap_enc_key_serialize_pub_key(key2, &l_ser_pkey_len_2);
    dap_assert(l_ser_pkey_len_1 == l_ser_pkey_len_2, "Pub key data size");
    dap_assert(!memcmp(l_ser_pkey_1, l_ser_pkey_2, l_ser_pkey_len_1), "Pub key data");


    dap_assert(key->_inheritor_size == key2->_inheritor_size, "Inheritor data size");
    dap_assert(!memcmp(key->_inheritor, key2->_inheritor, key->_inheritor_size), "Inheritor data");

    if (enc_test) {    
        const char* source = "simple test";
        size_t source_size = strlen(source);

        size_t encrypt_size = dap_enc_code_out_size(key, source_size, DAP_ENC_DATA_TYPE_RAW);
        uint8_t encrypt_result[encrypt_size];


        size_t encrypted_size = dap_enc_code(key2, source,
                                            source_size,
                                            encrypt_result,
                                            encrypt_size,
                                            DAP_ENC_DATA_TYPE_RAW);

        size_t min_decode_size = dap_enc_decode_out_size(key, encrypt_size, DAP_ENC_DATA_TYPE_RAW);

        uint8_t decode_result[min_decode_size];
        size_t decode_size = dap_enc_decode(key,
                                            encrypt_result,
                                            encrypted_size,
                                            decode_result,
                                            min_decode_size,
                                            DAP_ENC_DATA_TYPE_RAW);
        dap_assert_PIF(source_size == decode_size, "Check result decode size");

        dap_assert_PIF(memcmp(source, decode_result, source_size) == 0,
                    "Check source and encode->decode data");
    }

    dap_enc_key_delete(key);
    dap_enc_key_delete(key2);
    dap_enc_key_delete(key3);

    dap_pass_msg("Key serialize->deserialize");
    unlink(TEST_SER_FILE_NAME);
    DAP_DEL_MULTY(l_ser_key, l_deser_key, l_ser_skey_1, l_ser_skey_2, l_ser_pkey_1, l_ser_pkey_2);
}

/**
 * @key_type may be DAP_ENC_KEY_TYPE_SIG_BLISS, DAP_ENC_KEY_TYPE_SIG_TESLA, DAP_ENC_KEY_TYPE_SIG_PICNIC
 */
static void test_serialize_deserialize_pub_priv(dap_enc_key_type_t key_type)
{
    const char *kex_data = "1234567890123456789012345678901234567890"; //"123";
    size_t kex_size = strlen(kex_data);
    const size_t seed_size = 1 + random_uint32_t( 1000);
    uint8_t seed[seed_size];
    randombytes(seed, seed_size);

    // Generate key
    dap_enc_key_t* key = dap_enc_key_new_generate(key_type, kex_data, kex_size, seed, seed_size, 32);
    // Serialize key & save/read to/from buf
    size_t l_data_pub_size = 0;
    uint8_t *l_data_pub = dap_enc_key_serialize_pub_key(key, &l_data_pub_size);
    _write_key_in_file(l_data_pub, l_data_pub_size, TEST_SER_FILE_NAME);
    uint8_t *l_data_pub_read = _read_key_from_file(TEST_SER_FILE_NAME, l_data_pub_size);

    size_t l_data_priv_size = 0;
    uint8_t *l_data_priv = dap_enc_key_serialize_priv_key(key, &l_data_priv_size);
    _write_key_in_file(l_data_priv, l_data_priv_size, TEST_SER_FILE_NAME);
    uint8_t *l_data_priv_read = _read_key_from_file(TEST_SER_FILE_NAME, l_data_priv_size);

    // create new key2
    dap_enc_key_t *key2 = dap_enc_key_new(key_type);
    // Deserialize key2
    dap_assert(!dap_enc_key_deserialize_pub_key(key2, l_data_pub_read, l_data_pub_size), "Pub key deserialize");
    dap_assert(!dap_enc_key_deserialize_priv_key(key2, l_data_priv_read, l_data_priv_size), "Priv key deserialize");

    DAP_DEL_MULTY(l_data_pub, l_data_pub_read, l_data_priv, l_data_priv_read);

    dap_assert(key->priv_key_data_size == key2->priv_key_data_size, "Priv key data size");
    dap_assert(key->pub_key_data_size == key2->pub_key_data_size, "Pub key data size");
    dap_pass_msg("Key serialize->deserialize");

    size_t source_size = 10 + random_uint32_t( 20);
    uint8_t source_buf[source_size];
    size_t sig_buf_size = 0;
    uint8_t *sig_buf = NULL;
    randombytes(source_buf, source_size);

    // encode by key
    int is_sig = 0, is_vefify = 0;
    switch (key_type) {
        case DAP_ENC_KEY_TYPE_SIG_BLISS:
        case DAP_ENC_KEY_TYPE_SIG_PICNIC:
        case DAP_ENC_KEY_TYPE_SIG_TESLA:
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        case DAP_ENC_KEY_TYPE_SIG_ECDSA:
        case DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK:
        case DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM:
            sig_buf_size = dap_sign_create_output_unserialized_calc_size(key);
            sig_buf = calloc(sig_buf_size, 1);
            is_sig = key->sign_get(key, source_buf, source_size, sig_buf, sig_buf_size);
            break;
        default:
            sig_buf_size = 0;
    }
    dap_enc_key_delete(key);

    dap_assert_PIF(sig_buf_size > 0 && !is_sig, "Check make signature");

    // serialize & deserialize signature
    size_t sig_buf_len = sig_buf_size;
    uint8_t *l_sign_tmp = dap_enc_key_serialize_sign(key2->type, sig_buf, &sig_buf_len);
    dap_enc_key_signature_delete(key_type, sig_buf);
    sig_buf = dap_enc_key_deserialize_sign(key2->type, l_sign_tmp, &sig_buf_len);
    DAP_DELETE(l_sign_tmp);

    dap_assert_PIF(sig_buf, "Check serialize->deserialize signature");
    dap_assert(sig_buf_len < DAP_CHAIN_ATOM_MAX_SIZE, "Check signature size");  // if fail new sign, recheck define in cellframe-sdk

    // decode by key2
    switch (key_type) {
    case DAP_ENC_KEY_TYPE_SIG_BLISS:
    case DAP_ENC_KEY_TYPE_SIG_PICNIC:
    case DAP_ENC_KEY_TYPE_SIG_TESLA:
    case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
    case DAP_ENC_KEY_TYPE_SIG_FALCON:
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
    case DAP_ENC_KEY_TYPE_SIG_ECDSA:
    case DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK:
    case DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM:
        is_vefify = key2->sign_verify(key2, source_buf, source_size, sig_buf, sig_buf_size);
        break;
    default:
        is_vefify = 1;
    }
    //dap_enc_key_delete(key);
    dap_enc_key_delete(key2);
    dap_enc_key_signature_delete(key_type, sig_buf);


    dap_assert_PIF(!is_vefify, "Check verify signature");

    dap_pass_msg("Verify signature");
    unlink(TEST_SER_FILE_NAME);
}

void dap_enc_tests_run() {
    dap_print_module_name("dap_enc");
    dap_init_test_case();
    test_encode_decode_raw(500);
    test_encode_decode_raw_b64(500);
    test_encode_decode_raw_b64_url_safe(500);

    dap_print_module_name("key generate by seed");
    for (size_t i = 0; i < c_keys_count; ++i) {
        test_key_generate_by_seed(c_key_type_arr[i]);
    }

    for (size_t i = 0; i < c_keys_count; ++i) {
        char l_module_name[128] = { 0 };
        snprintf(l_module_name, sizeof(l_module_name) - 1, "dap_enc serialize->deserialize %s", s_key_type_to_str(c_key_type_arr[i]));
        dap_print_module_name(l_module_name);
        test_serialize_deserialize(c_key_type_arr[i], false);
    }

    dap_print_module_name("dap_enc serialize->deserialize IAES");
    test_serialize_deserialize(DAP_ENC_KEY_TYPE_IAES, true);
    dap_print_module_name("dap_enc serialize->deserialize OAES");
    test_serialize_deserialize(DAP_ENC_KEY_TYPE_OAES, true);

    for (size_t i = 0; i < c_keys_count; ++i) {
        char l_module_name[128] = { 0 };
        snprintf(l_module_name, sizeof(l_module_name) - 1, "dap_enc_sig serialize->deserialize %s", s_key_type_to_str(c_key_type_arr[i]));
        dap_print_module_name(l_module_name);
        test_serialize_deserialize_pub_priv(c_key_type_arr[i]);
    }
    // Добавляем тесты модуля Chipmunk
    dap_enc_chipmunk_tests_run();
    dap_cleanup_test_case();
}
