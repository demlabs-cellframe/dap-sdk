/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
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

/**
 * @file dap_net_transport_dns_stream.h
 * @brief DNS Tunnel Transport Adapter for DAP Stream Protocol
 * 
 * This module implements a DNS-based tunneling transport layer for DAP Stream,
 * providing data transmission through DNS queries and responses. This transport
 * is designed for bypassing firewalls that only allow DNS traffic.
 * 
 * **Features:**
 * - DNS query/response tunneling
 * - TXT record encoding
 * - Base32/Base64 encoding support
 * - Chunking for large data
 * - Connectionless operation (similar to UDP)
 * 
 * **Use Cases:**
 * - Bypassing firewalls that only allow DNS
 * - DPI evasion (looks like legitimate DNS traffic)
 * - Censorship circumvention
 * - Network environments with restricted protocols
 * 
 * **Architecture:**
 * ```
 * Application
 *     ↓
 * DAP Stream
 *     ↓
 * Transport Abstraction Layer
 *     ↓
 * DNS Tunnel Transport ← This module
 *     ↓
 * DNS Query/Response
 *     ↓
 * UDP Socket (dap_events_socket_t)
 *     ↓
 * Network (UDP/IP, port 53)
 * ```
 * 
 * **Protocol Stack:**
 * ```
 * +---------------------------+
 * | DAP Stream Packet         |
 * +---------------------------+
 * | DNS Tunnel Encoding        |
 * +---------------------------+
 * | DNS TXT Record            |
 * +---------------------------+
 * | DNS Query/Response        |
 * +---------------------------+
 * ```
 * 
 * @see dap_stream_transport.h
 * @see RFC 1035 - Domain Names - Implementation and Specification
 */

#pragma once

#include "dap_net_transport.h"
#include "dap_events_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DNS Tunnel Configuration Constants
// ============================================================================

/// DNS Transport Protocol Version
#define DAP_STREAM_DNS_VERSION 1

/// Default configuration values
#define DAP_STREAM_DNS_DEFAULT_MAX_RECORD_SIZE  255    ///< Max DNS TXT record size (RFC 1035)
#define DAP_STREAM_DNS_DEFAULT_MAX_QUERY_SIZE   512    ///< Max DNS query size (UDP)
#define DAP_STREAM_DNS_DEFAULT_TIMEOUT_MS       5000   ///< Default DNS query timeout

// ============================================================================
// Configuration Structures
// ============================================================================

/**
 * @brief DNS tunnel transport configuration
 */
typedef struct dap_stream_transport_dns_config {
    uint16_t max_record_size;      ///< Maximum DNS TXT record size (default: 255)
    uint16_t max_query_size;       ///< Maximum DNS query size (default: 512)
    uint32_t query_timeout_ms;     ///< DNS query timeout (milliseconds)
    bool use_base32;                ///< Use Base32 encoding (true) or Base64 (false)
    bool enable_compression;        ///< Enable data compression before encoding
    char *domain_suffix;            ///< Domain suffix for DNS queries (e.g., "example.com")
} dap_stream_transport_dns_config_t;

/**
 * @brief DNS tunnel transport private data
 */
typedef struct dap_stream_transport_dns_private {
    dap_stream_transport_dns_config_t config;  ///< Configuration
    dap_events_socket_t *esocket;              ///< Underlying UDP socket
    uint64_t session_id;                        ///< Session identifier
    uint32_t query_id;                          ///< DNS query ID counter
    uint32_t seq_num;                           ///< Sequence number for chunking
} dap_stream_transport_dns_private_t;

// ============================================================================
// Transport Registration
// ============================================================================

/**
 * @brief Register DNS tunnel transport adapter
 * @return 0 on success, negative error code on failure
 */
int dap_net_transport_dns_stream_register(void);

/**
 * @brief Unregister DNS tunnel transport adapter
 * @return 0 on success, negative error code on failure
 */
int dap_net_transport_dns_stream_unregister(void);

// ============================================================================
// Configuration Functions
// ============================================================================

/**
 * @brief Get default DNS tunnel configuration
 * @return Default configuration structure
 */
dap_stream_transport_dns_config_t dap_stream_transport_dns_config_default(void);

/**
 * @brief Set DNS tunnel configuration
 * @param a_transport DNS transport instance
 * @param a_config Configuration structure
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_dns_set_config(dap_net_transport_t *a_transport,
                                        const dap_stream_transport_dns_config_t *a_config);

/**
 * @brief Get DNS tunnel configuration
 * @param a_transport DNS transport instance
 * @param a_config Output configuration structure
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_dns_get_config(dap_net_transport_t *a_transport,
                                         dap_stream_transport_dns_config_t *a_config);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Check if stream is using DNS tunnel transport
 * @param a_stream Stream to check
 * @return true if DNS tunnel transport, false otherwise
 */
bool dap_stream_transport_is_dns(const dap_stream_t *a_stream);

/**
 * @brief Get DNS tunnel private data from stream
 * @param a_stream Stream instance
 * @return DNS tunnel private data or NULL
 */
dap_stream_transport_dns_private_t* dap_stream_transport_dns_get_private(dap_stream_t *a_stream);

#ifdef __cplusplus
}
#endif

