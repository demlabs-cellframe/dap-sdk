#include <arpa/inet.h>
#include "linux/rtnetlink.h"

#include "dap_network_monitor.h"
#include "dap_network_monitor_test.h"

enum events {
    NEW_LINK_EV,
    REMOVE_LINK_EV,
    NEW_INTERFACE_EV,
    NEW_GATEWAY_EV,
    REMOVE_INTERFACE_EV,
    REMOVE_GATEWAY_EV,
    REMOVE_ROUTE_EV
};

#define COUNT_TEST_EVENT_CASES 7


static dap_network_notification_t _test_event_cases[COUNT_TEST_EVENT_CASES];


static bool list_events_done[COUNT_TEST_EVENT_CASES] = {0};

void _addr_ip_check(uint32_t ip1, uint32_t ip2){
    dap_assert(ip1 == ip2, "Check dest ip");
}

void _addr_ip_str_check(const char *ip1, const char *ip2){
    dap_assert(dap_str_equals(ip1, ip2), "Check dest str ip");
}

void _network_callback(const dap_network_notification_t result)
{
    switch (result.type) {
        case IP_ADDR_ADD: {
            dap_test_msg("Interface %s now has ip address %s", result.addr.interface_name, result.addr.s_ip);
            dap_test_msg("Checking add new interface callback");
            _addr_ip_check(result.addr.ip, _test_event_cases[NEW_INTERFACE_EV].addr.ip);
            _addr_ip_str_check(result.addr.s_ip, _test_event_cases[NEW_INTERFACE_EV].addr.s_ip);
            dap_assert(dap_str_equals(result.addr.interface_name,
                                      _test_event_cases[NEW_INTERFACE_EV].addr.interface_name),
                       "Check interface name");
            list_events_done[NEW_INTERFACE_EV] = true;
        } break;
        case IP_ADDR_REMOVE: {
            dap_test_msg("Interface %s no longer has IP address %s",
                         result.addr.interface_name, result.addr.s_ip);

            dap_test_msg("Checking remove interface callback");

            _addr_ip_check(result.addr.ip, _test_event_cases[REMOVE_INTERFACE_EV].addr.ip);
            _addr_ip_str_check(result.addr.s_ip, _test_event_cases[REMOVE_INTERFACE_EV].addr.s_ip);

//            dap_assert(dap_str_equals(result.addr.interface_name,
//                                      _test_event_cases[REMOVE_INTERFACE_EV].addr.interface_name),
//                       "Check interface name");

            list_events_done[REMOVE_INTERFACE_EV] = true;
        } break;
        case IP_ROUTE_ADD: {
            if(result.route.gateway_address != (uint64_t) -1) { // gateway address is present
                dap_test_msg("Checking new gateway addr");
                dap_assert(result.route.gateway_address ==
                           _test_event_cases[NEW_GATEWAY_EV].route.gateway_address,
                           "Check gateway ip");

                dap_assert(dap_str_equals(result.route.s_gateway_address,
                                          _test_event_cases[NEW_GATEWAY_EV].route.s_gateway_address),
                           "Check gateway str ip");

                dap_assert(result.route.protocol == _test_event_cases[NEW_GATEWAY_EV].route.protocol,
                           "Check protocol");

                list_events_done[NEW_GATEWAY_EV] = true;
            }
//            dap_test_msg("Adding route to destination --> %s/%d proto %d and gateway %s\n",
//                         result.route.s_destination_address,
//                         result.route.netmask,
//                         result.route.protocol,
//                         result.route.s_gateway_address);
        } break;
        case IP_ROUTE_REMOVE: {
            if(result.route.destination_address == _test_event_cases[REMOVE_GATEWAY_EV].route.gateway_address) {
                dap_pass_msg("Gateway addr removed");
                dap_assert(dap_str_equals(result.route.s_destination_address,
                                          _test_event_cases[REMOVE_GATEWAY_EV].route.s_gateway_address),
                           "Check gateway str ip");

                dap_assert(result.route.protocol == _test_event_cases[REMOVE_GATEWAY_EV].route.protocol,
                           "Check protocol");

                list_events_done[REMOVE_GATEWAY_EV] = true;
            } else if(result.route.destination_address ==
                      _test_event_cases[REMOVE_ROUTE_EV].route.destination_address) {
                dap_pass_msg("Destination address removed");

                dap_assert(dap_str_equals(result.route.s_destination_address,
                                          _test_event_cases[REMOVE_ROUTE_EV].route.s_destination_address),
                           "Check dest str ip");

                dap_assert(result.route.protocol == _test_event_cases[REMOVE_ROUTE_EV].route.protocol,
                           "Check protocol");

                list_events_done[REMOVE_ROUTE_EV] = true;
            }

//            dap_test_msg("Deleting route to destination --> %s/%d proto %d and gateway %s\n",
//                         result.route.s_destination_address,
//                         result.route.netmask,
//                         result.route.protocol,
//                         result.route.s_gateway_address);
        } break;
        case IP_LINK_NEW: {
            dap_test_msg("New IP Link");
            if (result.link.is_up)
                dap_assert(dap_str_equals(result.link.interface_name, _test_event_cases[NEW_LINK_EV].link.interface_name),
                       "Check interface name");
            list_events_done[NEW_LINK_EV] = true;
        } break;
        case IP_LINK_DEL: {
            dap_test_msg("Remove IP Link");
            dap_assert(result.link.is_running == _test_event_cases[REMOVE_LINK_EV].link.is_running,
                       "Checking that the link is not running.");
            dap_assert(result.link.is_running == _test_event_cases[REMOVE_LINK_EV].link.is_up,
                       "Checking that the link is down.");
            list_events_done[REMOVE_LINK_EV] = true;
        } break;
        default: {
            dap_fail("The callback received a result type that is not processed")
        }
    }
}


static void init_test_case()
{
    bzero(_test_event_cases, sizeof (_test_event_cases));

    dap_network_notification_t * res;

    // new_link
    res = &_test_event_cases[NEW_LINK_EV];
    res->type = IP_LINK_NEW;
    strcpy(res->addr.s_ip, "10.1.0.111");
    strcpy(res->addr.interface_name, "tun10");
    res->addr.ip = 167837807;

    // remove_link_ev
    res = &_test_event_cases[REMOVE_LINK_EV];
    res->type = IP_LINK_DEL;
    res->link.is_running = false;
    res->link.is_up = false;

    // new_interface
    res = &_test_event_cases[NEW_INTERFACE_EV];
    res->type = IP_ADDR_ADD;
    strcpy(res->addr.s_ip, "10.1.0.111");
    strcpy(res->addr.interface_name, "tun10");
    res->addr.ip = 167837807;

    // new_gateway
    res = &_test_event_cases[NEW_GATEWAY_EV];
    res->type = IP_ROUTE_ADD;
    strcpy(res->route.s_gateway_address, "10.1.0.1");
    res->route.gateway_address = 167837697;
    res->route.protocol = RTPROT_STATIC;

    res = &_test_event_cases[REMOVE_GATEWAY_EV];
    res->type = IP_ROUTE_REMOVE;
    strcpy(res->route.s_gateway_address, "10.1.0.1");
    res->route.gateway_address = 167837697;
    res->route.protocol = RTPROT_STATIC;


    // remove interface
    res = &_test_event_cases[REMOVE_INTERFACE_EV];
    res->type = IP_ADDR_REMOVE;
    strcpy(res->addr.s_ip, "10.1.0.111");
    strcpy(res->addr.interface_name, "tun10");
    res->addr.ip = 167837807;

    // remote route
    res = &_test_event_cases[REMOVE_ROUTE_EV];
    res->type = IP_ROUTE_REMOVE;
    strcpy(res->route.s_destination_address, "10.1.0.111");
    res->route.destination_address = 167837807;
    res->route.protocol = RTPROT_KERNEL;
}

static void cleanup_test_case()
{

}

void dap_network_monitor_test_run(void)
{
    dap_print_module_name("dap_network_monitor");

    init_test_case();

    dap_network_monitor_init(_network_callback);

    const char *add_test_interfece = "sudo nmcli connection add type tun con-name "
                                     "DiveVPNTest autoconnect false ifname tun10 "
                                     "mode tun ip4 10.1.0.111 gw4 10.1.0.1";
    const char *up_test_interfece = "sudo nmcli connection up DiveVPNTest";
    const char *down_test_interfece = "sudo nmcli connection down DiveVPNTest";
    const char *delete_test_interfece = "sudo nmcli connection delete DiveVPNTest 2> /dev/null";

    system(delete_test_interfece);
    system(add_test_interfece);
    system(up_test_interfece);
    system(down_test_interfece);
    system(delete_test_interfece);
    sleep(120);

    for(int i = 0; i < COUNT_TEST_EVENT_CASES; i++) {
        if(list_events_done[i] == false) {
            dap_fail("Not all events were processed");
        }
    }

    dap_network_monitor_deinit();
    cleanup_test_case();
}
