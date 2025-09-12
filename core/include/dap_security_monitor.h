/*
 * Authors:
 * Security Team
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2025
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

#pragma once

#include "dap_common.h"
#include "dap_time.h"
#include "dap_hash.h"
#include "uthash.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

// Security event types for monitoring
typedef enum {
    DAP_SECURITY_EVENT_AUTH_FAILURE = 1,
    DAP_SECURITY_EVENT_BUFFER_OVERFLOW_ATTEMPT,
    DAP_SECURITY_EVENT_INTEGER_OVERFLOW_ATTEMPT,
    DAP_SECURITY_EVENT_INVALID_SIGNATURE,
    DAP_SECURITY_EVENT_SUSPICIOUS_PACKET_SIZE,
    DAP_SECURITY_EVENT_RATE_LIMIT_EXCEEDED,
    DAP_SECURITY_EVENT_PATH_TRAVERSAL_ATTEMPT,
    DAP_SECURITY_EVENT_SQL_INJECTION_ATTEMPT,
    DAP_SECURITY_EVENT_MEMORY_ALLOCATION_FAILURE,
    DAP_SECURITY_EVENT_CONSENSUS_ATTACK_ATTEMPT
} dap_security_event_type_t;

// Security event severity levels
typedef enum {
    DAP_SECURITY_SEVERITY_LOW = 1,
    DAP_SECURITY_SEVERITY_MEDIUM,
    DAP_SECURITY_SEVERITY_HIGH,
    DAP_SECURITY_SEVERITY_CRITICAL
} dap_security_severity_t;

// Security event structure
typedef struct dap_security_event {
    dap_security_event_type_t type;
    dap_security_severity_t severity;
    dap_time_t timestamp;
    char source_addr[INET6_ADDRSTRLEN];
    char description[256];
    char details[512];
    uint32_t count;  // Number of similar events
    dap_time_t first_seen;
    dap_time_t last_seen;
} dap_security_event_t;

// Rate limiting structure
typedef struct dap_security_rate_limit {
    dap_hash_fast_t source_hash;  // Hash of source identifier
    uint32_t count;
    dap_time_t window_start;
    dap_time_t last_event;
    UT_hash_handle hh;
} dap_security_rate_limit_t;

// Security monitoring configuration
typedef struct dap_security_monitor_config {
    bool enabled;
    uint32_t max_events_per_minute;
    uint32_t max_events_per_hour;
    uint32_t auto_ban_threshold;
    dap_time_t ban_duration;
    bool log_to_file;
    char log_file_path[256];
} dap_security_monitor_config_t;

// Initialize security monitoring system
int dap_security_monitor_init(dap_security_monitor_config_t *a_config);

// Deinitialize security monitoring system
void dap_security_monitor_deinit(void);

// Report security event
void dap_security_monitor_report_event(dap_security_event_type_t a_type,
                                       dap_security_severity_t a_severity,
                                       const char *a_source_addr,
                                       const char *a_description,
                                       const char *a_details);

// Check if source should be rate limited
bool dap_security_monitor_check_rate_limit(const char *a_source_addr, uint32_t a_max_per_minute);

// Get security statistics
typedef struct dap_security_stats {
    uint32_t total_events;
    uint32_t events_last_hour;
    uint32_t events_last_minute;
    uint32_t banned_sources;
    uint32_t rate_limited_sources;
} dap_security_stats_t;

dap_security_stats_t dap_security_monitor_get_stats(void);

// Security monitoring macros for easy integration
#define DAP_SECURITY_REPORT_AUTH_FAILURE(addr, details) \
    dap_security_monitor_report_event(DAP_SECURITY_EVENT_AUTH_FAILURE, DAP_SECURITY_SEVERITY_HIGH, addr, "Authentication failure", details)

#define DAP_SECURITY_REPORT_BUFFER_OVERFLOW(addr, details) \
    dap_security_monitor_report_event(DAP_SECURITY_EVENT_BUFFER_OVERFLOW_ATTEMPT, DAP_SECURITY_SEVERITY_CRITICAL, addr, "Buffer overflow attempt", details)

#define DAP_SECURITY_REPORT_INVALID_SIGNATURE(addr, details) \
    dap_security_monitor_report_event(DAP_SECURITY_EVENT_INVALID_SIGNATURE, DAP_SECURITY_SEVERITY_MEDIUM, addr, "Invalid signature", details)

#define DAP_SECURITY_REPORT_SUSPICIOUS_SIZE(addr, details) \
    dap_security_monitor_report_event(DAP_SECURITY_EVENT_SUSPICIOUS_PACKET_SIZE, DAP_SECURITY_SEVERITY_MEDIUM, addr, "Suspicious packet size", details)

#ifdef __cplusplus
}
#endif
