# Conversion Notes: Rust to C

This document describes the conversion of the PRECIS Rust library to C.

## Major Changes

### 1. Error Handling
- **Rust**: Uses `Result<T, E>` enum with pattern matching
- **C**: Uses return codes (0 = success, -1 = error) with error structures
- Error details stored in `precis_error_t` structure passed by pointer

### 2. Memory Management
- **Rust**: Automatic memory management with ownership/borrowing
- **C**: Manual memory management - all allocated strings must be freed
- `precis_string_t` structure tracks ownership

### 3. Traits to Function Pointers
- **Rust**: Traits (`StringClass`, `Profile`, `Rules`) with implementations
- **C**: Function pointers in structures or separate functions
- Static functions for fast invocation (similar to Rust's `PrecisFastInvocation`)

### 4. Enums
- **Rust**: Enums with associated data
- **C**: Plain enums or unions for tagged unions
- Error types use unions to store different error data

### 5. String Handling
- **Rust**: `String`, `&str`, `Cow<str>` with UTF-8 guarantees
- **C**: `char*` with length, UTF-8 handling must be explicit
- Current implementation assumes single-byte for simplicity (needs full UTF-8 support)

### 6. Generic Types
- **Rust**: Generics and associated types
- **C**: Void pointers or specific types, no generics

## Implementation Status

### ✅ Completed
- Error handling structures and functions
- Common Unicode property check functions (stubs for tables)
- Context rules implementation
- String class implementation
- Profile API structure
- Bidi rules structure
- Basic profile implementations (stubs)

### ⚠️ Needs Work
1. **Unicode Tables**: Currently empty stubs. Need to generate from Unicode data:
   - Letter/Digit categories
   - Exception tables
   - Backward compatible tables
   - Bidi class table
   - Script tables (Greek, Hebrew, etc.)
   - Joining type tables

2. **UTF-8 Decoding**: Current implementation assumes single-byte characters.
   Need proper UTF-8 decoding for:
   - Multi-byte character iteration
   - Proper codepoint extraction
   - Character position tracking

3. **Unicode Normalization**: Requires ICU or similar library:
   - NFC normalization
   - NFKC normalization
   - Compatibility decomposition checks

4. **Case Mapping**: Currently only ASCII. Need full Unicode:
   - Unicode-aware toLowerCase
   - Proper case folding

5. **Width Mapping**: Not implemented. Needs generated mapping table.

6. **Additional Mapping Rules**: 
   - Space normalization (Nickname profile)
   - Non-ASCII space mapping (OpaqueString profile)

## Build System

- **Rust**: Cargo with build.rs for code generation
- **C**: Makefile (could be enhanced with code generation step)

## Code Generation

The Rust version generates code at build time from Unicode data files. The C version needs:

1. A script to parse Unicode data files (similar to `precis-tools`)
2. Generate C source files with table definitions
3. Integrate into build process

## Dependencies

### Rust Version
- `unicode-normalization` - Unicode normalization
- `ucd-parse` - Unicode data parsing
- `lazy_static` - Static initialization

### C Version (Current)
- None (but needs ICU for full functionality)

### C Version (Recommended)
- ICU (International Components for Unicode) for:
  - Unicode normalization
  - Case mapping
  - UTF-8 handling

## API Differences

### Rust Example
```rust
use precis_profiles::UsernameCaseMapped;
let result = UsernameCaseMapped::prepare("Guybrush");
match result {
    Ok(s) => println!("{}", s),
    Err(e) => println!("Error: {}", e),
}
```

### C Example
```c
precis_string_t output;
precis_error_t error;
if (precis_username_case_mapped_prepare("Guybrush", 8, &output, &error) == 0) {
    printf("%.*s\n", (int)output.len, output.data);
    precis_string_free(&output);
} else {
    printf("Error: %s\n", precis_error_message(&error));
}
```

## Testing

The Rust version has comprehensive tests. The C version needs:
- Unit tests for each module
- Integration tests for profiles
- Test data from Unicode test files

## Performance Considerations

- C version may be faster for simple operations (no allocations)
- Rust version has better safety guarantees
- Both need proper Unicode table lookups (binary search)
- Memory management overhead differs (manual vs automatic)

## Future Improvements

1. Add ICU integration for full Unicode support
2. Generate Unicode tables from data files
3. Add comprehensive test suite
4. Add UTF-8 iterator utilities
5. Optimize table lookups (maybe perfect hashing)
6. Add documentation generation (Doxygen)
