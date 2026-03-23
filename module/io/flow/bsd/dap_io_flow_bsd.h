/**
 * @file dap_io_flow_bsd.h
 * @brief BSD-specific load balancing for SO_REUSEPORT
 * 
 * FreeBSD/OpenBSD/NetBSD support SO_REUSEPORT but not BPF attach.
 * Uses kernel's default round-robin or hash-based distribution.
 * 
 * Note: BSD kernel's SO_REUSEPORT may provide sticky sessions by default.
 * If not, falls back to Application-level LB (Tier 1).
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if BSD SO_REUSEPORT provides sticky sessions
 * 
 * BSD kernels may provide deterministic hash-based distribution
 * without needing BPF programs.
 * 
 * @return true if sticky sessions available, false otherwise
 */
bool dap_io_flow_bsd_reuseport_has_sticky_sessions(void);

#ifdef __cplusplus
}
#endif
