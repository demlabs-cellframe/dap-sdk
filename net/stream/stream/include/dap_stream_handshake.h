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
 * @file dap_stream_handshake.h
 * @brief DAP Stream Handshake Protocol (DSHP) v1.0
 * 
 * This file defines a transport-agnostic handshake protocol using TLV (Type-Length-Value)
 * binary encoding. DSHP replaces HTTP-based handshake with a more efficient, flexible
 * protocol that works over any binary transport (UDP, WebSocket, TLS, DNS, etc.).
 * 
 * Key Features:
 * - Binary TLV encoding (4 bytes overhead per field)
 * - Transport-agnostic (works over HTTP, UDP, WebSocket, etc.)
 * - Extensible (new TLV types can be added without breaking parsers)
 * - Efficient (34% smaller than HTTP/JSON for typical handshake)
 * - Obfuscation-friendly (binary format easier to disguise)
 * 
 * Protocol Structure:
 * - Magic number: 0x44415053 ('DAPS' in ASCII)
 * - Version: uint32_t (major.minor.patch.build)
 * - 6 message types: handshake request/response, session create/response, stream ready/start
 * - TLV type IDs allocated in ranges (0x0100-0x0FFF)
 * 
 * @date 2025-10-23
 * @author Cellframe Team
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dap_common.h"
#include "dap_enc_key.h"

// Forward declarations
typedef struct dap_cert dap_cert_t;
typedef struct dap_stream dap_stream_t;

/**
 * @brief DSHP protocol magic number ('DAPS' in ASCII)
 * 
 * @warning DPI DETECTION RISK: Static magic number is easily detected by DPI!
 * 
 * Current implementation uses static magic for development and testing.
 * This creates a unique signature that DPI systems can easily identify.
 * 
 * Mitigation strategies (to be implemented in Phase 5):
 * 1. XOR obfuscation with session key (HIGH priority)
 *    - Each session gets unique obfuscated magic
 *    - DPI sees random bytes instead of constant pattern
 * 
 * 2. Protocol mimicry (MEDIUM priority)
 *    - HTTPS transport: magic looks like TLS record header (0x17030300XX)
 *    - WebSocket transport: magic looks like WS frame header (0x81XX...)
 *    - DPI classifies as legitimate protocol
 * 
 * 3. Polymorphic magic rotation (LOW priority)
 *    - Rotate magic value every N packets
 *    - Multiple valid magic values
 * 
 * 4. No-magic mode (ultimate stealth)
 *    - Remove magic entirely
 *    - Validate by TLV structure only
 * 
 * For production deployment with DPI bypass requirements, magic obfuscation
 * MUST be enabled in obfuscation engine (Phase 5).
 * 
 * @see Phase 5: Obfuscation Module Implementation
 * @see dap_stream_obfuscation_obfuscate_magic() (to be implemented)
 */
#define DAP_STREAM_HANDSHAKE_MAGIC  0x44415053

/**
 * @brief Current DSHP protocol version (1.0.0.0)
 */
#define DAP_STREAM_HANDSHAKE_VERSION  0x01000000

/**
 * @brief Maximum TLV value size (64KB - 1)
 */
#define DAP_STREAM_HANDSHAKE_MAX_TLV_SIZE  65535

/**
 * @brief Message type identifiers
 * 
 * Defines the 6 message types in the DSHP protocol lifecycle.
 */
typedef enum dap_stream_handshake_msg_type {
    DSHP_MSG_HANDSHAKE_REQUEST        = 0x0001,  ///< Client → Server: Initial handshake
    DSHP_MSG_HANDSHAKE_RESPONSE       = 0x0002,  ///< Server → Client: Handshake result
    DSHP_MSG_SESSION_CREATE           = 0x0003,  ///< Client → Server: Create session (encrypted)
    DSHP_MSG_SESSION_CREATE_RESPONSE  = 0x0004,  ///< Server → Client: Session result (encrypted)
    DSHP_MSG_STREAM_READY             = 0x0005,  ///< Client → Server: Ready to stream
    DSHP_MSG_STREAM_START             = 0x0006   ///< Server → Client: Start streaming
} dap_stream_handshake_msg_type_t;

/**
 * @brief TLV type IDs - Message headers and control
 * 
 * Range: 0x0100-0x01FF
 */
typedef enum dap_stream_handshake_tlv_header {
    DSHP_TLV_MAGIC          = 0x0100,  ///< Protocol magic number (4 bytes)
    DSHP_TLV_VERSION        = 0x0101,  ///< Protocol version (4 bytes)
    DSHP_TLV_MESSAGE_TYPE   = 0x0102,  ///< Message type (2 bytes)
    DSHP_TLV_STATUS         = 0x0103   ///< Status code (1 byte: 0=success, 1=error)
} dap_stream_handshake_tlv_header_t;

/**
 * @brief TLV type IDs - Encryption parameters
 * 
 * Range: 0x0200-0x02FF
 */
typedef enum dap_stream_handshake_tlv_encryption {
    DSHP_TLV_ENC_TYPE           = 0x0200,  ///< Symmetric encryption algorithm (1 byte)
    DSHP_TLV_PKEY_EXCHANGE_TYPE = 0x0201,  ///< Public key exchange algorithm (1 byte)
    DSHP_TLV_PKEY_EXCHANGE_SIZE = 0x0202,  ///< Public key size (4 bytes)
    DSHP_TLV_BLOCK_KEY_SIZE     = 0x0203   ///< Symmetric key block size (4 bytes)
} dap_stream_handshake_tlv_encryption_t;

/**
 * @brief TLV type IDs - Authentication (keys, certs, signatures)
 * 
 * Range: 0x0300-0x03FF
 */
typedef enum dap_stream_handshake_tlv_auth {
    DSHP_TLV_ALICE_PUB_KEY   = 0x0300,  ///< Client public key (variable)
    DSHP_TLV_ALICE_SIGNATURE = 0x0301,  ///< Client signature (variable)
    DSHP_TLV_ALICE_CERT      = 0x0302,  ///< Client certificate (variable)
    DSHP_TLV_BOB_PUB_KEY     = 0x0600,  ///< Server public key (variable)
    DSHP_TLV_BOB_SIGNATURE   = 0x0601   ///< Server signature (variable)
} dap_stream_handshake_tlv_auth_t;

/**
 * @brief TLV type IDs - Extensions and optional features
 * 
 * Range: 0x0400-0x04FF
 */
typedef enum dap_stream_handshake_tlv_extensions {
    DSHP_TLV_OBFUSCATION_PARAMS = 0x0400,  ///< Obfuscation settings (variable)
    DSHP_TLV_TRANSPORT_HINTS    = 0x0401   ///< Transport hints (MTU, window, etc.)
} dap_stream_handshake_tlv_extensions_t;

/**
 * @brief TLV type IDs - Session management
 * 
 * Range: 0x0500-0x05FF
 */
typedef enum dap_stream_handshake_tlv_session {
    DSHP_TLV_SESSION_ID      = 0x0500,  ///< Session identifier (4 bytes)
    DSHP_TLV_SESSION_TIMEOUT = 0x0501   ///< Session idle timeout (4 bytes, seconds)
} dap_stream_handshake_tlv_session_t;

/**
 * @brief TLV type IDs - Errors and diagnostics
 * 
 * Range: 0x0700-0x07FF
 */
typedef enum dap_stream_handshake_tlv_errors {
    DSHP_TLV_ERROR_CODE    = 0x0700,  ///< Error code (4 bytes)
    DSHP_TLV_ERROR_MESSAGE = 0x0701   ///< Human-readable error (UTF-8 string)
} dap_stream_handshake_tlv_errors_t;

/**
 * @brief TLV type IDs - Stream parameters
 * 
 * Range: 0x0800-0x08FF
 */
typedef enum dap_stream_handshake_tlv_stream {
    DSHP_TLV_CHANNELS       = 0x0800,  ///< Active channel IDs (UTF-8 string, e.g., "C,F,N")
    DSHP_TLV_STREAM_ENC_TYPE = 0x0801,  ///< Stream encryption type (1 byte)
    DSHP_TLV_STREAM_ENC_SIZE = 0x0802,  ///< Stream encryption key size (4 bytes)
    DSHP_TLV_STREAM_ENC_HDR  = 0x0803   ///< Encrypt headers flag (1 byte: 0/1)
} dap_stream_handshake_tlv_stream_t;

/**
 * @brief TLV header structure (4 bytes total)
 * 
 * All TLV fields start with this header.
 */
typedef struct dap_stream_handshake_tlv_header {
    uint16_t type;    ///< TLV type identifier (network byte order)
    uint16_t length;  ///< Value length in bytes, NOT including header (network byte order)
} __attribute__((packed)) dap_stream_handshake_tlv_hdr_t;

/**
 * @brief Handshake request message structure
 * 
 * Client → Server: Initial handshake with encryption parameters
 */
typedef struct dap_stream_handshake_request {
    uint32_t magic;                      ///< DSHP magic number (0x44415053)
    uint32_t version;                    ///< Protocol version
    dap_enc_key_type_t enc_type;        ///< Symmetric encryption algorithm
    dap_enc_key_type_t pkey_exchange_type;  ///< Key exchange algorithm
    uint32_t pkey_exchange_size;         ///< Public key size
    uint32_t block_key_size;             ///< Block size
    uint8_t *alice_pub_key;              ///< Client public key (allocated)
    size_t alice_pub_key_size;           ///< Public key size
    uint8_t *alice_signature;            ///< Optional signature (NULL if not present)
    size_t alice_signature_size;         ///< Signature size (0 if not present)
    dap_cert_t *alice_cert;              ///< Optional certificate (NULL if not present)
} dap_stream_handshake_request_t;

/**
 * @brief Handshake response message structure
 * 
 * Server → Client: Handshake result with session ID and server key
 */
typedef struct dap_stream_handshake_response {
    uint32_t magic;                      ///< DSHP magic number
    uint32_t version;                    ///< Protocol version
    uint8_t status;                      ///< 0 = success, 1 = error
    uint32_t session_id;                 ///< Assigned session ID
    uint32_t session_timeout;            ///< Session timeout in seconds (0 = no timeout)
    uint8_t *bob_pub_key;                ///< Server public key (allocated)
    size_t bob_pub_key_size;             ///< Public key size
    uint8_t *bob_signature;              ///< Optional signature (NULL if not present)
    size_t bob_signature_size;           ///< Signature size (0 if not present)
    uint32_t error_code;                 ///< Error code (only if status != 0)
    char *error_message;                 ///< Error message (NULL if no error)
} dap_stream_handshake_response_t;

/**
 * @brief Session create request message structure
 * 
 * Client → Server: Request to create streaming session (encrypted)
 */
typedef struct dap_stream_session_create_request {
    uint32_t magic;                      ///< DSHP magic number
    uint32_t version;                    ///< Protocol version
    char *channels;                      ///< Active channel IDs (allocated string)
    dap_enc_key_type_t enc_type;        ///< Stream encryption type
    uint32_t enc_key_size;               ///< Encryption key size
    bool enc_headers;                    ///< Encrypt headers flag
} dap_stream_session_create_request_t;

/**
 * @brief Session create response message structure
 * 
 * Server → Client: Session creation result (encrypted)
 */
typedef struct dap_stream_session_create_response {
    uint32_t magic;                      ///< DSHP magic number
    uint32_t version;                    ///< Protocol version
    uint8_t status;                      ///< 0 = success, 1 = error
    uint32_t session_id;                 ///< Final session ID (may differ from handshake)
    uint32_t error_code;                 ///< Error code (only if status != 0)
    char *error_message;                 ///< Error message (NULL if no error)
} dap_stream_session_create_response_t;

/**
 * @brief Initialize DSHP handshake subsystem
 * @return 0 on success, negative error code on failure
 */
int dap_stream_handshake_init(void);

/**
 * @brief Cleanup DSHP handshake subsystem
 */
void dap_stream_handshake_deinit(void);

/**
 * @brief Create and serialize handshake request to binary TLV format
 * @param a_request Request structure to serialize
 * @param a_data_out Output buffer pointer (caller must free with DAP_DELETE)
 * @param a_data_size_out Output buffer size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_handshake_request_create(const dap_stream_handshake_request_t *a_request,
                                         void **a_data_out,
                                         size_t *a_data_size_out);

/**
 * @brief Parse binary TLV data into handshake request structure
 * @param a_data Input TLV data
 * @param a_data_size Size of input data
 * @param a_request_out Output structure (caller must free with dap_stream_handshake_request_free)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_handshake_request_parse(const void *a_data,
                                        size_t a_data_size,
                                        dap_stream_handshake_request_t **a_request_out);

/**
 * @brief Free handshake request structure
 * @param a_request Request to free
 */
void dap_stream_handshake_request_free(dap_stream_handshake_request_t *a_request);

/**
 * @brief Create and serialize handshake response to binary TLV format
 * @param a_response Response structure to serialize
 * @param a_data_out Output buffer pointer (caller must free with DAP_DELETE)
 * @param a_data_size_out Output buffer size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_handshake_response_create(const dap_stream_handshake_response_t *a_response,
                                          void **a_data_out,
                                          size_t *a_data_size_out);

/**
 * @brief Parse binary TLV data into handshake response structure
 * @param a_data Input TLV data
 * @param a_data_size Size of input data
 * @param a_response_out Output structure (caller must free with dap_stream_handshake_response_free)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_handshake_response_parse(const void *a_data,
                                         size_t a_data_size,
                                         dap_stream_handshake_response_t **a_response_out);

/**
 * @brief Free handshake response structure
 * @param a_response Response to free
 */
void dap_stream_handshake_response_free(dap_stream_handshake_response_t *a_response);

/**
 * @brief Create and serialize session create request to binary TLV format
 * @param a_request Request structure to serialize
 * @param a_data_out Output buffer pointer (caller must free with DAP_DELETE)
 * @param a_data_size_out Output buffer size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_session_create_request_create(const dap_stream_session_create_request_t *a_request,
                                               void **a_data_out,
                                               size_t *a_data_size_out);

/**
 * @brief Parse binary TLV data into session create request structure
 * @param a_data Input TLV data
 * @param a_data_size Size of input data
 * @param a_request_out Output structure (caller must free with dap_stream_session_create_request_free)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_session_create_request_parse(const void *a_data,
                                              size_t a_data_size,
                                              dap_stream_session_create_request_t **a_request_out);

/**
 * @brief Free session create request structure
 * @param a_request Request to free
 */
void dap_stream_session_create_request_free(dap_stream_session_create_request_t *a_request);

/**
 * @brief Create and serialize session create response to binary TLV format
 * @param a_response Response structure to serialize
 * @param a_data_out Output buffer pointer (caller must free with DAP_DELETE)
 * @param a_data_size_out Output buffer size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_session_create_response_create(const dap_stream_session_create_response_t *a_response,
                                                void **a_data_out,
                                                size_t *a_data_size_out);

/**
 * @brief Parse binary TLV data into session create response structure
 * @param a_data Input TLV data
 * @param a_data_size Size of input data
 * @param a_response_out Output structure (caller must free with dap_stream_session_create_response_free)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_session_create_response_parse(const void *a_data,
                                               size_t a_data_size,
                                               dap_stream_session_create_response_t **a_response_out);

/**
 * @brief Free session create response structure
 * @param a_response Response to free
 */
void dap_stream_session_create_response_free(dap_stream_session_create_response_t *a_response);

/**
 * @brief Helper: Write TLV field to buffer
 * @param a_buffer Output buffer
 * @param a_offset Current offset in buffer (updated on success)
 * @param a_buffer_size Total buffer size
 * @param a_type TLV type ID
 * @param a_value Value data
 * @param a_value_size Value size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_handshake_tlv_write(uint8_t *a_buffer,
                                     size_t *a_offset,
                                     size_t a_buffer_size,
                                     uint16_t a_type,
                                     const void *a_value,
                                     uint16_t a_value_size);

/**
 * @brief Helper: Read TLV field from buffer
 * @param a_buffer Input buffer
 * @param a_offset Current offset in buffer (updated on success)
 * @param a_buffer_size Total buffer size
 * @param a_type_out Output TLV type ID
 * @param a_value_out Output value pointer (points into buffer, not allocated)
 * @param a_value_size_out Output value size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_handshake_tlv_read(const uint8_t *a_buffer,
                                    size_t *a_offset,
                                    size_t a_buffer_size,
                                    uint16_t *a_type_out,
                                    const void **a_value_out,
                                    uint16_t *a_value_size_out);

/**
 * @brief Helper: Validate magic number and version
 * @param a_magic Magic number to check
 * @param a_version Version to check
 * @return 0 if valid, negative error code if invalid
 */
int dap_stream_handshake_validate_header(uint32_t a_magic, uint32_t a_version);

