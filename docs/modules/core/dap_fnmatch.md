# DAP Filename Matching

## Overview

The `dap_fnmatch` module provides functions for pattern matching similar to POSIX `fnmatch`. It is used for filename patterns, configuration masks, and filters in the DAP SDK.

## Purpose

Pattern matching is critical for:
- **File systems**: Mask‑based file search
- **Configuration filters**: Applying rules to groups
- **Input validation**: Format checks
- **Routing**: Filtering network requests
- **Log processing**: Pattern‑based filtering

## Features

### 🔍 **Pattern matching**
- Standard patterns (`*`, `?`, `[...]`)
- GNU extensions (optional)
- Case‑insensitive comparison

### ⚙️ **Configurable flags**
- Control matching behavior
- Path‑aware handling
- Escaping of special characters

### 🚀 **High performance**
- Optimized algorithms
- Minimal overhead
- Cross‑platform support

## Matching control flags

### Core flags

```c
#define FNM_PATHNAME    (1 << 0) /* No wildcard can ever match `/'.  */
/* Wildcards never match '/' */

#define FNM_NOESCAPE    (1 << 1) /* Backslashes don't quote special chars.  */
/* Backslashes do not escape special characters */

#define FNM_PERIOD      (1 << 2) /* Leading `.' is matched only explicitly.  */
/* Leading '.' must be matched explicitly */
```

### Extended flags (GNU)

```c
#define FNM_FILE_NAME   FNM_PATHNAME   /* Preferred GNU name.  */
/* Preferred GNU name for FNM_PATHNAME */

#define FNM_LEADING_DIR (1 << 3)   /* Ignore `/...' after a match.  */
/* Ignore '/...' after a match */

#define FNM_CASEFOLD    (1 << 4)   /* Compare without regard to case.  */
/* Case‑insensitive compare */

#define FNM_EXTMATCH    (1 << 5)   /* Use ksh-like extended matching. */
/* Use ksh‑style extended matching */
```

## API functions

### Main matching function

```c
// Match a string against a pattern
extern int dap_fnmatch(const char *pattern, const char *string, int flags);
```

**Parameters:**
- `pattern` - pattern to match
- `string` - string to test
- `flags` - control flags

**Returns:**
- `0` - matched
- `FNM_NOMATCH` - no match

## Pattern syntax

### Basic wildcards

| Symbol | Description | Example | Matches |
|--------|-------------|---------|---------|
| `*` | Any sequence of characters | `*.txt` | `file.txt`, `data.txt` |
| `?` | Any single character | `file?.txt` | `file1.txt`, `fileA.txt` |
| `[abc]` | One char from a set | `file[123].txt` | `file1.txt`, `file2.txt`, `file3.txt` |
| `[a-z]` | Character range | `file[a-z].txt` | `filea.txt`, `fileb.txt`, ... |
| `[!abc]` | Any char except set | `file[!0-9].txt` | `filea.txt`, `fileB.txt` |

### Extended features (FNM_EXTMATCH)

| Construct | Description | Example | Matches |
|-----------|-------------|---------|---------|
| `?(pattern)` | Zero or one occurrence | `file?(1).txt` | `file.txt`, `file1.txt` |
| `*(pattern)` | Zero or more occurrences | `*(file).txt` | `file.txt`, `filefile.txt` |
| `+(pattern)` | One or more occurrences | `+(file).txt` | `file.txt`, `filefile.txt` |
| `@(pattern)` | Exactly one occurrence | `@(file).txt` | `file.txt` |
| `!(pattern)` | Anything except pattern | `!(file).txt` | `data.txt`, `info.txt` |

## Usage

### Basic filename matching

```c
#include "dap_fnmatch.h"

// Check file extension
int check_file_extension(const char *filename) {
    if (dap_fnmatch("*.txt", filename, 0) == 0) {
        printf("Text file: %s\n", filename);
        return 1;
    }
    return 0;
}

// Check with path awareness
int check_path_pattern(const char *filepath) {
    // '*' does not match '/' with FNM_PATHNAME
    if (dap_fnmatch("src/*.c", filepath, FNM_PATHNAME) == 0) {
        printf("C file in src: %s\n", filepath);
        return 1;
    }
    return 0;
}
```

### Working with configuration patterns

```c
// Validate configuration filenames
bool is_valid_config_file(const char *filename) {
    // Allowed patterns: config*.json, settings*.yaml
    const char *patterns[] = {
        "config*.json",
        "settings*.yaml",
        NULL
    };

    for (int i = 0; patterns[i]; i++) {
        if (dap_fnmatch(patterns[i], filename, FNM_CASEFOLD) == 0) {
            return true;
        }
    }
    return false;
}

// Apply pattern to a list of files
void process_files_by_pattern(char **filenames, const char *pattern) {
    for (int i = 0; filenames[i]; i++) {
        if (dap_fnmatch(pattern, filenames[i], 0) == 0) {
            process_file(filenames[i]);
        }
    }
}
```

### Advanced matching

```c
// Using extended patterns (if supported)
int advanced_pattern_matching(const char *filename) {
    // Matches: test.c, test.h, test.cpp, test.hpp
    if (dap_fnmatch("test.@(c|cpp|h|hpp)", filename, FNM_EXTMATCH) == 0) {
        printf("Extended pattern match: %s\n", filename);
        return 1;
    }

    // Matches: file.txt, file1.txt, file2.txt, but NOT file.txt.bak
    if (dap_fnmatch("file+([0-9]).txt", filename, FNM_EXTMATCH) == 0) {
        printf("Digit pattern match: %s\n", filename);
        return 1;
    }

    return 0;
}
```

### Working with paths

```c
// Filter paths
bool should_process_path(const char *path) {
    // Process all .c files in src/ but not in subdirectories
    if (dap_fnmatch("src/*.c", path, FNM_PATHNAME) == 0) {
        return true;
    }

    // Process all .h files anywhere
    if (dap_fnmatch("*.h", path, 0) == 0) {
        return true;
    }

    return false;
}

// Ignore hidden files
bool is_hidden_file(const char *filename) {
    // Leading '.' must be explicit
    if (dap_fnmatch(".*", filename, FNM_PERIOD) == 0) {
        return true;
    }
    return false;
}
```

### Case‑insensitive matching

```c
// Case‑insensitive search
bool case_insensitive_match(const char *pattern, const char *string) {
    return dap_fnmatch(pattern, string, FNM_CASEFOLD) == 0;
}

// Example
void find_log_files(char **files) {
    for (int i = 0; files[i]; i++) {
        // Find all log files regardless of case
        if (case_insensitive_match("*log*", files[i])) {
            printf("Found log file: %s\n", files[i]);
        }
    }
}
```

## Implementation details

### Matching algorithm

This module uses an optimized matching algorithm that:
- Is **linear in complexity** for most patterns
- **Uses finite automata** for complex patterns
- Supports **backtracking** for quantified expressions

### Performance

| Pattern type | Complexity | Examples |
|--------------|-----------|----------|
| Simple | O(n) | `*.txt`, `file?` |
| Ranges | O(n) | `[a-z]*`, `[0-9]+` |
| Complex | O(n²) | Nested quantifiers |
| Extended | O(n²) | `*(a*b*c)` |

### Optimizations

1. **Early termination**: Exit on first mismatch
2. **Caching**: Reuse compiled patterns
3. **SIMD instructions**: Vectorization for simple comparisons

## Usage in DAP SDK

### Configuration file filtering

```c
// Load configuration files by pattern
void load_config_files(const char *config_dir) {
    DIR *dir = opendir(config_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Load only .json configuration files
        if (dap_fnmatch("*.json", entry->d_name, 0) == 0) {
            char filepath[PATH_MAX];
            snprintf(filepath, sizeof(filepath), "%s/%s",
                    config_dir, entry->d_name);
            load_json_config(filepath);
        }
    }
    closedir(dir);
}
```

### Network request validation

```c
// Filter API endpoints
bool is_valid_api_endpoint(const char *endpoint) {
    // Allowed endpoints: /api/v1/*, /api/v2/*
    const char *valid_patterns[] = {
        "/api/v1/*",
        "/api/v2/*",
        NULL
    };

    for (int i = 0; valid_patterns[i]; i++) {
        if (dap_fnmatch(valid_patterns[i], endpoint, FNM_PATHNAME) == 0) {
            return true;
        }
    }
    return false;
}
```

### Log processing

```c
// Filter log messages
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

log_level_t parse_log_level(const char *log_line) {
    if (dap_fnmatch("*DEBUG*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_DEBUG;
    }
    if (dap_fnmatch("*INFO*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_INFO;
    }
    if (dap_fnmatch("*WARN*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_WARN;
    }
    if (dap_fnmatch("*ERROR*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_ERROR;
    }

    return LOG_LEVEL_INFO; // Default
}
```

### Working with databases

```c
// Фильтрация записей базы данных
bool matches_record_pattern(const char *record_key, const char *pattern) {
    // Поддержка шаблонов в ключах записей
    return dap_fnmatch(pattern, record_key, 0) == 0;
}

// Example usage
void query_database(const char *pattern) {
    // Find all records with keys like "user_*_profile"
    database_foreach_record(record) {
        if (matches_record_pattern(record->key, pattern)) {
            process_record(record);
        }
    }
}
```

## Related modules

- `dap_strfuncs.h` - String utilities
- `dap_common.h` - Common definitions
- `dap_fnmatch_loop.h` - Iterative matching

## Security notes

### Input validation

```c
// Always validate input parameters
bool safe_pattern_match(const char *pattern, const char *string, int flags) {
    if (!pattern || !string) {
        return false;
    }

    // Limit pattern length to prevent DoS
    if (strlen(pattern) > MAX_PATTERN_LENGTH) {
        log_it(L_WARNING, "Pattern too long: %zu", strlen(pattern));
        return false;
    }

    return dap_fnmatch(pattern, string, flags) == 0;
}
```

### Protection against ReDoS attacks

```c
// Avoid dangerous patterns
const char *dangerous_patterns[] = {
    "(a+)+b",    // Catastrophic backtracking
    "(a*)*",     // Exponential complexity
    "(a|a)*",    // Inefficient alternations
    NULL
};

bool is_safe_pattern(const char *pattern) {
    for (int i = 0; dangerous_patterns[i]; i++) {
        if (strstr(pattern, dangerous_patterns[i])) {
            return false;
        }
    }
    return true;
}
```

### Special character handling

```c
// Escape user input
char *escape_pattern(const char *user_input) {
    // Replace special characters with escaped
    return dap_str_replace_char(user_input, '*', '\\*');
}
```

## Debugging

### Matching diagnostics

```c
// Function for matching diagnostics
void debug_pattern_match(const char *pattern, const char *string, int flags) {
    int result = dap_fnmatch(pattern, string, flags);
    printf("Pattern matching debug:\n");
    printf("  Pattern: '%s'\n", pattern);
    printf("  String:  '%s'\n", string);
    printf("  Flags:   0x%x\n", flags);
    printf("  Result:  %s (%d)\n",
           result == 0 ? "MATCH" : "NO MATCH", result);

    // Show active flags
    if (flags & FNM_PATHNAME) printf("  - FNM_PATHNAME\n");
    if (flags & FNM_NOESCAPE) printf("  - FNM_NOESCAPE\n");
    if (flags & FNM_PERIOD) printf("  - FNM_PERIOD\n");
    if (flags & FNM_CASEFOLD) printf("  - FNM_CASEFOLD\n");
    if (flags & FNM_EXTMATCH) printf("  - FNM_EXTMATCH\n");
}
```

### Pattern testing

```c
// Comprehensive testing
void test_fnmatch_patterns() {
    struct {
        const char *pattern;
        const char *test_string;
        int flags;
        bool expected_match;
    } test_cases[] = {
        {"*.txt", "file.txt", 0, true},
        {"*.txt", "file.TXT", FNM_CASEFOLD, true},
        {"src/*.c", "src/main.c", FNM_PATHNAME, true},
        {"src/*.c", "src/test/main.c", FNM_PATHNAME, false},
        {"file[0-9].txt", "file5.txt", 0, true},
        {"file[!0-9].txt", "fileA.txt", 0, true},
        {NULL, NULL, 0, false}
    };

    for (int i = 0; test_cases[i].pattern; i++) {
        int result = dap_fnmatch(test_cases[i].pattern,
                                test_cases[i].test_string,
                                test_cases[i].flags);
        bool actual_match = (result == 0);

        if (actual_match != test_cases[i].expected_match) {
            printf("TEST FAILED: %s %c %s (flags=0x%x)\n",
                   test_cases[i].pattern,
                   test_cases[i].expected_match ? '=' : '!',
                   test_cases[i].test_string,
                   test_cases[i].flags);
        } else {
            printf("TEST PASSED: %s %c %s\n",
                   test_cases[i].pattern,
                   test_cases[i].expected_match ? '=' : '!',
                   test_cases[i].test_string);
        }
    }
}
```

### Performance profiling

```c
// Measure matching performance
void benchmark_fnmatch(const char *pattern, char **test_strings, int num_strings) {
    clock_t start = clock();

    int matches = 0;
    for (int i = 0; i < num_strings; i++) {
        if (dap_fnmatch(pattern, test_strings[i], 0) == 0) {
            matches++;
        }
    }

    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;

    printf("Pattern '%s': %d matches in %.3f seconds (%.1f matches/sec)\n",
           pattern, matches, time_spent, matches / time_spent);
}
```

This module provides powerful and efficient pattern‑matching capabilities in the DAP SDK, enabling flexible and high‑performance work with file systems, configurations, and data filtering.

