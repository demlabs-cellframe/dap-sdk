/**
 * @file dap_conn_handle.h
 * @brief Generation-checked connection handle (no @ref dap_conn.h / OLB).
 *
 * Slot pointer + captured generation.  Resolve to @c dap_conn_t* only via
 * @ref dap_conn.h (@c dap_conn_resolve).
 */
#pragma once

#include <stdint.h>

typedef struct {
    struct dap_conn *c;
    uint64_t         gen;
} dap_conn_handle_t;
