/**
 * @file test_dap_net.c
 * @brief Unit tests for DAP network utilities module
 * @details Tests hostname resolution, address parsing, and network utilities
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_net.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_net"

/**
 * @brief Test: Config address parsing
 */
static void s_test_net_parse_config_address(void)
{
    log_it(L_INFO, "Testing config address parsing");
    
    char l_addr[256];
    uint16_t l_port;
    struct sockaddr_storage l_saddr;
    int l_family;
    
    // Test IPv4:port format
    int l_ret = dap_net_parse_config_address("192.168.1.1:8080", 
                                              l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Parse 192.168.1.1:8080 returned: %d", l_ret);
    if (l_ret == 0) {
        log_it(L_DEBUG, "Parsed addr: %s, port: %u", l_addr, l_port);
        dap_assert(l_port == 8080, "Port parsed correctly");
    }
    
    // Test localhost:port format
    l_ret = dap_net_parse_config_address("localhost:9999",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Parse localhost:9999 returned: %d", l_ret);
    if (l_ret == 0) {
        log_it(L_DEBUG, "Parsed addr: %s, port: %u", l_addr, l_port);
        dap_assert(l_port == 9999, "Port parsed correctly");
    }
    
    // Test IPv6:port format
    l_ret = dap_net_parse_config_address("[::1]:8888",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Parse [::1]:8888 returned: %d", l_ret);
    if (l_ret == 0) {
        log_it(L_DEBUG, "Parsed IPv6 addr: %s, port: %u", l_addr, l_port);
        dap_assert(l_port == 8888, "IPv6 port parsed correctly");
    }
    
    // Test invalid format (missing port)
    l_ret = dap_net_parse_config_address("192.168.1.1",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Parse 192.168.1.1 (no port) returned: %d", l_ret);
    
    // Test invalid format (bad port)
    l_ret = dap_net_parse_config_address("192.168.1.1:99999",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Parse 192.168.1.1:99999 (invalid port) returned: %d", l_ret);
    
    // Test NULL handling
    l_ret = dap_net_parse_config_address(NULL, l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Parse NULL address returned: %d", l_ret);
    dap_pass_msg("Config address parsing edge cases handled");
}

/**
 * @brief Test: Hostname resolution
 */
static void s_test_net_resolve_host(void)
{
    log_it(L_INFO, "Testing hostname resolution");
    
    struct sockaddr_storage l_addr_out;
    int l_family;
    
    // Test IPv4 localhost
    int l_ret = dap_net_resolve_host("127.0.0.1", "8080", true, &l_addr_out, &l_family);
    log_it(L_DEBUG, "Resolve 127.0.0.1:8080 returned: %d", l_ret);
    if (l_ret == 0) {
        dap_assert(l_family == AF_INET, "IPv4 family detected");
        dap_pass_msg("IPv4 localhost resolved");
    }
    
    // Test with NULL port
    l_ret = dap_net_resolve_host("localhost", NULL, false, &l_addr_out, &l_family);
    log_it(L_DEBUG, "Resolve localhost (no port) returned: %d", l_ret);
    
    // Test invalid hostname
    l_ret = dap_net_resolve_host("invalid.invalid.invalid.xyz", "8080", 
                                 false, &l_addr_out, &l_family);
    log_it(L_DEBUG, "Resolve invalid hostname returned: %d (expected != 0)", l_ret);
    dap_assert(l_ret != 0, "Invalid hostname fails as expected");
    
    // Test NULL output handling
    l_ret = dap_net_resolve_host("127.0.0.1", "8080", true, NULL, NULL);
    log_it(L_DEBUG, "Resolve with NULL output returned: %d", l_ret);
    
    dap_pass_msg("Hostname resolution tests completed");
}

/**
 * @brief Test: Network address validation
 */
static void s_test_net_address_validation(void)
{
    log_it(L_INFO, "Testing network address validation");
    
    struct sockaddr_storage l_addr;
    int l_family;
    
    // Test valid IPv4
    int l_ret = dap_net_resolve_host("192.168.1.1", "80", true, &l_addr, &l_family);
    log_it(L_DEBUG, "Valid IPv4 resolution: %d", l_ret);
    
    // Test valid IPv6
    l_ret = dap_net_resolve_host("::1", "80", true, &l_addr, &l_family);
    log_it(L_DEBUG, "Valid IPv6 resolution: %d", l_ret);
    if (l_ret == 0) {
        dap_assert(l_family == AF_INET6, "IPv6 family detected");
    }
    
    // Test empty hostname
    l_ret = dap_net_resolve_host("", "80", false, &l_addr, &l_family);
    log_it(L_DEBUG, "Empty hostname resolution: %d", l_ret);
    
    dap_pass_msg("Address validation completed");
}

/**
 * @brief Test: Port range validation
 */
static void s_test_port_validation(void)
{
    log_it(L_INFO, "Testing port validation");
    
    char l_addr[256];
    uint16_t l_port;
    struct sockaddr_storage l_saddr;
    int l_family;
    
    // Test valid ports
    int l_ret = dap_net_parse_config_address("127.0.0.1:1", 
                                              l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Port 1: %d", l_ret);
    
    l_ret = dap_net_parse_config_address("127.0.0.1:80",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Port 80: %d", l_ret);
    
    l_ret = dap_net_parse_config_address("127.0.0.1:65535",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Port 65535: %d", l_ret);
    
    // Test invalid ports
    l_ret = dap_net_parse_config_address("127.0.0.1:0",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Port 0: %d", l_ret);
    
    l_ret = dap_net_parse_config_address("127.0.0.1:65536",
                                         l_addr, &l_port, &l_saddr, &l_family);
    log_it(L_DEBUG, "Port 65536 (out of range): %d", l_ret);
    
    dap_pass_msg("Port validation completed");
}

/**
 * @brief Test: Multiple address formats
 */
static void s_test_address_formats(void)
{
    log_it(L_INFO, "Testing various address formats");
    
    char l_addr[256];
    uint16_t l_port;
    struct sockaddr_storage l_saddr;
    int l_family;
    
    // Test different IPv4 formats
    const char *l_test_addresses[] = {
        "0.0.0.0:8080",
        "255.255.255.255:80",
        "10.0.0.1:443",
        "172.16.0.1:22",
        NULL
    };
    
    for (int i = 0; l_test_addresses[i] != NULL; i++) {
        int l_ret = dap_net_parse_config_address(l_test_addresses[i],
                                                  l_addr, &l_port, &l_saddr, &l_family);
        log_it(L_DEBUG, "Parse '%s': %d", l_test_addresses[i], l_ret);
    }
    
    dap_pass_msg("Multiple address formats tested");
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_net", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Net - Unit Tests ===");
    
    // Run tests
    s_test_net_parse_config_address();
    s_test_net_resolve_host();
    s_test_net_address_validation();
    s_test_port_validation();
    s_test_address_formats();
    
    log_it(L_INFO, "=== All Net Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}
