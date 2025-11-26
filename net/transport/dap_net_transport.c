/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net
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
 * @file dap_net_transport.c
 * @brief Network Transport Abstraction Layer implementation
 * @date 2025-10-23
 * @author Cellframe Team
 */

#include <string.h>
#include <stdio.h>
#include "dap_net_transport.h"
#include "dap_stream_obfuscation.h"
#include "dap_stream.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_list.h"
#include "dap_module.h"
#include "dap_events_socket.h"
#include "dap_net.h"



#define LOG_TAG "dap_net_transport"

// Global transport registry (hash table keyed by transport type)
static dap_net_transport_t *s_transport_registry = NULL;

// Thread safety (if needed, can add pthread_rwlock_t here)
// For now, assuming single-threaded init/registration

// Global flag to track initialization state
static bool s_transport_registry_initialized = false;

/**
 * @brief Initialize transport abstraction system
 * @return 0 on success, -1 on failure
 * @note Called automatically by dap_module system, should not be called directly
 * @note Not declared in header - internal function accessed only via module system
 */
int dap_net_transport_init(void)
{
    // Idempotent: safe to call multiple times
    if (s_transport_registry_initialized) {
        log_it(L_DEBUG, "Transport registry already initialized, skipping");
        return 0;
    }
    
    log_it(L_NOTICE, "Initializing DAP Network Transport Abstraction Layer");
    
    // Initialize registry (hash table starts empty)
    s_transport_registry = NULL;
    s_transport_registry_initialized = true;
    
    log_it(L_INFO, "Transport registry initialized");
    return 0;
}

/**
 * @brief Cleanup transport abstraction system
 * @note Idempotent: safe to call multiple times
 * @note Called automatically by dap_module system, should not be called directly
 * @note Not declared in header - internal function accessed only via module system
 */
void dap_net_transport_deinit(void)
{
    log_it(L_NOTICE, "Deinitializing DAP Network Transport Abstraction Layer");
}

/**
 * @brief Register a new transport implementation
 * @param a_name Transport name (max 63 chars)
 * @param a_type Transport type identifier
 * @param a_ops Operations table (must remain valid)
 * @param a_inheritor Transport-specific private data (optional)
 * @return 0 on success, negative error code on failure
 * @note Automatically initializes registry if not initialized yet (for constructor support)
 */
int dap_net_transport_register(const char *a_name, 
                                    dap_net_transport_type_t a_type, 
                                    const dap_net_transport_ops_t *a_ops,
                                    dap_net_transport_socket_type_t a_socket_type,
                                    void *a_inheritor)
{
    // Validate parameters
    if (!a_name || !a_ops) {
        log_it(L_ERROR, "Invalid parameters: name=%p, ops=%p", a_name, a_ops);
        return -1;
    }
    
    // Auto-initialize registry if not initialized yet (for constructor-based registration)
    // This allows transports to register themselves via constructors before dap_net_transport_init()
    if (!s_transport_registry_initialized) {
        log_it(L_DEBUG, "Registry not initialized, auto-initializing for transport '%s'", a_name);
        s_transport_registry = NULL;
        s_transport_registry_initialized = true;
    }
    
    // Check if transport type already registered
    dap_net_transport_t *l_existing = NULL;
    HASH_FIND_INT(s_transport_registry, &a_type, l_existing);
    if (l_existing) {
        log_it(L_DEBUG, "Transport type 0x%02X already registered as '%s' (idempotent: returning success)", 
               a_type, l_existing->name);
        return 0;  // Idempotent: return success if already registered
    }
    
    // Allocate new transport structure
    dap_net_transport_t *l_transport = DAP_NEW_Z(dap_net_transport_t);
    if (!l_transport) {
        log_it(L_CRITICAL, "Failed to allocate memory for transport '%s'", a_name);
        return -3;
    }
    
    // Initialize transport fields
    l_transport->type = a_type;
    l_transport->ops = a_ops;
    l_transport->_inheritor = a_inheritor;
    l_transport->obfuscation = NULL;  // No obfuscation by default
    l_transport->socket_type = a_socket_type;  // Set socket type
    
    // Default to true, specific transports can override in their init()
    l_transport->has_session_control = true;
    
    // Copy name (truncate if too long)
    strncpy(l_transport->name, a_name, sizeof(l_transport->name) - 1);
    l_transport->name[sizeof(l_transport->name) - 1] = '\0';
    
    // Query capabilities if supported
    if (a_ops->get_capabilities) {
        l_transport->capabilities = a_ops->get_capabilities(l_transport);
    } else {
        l_transport->capabilities = 0;  // No capabilities reported
    }
    
    // Call init callback if provided
    if (a_ops->init) {
        int l_ret = a_ops->init(l_transport, NULL);  // TODO: pass config
        if (l_ret != 0) {
            log_it(L_ERROR, "Transport '%s' init() failed with code %d", a_name, l_ret);
            DAP_DELETE(l_transport);
            return l_ret;
        }
    }
    
    // Add to registry hash table
    HASH_ADD_INT(s_transport_registry, type, l_transport);
    
    log_it(L_NOTICE, "Registered transport: %s (type=0x%02X, socket_type=%d, caps=0x%04X)", 
           l_transport->name, l_transport->type, l_transport->socket_type, l_transport->capabilities);
    
    return 0;
}

/**
 * @brief Unregister a transport implementation
 * @param a_type Transport type to unregister
 * @return 0 on success, -1 if not found (idempotent: safe to call multiple times)
 */
int dap_net_transport_unregister(dap_net_transport_type_t a_type)
{
    // If registry is already cleared or deinitialized (e.g., by dap_net_transport_deinit),
    // silently return success (idempotent operation)
    // Check both flags to ensure safety in all scenarios
    if (!s_transport_registry_initialized || !s_transport_registry) {
        log_it(L_DEBUG, "Transport registry not initialized or already cleared, skipping unregister for type 0x%02X", a_type);
        return 0;
    }
    
    // Find transport by type
    dap_net_transport_t *l_transport = NULL;
    HASH_FIND_INT(s_transport_registry, &a_type, l_transport);
    
    if (!l_transport) {
        log_it(L_DEBUG, "Transport type 0x%02X not registered (already unregistered)", a_type);
        return 0;  // Idempotent: return success if already unregistered
    }
    
    log_it(L_INFO, "Unregistering transport: %s (type=0x%02X)", 
           l_transport->name, l_transport->type);
    
    // Call deinit if provided
    if (l_transport->ops && l_transport->ops->deinit) {
        l_transport->ops->deinit(l_transport);
    }
    
    // Remove from hash table
    HASH_DEL(s_transport_registry, l_transport);
    
    // Free transport structure
    DAP_DELETE(l_transport);
    
    log_it(L_DEBUG, "Transport type 0x%02X unregistered successfully", a_type);
    return 0;
}

/**
 * @brief Find registered transport by type
 * @param a_type Transport type to find
 * @return Transport instance or NULL if not found
 */
dap_net_transport_t *dap_net_transport_find(dap_net_transport_type_t a_type)
{
    dap_net_transport_t *l_transport = NULL;
    HASH_FIND_INT(s_transport_registry, &a_type, l_transport);
    
    if (!l_transport) {
        log_it(L_DEBUG, "Transport type 0x%02X not found in registry", a_type);
    }
    
    return l_transport;
}

/**
 * @brief Find registered transport by name
 * @param a_name Transport name to find
 * @return Transport instance or NULL if not found
 */
dap_net_transport_t *dap_net_transport_find_by_name(const char *a_name)
{
    if (!a_name) {
        return NULL;
    }
    
    if (!s_transport_registry_initialized || !s_transport_registry) {
        return NULL;
    }
    
    // Linear search through hash table (names are not indexed)
    dap_net_transport_t *l_transport, *l_tmp;
    HASH_ITER(hh, s_transport_registry, l_transport, l_tmp) {
        if (l_transport && dap_strcmp(l_transport->name, a_name) == 0) {
            return l_transport;
        }
    }
    
    log_it(L_DEBUG, "Transport '%s' not found in registry", a_name);
    return NULL;
}

/**
 * @brief Get list of all registered transports
 * @return Linked list of dap_net_transport_t* (caller must free list, not contents)
 */
dap_list_t *dap_net_transport_list_all(void)
{
    dap_list_t *l_list = NULL;
    
    if (!s_transport_registry_initialized || !s_transport_registry) {
        return NULL;
    }
    
    dap_net_transport_t *l_transport, *l_tmp;
    HASH_ITER(hh, s_transport_registry, l_transport, l_tmp) {
        if (l_transport) {
            l_list = dap_list_append(l_list, l_transport);
        }
    }
    
    return l_list;
}

/**
 * @brief Get transport name string
 */
const char *dap_net_transport_type_to_str(dap_net_transport_type_t a_type)
{
    switch (a_type) {
        case DAP_NET_TRANSPORT_HTTP:          return "HTTP";
        case DAP_NET_TRANSPORT_UDP_BASIC:     return "UDP_BASIC";
        case DAP_NET_TRANSPORT_UDP_RELIABLE:  return "UDP_RELIABLE";
        case DAP_NET_TRANSPORT_UDP_QUIC_LIKE: return "UDP_QUIC_LIKE";
        case DAP_NET_TRANSPORT_WEBSOCKET:     return "WEBSOCKET";
        case DAP_NET_TRANSPORT_TLS_DIRECT:    return "TLS_DIRECT";
        case DAP_NET_TRANSPORT_DNS_TUNNEL:    return "DNS_TUNNEL";
        default:                                 return "UNKNOWN";
    }
}

/**
 * @brief Parse transport type from string
 */
dap_net_transport_type_t dap_net_transport_type_from_str(const char *a_str)
{
    if (!a_str) {
        return DAP_NET_TRANSPORT_HTTP;
    }
    
    // HTTP/HTTPS
    if (strcmp(a_str, "http") == 0 || strcmp(a_str, "https") == 0) {
        return DAP_NET_TRANSPORT_HTTP;
    }
    
    // UDP variants
    if (strcmp(a_str, "udp") == 0 || strcmp(a_str, "udp_basic") == 0) {
        return DAP_NET_TRANSPORT_UDP_BASIC;
    }
    if (strcmp(a_str, "udp_reliable") == 0) {
        return DAP_NET_TRANSPORT_UDP_RELIABLE;
    }
    if (strcmp(a_str, "udp_quic") == 0 || strcmp(a_str, "quic") == 0) {
        return DAP_NET_TRANSPORT_UDP_QUIC_LIKE;
    }
    
    // WebSocket
    if (strcmp(a_str, "websocket") == 0 || strcmp(a_str, "ws") == 0) {
        return DAP_NET_TRANSPORT_WEBSOCKET;
    }
    
    // TLS Direct
    if (strcmp(a_str, "tls") == 0 || strcmp(a_str, "tls_direct") == 0) {
        return DAP_NET_TRANSPORT_TLS_DIRECT;
    }
    
    // DNS Tunnel
    if (strcmp(a_str, "dns") == 0 || strcmp(a_str, "dns_tunnel") == 0) {
        return DAP_NET_TRANSPORT_DNS_TUNNEL;
    }
    
    log_it(L_WARNING, "Unknown transport type '%s', defaulting to HTTP", a_str);
    return DAP_NET_TRANSPORT_HTTP;
}

/**
 * @brief Attach obfuscation engine to transport
 * @param a_transport Transport to attach obfuscation to
 * @param a_obfuscation Obfuscation engine instance
 * @return 0 on success, -1 on failure
 */
int dap_net_transport_attach_obfuscation(dap_net_transport_t *a_transport, 
                                              dap_stream_obfuscation_t *a_obfuscation)
{
    if (!a_transport) {
        log_it(L_ERROR, "Cannot attach obfuscation: transport is NULL");
        return -1;
    }
    
    if (!a_obfuscation) {
        log_it(L_ERROR, "Cannot attach obfuscation: obfuscation engine is NULL");
        return -1;
    }
    
    if (a_transport->obfuscation) {
        log_it(L_WARNING, "Transport '%s' already has obfuscation attached, replacing", 
               a_transport->name);
    }
    
    a_transport->obfuscation = a_obfuscation;
    
    log_it(L_INFO, "Attached obfuscation engine to transport '%s'", a_transport->name);
    return 0;
}

/**
 * @brief Detach obfuscation engine from transport
 * @param a_transport Transport to detach obfuscation from
 */
void dap_net_transport_detach_obfuscation(dap_net_transport_t *a_transport)
{
    if (!a_transport) {
        log_it(L_ERROR, "Cannot detach obfuscation: transport is NULL");
        return;
    }
    
    if (!a_transport->obfuscation) {
        log_it(L_DEBUG, "Transport '%s' has no obfuscation attached", a_transport->name);
        return;
    }
    
    log_it(L_INFO, "Detached obfuscation engine from transport '%s'", a_transport->name);
    a_transport->obfuscation = NULL;
}

/**
 * @brief Write data through transport with obfuscation
 * 
 * This function wraps the transport's write operation, automatically
 * applying obfuscation if an obfuscation engine is attached to the transport.
 * 
 * @param a_stream Stream to write to
 * @param a_data Data to write
 * @param a_size Size of data
 * @return Bytes written, or negative error code
 */
ssize_t dap_net_transport_write_obfuscated(dap_stream_t *a_stream, 
                                               const void *a_data, 
                                               size_t a_size)
{
    if (!a_stream || !a_stream->stream_transport) {
        log_it(L_ERROR, "Cannot write: invalid stream or transport");
        return -1;
    }

    dap_net_transport_t *l_transport = a_stream->stream_transport;
    
    if (!l_transport->ops || !l_transport->ops->write) {
        log_it(L_ERROR, "Transport does not support write operation");
        return -1;
    }

    // If obfuscation is attached, apply it before writing
    if (l_transport->obfuscation) {
        void *l_obfuscated_data = NULL;
        size_t l_obfuscated_size = 0;
        
        // Apply obfuscation (includes padding, mixing, mimicry, etc)
        int l_ret = dap_stream_obfuscation_apply(
            l_transport->obfuscation,
            a_data,
            a_size,
            &l_obfuscated_data,
            &l_obfuscated_size
        );
        
        if (l_ret != 0 || !l_obfuscated_data) {
            log_it(L_ERROR, "Obfuscation failed: %d", l_ret);
            return -1;
        }
        
        // Write obfuscated data
        ssize_t l_written = l_transport->ops->write(a_stream, l_obfuscated_data, l_obfuscated_size);
        
        // Clean up obfuscated buffer
        DAP_DELETE(l_obfuscated_data);
        
        if (l_written < 0) {
            log_it(L_ERROR, "Transport write failed: %zd", l_written);
            return l_written;
        }
        
        // Return original data size (not obfuscated size) for caller transparency
        log_it(L_DEBUG, "Wrote %zu bytes (obfuscated to %zu)", a_size, l_obfuscated_size);
        return (ssize_t)a_size;
    }
    
    // No obfuscation - direct write
    return l_transport->ops->write(a_stream, a_data, a_size);
}

/**
 * @brief Read data through transport with deobfuscation
 * 
 * This function wraps the transport's read operation, automatically
 * removing obfuscation if an obfuscation engine is attached to the transport.
 * 
 * @param a_stream Stream to read from
 * @param a_buffer Buffer to read into
 * @param a_size Maximum bytes to read
 * @return Bytes read, 0 on EOF, or negative error code
 */
ssize_t dap_net_transport_read_deobfuscated(dap_stream_t *a_stream, 
                                                void *a_buffer, 
                                                size_t a_size)
{
    if (!a_stream || !a_stream->stream_transport || !a_buffer) {
        log_it(L_ERROR, "Cannot read: invalid arguments");
        return -1;
    }

    dap_net_transport_t *l_transport = a_stream->stream_transport;
    
    if (!l_transport->ops || !l_transport->ops->read) {
        log_it(L_ERROR, "Transport does not support read operation");
        return -1;
    }

    // If obfuscation is attached, we need to read obfuscated data first
    if (l_transport->obfuscation) {
        // Allocate temporary buffer for obfuscated data
        // Note: Obfuscated data may be larger than original (padding, mimicry headers)
        size_t l_obf_buffer_size = a_size + 1024;  // Extra space for headers/padding
        void *l_obf_buffer = DAP_NEW_Z_SIZE(uint8_t, l_obf_buffer_size);
        if (!l_obf_buffer) {
            log_it(L_CRITICAL, "Memory allocation failed for obfuscated read buffer");
            return -1;
        }
        
        // Read obfuscated data from transport
        ssize_t l_read = l_transport->ops->read(a_stream, l_obf_buffer, l_obf_buffer_size);
        
        if (l_read < 0) {
            log_it(L_ERROR, "Transport read failed: %zd", l_read);
            DAP_DELETE(l_obf_buffer);
            return l_read;
        }
        
        if (l_read == 0) {
            // EOF
            DAP_DELETE(l_obf_buffer);
            return 0;
        }
        
        // Remove obfuscation
        void *l_clean_data = NULL;
        size_t l_clean_size = 0;
        
        int l_ret = dap_stream_obfuscation_remove(
            l_transport->obfuscation,
            l_obf_buffer,
            l_read,
            &l_clean_data,
            &l_clean_size
        );
        
        DAP_DELETE(l_obf_buffer);
        
        if (l_ret != 0 || !l_clean_data) {
            log_it(L_ERROR, "Deobfuscation failed: %d", l_ret);
            if (l_clean_data) {
                DAP_DELETE(l_clean_data);
            }
            return -1;
        }
        
        // Copy deobfuscated data to caller's buffer
        size_t l_copy_size = (l_clean_size < a_size) ? l_clean_size : a_size;
        memcpy(a_buffer, l_clean_data, l_copy_size);
        
        DAP_DELETE(l_clean_data);
        
        log_it(L_DEBUG, "Read %zu bytes (deobfuscated from %zd)", l_copy_size, l_read);
        return (ssize_t)l_copy_size;
    }
    
    // No obfuscation - direct read
    return l_transport->ops->read(a_stream, a_buffer, a_size);
}

/**
 * @brief Prepare transport-specific resources for client stage
 * 
 * Routes stage preparation request to transport implementation.
 * If transport doesn't provide stage_prepare callback, uses default TCP socket creation.
 * 
 * @param a_transport_type Transport type to use
 * @param a_params Stage preparation parameters
 * @param a_result Output parameter for preparation result
 * @return 0 on success, negative error code on failure
 */
int dap_net_transport_stage_prepare(dap_net_transport_type_t a_transport_type,
                                      const dap_net_stage_prepare_params_t *a_params,
                                      dap_net_stage_prepare_result_t *a_result)
{
    // Fail-fast: validate inputs immediately
    if (!a_params || !a_result) {
        log_it(L_ERROR, "Invalid arguments for stage_prepare");
        if (a_result) {
            a_result->esocket = NULL;
            a_result->error_code = -1;
        }
        return -1;
    }
    
    // Initialize result
    a_result->esocket = NULL;
    a_result->error_code = 0;
    
    // Fail-fast: transport must exist
    dap_net_transport_t *l_transport = dap_net_transport_find(a_transport_type);
    if (!l_transport) {
        log_it(L_ERROR, "Transport type %d not found", a_transport_type);
        a_result->error_code = -1;
        return -1;
    }
    
    // Fail-fast: transport must provide stage_prepare callback
    if (!l_transport->ops || !l_transport->ops->stage_prepare) {
        log_it(L_ERROR, "Transport type %d does not provide stage_prepare callback", a_transport_type);
        a_result->error_code = -2;
        return -2;
    }
    
    // Delegate to transport-specific implementation
    int l_ret = l_transport->ops->stage_prepare(l_transport, a_params, a_result);
    if (l_ret != 0) {
        log_it(L_ERROR, "Transport stage_prepare failed for type %d: %d", a_transport_type, l_ret);
        a_result->error_code = l_ret;
        return l_ret;
    }
    
    // Fail-fast: transport must return valid socket
    if (!a_result->esocket) {
        log_it(L_ERROR, "Transport stage_prepare returned success but esocket is NULL for type %d", a_transport_type);
        a_result->error_code = -3;
        return -3;
    }
    
    log_it(L_DEBUG, "Transport %d prepared socket via stage_prepare callback", a_transport_type);
    return 0;
}

