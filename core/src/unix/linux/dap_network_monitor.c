#include <linux/netlink.h>
#include <pthread.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include "dap_network_monitor.h"
#include "dap_events_socket.h"
#include "dap_worker.h"

#define LOG_TAG "dap_network_monitor"

static dap_network_monitor_notification_callback_t s_notify_cb;
static dap_events_socket_uuid_t es_uuid;
static dap_worker_t *es_worker;

static void s_callback_read( dap_events_socket_t * a_es, void * arg ) {
    size_t l_len = a_es->buf_in_size;
    for ( struct nlmsghdr *nlh = (struct nlmsghdr*)a_es->buf_in; NLMSG_OK(nlh, l_len); nlh = NLMSG_NEXT(nlh, l_len) ) {
        dap_network_notification_t notify_data = { .type = nlh->nlmsg_type };
        switch ( nlh->nlmsg_type ) {
        case NLMSG_DONE:
            break;

        case NLMSG_ERROR: {
            struct nlmsgerr *err = (struct nlmsgerr*)NLMSG_DATA(nlh);
            log_it(L_ERROR, "Netlink message error %d: %s", err->error, dap_strerror(err->error));
        } break;
        
        case RTM_NEWADDR: case RTM_DELADDR: {
            struct ifaddrmsg *ifa = (struct ifaddrmsg*)NLMSG_DATA(nlh);
            size_t rtl = IFA_PAYLOAD(nlh);
            for ( struct rtattr *rta = IFA_RTA(ifa); RTA_OK(rta, rtl); rta = RTA_NEXT(rta, rtl) ) {
                if ( rta->rta_type == IFA_LOCAL) {
                    if_indextoname(ifa->ifa_index, notify_data.addr.interface_name);
                    inet_ntop(AF_INET, RTA_DATA(rta), notify_data.addr.s_ip, sizeof(notify_data.addr.s_ip));
                    // TODO: for IPv6 we'll need getaddrinfo() with aapropriate handling
                    notify_data.addr.ip = htonl(*(uint32_t*)RTA_DATA(rta));
                    break;
                }
            }
            s_notify_cb(&notify_data);
        } continue;
        
        case RTM_NEWROUTE: case RTM_DELROUTE: {
            struct rtmsg *rt = (struct rtmsg*)NLMSG_DATA(nlh);
            notify_data.route.protocol = rt->rtm_protocol;
            notify_data.route.netmask = rt->rtm_dst_len;
            size_t rtl = RTM_PAYLOAD(nlh);
            for ( struct rtattr *rta = RTM_RTA(rt); RTA_OK(rta, rtl); rta = RTA_NEXT(rta, rtl) ) {
                switch ( rta->rta_type ) {
                case RTA_DST:
                    notify_data.route.destination_address = htonl(*(uint32_t*)RTA_DATA(rta));
                    inet_ntop(AF_INET, RTA_DATA(rta), notify_data.route.s_destination_address, sizeof(notify_data.route.s_destination_address));
                    break;
                case RTA_GATEWAY:
                    notify_data.route.gateway_address = htonl(*(uint32_t*)RTA_DATA(rta));
                    inet_ntop(AF_INET, RTA_DATA(rta), notify_data.route.s_gateway_address, sizeof(notify_data.route.s_gateway_address));
                    break;
                default: break;
                }
            }
            s_notify_cb(&notify_data);
        } continue;

        case RTM_NEWLINK: case RTM_DELLINK: {
            struct ifinfomsg *ifi = (struct ifinfomsg*)NLMSG_DATA(nlh);
            size_t rtl = IFLA_PAYLOAD(nlh);
            for ( struct rtattr *rta = IFLA_RTA(ifi); RTA_OK(rta, rtl); rta = RTA_NEXT(rta, rtl) ) {
                if ( rta->rta_type == IFLA_IFNAME ) {
                    strncpy(notify_data.link.interface_name, (const char*)RTA_DATA(rta), IF_NAMESIZE);
                    notify_data.link.is_running = ifi->ifi_flags & IFF_RUNNING;
                    notify_data.link.is_up = ifi->ifi_flags & IFF_UP;
                    // TODO: notify DELLINK?
                }
            }
            s_notify_cb(&notify_data);
        } continue;

        default : continue;
        }
        break; // an error occured
    }
}

static bool s_callback_write( dap_events_socket_t * a_es, void * arg ) {
#if 0
    static int seq = 0;
    struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
    memcpy( a_es->buf_out, &(struct nlmsghdr){ .nlmsg_flags = NLM_F_ACK, .nlmsg_seq = ++seq, .nlmsg_pid = pthread_self() << 16 | getpid() }, sizeof(struct nlmsghdr) );
    a_es->buf_out_size = NLMSG_HDRLEN;
#endif
    // In dap_context.c do the following:
    /*  
    struct iovec iov = { a_es->buf_out, a_es->buf_out_size };
    struct msghdr msg = {&a_es->addr_storage, a_es->addr_size, &iov, 1, NULL, 0, 0};
    sendmsg(a_es->fd, &msg, 0);
    */
    return false;
}

int dap_network_monitor_init(dap_network_monitor_notification_callback_t cb)
{
    if ( !cb )
        return log_it(L_ERROR, "No notify callback provided"), -3;

    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if ( fd < 0 )
        return log_it(L_ERROR, "socket(AF_NETLINK) error %d: %s", errno, dap_strerror(errno)), -1;
    struct sockaddr_nl storage =
        { .nl_family = AF_NETLINK, .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE, .nl_pid = pthread_self() << 16 | getpid() };
    if ( bind(fd, (struct sockaddr*)&storage, sizeof(storage)) < 0 )
        return log_it(L_ERROR, "bind() on netlink socket error %d: %s", errno, dap_strerror(errno)), -2;

    dap_events_socket_callbacks_t l_cb = { .read_callback = s_callback_read, .write_callback = s_callback_write };
    dap_events_socket_t *l_es = dap_events_socket_wrap_no_add(fd, &l_cb);
    l_es->type = DESCRIPTOR_TYPE_SOCKET_RAW;
    memcpy(&l_es->addr_storage, &storage, sizeof(storage));
    l_es->addr_size = sizeof(storage);
    l_es->flags &= DAP_SOCK_MSG_ORIENTED;
    l_es->no_close = true;
    es_uuid = l_es->uuid;
    s_notify_cb = cb;
    es_worker = dap_events_worker_get_auto();
    dap_worker_add_events_socket( es_worker, l_es );
    log_it(L_INFO, "Network monitor initialized");
    return 0;
}

void dap_network_monitor_deinit(void)
{
    dap_events_socket_remove_and_delete_mt( es_worker, es_uuid );
}