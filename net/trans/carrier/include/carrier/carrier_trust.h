/**
 * @file carrier_trust.h
 * @brief Prepared-trust contract for TLS-based VPN transports.
 *
 * Per docs/VPN_TLS_SERVER_TRUST_CDB_PLAN.md (owner decision): the transport
 * adapter performs the handshake and identity check but NEVER fetches trust
 * itself. A configuration source (local config now, CDB later) prepares a
 * carrier_trust_t and hands it to the adapter; the adapter is unchanged when
 * the source changes.
 *
 * Rules (contract):
 *   - No TOFU. Missing trust for the selected mode is a configuration error,
 *     never a silent switch to verify-none.
 *   - A pin is never inherited from another host.
 *   - SPKI pin (our standard) and full-DER pin (Xray pinnedPeerCertSha256
 *     interop) are distinct formats; both are explicit, validated fields.
 *   - The TLS certificate does not replace the wallet key / order / tx / VPN
 *     authorization: client identity stays the project virtual-cert + DAP
 *     signatures (web3 pKeyHashCert model) inside the tunnel.
 *
 * No SDK dependencies. This header is a data contract + validation helpers.
 */

#ifndef CARRIER_TRUST_H
#define CARRIER_TRUST_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum carrier_trust_mode {
    CARRIER_TRUST_NONE = 0,     /* not configured -> error, never verify-none */
    CARRIER_TRUST_SYSTEM_CA,    /* public CA + name/IP check (e.g. mask site) */
    CARRIER_TRUST_PRIVATE_CA,   /* project CA delivered out-of-band (CDB/bundle) */
    CARRIER_TRUST_SPKI_PIN      /* explicit SPKI or full-DER pin */
} carrier_trust_mode_t;

#define CARRIER_TRUST_ID_MAX      64
#define CARRIER_TRUST_HOST_MAX    256
#define CARRIER_TRUST_NAME_MAX    256
#define CARRIER_TRUST_PIN_HEX     65    /* 64 hex chars + NUL */
#define CARRIER_TRUST_CA_REF_MAX  256
#define CARRIER_TRUST_SECRET_MAX  128   /* protocol secret: password / VLESS UUID */

/**
 * Prepared trust record for one endpoint. Field names follow the CDB-trust
 * contract (server_id / host / port / tls_server_name / trust_mode /
 * spki_sha256[] / ca_reference / revision / valid_from / valid_until).
 * secret carries the per-user protocol secret (Trojan/AnyTLS/ShadowTLS
 * password, XHTTP VLESS UUID) provisioned alongside the trust record.
 */
typedef struct carrier_trust {
    char     server_id[CARRIER_TRUST_ID_MAX];
    char     host[CARRIER_TRUST_HOST_MAX];
    uint16_t port;
    char     tls_server_name[CARRIER_TRUST_NAME_MAX];  /* SNI + X.509 name check */
    carrier_trust_mode_t mode;
    char     spki_sha256[CARRIER_TRUST_PIN_HEX];       /* hex SPKI SHA-256 */
    char     cert_sha256[CARRIER_TRUST_PIN_HEX];       /* hex full-DER SHA-256 (Xray) */
    char     ca_reference[CARRIER_TRUST_CA_REF_MAX];   /* trusted CA name/path */
    char     secret[CARRIER_TRUST_SECRET_MAX];         /* provisioned, never hardcoded */
    uint32_t revision;
    int64_t  valid_from;     /* unix seconds; 0 = unbounded */
    int64_t  valid_until;    /* unix seconds; 0 = unbounded */
} carrier_trust_t;

/** True if a_hex is exactly 64 lowercase/uppercase hex digits. */
static inline bool carrier_trust_is_hex64(const char *a_hex)
{
    if (!a_hex) return false;
    size_t l_n = strlen(a_hex);
    if (l_n != 64) return false;
    for (size_t i = 0; i < l_n; ++i) {
        char c = a_hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

/**
 * Validate that the record carries the trust material its mode requires.
 * Does NOT verify any certificate — that is the TLS adapter's job at handshake.
 * Returns true when the record is usable for a connection attempt.
 */
static inline bool carrier_trust_prepared(const carrier_trust_t *a_t, int64_t a_now_unix)
{
    if (!a_t || a_t->mode == CARRIER_TRUST_NONE) return false;
    if (!a_t->host[0] || !a_t->port) return false;
    if (a_t->valid_from && a_now_unix < a_t->valid_from) return false;
    if (a_t->valid_until && a_now_unix > a_t->valid_until) return false;

    switch (a_t->mode) {
        case CARRIER_TRUST_SYSTEM_CA:
            /* name/IP check mandatory for CA modes */
            return a_t->tls_server_name[0] != '\0';
        case CARRIER_TRUST_PRIVATE_CA:
            return a_t->ca_reference[0] != '\0' && a_t->tls_server_name[0] != '\0';
        case CARRIER_TRUST_SPKI_PIN:
            /* at least one explicit, well-formed pin; no implicit verify-none */
            return carrier_trust_is_hex64(a_t->spki_sha256) ||
                   carrier_trust_is_hex64(a_t->cert_sha256);
        default:
            return false;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* CARRIER_TRUST_H */