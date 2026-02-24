/*
 * Authors:
 * Cellframe SDK Team
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_dap_circular.c
 * @brief Unit tests for circular buffer (dap_cbuf)
 */

#include <dap_test.h>
#include <dap_common.h>
#include <dap_cbuf.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *s_chars_string = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
#define MAX_RESULT_BUF_LEN 8096

/**
 * @brief Simple circular buffer write test
 */
static void test_circular_simple_write(void)
{
    const int l_buf_size = 8;
    dap_cbuf_t l_cb = dap_cbuf_create(l_buf_size);
    dap_assert(l_cb != NULL, "Buffer created");

    dap_cbuf_push(l_cb, s_chars_string, l_buf_size);

    int l_fd[2];
    dap_assert(socketpair(PF_LOCAL, SOCK_STREAM, 0, l_fd) == 0, "Socket pair created");

    int l_ret = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
    dap_assert(l_ret == l_buf_size, "Check ret write in socket");

    l_ret = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
    dap_assert(l_ret == 0, "Check ret write in socket");
    
    char l_result_buf[MAX_RESULT_BUF_LEN] = {0};
    ssize_t l_res = read(l_fd[1], l_result_buf, 44);
    dap_assert(l_res == l_buf_size, "Check buf size");
    dap_assert(memcmp(l_result_buf, s_chars_string, l_buf_size) == 0, "Check result buf");
    dap_assert(dap_cbuf_get_size(l_cb) == 0, "Check data size");

    close(l_fd[0]);
    close(l_fd[1]);
    dap_cbuf_delete(l_cb);
    dap_pass_msg("Test simple");
}

/**
 * @brief Double write to circular buffer test
 */
static void test_circular_double_write(void)
{
    const int l_buf_size = 8;
    const char *l_expected_string = "0123456701";
    int l_expected_string_len = strlen(l_expected_string);
    dap_cbuf_t l_cb = dap_cbuf_create(l_buf_size);
    dap_assert(l_cb != NULL, "Buffer created");

    dap_cbuf_push(l_cb, s_chars_string, l_buf_size);

    int l_fd[2];
    dap_assert(socketpair(PF_LOCAL, SOCK_STREAM, 0, l_fd) == 0, "Socket pair created");

    int l_ret = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
    dap_assert(l_ret > 0, "First write successful");

    dap_cbuf_push(l_cb, s_chars_string, 2);

    l_ret = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
    dap_assert(l_ret == 2, "Check ret write in socket");

    char l_result_buf[MAX_RESULT_BUF_LEN] = {0};
    ssize_t l_res = read(l_fd[1], l_result_buf, 44);
    dap_assert(l_res == l_expected_string_len, "Check buf size");
    dap_assert(dap_str_equals(l_result_buf, l_expected_string), "Check result buf");
    dap_assert(dap_cbuf_get_size(l_cb) == 0, "Check data size");

    dap_cbuf_delete(l_cb);
    close(l_fd[0]);
    close(l_fd[1]);
    dap_pass_msg("Double write");
}

/**
 * @brief Defragmented write to circular buffer test
 */
static void test_circular_defrag_write(void)
{
    const int l_buf_size = 8;
    const char *l_expected_string = "56701201";
    int l_expected_string_len = strlen(l_expected_string);
    dap_cbuf_t l_cb = dap_cbuf_create(l_buf_size);
    dap_assert(l_cb != NULL, "Buffer created");

    dap_cbuf_push(l_cb, s_chars_string, l_buf_size);
    dap_cbuf_pop(l_cb, 5, NULL);
    dap_cbuf_push(l_cb, s_chars_string, 3);
    // expected string here 567012

    int l_fd[2];
    dap_assert(socketpair(PF_LOCAL, SOCK_STREAM, 0, l_fd) == 0, "Socket pair created");

    // write 567012
    int l_ret = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
    dap_assert(l_ret == 6, "Check ret write in socket");

    // push 01
    dap_cbuf_push(l_cb, s_chars_string, 2);

    // write 01
    l_ret = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
    dap_assert(l_ret == 2, "Check ret write in socket");

    char l_result_buf[MAX_RESULT_BUF_LEN] = {0};
    ssize_t l_res = read(l_fd[1], l_result_buf, MAX_RESULT_BUF_LEN);
    dap_assert(l_res == l_expected_string_len, "Check buf size");
    dap_assert(dap_str_equals(l_result_buf, l_expected_string), "Check result buf");
    dap_assert(dap_cbuf_get_size(l_cb) == 0, "Check data size");

    dap_cbuf_delete(l_cb);
    close(l_fd[0]);
    close(l_fd[1]);
    dap_pass_msg("Defrag write");
}

/**
 * @brief Write to bad socket test
 */
static void test_circular_write_bad_socket(void)
{
    const int l_buf_size = 8;
    dap_cbuf_t l_cb = dap_cbuf_create(l_buf_size);
    dap_assert(l_cb != NULL, "Buffer created");

    dap_cbuf_push(l_cb, s_chars_string, l_buf_size);

    int l_fd[2];
    dap_assert(socketpair(PF_LOCAL, SOCK_STREAM, 0, l_fd) == 0, "Socket pair created");
    int l_fd2[2];
    dap_assert(socketpair(PF_LOCAL, SOCK_STREAM, 0, l_fd2) == 0, "Socket pair 2 created");

    close(l_fd[0]);
    int l_ret = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
    dap_assert(l_ret == -1, "Check ret write in socket");

    l_ret = dap_cbuf_write_in_socket(l_cb, l_fd2[0]);
    dap_assert(l_ret == 8, "Check ret write in socket");
    
    char l_result_buf[MAX_RESULT_BUF_LEN] = {0};
    ssize_t l_res = read(l_fd2[1], l_result_buf, MAX_RESULT_BUF_LEN);
    dap_assert(l_res == l_buf_size, "Check buf size");
    dap_assert(dap_strn_equals(l_result_buf, s_chars_string, l_buf_size), "Check result buf");

    l_ret = dap_cbuf_write_in_socket(l_cb, l_fd2[0]);
    dap_assert(l_ret == 0, "Check zero write");
    dap_assert(dap_cbuf_get_size(l_cb) == 0, "Check data size");

    close(l_fd[1]);
    close(l_fd2[0]);
    close(l_fd2[1]);
    dap_cbuf_delete(l_cb);
    dap_pass_msg("Test bad socket");
}

/**
 * @brief Load test for circular buffer
 */
static void test_circular_load(void)
{
    srand(time(NULL));

    int l_iterations = 230;
    const char *l_digits = "123456789";
    const int l_buf_size = strlen(l_digits);
    dap_cbuf_t l_cb = dap_cbuf_create(l_buf_size);
    dap_assert(l_cb != NULL, "Buffer created");

    int l_fd[2];
    dap_assert(socketpair(PF_LOCAL, SOCK_STREAM, 0, l_fd) == 0, "Socket pair created");

    int l_count_written_bytes = 0;

    // defrag buffer
    dap_cbuf_push(l_cb, (void*)l_digits, strlen(l_digits));
    dap_cbuf_pop(l_cb, strlen(l_digits) - 1, NULL);
    dap_cbuf_push(l_cb, (void*)l_digits, 3);
    l_count_written_bytes = 4;

    char l_expected_buffer[MAX_RESULT_BUF_LEN];
    memset(l_expected_buffer, 0, MAX_RESULT_BUF_LEN);
    dap_cbuf_read(l_cb, l_count_written_bytes, l_expected_buffer);

    int l_count_write_bytes = 4;
    do {
        int l_r = dap_cbuf_write_in_socket(l_cb, l_fd[0]);
        dap_assert_PIF(l_r == l_count_write_bytes, "Check write bytes");
        dap_assert_PIF(dap_cbuf_get_size(l_cb) == 0, "buf size must be 0!");

        l_count_write_bytes = rand() % strlen(l_digits);
        dap_cbuf_push(l_cb, (void*)l_digits, l_count_write_bytes);
        memcpy(l_expected_buffer + l_count_written_bytes, l_digits, l_count_write_bytes);
        l_count_written_bytes += l_count_write_bytes;
        
    } while (--l_iterations);
    l_count_written_bytes -= l_count_write_bytes; // last bytes will not be written

    char l_result_buf[MAX_RESULT_BUF_LEN] = {0};
    ssize_t l_res = read(l_fd[1], l_result_buf, MAX_RESULT_BUF_LEN);
    dap_assert(l_res == l_count_written_bytes, "Check count written and read from socket bytes");
    dap_assert(memcmp(l_expected_buffer, l_result_buf, l_res) == 0, "Check expected and result buffer");

    dap_cbuf_delete(l_cb);
    close(l_fd[0]);
    close(l_fd[1]);
    dap_pass_msg("Load test");
}

int main(void)
{
    // switch off debug info from library
    dap_log_level_set(L_CRITICAL);

    dap_print_module_name("dap_circular");

    test_circular_simple_write();
    test_circular_double_write();
    test_circular_defrag_write();
    test_circular_write_bad_socket();
    test_circular_load();

    return 0;
}
