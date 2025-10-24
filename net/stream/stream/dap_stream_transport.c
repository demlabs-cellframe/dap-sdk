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
 * @file dap_stream_transport.c
 * @brief Transport Abstraction Layer implementation
 * @date 2025-10-23
 * @author Cellframe Team
 */

#include <string.h>
#include <stdio.h>
#include "dap_stream_transport.h"
#include "dap_stream_obfuscation.h"
#include "dap_stream.h"
#include "dap_common.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_stream_transport"

// Global transport registry (hash table keyed by transport type)
static dap_stream_transport_t *s_transport_registry = NULL;

// Thread safety (if needed, can add pthread_rwlock_t here)
// For now, assuming single-threaded init/registration

/**
 * @brief Initialize transport abstraction system
 * @return 0 on success, -1 on failure
 */
int dap_stream_transport_init(void)
{
    log_it(L_NOTICE, "Initializing DAP Stream Transport Abstraction Layer");
    
    // Initialize registry (hash table starts empty)
    s_transport_registry = NULL;
    
    log_it(L_INFO, "Transport registry initialized (empty)");
    return 0;
}

/**
 * @brief Cleanup transport abstraction system
 */
void dap_stream_transport_deinit(void)
{
    log_it(L_NOTICE, "Deinitializing DAP Stream Transport Abstraction Layer");
    
    // Iterate through all registered transports and unregister
    dap_stream_transport_t *l_transport, *l_tmp;
    HASH_ITER(hh, s_transport_registry, l_transport, l_tmp) {
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
    }
    
    s_transport_registry = NULL;
    log_it(L_INFO, "Transport registry cleared");
}

/**
 * @brief Register a new transport implementation
 * @param a_name Transport name (max 63 chars)
 * @param a_type Transport type identifier
 * @param a_ops Operations table (must remain valid)
 * @param a_inheritor Transport-specific private data (optional)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_register(const char *a_name, 
                                    dap_stream_transport_type_t a_type, 
                                    const dap_stream_transport_ops_t *a_ops, 
                                    void *a_inheritor)
{
    // Validate parameters
    if (!a_name || !a_ops) {
        log_it(L_ERROR, "Invalid parameters: name=%p, ops=%p", a_name, a_ops);
        return -1;
    }
    
    // Check if transport type already registered
    dap_stream_transport_t *l_existing = NULL;
    HASH_FIND_INT(s_transport_registry, &a_type, l_existing);
    if (l_existing) {
        log_it(L_WARNING, "Transport type 0x%02X already registered as '%s'", 
               a_type, l_existing->name);
        return -2;
    }
    
    // Allocate new transport structure
    dap_stream_transport_t *l_transport = DAP_NEW_Z(dap_stream_transport_t);
    if (!l_transport) {
        log_it(L_CRITICAL, "Failed to allocate memory for transport '%s'", a_name);
        return -3;
    }
    
    // Initialize transport fields
    l_transport->type = a_type;
    l_transport->ops = a_ops;
    l_transport->_inheritor = a_inheritor;
    l_transport->obfuscation = NULL;  // No obfuscation by default
    
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
    
    log_it(L_NOTICE, "Registered transport: %s (type=0x%02X, caps=0x%04X)", 
           l_transport->name, l_transport->type, l_transport->capabilities);
    
    return 0;
}

/**
 * @brief Unregister a transport implementation
 * @param a_type Transport type to unregister
 * @return 0 on success, -1 if not found
 */
int dap_stream_transport_unregister(dap_stream_transport_type_t a_type)
{
    // Find transport by type
    dap_stream_transport_t *l_transport = NULL;
    HASH_FIND_INT(s_transport_registry, &a_type, l_transport);
    
    if (!l_transport) {
        log_it(L_WARNING, "Transport type 0x%02X not registered", a_type);
        return -1;
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
dap_stream_transport_t *dap_stream_transport_find(dap_stream_transport_type_t a_type)
{
    dap_stream_transport_t *l_transport = NULL;
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
dap_stream_transport_t *dap_stream_transport_find_by_name(const char *a_name)
{
    if (!a_name) {
        return NULL;
    }
    
    // Linear search through hash table (names are not indexed)
    dap_stream_transport_t *l_transport, *l_tmp;
    HASH_ITER(hh, s_transport_registry, l_transport, l_tmp) {
        if (dap_strcmp(l_transport->name, a_name) == 0) {
            return l_transport;
        }
    }
    
    log_it(L_DEBUG, "Transport '%s' not found in registry", a_name);
    return NULL;
}

/**
 * @brief Get list of all registered transports
 * @return Linked list of dap_stream_transport_t* (caller must free list, not contents)
 */
dap_list_t *dap_stream_transport_list_all(void)
{
    dap_list_t *l_list = NULL;
    
    dap_stream_transport_t *l_transport, *l_tmp;
    HASH_ITER(hh, s_transport_registry, l_transport, l_tmp) {
        l_list = dap_list_append(l_list, l_transport);
    }
    
    return l_list;
}

/**
 * @brief Attach obfuscation engine to transport
 * @param a_transport Transport to attach obfuscation to
 * @param a_obfuscation Obfuscation engine instance
 * @return 0 on success, -1 on failure
 */
int dap_stream_transport_attach_obfuscation(dap_stream_transport_t *a_transport, 
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
void dap_stream_transport_detach_obfuscation(dap_stream_transport_t *a_transport)
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
ssize_t dap_stream_transport_write_obfuscated(dap_stream_t *a_stream, 
                                               const void *a_data, 
                                               size_t a_size)
{
    if (!a_stream || !a_stream->stream_transport) {
        log_it(L_ERROR, "Cannot write: invalid stream or transport");
        return -1;
    }

    dap_stream_transport_t *l_transport = a_stream->stream_transport;
    
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
ssize_t dap_stream_transport_read_deobfuscated(dap_stream_t *a_stream, 
                                                void *a_buffer, 
                                                size_t a_size)
{
    if (!a_stream || !a_stream->stream_transport || !a_buffer) {
        log_it(L_ERROR, "Cannot read: invalid arguments");
        return -1;
    }

    dap_stream_transport_t *l_transport = a_stream->stream_transport;
    
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

