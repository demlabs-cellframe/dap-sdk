/*
 * Transport-independent QoS measurement via probe/echo protocol extension.
 *
 * Latency probe mechanism:
 *
 * UDP/TLS/DNS transport:
 *   Client uses FSM STAGE_QOS_PROBE branch (STAGE_BEGIN → STAGE_QOS_PROBE).
 *   Sends 800 bytes through the normal obfuscation path with PROBE_MAGIC header.
 *   Server deobfuscates, checks magic → echoes instead of doing KEM.
 *
 * HTTP/WS transport (legacy):
 *   Client POSTs probe packet to /qos_probe HTTP endpoint.
 *   Server echoes without enc_init handshake.
 *
 * Bandwidth test:
 *   HTTP/WS: POST BW_REQUEST → server replies with bw_bytes of data.
 *   UDP/TLS/DNS: burst of N probe/echo round-trips for aggregate estimation.
 *
 * All of this goes through the existing transport stack — no code duplication.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_net_trans.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Protocol constants                                                        */
/* ========================================================================== */

#define DAP_QOS_PROBE_MAGIC       0x44415050U  /* "DAPP" (LE) */
#define DAP_QOS_ECHO_MAGIC        0x44415045U  /* "DAPE" (LE) */

#define DAP_QOS_TYPE_PROBE        0x01
#define DAP_QOS_TYPE_ECHO         0x02
#define DAP_QOS_TYPE_BW_REQUEST   0x03
#define DAP_QOS_TYPE_BW_DATA      0x04

/*
 * pkey_exchange_type value used by STAGE_QOS_PROBE to signal transports
 * that the handshake payload is a probe packet, not a KEM public key.
 */
#define DAP_ENC_KEY_TYPE_QOS_PROBE  0xFE

/* ========================================================================== */
/*  Wire formats (packed, fits into 800-byte handshake slot)                  */
/* ========================================================================== */

typedef struct DAP_ALIGN_PACKED {
    uint32_t magic;       /* DAP_QOS_PROBE_MAGIC                    */
    uint8_t  type;        /* DAP_QOS_TYPE_PROBE or DAP_QOS_TYPE_BW_REQUEST */
    uint8_t  _pad[3];
    uint64_t probe_id;    /* random, for request/response matching   */
    uint64_t client_ts;   /* client CLOCK_MONOTONIC nanoseconds      */
    uint32_t bw_bytes;    /* BW_REQUEST only: how many bytes to send */
    uint32_t _reserved;
} dap_qos_probe_pkt_t;   /* 32 bytes; rest of 800 is random padding */

typedef struct DAP_ALIGN_PACKED {
    uint32_t magic;       /* DAP_QOS_ECHO_MAGIC                     */
    uint8_t  type;        /* DAP_QOS_TYPE_ECHO or DAP_QOS_TYPE_BW_DATA */
    uint8_t  _pad[3];
    uint64_t probe_id;    /* echoed from probe                       */
    uint64_t client_ts;   /* echoed from probe                       */
    uint64_t server_ts;   /* server CLOCK_MONOTONIC nanoseconds      */
} dap_qos_echo_pkt_t;    /* 32 bytes; rest of response is random/data */

/* ========================================================================== */
/*  Server-side handlers                                                      */
/* ========================================================================== */

/**
 * Check whether a deobfuscated handshake payload is a QoS probe.
 * Used by UDP server to detect probe in the obfuscated handshake path.
 * @return true if the first 4 bytes match DAP_QOS_PROBE_MAGIC.
 */
bool dap_qos_is_probe(const void *a_payload, size_t a_size);

/**
 * Build an echo response for a probe packet.
 * Caller must DAP_DELETE(*a_echo_out).
 * @return 0 on success, -1 on error.
 */
int  dap_qos_build_echo(const void *a_probe, size_t a_probe_size,
                         void **a_echo_out, size_t *a_echo_size);

/**
 * Register /qos_probe HTTP handler on the given HTTP server.
 * Used by legacy HTTP transport for lightweight probe/echo.
 */
struct dap_http_server;
void dap_net_trans_qos_add_proc(struct dap_http_server *a_http);

/* ========================================================================== */
/*  Client-side QoS API (transport-independent, blocking)                     */
/* ========================================================================== */

/**
 * Open a short probe session through the given transport, send probe/echo,
 * measure round-trip latency, and close.
 * @return Latency in ms (>=0) on success, negative on failure.
 */
int dap_net_trans_qos_probe_latency(dap_net_trans_t *a_trans,
                                     const char *a_host, uint16_t a_port,
                                     uint32_t a_timeout_ms);

/**
 * N × probe_latency.  Returns per-probe latencies in a_out_rtt.
 */
int dap_net_trans_qos_measure_rtt(dap_net_trans_t *a_trans,
                                   const char *a_host, uint16_t a_port,
                                   uint32_t a_count, uint32_t a_timeout_ms,
                                   uint32_t *a_out_rtt, uint32_t *a_out_ok);

/**
 * Probe → bandwidth_test → server streams data → measure throughput.
 */
int dap_net_trans_qos_measure_throughput(dap_net_trans_t *a_trans,
                                         const char *a_host, uint16_t a_port,
                                         uint32_t a_timeout_ms,
                                         float *a_out_down_mbps,
                                         float *a_out_up_mbps);

#ifdef __cplusplus
}
#endif
