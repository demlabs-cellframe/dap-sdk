/**
 * @file test_network_monitor.c
 * @brief Integration test for DAP network monitor module
 * @details Tests network interface and routing notifications
 *          Requires: Linux, sudo/root privileges, nmcli
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <arpa/inet.h>
#include "linux/rtnetlink.h"

#include "dap_common.h"
#include "dap_test.h"
#include "linux/dap_network_monitor.h"

#define LOG_TAG "test_network_monitor"

enum events {
    NEW_INTERFACE_EV,
    NEW_GATEWAY_EV,
    REMOVE_INTERFACE_EV,
    REMOVE_GATEWAY_EV,
    REMOVE_ROUTE_EV
};

#define COUNT_TEST_EVENT_CASES 5

static dap_network_notification_t s_test_event_cases[COUNT_TEST_EVENT_CASES];
static bool s_list_events_done[COUNT_TEST_EVENT_CASES] = {0};

/**
 * @brief Network notification callback
 */
static void s_network_callback(const dap_network_notification_t *a_result)
{
    if (a_result->type == IP_ADDR_ADD || a_result->type == IP_ADDR_REMOVE)
    {
        log_it(L_DEBUG, "Interface %s %s has IP address %s",
               a_result->addr.interface_name, 
               (a_result->type == IP_ADDR_ADD ? "now" : "no longer"),
               a_result->addr.s_ip);
        
        enum events l_event = (a_result->type == IP_ADDR_ADD) ? 
                              NEW_INTERFACE_EV : REMOVE_INTERFACE_EV;

        log_it(L_DEBUG, "Checking %s", 
               (l_event == NEW_INTERFACE_EV ? 
                "add new interface callback" : "remove interface callback"));

        dap_assert(a_result->addr.ip == s_test_event_cases[l_event].addr.ip,
                   "Check dest ip");

        dap_assert(dap_str_equals(a_result->addr.s_ip, 
                                  s_test_event_cases[l_event].addr.s_ip),
                   "Check dest str ip");

        dap_assert(dap_str_equals(a_result->addr.interface_name,
                                  s_test_event_cases[l_event].addr.interface_name),
                   "Check interface name");

        s_list_events_done[l_event] = true;

    } else if (a_result->type == IP_ROUTE_ADD || a_result->type == IP_ROUTE_REMOVE) {

        if (a_result->type == IP_ROUTE_REMOVE) {

            if (a_result->route.destination_address == 
                s_test_event_cases[REMOVE_GATEWAY_EV].route.gateway_address) {
                
                log_it(L_DEBUG, "Gateway addr removed");
                dap_assert(dap_str_equals(a_result->route.s_destination_address,
                                          s_test_event_cases[REMOVE_GATEWAY_EV].route.s_gateway_address),
                           "Check gateway str ip");

                dap_assert(a_result->route.protocol == 
                          s_test_event_cases[REMOVE_GATEWAY_EV].route.protocol,
                           "Check protocol");

                s_list_events_done[REMOVE_GATEWAY_EV] = true;
                
            } else if (a_result->route.destination_address ==
                      s_test_event_cases[REMOVE_ROUTE_EV].route.destination_address) {
                
                log_it(L_DEBUG, "Destination address removed");

                dap_assert(dap_str_equals(a_result->route.s_destination_address,
                                          s_test_event_cases[REMOVE_ROUTE_EV].route.s_destination_address),
                           "Check dest str ip");

                dap_assert(a_result->route.protocol == 
                          s_test_event_cases[REMOVE_ROUTE_EV].route.protocol,
                           "Check protocol");

                s_list_events_done[REMOVE_ROUTE_EV] = true;
            }

        } else if (a_result->type == IP_ROUTE_ADD) {
            // Gateway address is present
            if (a_result->route.gateway_address != (uint64_t) -1) { 
                log_it(L_DEBUG, "Checking new gateway addr");
                dap_assert(a_result->route.gateway_address ==
                           s_test_event_cases[NEW_GATEWAY_EV].route.gateway_address,
                           "Check gateway ip");

                dap_assert(dap_str_equals(a_result->route.s_gateway_address,
                                          s_test_event_cases[NEW_GATEWAY_EV].route.s_gateway_address),
                           "Check gateway str ip");

                dap_assert(a_result->route.protocol == 
                          s_test_event_cases[NEW_GATEWAY_EV].route.protocol,
                           "Check protocol");

                s_list_events_done[NEW_GATEWAY_EV] = true;
            }
        }
    }
}

/**
 * @brief Initialize test cases with expected events
 */
static void s_init_test_case(void)
{
    memset(s_test_event_cases, 0, sizeof(s_test_event_cases));

    dap_network_notification_t *l_res;

    // New interface event
    l_res = &s_test_event_cases[NEW_INTERFACE_EV];
    l_res->type = IP_ADDR_ADD;
    strcpy(l_res->addr.s_ip, "10.1.0.111");
    strcpy(l_res->addr.interface_name, "tun10");
    l_res->addr.ip = 167837807;

    // New gateway event
    l_res = &s_test_event_cases[NEW_GATEWAY_EV];
    l_res->type = IP_ROUTE_ADD;
    strcpy(l_res->route.s_gateway_address, "10.1.0.1");
    l_res->route.gateway_address = 167837697;
    l_res->route.protocol = RTPROT_STATIC;

    // Remove gateway event
    l_res = &s_test_event_cases[REMOVE_GATEWAY_EV];
    l_res->type = IP_ROUTE_REMOVE;
    strcpy(l_res->route.s_gateway_address, "10.1.0.1");
    l_res->route.gateway_address = 167837697;
    l_res->route.protocol = RTPROT_STATIC;

    // Remove interface event
    l_res = &s_test_event_cases[REMOVE_INTERFACE_EV];
    l_res->type = IP_ADDR_REMOVE;
    strcpy(l_res->addr.s_ip, "10.1.0.111");
    strcpy(l_res->addr.interface_name, "tun10");
    l_res->addr.ip = 167837807;

    // Remove route event
    l_res = &s_test_event_cases[REMOVE_ROUTE_EV];
    l_res->type = IP_ROUTE_REMOVE;
    strcpy(l_res->route.s_destination_address, "10.1.0.111");
    l_res->route.destination_address = 167837807;
    l_res->route.protocol = RTPROT_KERNEL;
}

/**
 * @brief Test network monitor functionality
 */
static void s_test_network_monitor(void)
{
    log_it(L_INFO, "Testing network monitor with real network changes");
    
    s_init_test_case();

    int l_ret = dap_network_monitor_init(s_network_callback);
    dap_assert(l_ret == 0, "Network monitor init");

    // Commands to manipulate network interfaces
    const char *l_add_interface = "sudo nmcli connection add type tun con-name "
                                  "DiveVPNTest autoconnect false ifname tun10 "
                                  "mode tun ip4 10.1.0.111 gw4 10.1.0.1 2>&1";
    const char *l_up_interface = "sudo nmcli connection up DiveVPNTest 2>&1";
    const char *l_down_interface = "sudo nmcli connection down DiveVPNTest 2>&1";
    const char *l_delete_interface = "sudo nmcli connection delete DiveVPNTest 2>&1";

    // Clean up any existing test interface
    log_it(L_DEBUG, "Cleaning up any existing test interface");
    system(l_delete_interface);

    // Create and manipulate test interface
    log_it(L_DEBUG, "Creating test interface");
    int l_result = system(l_add_interface);
    if (l_result != 0) {
        log_it(L_WARNING, "Failed to create test interface, check sudo privileges");
    }

    log_it(L_DEBUG, "Bringing interface up");
    system(l_up_interface);

    // Give network monitor time to process events
    sleep(2);

    log_it(L_DEBUG, "Bringing interface down");
    system(l_down_interface);

    // Give network monitor time to process events
    sleep(2);

    log_it(L_DEBUG, "Deleting test interface");
    system(l_delete_interface);

    // Give network monitor time to process events
    sleep(1);

    // Verify all events were processed
    for (int i = 0; i < COUNT_TEST_EVENT_CASES; i++) {
        if (!s_list_events_done[i]) {
            log_it(L_WARNING, "Event %d was not processed", i);
            dap_fail("Not all events were processed");
        }
    }

    dap_network_monitor_deinit();
    log_it(L_INFO, "Network monitor test completed");
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_network_monitor", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    log_it(L_INFO, "=== DAP Network Monitor - Integration Test ===");
    log_it(L_INFO, "This test requires sudo/root privileges and nmcli");
    
    // Run test
    s_test_network_monitor();
    
    log_it(L_INFO, "=== All Network Monitor Tests PASSED! ===");
    
    dap_common_deinit();
    return 0;
}


