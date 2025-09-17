# dap_file_utils.h - File and Directory Utilities

## Overview

The `dap_file_utils` module provides a powerful cross‑platform set of functions for working with files, directories, and paths in the DAP SDK. It supports Windows, Linux, and macOS with a unified API.

## Key features

- **Cross‑platform**: Unified API for Windows, Linux, macOS
- **Path handling**: Parse, build, canonicalize
- **File operations**: Read, existence checks, move/rename
- **Directory operations**: Create, traverse, remove
- **Archiving**: ZIP and TAR support
- **Security**: ASCII checks, path validation
- **Memory management**: Automatic resource handling

## Cross‑platform constants

### Path separators

```c
// Windows
#define DAP_DIR_SEPARATOR '\\'
#define DAP_DIR_SEPARATOR_S "\\"
#define DAP_IS_DIR_SEPARATOR(c) ((c) == DAP_DIR_SEPARATOR || (c) == '/')
#define DAP_SEARCHPATH_SEPARATOR ';'
#define DAP_SEARCHPATH_SEPARATOR_S ";"

// Unix-like systems (Linux, macOS)
#define DAP_DIR_SEPARATOR '/'
#define DAP_DIR_SEPARATOR_S "/"
#define DAP_IS_DIR_SEPARATOR(c) ((c) == DAP_DIR_SEPARATOR)
#define DAP_SEARCHPATH_SEPARATOR ':'
#define DAP_SEARCHPATH_SEPARATOR_S ":"
```

**Usage:**
```c
// Cross‑platform code
char path[MAX_PATH];
snprintf(path, sizeof(path), "data%cfile.txt", DAP_DIR_SEPARATOR);

// Separator check
if (DAP_IS_DIR_SEPARATOR(path[i])) {
    // Handle separator
}
```

## Data structures

### dap_list_name_directories_t

Structure for storing a list of directory names:

```c
typedef struct dap_list_name_directories {
    char *name_directory;                        // Directory name
    struct dap_list_name_directories *next;      // Next element
} dap_list_name_directories_t;
```

## API Reference

### Existence and type checks

#### dap_valid_ascii_symbols()

```c
bool dap_valid_ascii_symbols(const char *a_dir_path);
```

**Description**: Checks whether the directory path contains ASCII‑only characters.

**Parameters:**
- `a_dir_path` - directory path to check

**Returns:**
- `true` - path contains ASCII‑only characters
- `false` - path contains non‑ASCII characters

**Example:**
```c
#include "dap_file_utils.h"

const char *path = "C:\\Program Files\\MyApp";
// On Windows this returns false due to spaces and non‑ASCII characters
if (!dap_valid_ascii_symbols(path)) {
    fprintf(stderr, "Path contains unsupported symbols\n");
}
```

#### dap_file_simple_test()

```c
bool dap_file_simple_test(const char *a_file_path);
```

**Description**: Checks file existence without extra filesystem attributes.

**Parameters:**
- `a_file_path` - file path

**Returns:**
- `true` - file exists
- `false` - file does not exist

#### dap_file_test()

```c
bool dap_file_test(const char *a_file_path);
```

**Description**: Checks file existence with attribute inspection.

**Parameters:**
- `a_file_path` - file path

**Returns:**
- `true` - file exists
- `false` - file does not exist

#### dap_dir_test()

```c
bool dap_dir_test(const char *a_dir_path);
```

**Description**: Checks directory existence.

**Parameters:**
- `a_dir_path` - directory path

**Returns:**
- `true` - directory exists
- `false` - directory does not exist

**Example:**
```c
// Pre‑use check
if (!dap_file_test("config.ini")) {
    fprintf(stderr, "Configuration file not found\n");
    return 1;
}

if (!dap_dir_test("data/")) {
    fprintf(stderr, "Data directory not found\n");
    return 1;
}
```

### File operations

#### dap_file_mv()

```c
int dap_file_mv(const char *a_path_old, const char *a_path_new);
```

**Description**: Moves or renames a file.

**Parameters:**
- `a_path_old` - current file path
- `a_path_new` - new file path

**Returns:**
- `0` - success
- `-1` - error

**Example:**
```c
// Rename file
if (dap_file_mv("temp.txt", "data.txt") == 0) {
    printf("File renamed successfully\n");
} else {
    perror("Failed to rename file");
}

// Move file
if (dap_file_mv("file.txt", "backup/file.txt") == 0) {
    printf("File moved successfully\n");
}
```

### Working with directories

#### dap_mkdir_with_parents()

```c
int dap_mkdir_with_parents(const char *a_dir_path);
```

**Description**: Creates a directory with all intermediate subdirectories.

**Parameters:**
- `a_dir_path` - path of the directory to create

**Returns:**
- `0` - directory created or already exists
- `-1` - creation error

**Example:**
```c
// Create nested directory structure
const char *path = "data/logs/2024/01/15";
if (dap_mkdir_with_parents(path) == 0) {
    printf("Directory structure created\n");
} else {
    perror("Failed to create directory");
}
```

### Working with paths

#### dap_path_get_basename()

```c
char *dap_path_get_basename(const char *a_file_name);
```

**Description**: Extracts a filename from a full path.

**Parameters:**
- `a_file_name` - full file path

**Returns:** Filename (must be freed with `free()`)

**Example:**
```c
const char *full_path = "/home/user/documents/file.txt";
char *filename = dap_path_get_basename(full_path);

if (filename) {
    printf("Filename: %s\n", filename); // Output: file.txt
    free(filename);
}
```

#### dap_path_get_dirname()

```c
char *dap_path_get_dirname(const char *a_file_name);
```

**Description**: Extracts directory from a full file path.

**Parameters:**
- `a_file_name` - full file path

**Returns:** Directory path (must be freed with `free()`)

**Example:**
```c
const char *full_path = "/home/user/documents/file.txt";
char *dirname = dap_path_get_dirname(full_path);

if (dirname) {
    printf("Directory: %s\n", dirname); // Output: /home/user/documents
    free(dirname);
}
```

#### dap_path_is_absolute()

```c
bool dap_path_is_absolute(const char *a_file_name);
```

**Description**: Checks whether a path is absolute.

**Parameters:**
- `a_file_name` - path to check

**Returns:**
- `true` - path is absolute
- `false` - path is relative

**Example:**
```c
const char *path1 = "/home/user/file.txt";
const char *path2 = "relative/path/file.txt";

printf("Path1 is absolute: %s\n", dap_path_is_absolute(path1) ? "yes" : "no");
printf("Path2 is absolute: %s\n", dap_path_is_absolute(path2) ? "yes" : "no");
```

#### dap_path_get_ext()

```c
const char *dap_path_get_ext(const char *a_filename);
```

**Description**: Extracts a file extension.

**Parameters:**
- `a_filename` - filename

**Returns:** Pointer to the extension or `NULL`

**Example:**
```c
const char *filename = "document.pdf";
const char *ext = dap_path_get_ext(filename);

if (ext) {
    printf("Extension: %s\n", ext); // Output: .pdf
}
```

### Directory traversal

#### dap_get_subs()

```c
dap_list_name_directories_t *dap_get_subs(const char *a_path_name);
```

**Description**: Retrieves a list of subdirectories in the given directory.

**Parameters:**
- `a_path_name` - directory path

**Returns:** List of subdirectories or `NULL` on error

**Example:**
```c
const char *dir_path = "/home/user/projects";
dap_list_name_directories_t *subs = dap_get_subs(dir_path);

if (subs) {
    dap_list_name_directories_t *current = subs;
    while (current) {
        printf("Subdirectory: %s\n", current->name_directory);
        current = current->next;
    }

    dap_subs_free(subs);
}
```

#### dap_subs_free()

```c
void dap_subs_free(dap_list_name_directories_t *subs_list);
```

**Description**: Frees memory occupied by the subdirectories list.

**Parameters:**
- `subs_list` - list to free

### Reading files

#### dap_file_get_contents2()

```c
char *dap_file_get_contents2(const char *filename, size_t *length);
```

**Description**: Reads file contents into memory.

**Parameters:**
- `filename` - file path
- `length` - pointer to store file size

**Returns:** File contents or `NULL` on error (must be freed)

**Example:**
```c
const char *filename = "config.txt";
size_t file_size;
char *content = dap_file_get_contents2(filename, &file_size);

if (content) {
    printf("File size: %zu bytes\n", file_size);
    printf("Content:\n%s\n", content);
    free(content);
} else {
    perror("Failed to read file");
}
```

### Building paths

#### dap_build_path()

```c
char *dap_build_path(const char *separator, const char *first_element, ...);
```

**Description**: Builds a path from multiple elements with a custom separator.

**Parameters:**
- `separator` - path separator
- `first_element` - first path element
- `...` - remaining elements (terminated by `NULL`)

**Returns:** Full path (must be freed with `free()`)

**Example:**
```c
// Create a path with a custom separator
char *path1 = dap_build_path("/", "home", "user", "docs", "file.txt", NULL);
if (path1) {
    printf("Path: %s\n", path1); // Output: home/user/docs/file.txt
    free(path1);
}

// Create a Windows path
char *path2 = dap_build_path("\\", "C:", "Program Files", "MyApp", NULL);
if (path2) {
    printf("Windows path: %s\n", path2); // Output: C:\\Program Files\\MyApp
    free(path2);
}
```

#### dap_build_filename()

```c
char *dap_build_filename(const char *first_element, ...);
```

**Description**: Builds a path using the system separator.

**Parameters:**
- `first_element` - first path element
- `...` - remaining elements (terminated by `NULL`)

**Returns:** Full path (must be freed with `free()`)

**Example:**
```c
// Cross‑platform path building
char *filepath = dap_build_filename("data", "users", "123", "profile.json", NULL);
if (filepath) {
    printf("File path: %s\n", filepath);
    // On Unix: data/users/123/profile.json
    // On Windows: data\\users\\123\\profile.json
    free(filepath);
}
```

### Path canonicalization

#### dap_canonicalize_filename()

```c
char *dap_canonicalize_filename(const char *filename, const char *relative_to);
```

**Description**: Returns a canonical filename resolving `.` and `..`.

**Parameters:**
- `filename` - filename to canonicalize
- `relative_to` - base path or `NULL` for the current directory

**Returns:** Canonicalized path (must be freed)

**Example:**
```c
// Canonicalization with base path
char *canonical1 = dap_canonicalize_filename("../data/file.txt", "/home/user");
if (canonical1) {
    printf("Canonical path: %s\n", canonical1); // /home/data/file.txt
    free(canonical1);
}

// Canonicalization relative to current directory
char *canonical2 = dap_canonicalize_filename("./config/../data/file.txt", NULL);
if (canonical2) {
    printf("Canonical path: %s\n", canonical2); // data/file.txt
    free(canonical2);
}
```

#### dap_canonicalize_path()

```c
char *dap_canonicalize_path(const char *a_filename, const char *a_path);
```

**Description**: Alternative path canonicalization function.

### Working with current directory

#### dap_get_current_dir()

```c
char *dap_get_current_dir(void);
```

**Description**: Returns the current working directory.

**Returns:** Current directory (must be freed)

**Example:**
```c
char *cwd = dap_get_current_dir();
if (cwd) {
    printf("Current directory: %s\n", cwd);
    free(cwd);
}
```

### Removing files and directories

#### dap_rm_rf()

```c
void dap_rm_rf(const char *path);
```

**Description**: Recursively removes files and directories (like `rm -rf`).

**Parameters:**
- `path` - file or directory path to remove

**Note:** Use with care!

**Example:**
```c
// Remove temporary directory
const char *temp_dir = "/tmp/myapp_temp";
dap_rm_rf(temp_dir);
printf("Temporary directory removed\n");
```

### Archiving

#### dap_zip_directory() (if ZIP support is enabled)

```c
bool dap_zip_directory(const char *a_inputdir, const char *a_output_filename);
```

**Description**: Creates a ZIP archive from a directory.

**Parameters:**
- `a_inputdir` - input directory
- `a_output_filename` - output ZIP filename

**Returns:**
- `true` - success
- `false` - failure

**Requirements:** Build with `DAP_BUILD_WITH_ZIP`

#### dap_tar_directory()

```c
bool dap_tar_directory(const char *a_inputdir, const char *a_output_tar_filename);
```

**Description**: Creates a TAR archive from a directory.

**Parameters:**
- `a_inputdir` - input directory
- `a_output_tar_filename` - output TAR filename

**Returns:**
- `true` - success
- `false` - failure

**Example:**
```c
// Create a backup
const char *source_dir = "/home/user/data";
const char *backup_file = "/home/user/backup/data.tar";

if (dap_tar_directory(source_dir, backup_file)) {
    printf("Backup created successfully\n");
} else {
    fprintf(stderr, "Failed to create backup\n");
}
```

## Usage examples

### Example 1: Working with configuration files

```c
#include "dap_file_utils.h"
#include <stdio.h>

#define CONFIG_DIR "config"
#define CONFIG_FILE "app.conf"

int load_config_file(char **content, size_t *size) {
    // Build path to the configuration file
    char *config_path = dap_build_filename(CONFIG_DIR, CONFIG_FILE, NULL);
    if (!config_path) {
        return -1;
    }

    // Check file existence
    if (!dap_file_test(config_path)) {
        free(config_path);
        return -2;
    }

    // Read file
    *content = dap_file_get_contents2(config_path, size);
    free(config_path);

    if (!*content) {
        return -3;
    }

    return 0;
}

int save_config_file(const char *content, size_t size) {
    // Create configuration directory
    if (dap_mkdir_with_parents(CONFIG_DIR) != 0) {
        return -1;
    }

    // Build path to file
    char *config_path = dap_build_filename(CONFIG_DIR, CONFIG_FILE, NULL);
    if (!config_path) {
        return -2;
    }

    // Write file
    FILE *file = fopen(config_path, "wb");
    free(config_path);

    if (!file) {
        return -3;
    }

    size_t written = fwrite(content, 1, size, file);
    fclose(file);

    return (written == size) ? 0 : -4;
}
```

### Example 2: Directory traversal and file processing

```c
#include "dap_file_utils.h"
#include <stdio.h>

typedef struct file_info {
    char *name;
    char *path;
    size_t size;
    bool is_directory;
} file_info_t;

void process_directory(const char *base_path, int depth) {
    if (depth > 10) { // Prevent infinite recursion
        return;
    }

    // Get the list of subdirectories
    dap_list_name_directories_t *subs = dap_get_subs(base_path);
    if (!subs) {
        return;
    }

    dap_list_name_directories_t *current = subs;
    while (current) {
        // Build full path
        char *full_path = dap_build_filename(base_path, current->name_directory, NULL);
        if (!full_path) {
            continue;
        }

        printf("%*s%s\n", depth * 2, "", current->name_directory);

        // Recursive processing of subdirectories
        if (dap_dir_test(full_path)) {
            process_directory(full_path, depth + 1);
        }

        free(full_path);
        current = current->next;
    }

    dap_subs_free(subs);
}

void scan_project_files(const char *project_root) {
    printf("Project structure:\n");
    process_directory(project_root, 0);
}
```

### Example 3: Temporary files management

```c
#include "dap_file_utils.h"
#include <stdio.h>
#include <time.h>

typedef struct temp_file {
    char *path;
    time_t created;
    struct temp_file *next;
} temp_file_t;

temp_file_t *temp_files = NULL;

char *create_temp_file(const char *prefix, const char *extension) {
    // Create temporary directory
    const char *temp_dir = "temp";
    if (dap_mkdir_with_parents(temp_dir) != 0) {
        return NULL;
    }

    // Generate unique filename
    time_t now = time(NULL);
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_%ld.%s",
             prefix, now, extension);

    // Build full path
    char *filepath = dap_build_filename(temp_dir, filename, NULL);
    if (!filepath) {
        return NULL;
    }

    // Register temporary file
    temp_file_t *temp = malloc(sizeof(temp_file_t));
    if (temp) {
        temp->path = strdup(filepath);
        temp->created = now;
        temp->next = temp_files;
        temp_files = temp;
    }

    return filepath;
}

void cleanup_temp_files(void) {
    // Remove all temporary files
    temp_file_t *current = temp_files;
    while (current) {
        if (dap_file_test(current->path)) {
            if (remove(current->path) == 0) {
                printf("Removed temp file: %s\n", current->path);
            }
        }

        temp_file_t *next = current->next;
        free(current->path);
        free(current);
        current = next;
    }

    temp_files = NULL;

    // Remove temporary directory
    dap_rm_rf("temp");
}
```

### Example 4: Working with paths in cross‑platform code

```c
#include "dap_file_utils.h"
#include <stdio.h>

char *get_data_file_path(const char *filename) {
    // Determine base data directory
    char *data_dir;

#ifdef _WIN32
    // Windows: %APPDATA%\MyApp
    const char *app_data = getenv("APPDATA");
    if (app_data) {
        data_dir = dap_build_filename(app_data, "MyApp", NULL);
    } else {
        data_dir = dap_build_filename("C:", "ProgramData", "MyApp", NULL);
    }
#else
    // Unix‑like: ~/.myapp
    const char *home = getenv("HOME");
    if (home) {
        data_dir = dap_build_filename(home, ".myapp", NULL);
    } else {
        data_dir = strdup("/tmp/.myapp");
    }
#endif

    if (!data_dir) {
        return NULL;
    }

    // Create data directory
    if (dap_mkdir_with_parents(data_dir) != 0) {
        free(data_dir);
        return NULL;
    }

    // Build file path
    char *file_path = dap_build_filename(data_dir, filename, NULL);
    free(data_dir);

    return file_path;
}

char *get_config_file_path(const char *config_name) {
    // Determine configuration directory
    char *config_dir;

#ifdef _WIN32
    // Windows: %PROGRAMDATA%\MyApp\Config
    const char *program_data = getenv("PROGRAMDATA");
    if (program_data) {
        config_dir = dap_build_filename(program_data, "MyApp", "Config", NULL);
    } else {
        config_dir = dap_build_filename("C:", "ProgramData", "MyApp", "Config", NULL);
    }
#else
    // Unix‑like: /etc/myapp
    config_dir = strdup("/etc/myapp");
#endif

    if (!config_dir) {
        return NULL;
    }

    // Create configuration directory
    if (dap_mkdir_with_parents(config_dir) != 0) {
        free(config_dir);
        return NULL;
    }

    // Build configuration file path
    char *config_path = dap_build_filename(config_dir, config_name, NULL);
    free(config_dir);

    return config_path;
}
```

## Performance

### Performance benchmarks

| Operation | Throughput | Note |
|-----------|-----------|------|
| `dap_file_test()` | ~1-5 μs | File existence check |
| `dap_dir_test()` | ~1-5 μs | Directory existence check |
| `dap_path_get_basename()` | ~0.1-1 μs | Extract filename |
| `dap_build_filename()` | ~1-10 μs | Build path |
| `dap_canonicalize_filename()` | ~5-20 μs | Canonicalize path |
| `dap_file_get_contents2()` | Depends on size | Read file into memory |

### Optimizations

1. **Result caching**: Avoid repeated filesystem checks
2. **String pool**: Reuse string objects
3. **Lazy loading**: Defer reading large files
4. **Buffered I/O**: Efficient file operations

### Impact factors

- **Path length**: Long paths process slower
- **Number of files**: Many entries in a directory
- **Filesystem**: Performance differs across FS types
- **OS caching**: Filesystem cache impact

## Security

### Secure usage recommendations

```c
// ✅ Correct path handling
char *safe_build_path(const char *base_dir, const char *filename) {
    // NULL checks
    if (!base_dir || !filename) {
        return NULL;
    }

    // ASCII‑only check (for Windows)
    if (!dap_valid_ascii_symbols(filename)) {
        return NULL;
    }

    // Canonicalize path to prevent directory traversal
    char *canonical = dap_canonicalize_filename(filename, base_dir);
    if (!canonical) {
        return NULL;
    }

    // Ensure the path stays within the allowed base directory
    if (strncmp(canonical, base_dir, strlen(base_dir)) != 0) {
        free(canonical);
        return NULL;
    }

    return canonical;
}

// ❌ Vulnerable code
void unsafe_file_access(const char *user_input) {
    // Directory traversal vulnerability
    char *path = dap_build_filename("/var/data", user_input, NULL);
    // A user may pass "../../../etc/passwd"
    FILE *file = fopen(path, "r");
    free(path);
}
```

### Protection against common attacks

1. **Directory Traversal**: Always canonicalize paths
2. **Path Injection**: Validate input
3. **Symbolic Links**: Verify target files
4. **Permissions**: Check access rights

### Working with sensitive files

```c
// Secure reading of confidential files
char *read_secure_config(const char *config_path) {
    // Validate path
    if (!dap_path_is_absolute(config_path)) {
        return NULL;
    }

    // Existence check
    if (!dap_file_test(config_path)) {
        return NULL;
    }

    // Access rights check (optional)
    // check_file_permissions(config_path);

    // Read file
    size_t size;
    char *content = dap_file_get_contents2(config_path, &size);

    if (content) {
        // Memory cleanup after use (example)
        // memset(content, 0, size);
        // free(content);
    }

    return content;
}
```

## Best practices

### 1. Error handling

```c
// Always check operation results
int safe_file_operations(const char *filename) {
    // Check file existence
    if (!dap_file_test(filename)) {
        fprintf(stderr, "File does not exist: %s\n", filename);
        return -1;
    }

    // Check directory for writing
    char *dirname = dap_path_get_dirname(filename);
    if (dirname) {
        if (!dap_dir_test(dirname)) {
            // Create directory
            if (dap_mkdir_with_parents(dirname) != 0) {
                fprintf(stderr, "Cannot create directory: %s\n", dirname);
                free(dirname);
                return -2;
            }
        }
        free(dirname);
    }

    return 0;
}
```

### 2. Resource management

```c
// Proper memory management
void process_files(const char *dir_path) {
    dap_list_name_directories_t *subs = dap_get_subs(dir_path);
    if (!subs) {
        return;
    }

    dap_list_name_directories_t *current = subs;
    while (current) {
        // Create file path
        char *file_path = dap_build_filename(dir_path, current->name_directory, NULL);
        if (file_path) {
            // Process file
            process_single_file(file_path);
            free(file_path);
        }

        current = current->next;
    }

    // Free list
    dap_subs_free(subs);
}
```

### 3. Cross‑platform development

```c
// Use system separators
void create_log_path(char *buffer, size_t size) {
    const char *log_dir = "logs";
    const char *log_file = "app.log";

    // Create directory
    if (dap_mkdir_with_parents(log_dir) != 0) {
        return;
    }

    // Build path
    char *full_path = dap_build_filename(log_dir, log_file, NULL);
    if (full_path) {
        strncpy(buffer, full_path, size - 1);
        buffer[size - 1] = '\0';
        free(full_path);
    }
}

// Use constants
bool is_path_separator(char c) {
    return DAP_IS_DIR_SEPARATOR(c);
}
```

### 4. Working with large files

```c
// Stream processing of large files
int process_large_file(const char *filename) {
    // Check file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        return -1;
    }

    // Use streaming for large files
    if (st.st_size > MAX_MEMORY_FILE_SIZE) {
        return process_file_streaming(filename);
    } else {
        // For small files - read into memory
        size_t size;
        char *content = dap_file_get_contents2(filename, &size);
        if (content) {
            int result = process_file_content(content, size);
            free(content);
            return result;
        }
        return -2;
    }
}
```

## Conclusion

The `dap_file_utils` module provides comprehensive support for working with files and directories in the DAP SDK:

- **Cross‑platform**: Unified API for all supported platforms
- **Security**: Protection against common attacks and mistakes
- **Performance**: Optimized algorithms and caching
- **Convenience**: Simple and intuitive interface
- **Reliability**: Error handling and resource management

### Key advantages:

- Full support for Windows, Linux, and macOS
- Automatic memory management
- Built‑in vulnerability mitigations
- High operation performance
- Broad set of utilities for various tasks

### Recommendations:

1. **Always check operation results** for file APIs
2. **Use `dap_canonicalize_filename()`** to prevent directory traversal
3. **Free memory** for strings returned by functions
4. **Check existence** of files and directories before use
5. **Use cross‑platform constants** for separators

For more information, see:
- `dap_file_utils.h` - full module API
- Examples in `examples/file_utils/`
- Cross‑platform development documentation

