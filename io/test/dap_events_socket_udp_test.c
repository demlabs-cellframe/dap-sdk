/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2026
 * All rights reserved.

This file is part of DAP the open source project.

DAP is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DAP is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See more details here <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "dap_common.h"
#include "dap_events_socket_udp.h"
#include "dap_events_socket_udp_test.h"

#define TEST_PASS(name) log_it(L_INFO, "[✓] %s", name)
#define TEST_FAIL(name, reason) do { \
    log_it(L_ERROR, "[✗] %s: %s", name, reason); \
    return -1; \
} while(0)

// =============================================================================
// TEST: Address comparison
// =============================================================================

static int test_addr_equal(void)
{
    const char *TEST_NAME = "dap_events_socket_udp_addr_equal";
    
    // Test IPv4 equality
    struct sockaddr_storage addr1 = {0}, addr2 = {0};
    struct sockaddr_in *addr1_4 = (struct sockaddr_in*)&addr1;
    struct sockaddr_in *addr2_4 = (struct sockaddr_in*)&addr2;
    
    addr1_4->sin_family = AF_INET;
    addr1_4->sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &addr1_4->sin_addr);
    
    addr2_4->sin_family = AF_INET;
    addr2_4->sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &addr2_4->sin_addr);
    
    if (!dap_events_socket_udp_addr_equal(&addr1, &addr2)) {
        TEST_FAIL(TEST_NAME, "Identical IPv4 addresses not equal");
    }
    
    // Test different ports
    addr2_4->sin_port = htons(8081);
    if (dap_events_socket_udp_addr_equal(&addr1, &addr2)) {
        TEST_FAIL(TEST_NAME, "Different ports detected as equal");
    }
    
    // Test different IPs
    addr2_4->sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.2", &addr2_4->sin_addr);
    if (dap_events_socket_udp_addr_equal(&addr1, &addr2)) {
        TEST_FAIL(TEST_NAME, "Different IPs detected as equal");
    }
    
    // Test NULL handling
    if (dap_events_socket_udp_addr_equal(NULL, &addr2)) {
        TEST_FAIL(TEST_NAME, "NULL address detected as equal");
    }
    
    TEST_PASS(TEST_NAME);
    return 0;
}

// =============================================================================
// TEST: Address hashing
// =============================================================================

static int test_addr_hash(void)
{
    const char *TEST_NAME = "dap_events_socket_udp_addr_hash";
    
    struct sockaddr_storage addr1 = {0}, addr2 = {0};
    struct sockaddr_in *addr1_4 = (struct sockaddr_in*)&addr1;
    struct sockaddr_in *addr2_4 = (struct sockaddr_in*)&addr2;
    
    addr1_4->sin_family = AF_INET;
    addr1_4->sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &addr1_4->sin_addr);
    
    addr2_4->sin_family = AF_INET;
    addr2_4->sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &addr2_4->sin_addr);
    
    uint32_t hash1 = dap_events_socket_udp_addr_hash(&addr1);
    uint32_t hash2 = dap_events_socket_udp_addr_hash(&addr2);
    
    // Identical addresses should have identical hashes
    if (hash1 != hash2) {
        TEST_FAIL(TEST_NAME, "Identical addresses have different hashes");
    }
    
    // Different addresses should have different hashes (probabilistic)
    addr2_4->sin_port = htons(8081);
    hash2 = dap_events_socket_udp_addr_hash(&addr2);
    if (hash1 == hash2) {
        TEST_FAIL(TEST_NAME, "Different addresses have same hash (collision)");
    }
    
    // NULL should return 0
    if (dap_events_socket_udp_addr_hash(NULL) != 0) {
        TEST_FAIL(TEST_NAME, "NULL address hash is not 0");
    }
    
    TEST_PASS(TEST_NAME);
    return 0;
}

// =============================================================================
// TEST: Address to string conversion
// =============================================================================

static int test_addr_to_string(void)
{
    const char *TEST_NAME = "dap_events_socket_udp_addr_to_string";
    
    struct sockaddr_storage addr = {0};
    struct sockaddr_in *addr_4 = (struct sockaddr_in*)&addr;
    
    addr_4->sin_family = AF_INET;
    addr_4->sin_port = htons(8080);
    inet_pton(AF_INET, "192.168.1.1", &addr_4->sin_addr);
    
    const char *str = dap_events_socket_udp_addr_to_string(&addr);
    
    if (!str) {
        TEST_FAIL(TEST_NAME, "Returned NULL for valid address");
    }
    
    // Should contain IP
    if (strstr(str, "192.168.1.1") == NULL) {
        TEST_FAIL(TEST_NAME, "String doesn't contain IP address");
    }
    
    // Should contain port
    if (strstr(str, "8080") == NULL) {
        TEST_FAIL(TEST_NAME, "String doesn't contain port");
    }
    
    // NULL handling
    const char *null_str = dap_events_socket_udp_addr_to_string(NULL);
    if (null_str == NULL || strcmp(null_str, "(null)") != 0) {
        TEST_FAIL(TEST_NAME, "NULL address not handled correctly");
    }
    
    TEST_PASS(TEST_NAME);
    return 0;
}

// =============================================================================
// TEST: Packet forwarding (basic API check)
// =============================================================================

static int test_forward_packet(void)
{
    const char *TEST_NAME = "dap_events_socket_udp_forward_packet";
    
    // Test NULL handling
    if (dap_events_socket_udp_forward_packet(NULL, NULL) >= 0) {
        TEST_FAIL(TEST_NAME, "NULL arguments not rejected");
    }
    
    // Note: Full test requires actual esocket infrastructure
    // which is tested in integration tests
    
    TEST_PASS(TEST_NAME);
    return 0;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_events_socket_udp_test_run(void)
{
    log_it(L_INFO, "=== Running dap_events_socket_udp unit tests ===");
    
    int result = 0;
    
    result |= test_addr_equal();
    result |= test_addr_hash();
    result |= test_addr_to_string();
    result |= test_forward_packet();
    
    if (result == 0) {
        log_it(L_INFO, "=== All dap_events_socket_udp tests PASSED ===");
    } else {
        log_it(L_ERROR, "=== Some dap_events_socket_udp tests FAILED ===");
    }
    
    return result;
}

