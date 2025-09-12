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

#include "dap_security_monitor.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "uthash.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LOG_TAG "security_monitor"

// Global security monitoring state
static dap_security_monitor_config_t s_config = {0};
static dap_security_rate_limit_t *s_rate_limits = NULL;
static pthread_rwlock_t s_rate_limits_lock = PTHREAD_RWLOCK_INITIALIZER;
static dap_security_stats_t s_stats = {0};
static pthread_mutex_t s_stats_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *s_log_file = NULL;

// Event type to string mapping
static const char* s_event_type_to_string(dap_security_event_type_t a_type) {
    switch (a_type) {
        case DAP_SECURITY_EVENT_AUTH_FAILURE: return "AUTH_FAILURE";
        case DAP_SECURITY_EVENT_BUFFER_OVERFLOW_ATTEMPT: return "BUFFER_OVERFLOW_ATTEMPT";
        case DAP_SECURITY_EVENT_INTEGER_OVERFLOW_ATTEMPT: return "INTEGER_OVERFLOW_ATTEMPT";
        case DAP_SECURITY_EVENT_INVALID_SIGNATURE: return "INVALID_SIGNATURE";
        case DAP_SECURITY_EVENT_SUSPICIOUS_PACKET_SIZE: return "SUSPICIOUS_PACKET_SIZE";
        case DAP_SECURITY_EVENT_RATE_LIMIT_EXCEEDED: return "RATE_LIMIT_EXCEEDED";
        case DAP_SECURITY_EVENT_PATH_TRAVERSAL_ATTEMPT: return "PATH_TRAVERSAL_ATTEMPT";
        case DAP_SECURITY_EVENT_SQL_INJECTION_ATTEMPT: return "SQL_INJECTION_ATTEMPT";
        case DAP_SECURITY_EVENT_MEMORY_ALLOCATION_FAILURE: return "MEMORY_ALLOCATION_FAILURE";
        case DAP_SECURITY_EVENT_CONSENSUS_ATTACK_ATTEMPT: return "CONSENSUS_ATTACK_ATTEMPT";
        default: return "UNKNOWN";
    }
}

// Severity to string mapping
static const char* s_severity_to_string(dap_security_severity_t a_severity) {
    switch (a_severity) {
        case DAP_SECURITY_SEVERITY_LOW: return "LOW";
        case DAP_SECURITY_SEVERITY_MEDIUM: return "MEDIUM";
        case DAP_SECURITY_SEVERITY_HIGH: return "HIGH";
        case DAP_SECURITY_SEVERITY_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

// Initialize security monitoring system
int dap_security_monitor_init(dap_security_monitor_config_t *a_config) {
    if (!a_config) {
        log_it(L_ERROR, "Security monitor config is NULL");
        return -1;
    }
    
    s_config = *a_config;
    
    // Open log file if configured
    if (s_config.log_to_file && s_config.log_file_path[0]) {
        s_log_file = fopen(s_config.log_file_path, "a");
        if (!s_log_file) {
            log_it(L_WARNING, "Failed to open security log file: %s", s_config.log_file_path);
        }
    }
    
    log_it(L_NOTICE, "Security monitoring system initialized");
    return 0;
}

// Deinitialize security monitoring system
void dap_security_monitor_deinit(void) {
    // Clean up rate limits
    pthread_rwlock_wrlock(&s_rate_limits_lock);
    dap_security_rate_limit_t *l_rate_limit, *l_tmp;
    HASH_ITER(hh, s_rate_limits, l_rate_limit, l_tmp) {
        HASH_DEL(s_rate_limits, l_rate_limit);
        DAP_DELETE(l_rate_limit);
    }
    pthread_rwlock_unlock(&s_rate_limits_lock);
    
    // Close log file
    if (s_log_file) {
        fclose(s_log_file);
        s_log_file = NULL;
    }
    
    log_it(L_NOTICE, "Security monitoring system deinitialized");
}

// Report security event
void dap_security_monitor_report_event(dap_security_event_type_t a_type,
                                       dap_security_severity_t a_severity,
                                       const char *a_source_addr,
                                       const char *a_description,
                                       const char *a_details) {
    if (!s_config.enabled) {
        return;
    }
    
    dap_time_t l_now = dap_time_now();
    
    // Update statistics
    pthread_mutex_lock(&s_stats_lock);
    s_stats.total_events++;
    s_stats.events_last_minute++;  // Simplified - would need proper time window tracking
    s_stats.events_last_hour++;
    pthread_mutex_unlock(&s_stats_lock);
    
    // Log to system log
    log_it(a_severity >= DAP_SECURITY_SEVERITY_HIGH ? L_WARNING : L_INFO,
           "SECURITY EVENT [%s] %s from %s: %s - %s",
           s_severity_to_string(a_severity),
           s_event_type_to_string(a_type),
           a_source_addr ? a_source_addr : "unknown",
           a_description ? a_description : "",
           a_details ? a_details : "");
    
    // Log to security log file
    if (s_log_file) {
        fprintf(s_log_file, "%lu,%s,%s,%s,%s,%s\n",
                (unsigned long)l_now,
                s_severity_to_string(a_severity),
                s_event_type_to_string(a_type),
                a_source_addr ? a_source_addr : "unknown",
                a_description ? a_description : "",
                a_details ? a_details : "");
        fflush(s_log_file);
    }
}

// Check if source should be rate limited
bool dap_security_monitor_check_rate_limit(const char *a_source_addr, uint32_t a_max_per_minute) {
    if (!s_config.enabled || !a_source_addr) {
        return false;
    }
    
    dap_time_t l_now = dap_time_now();
    dap_hash_fast_t l_source_hash;
    dap_hash_fast(a_source_addr, strlen(a_source_addr), &l_source_hash);
    
    pthread_rwlock_wrlock(&s_rate_limits_lock);
    
    dap_security_rate_limit_t *l_rate_limit = NULL;
    HASH_FIND(hh, s_rate_limits, &l_source_hash, sizeof(l_source_hash), l_rate_limit);
    
    if (!l_rate_limit) {
        // Create new rate limit entry
        l_rate_limit = DAP_NEW_Z(dap_security_rate_limit_t);
        l_rate_limit->source_hash = l_source_hash;
        l_rate_limit->window_start = l_now;
        l_rate_limit->count = 1;
        l_rate_limit->last_event = l_now;
        HASH_ADD(hh, s_rate_limits, source_hash, sizeof(l_source_hash), l_rate_limit);
        pthread_rwlock_unlock(&s_rate_limits_lock);
        return false;
    }
    
    // Check if window expired (1 minute = 60 seconds)
    if (l_now - l_rate_limit->window_start > 60) {
        l_rate_limit->window_start = l_now;
        l_rate_limit->count = 1;
        l_rate_limit->last_event = l_now;
        pthread_rwlock_unlock(&s_rate_limits_lock);
        return false;
    }
    
    l_rate_limit->count++;
    l_rate_limit->last_event = l_now;
    
    bool l_rate_limited = l_rate_limit->count > a_max_per_minute;
    
    if (l_rate_limited) {
        pthread_mutex_lock(&s_stats_lock);
        s_stats.rate_limited_sources++;
        pthread_mutex_unlock(&s_stats_lock);
        
        dap_security_monitor_report_event(DAP_SECURITY_EVENT_RATE_LIMIT_EXCEEDED,
                                          DAP_SECURITY_SEVERITY_HIGH,
                                          a_source_addr,
                                          "Rate limit exceeded",
                                          dap_strdup_printf("Count: %u, Max: %u", l_rate_limit->count, a_max_per_minute));
    }
    
    pthread_rwlock_unlock(&s_rate_limits_lock);
    return l_rate_limited;
}

// Get security statistics
dap_security_stats_t dap_security_monitor_get_stats(void) {
    pthread_mutex_lock(&s_stats_lock);
    dap_security_stats_t l_stats = s_stats;
    pthread_mutex_unlock(&s_stats_lock);
    return l_stats;
}
