/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

/**
 * @file json_samples.h
 * @brief Test fixtures for JSON testing
 * @details Common JSON test data and utilities for unit tests
 */

// Sample JSON strings for testing
static const char* JSON_SAMPLE_SIMPLE = "{\"name\":\"test\",\"value\":123}";

static const char* JSON_SAMPLE_NESTED = "{"
    "\"user\":{"
        "\"id\":1,"
        "\"name\":\"John\","
        "\"email\":\"john@example.com\""
    "},"
    "\"status\":\"active\""
"}";

static const char* JSON_SAMPLE_ARRAY = "{"
    "\"numbers\":[1,2,3,4,5],"
    "\"strings\":[\"a\",\"b\",\"c\"]"
"}";

static const char* JSON_SAMPLE_COMPLEX = "{"
    "\"crypto\":{"
        "\"algorithm\":\"dilithium\","
        "\"key_size\":2048,"
        "\"post_quantum\":true"
    "},"
    "\"network\":{"
        "\"nodes\":["
            "{\"id\":1,\"address\":\"127.0.0.1\",\"port\":8080},"
            "{\"id\":2,\"address\":\"127.0.0.1\",\"port\":8081}"
        "],"
        "\"protocol\":\"DAP\""
    "}"
"}";

// Invalid JSON samples for error testing
static const char* JSON_SAMPLE_INVALID_SYNTAX = "{\"name\":\"test\",\"value\":}";
static const char* JSON_SAMPLE_INVALID_EMPTY = "";
static const char* JSON_SAMPLE_INVALID_NULL = NULL;

// Crypto test data samples
static const char* CRYPTO_SAMPLE_HASH_INPUT = "DAP SDK test data for hashing";
static const char* CRYPTO_SAMPLE_SIGN_MESSAGE = "Message to be signed with post-quantum algorithms";

