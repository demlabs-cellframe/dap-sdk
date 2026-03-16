#pragma once

typedef enum dap_net_trans_type {
    DAP_NET_TRANS_MIN            = 0x01,
    DAP_NET_TRANS_HTTP           = 0x01,
    DAP_NET_TRANS_UDP_BASIC      = 0x02,
    DAP_NET_TRANS_UDP_RELIABLE   = 0x03,
    DAP_NET_TRANS_UDP_QUIC_LIKE  = 0x04,
    DAP_NET_TRANS_WEBSOCKET      = 0x05,
    DAP_NET_TRANS_TLS_DIRECT     = 0x06,
    DAP_NET_TRANS_DNS_TUNNEL     = 0x07,
    DAP_NET_TRANS_WEBSOCKET_SYSTEM = 0x08,
    DAP_NET_TRANS_MAX            = 0x08
} dap_net_trans_type_t;
