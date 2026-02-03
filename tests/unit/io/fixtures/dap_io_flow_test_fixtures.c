/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef __linux__
#ifdef HAVE_LIBCAP
#include <sys/capability.h>
#include <linux/capability.h>
#endif
#endif

#include "dap_io_flow_test_fixtures.h"
#include "dap_common.h"

#define LOG_TAG "io_flow_test_fixtures"

static bool s_initialized = false;

/**
 * @brief Cross-platform sleep in milliseconds
 */
static void s_sleep_ms(uint32_t a_ms)
{
#ifdef _WIN32
    Sleep(a_ms);
#else
    usleep(a_ms * 1000);
#endif
}

/**
 * @brief Cross-platform check if running as root/admin
 */
static bool s_is_privileged(void)
{
#ifdef _WIN32
    BOOL l_is_admin = FALSE;
    PSID l_admin_group = NULL;
    SID_IDENTIFIER_AUTHORITY l_nt_auth = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&l_nt_auth, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0,
                                  &l_admin_group)) {
        CheckTokenMembership(NULL, l_admin_group, &l_is_admin);
        FreeSid(l_admin_group);
    }
    return l_is_admin != FALSE;
#else
    return geteuid() == 0;
#endif
}

int dap_io_flow_test_fixtures_init(void)
{
    if (s_initialized) {
        return 0;
    }
    
    log_it(L_INFO, "Initializing IO flow test fixtures");
    s_initialized = true;
    return 0;
}

void dap_io_flow_test_fixtures_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    
    log_it(L_INFO, "Deinitializing IO flow test fixtures");
    s_initialized = false;
}

dap_io_flow_test_context_t *dap_io_flow_test_context_create(dap_io_flow_test_tier_t a_tier)
{
    if (a_tier >= IO_FLOW_TIER_COUNT) {
        log_it(L_ERROR, "Invalid tier: %d", a_tier);
        return NULL;
    }
    
    if (!dap_io_flow_test_tier_available(a_tier)) {
        log_it(L_WARNING, "Tier %s not available on this platform", 
               dap_io_flow_test_tier_name(a_tier));
        return NULL;
    }
    
    dap_io_flow_test_context_t *l_ctx = DAP_NEW_Z(dap_io_flow_test_context_t);
    if (!l_ctx) {
        return NULL;
    }
    
    l_ctx->tier = a_tier;
    l_ctx->initialized = true;
    
    log_it(L_INFO, "Created test context for tier: %s", dap_io_flow_test_tier_name(a_tier));
    return l_ctx;
}

void dap_io_flow_test_context_delete(dap_io_flow_test_context_t *a_ctx)
{
    if (!a_ctx) {
        return;
    }
    
    log_it(L_INFO, "Deleting test context for tier: %s", 
           dap_io_flow_test_tier_name(a_ctx->tier));
    DAP_DELETE(a_ctx);
}

bool dap_io_flow_test_tier_available(dap_io_flow_test_tier_t a_tier)
{
    switch (a_tier) {
        case IO_FLOW_TIER_APPLICATION:
            // Application tier is always available on all platforms
            return true;
            
        case IO_FLOW_TIER_CBPF:
#ifdef __linux__
            return true;
#else
            return false;
#endif
            
        case IO_FLOW_TIER_EBPF:
#ifdef __linux__
            return dap_io_flow_test_ebpf_capable();
#else
            return false;
#endif
            
        default:
            return false;
    }
}

const char *dap_io_flow_test_tier_name(dap_io_flow_test_tier_t a_tier)
{
    switch (a_tier) {
        case IO_FLOW_TIER_APPLICATION:
            return "Application";
        case IO_FLOW_TIER_CBPF:
            return "Classic BPF";
        case IO_FLOW_TIER_EBPF:
            return "Extended BPF";
        default:
            return "Unknown";
    }
}

int dap_io_flow_test_basic_transfer(dap_io_flow_test_context_t *a_ctx, size_t a_data_size)
{
    if (!a_ctx || !a_ctx->initialized) {
        return -1;
    }
    
    // Simulate data transfer
    a_ctx->bytes_sent += a_data_size;
    a_ctx->bytes_received += a_data_size;
    a_ctx->packets_sent++;
    a_ctx->packets_received++;
    
    log_it(L_DEBUG, "Basic transfer: %zu bytes via %s tier", 
           a_data_size, dap_io_flow_test_tier_name(a_ctx->tier));
    
    return 0;
}

int dap_io_flow_test_throughput(dap_io_flow_test_context_t *a_ctx, 
                                 uint32_t a_duration_ms,
                                 uint64_t *a_throughput_out)
{
    if (!a_ctx || !a_ctx->initialized || !a_throughput_out) {
        return -1;
    }
    
    uint64_t l_start_bytes = a_ctx->bytes_sent;
    uint32_t l_iterations = a_duration_ms / 10;
    if (l_iterations == 0) l_iterations = 1;
    
    for (uint32_t i = 0; i < l_iterations; i++) {
        dap_io_flow_test_basic_transfer(a_ctx, 1024);
        s_sleep_ms(10);
    }
    
    uint64_t l_transferred = a_ctx->bytes_sent - l_start_bytes;
    *a_throughput_out = (l_transferred * 1000) / a_duration_ms;
    
    log_it(L_INFO, "Throughput test: %lu bytes/sec via %s tier",
           (unsigned long)*a_throughput_out, dap_io_flow_test_tier_name(a_ctx->tier));
    
    return 0;
}

bool dap_io_flow_test_ebpf_capable(void)
{
#ifdef __linux__
#ifdef HAVE_LIBCAP
    cap_t l_caps = cap_get_proc();
    if (!l_caps) {
        return s_is_privileged();
    }
    
    cap_flag_value_t l_cap_val;
    bool l_capable = false;
    
#ifdef CAP_BPF
    if (cap_get_flag(l_caps, CAP_BPF, CAP_EFFECTIVE, &l_cap_val) == 0) {
        if (l_cap_val == CAP_SET) {
            l_capable = true;
        }
    }
#endif
    
    if (!l_capable) {
        if (cap_get_flag(l_caps, CAP_SYS_ADMIN, CAP_EFFECTIVE, &l_cap_val) == 0) {
            if (l_cap_val == CAP_SET) {
                l_capable = true;
            }
        }
    }
    
    if (!l_capable) {
        l_capable = s_is_privileged();
    }
    
    cap_free(l_caps);
    return l_capable;
#else
    // No libcap - just check if running as root
    return s_is_privileged();
#endif
#else
    return false;
#endif
}
