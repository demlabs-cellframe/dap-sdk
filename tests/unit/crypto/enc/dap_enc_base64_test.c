#include "dap_enc_base64_test.h"
#include "dap_common.h"

void test_encode_decode_base64(int count_steps, dap_enc_data_type_t standard)
{
    size_t source_size = 0;

    for (int i = 1; i <= count_steps; i++) {
        int step = 1 + random_uint32_t( 20 );
        source_size += (size_t)step;
        uint8_t source[source_size];
        char encode_result[DAP_ENC_BASE64_ENCODE_SIZE(source_size)];
        uint8_t decode_result[source_size];
        randombytes(source, source_size);

        size_t encrypted_size = dap_enc_base64_encode(source, source_size, encode_result, standard);
        size_t out_size = dap_enc_base64_decode(encode_result, encrypted_size, decode_result, standard);

        dap_assert_PIF(encrypted_size == DAP_ENC_BASE64_ENCODE_SIZE(source_size), "Calculate encrypted_size");
        dap_assert_PIF(source_size == out_size, "Check result decode size");
        dap_assert_PIF(memcmp(source, decode_result, source_size) == 0, "Check source and encode->decode data");
    }
}

void dap_enc_base64_tests_run(int a_times) {
    dap_print_module_name("BASE64");

    char l_msg[120] = {0};
    int l_t1 = get_cur_time_msec();
    test_encode_decode_base64(a_times, DAP_ENC_DATA_TYPE_B64);
    int l_t2 = get_cur_time_msec();
    sprintf(l_msg, "Encode and decode DAP_ENC_STANDARD_B64 %d times", a_times);
    benchmark_mgs_time(l_msg, l_t2 - l_t1);

    l_t1 = get_cur_time_msec();
    test_encode_decode_base64(a_times, DAP_ENC_DATA_TYPE_B64_URLSAFE);
    l_t2 = get_cur_time_msec();
    sprintf(l_msg, "Encode and decode DAP_ENC_STANDARD_B64_URLSAFE %d times", a_times);
    benchmark_mgs_time(l_msg, l_t2 - l_t1);
}
