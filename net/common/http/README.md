# Common HTTP Module

This module provides common HTTP header functionality shared between client and server components.

## Location

`/dap-sdk/net/common/http/`

## Purpose

Previously, HTTP header functionality was duplicated between client and server modules. This common module eliminates duplication and provides a clean architecture where both client and server can share the same core HTTP header management code.

## Components

### Headers
- `include/dap_http_header.h` - Common HTTP header structure and function declarations

### Source
- `src/dap_http_header.c` - Implementation of common functions

## API Functions

```c
// Header management
dap_http_header_t *dap_http_header_add(dap_http_header_t **a_top, const char *a_name, const char *a_value);
dap_http_header_t *dap_http_header_find(dap_http_header_t *a_top, const char *a_name);
void dap_http_header_remove(dap_http_header_t **a_top, dap_http_header_t *a_hdr);
dap_http_header_t *dap_http_headers_dup(dap_http_header_t *a_top);

// Universal header parser
int dap_http_header_parse_line(const char *a_line, size_t a_line_len, 
                               char *a_name_out, size_t a_name_max,
                               char *a_value_out, size_t a_value_max);

// Debug output
void dap_http_header_print(dap_http_header_t *a_headers);
```

## Usage

### In Client Code
```c
#include "../common/http/include/dap_http_header.h"
```

### In Server Code
The server maintains backward compatibility through its existing header file which now includes the common module.

## Architecture Benefits

1. **No Code Duplication** - Single implementation for header management
2. **Clean Dependencies** - Client doesn't depend on server headers
3. **Easier Maintenance** - Changes to header functionality only need to be made in one place
4. **Better Testing** - Common functionality can be tested independently 