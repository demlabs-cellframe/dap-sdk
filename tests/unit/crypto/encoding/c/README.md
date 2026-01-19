# PRECIS Framework C Library

This is a C implementation of the PRECIS Framework (Preparation, Enforcement, and Comparison of Internationalized Strings) as defined in RFC 8264.

## Overview

This library provides C APIs for:
- **String Classes**: IdentifierClass and FreeformClass
- **Profiles**: UsernameCaseMapped, UsernameCasePreserved, OpaqueString, and Nickname
- **Operations**: prepare, enforce, and compare internationalized strings

## Building

```bash
make
```

This will create:
- `lib/libprecis.a` - Static library
- `lib/libprecis.so` - Shared library

## Installation

```bash
make install
```

## Usage

### Basic Example

```c
#include <precis/precis.h>
#include <stdio.h>
#include <string.h>

int main() {
    precis_string_t output;
    precis_error_t error;
    
    memset(&output, 0, sizeof(output));
    memset(&error, 0, sizeof(error));
    
    const char *input = "Guybrush";
    size_t input_len = strlen(input);
    
    int result = precis_username_case_mapped_prepare(
        input, input_len, &output, &error);
    
    if (result == 0) {
        printf("Prepared: %.*s\n", (int)output.len, output.data);
        precis_string_free(&output);
    } else {
        printf("Error: %s\n", precis_error_message(&error));
    }
    
    return 0;
}
```

### Compiling with the library

```bash
gcc -o example example.c -L./lib -lprecis -I.
```

## API Overview

### Error Handling

All functions return `0` on success and `-1` on error. Error details are stored in a `precis_error_t` structure.

```c
precis_error_t error;
if (precis_username_case_mapped_prepare(input, len, &output, &error) != 0) {
    const char *msg = precis_error_message(&error);
    // Handle error
}
```

### String Management

Strings are returned in `precis_string_t` structures. You must free them when done:

```c
precis_string_t str;
// ... use str ...
precis_string_free(&str);
```

### Profile Functions

#### UsernameCaseMapped

```c
int precis_username_case_mapped_prepare(const char *input, size_t input_len,
                                         precis_string_t *output, precis_error_t *error);
int precis_username_case_mapped_enforce(const char *input, size_t input_len,
                                       precis_string_t *output, precis_error_t *error);
int precis_username_case_mapped_compare(const char *s1, size_t len1,
                                       const char *s2, size_t len2,
                                       bool *result, precis_error_t *error);
```

#### UsernameCasePreserved

```c
int precis_username_case_preserved_prepare(...);
int precis_username_case_preserved_enforce(...);
int precis_username_case_preserved_compare(...);
```

#### OpaqueString

```c
int precis_opaque_string_prepare(...);
int precis_opaque_string_enforce(...);
int precis_opaque_string_compare(...);
```

#### Nickname

```c
int precis_nickname_prepare(...);
int precis_nickname_enforce(...);
int precis_nickname_compare(...);
```

## Implementation Status

### Completed
- ✅ Error handling structures and functions
- ✅ Common Unicode property checks (stubs for generated tables)
- ✅ Context rules implementation
- ✅ String class implementation
- ✅ Basic API structure

### TODO
- ⚠️ Unicode table generation (currently stubs)
- ⚠️ Full UTF-8 decoding support
- ⚠️ Unicode normalization (requires ICU or similar)
- ⚠️ Profile implementations (usernames, passwords, nicknames)
- ⚠️ Bidi rules implementation
- ⚠️ Case mapping (full Unicode support)
- ⚠️ Width mapping
- ⚠️ Additional mapping rules

## Notes

1. **Unicode Tables**: The Unicode property tables need to be generated from Unicode data files, similar to how the Rust version does it. Currently, these are stubbed out.

2. **Unicode Normalization**: Full Unicode normalization requires a library like ICU. The current implementation has stubs for this.

3. **UTF-8 Handling**: The current implementation assumes single-byte characters for simplicity. Full UTF-8 decoding needs to be implemented.

4. **Memory Management**: All allocated strings must be freed using `precis_string_free()`.

## Differences from Rust Version

- Manual memory management (no automatic cleanup)
- Error codes instead of Result types
- Function pointers instead of traits
- C-style strings instead of Rust's String type
- No ownership/borrowing semantics

## License

Same as the Rust version: MIT/Apache-2.0
