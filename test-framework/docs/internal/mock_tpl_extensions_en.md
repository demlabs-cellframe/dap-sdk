# DAP Mock Template Extensions

**Internal Documentation** - For developers extending the mock framework

## Overview

DAP Mock Template Extensions provide specialized constructs and variable functions for generating C code used in the mock framework. These extensions are located in `test-framework/mocks/lib/dap_tpl/` and are loaded via the `--extensions-dir` parameter.

The extensions extend the base dap_tpl template system with C-specific constructs and variable transformations needed for mock code generation.

### Usage

```bash
source dap_tpl/dap_tpl.sh

replace_template_placeholders \
    template.tpl \
    output.h \
    --extensions-dir "../mocks/lib/dap_tpl" \
    "FEATURE_NAME=ENABLE_DEBUG" \
    "TYPE=dap_list_t*"
```

### Constructs

#### c_ifdef - C Preprocessor Conditional Compilation

Generates preprocessor directives `#ifdef`, `#elifdef`, `#else`, `#endif`.

**Syntax:**
```
{{#c_ifdef FEATURE_NAME}}
#ifdef FEATURE_NAME
// code here
{{#c_elif OTHER_FEATURE}}
#elifdef OTHER_FEATURE
// alternative code
{{#c_else}}
#else
// default code
{{/c_ifdef}}
#endif
```

**Example:**
```
{{#c_ifdef ENABLE_DEBUG}}
#ifdef ENABLE_DEBUG
    log_debug("Debug mode enabled");
{{#c_else}}
#else
    // Release mode
{{/c_ifdef}}
#endif
```

**Features:**
- Automatically removes extra newlines after directives
- Properly handles nested conditions
- Supports multiple `#c_elif` blocks

#### c_for - C-formatted For Loops

Generates C arrays and initializers with proper comma handling.

**Syntax:**
```
{{#c_for key,val in PAIRS}}
{ .key = {{key}}, .val = {{val}} },
{{/c_for}}
```

**Example:**
```
{{#set PAIRS=name|string
id|int
count|size_t}}

static const struct {
    const char *key;
    const char *type;
} pairs[] = {
{{#c_for key,val in PAIRS}}
    { .key = "{{key}}", .type = "{{val}}" },
{{/c_for}}
};
```

**Result:**
```c
static const struct {
    const char *key;
    const char *type;
} pairs[] = {
    { .key = "name", .type = "string" },
    { .key = "id", .type = "int" },
    { .key = "count", .type = "size_t" },
};
```

**Features:**
- Automatically adds commas between elements
- Removes trailing comma after last element
- Supports nested structures

#### c_struct - typedef struct Generation

Generates C structure definitions with automatic `_t` suffix.

**Syntax:**
```
{{#c_struct StructName}}
int field1;
char* field2;
{{/c_struct}}
```

**Example:**
```
{{#c_struct MockConfig}}
bool enabled;
int timeout_ms;
const char* name;
{{/c_struct}}
```

**Result:**
```c
typedef struct {
    bool enabled;
    int timeout_ms;
    const char* name;
} MockConfig_t;
```

**Features:**
- Automatically adds `typedef struct {...} StructName_t;`
- Preserves field formatting
- Supports nested structures

#### c_define_chain - Chain of #define Directives

Generates a sequence of `#define` directives with proper formatting.

**Syntax:**
```
{{#c_define_chain}}
#define A 1
#define B 2
{{/c_define_chain}}
```

**Example:**
```
{{#set VALUES=SUCCESS|ERROR|TIMEOUT}}
{{#c_define_chain}}
{{#for value in VALUES}}
#define STATUS_{{value}} {{value}}
{{/for}}
{{/c_define_chain}}
```

**Result:**
```c
#define STATUS_SUCCESS SUCCESS
#define STATUS_ERROR ERROR
#define STATUS_TIMEOUT TIMEOUT
```

### Variable Functions

#### normalize_name

Converts value to valid C identifier by replacing special characters.

**Syntax:** `{{VAR|normalize_name}}`

**Examples:**
```
{{TYPE|normalize_name}}
```

- `dap_list_t*` → `dap_list_t_PTR`
- `char**` → `char_PTR_PTR`
- `const char*` → `const_char_PTR`

**Usage:**
```
{{#set TYPE=dap_list_t*}}
typedef {{TYPE}} {{TYPE|normalize_name}}_wrapper_t;
// Result: typedef dap_list_t* dap_list_t_PTR_wrapper_t;
```

#### escape_name

Escapes special characters in name.

**Syntax:** `{{VAR|escape_name}}`

**Examples:**
```
{{TYPE|escape_name}}
```

- `dap_list_t*` → `dap_list_t\*`
- `char**` → `char\*\*`

#### escape_char

Replaces specific character with replacement string.

**Syntax:** `{{VAR|escape_char|char|replacement}}`

**Examples:**
```
{{TYPE|escape_char|*|_PTR}}
```

- `dap_list_t*` → `dap_list_t_PTR`
- `char**` → `char_PTR_PTR`

#### sanitize_name

Removes invalid characters from name, keeping only letters, digits, and underscores.

**Syntax:** `{{VAR|sanitize_name}}`

**Examples:**
```
{{NAME|sanitize_name}}
```

- `my-module` → `mymodule`
- `test@123` → `test123`
- `module.name` → `modulename`

**Usage:**
```
{{#set MODULE=dap-crypto}}
#ifndef {{MODULE|sanitize_name}}_MOCKS_H
#define {{MODULE|sanitize_name}}_MOCKS_H
// Result: #ifndef dapcrypto_MOCKS_H
```

### Usage Examples

#### Example 1: Generating Mock Header

```bash
# template.tpl
{{#c_ifdef ENABLE_MOCKS}}
#ifdef ENABLE_MOCKS
#ifndef {{MODULE|sanitize_name}}_MOCKS_H
#define {{MODULE|sanitize_name}}_MOCKS_H

#include "dap_mock.h"

{{#for func in FUNCTIONS}}
{{func|split|pipe}}
DAP_MOCK_DECLARE({{func|part|0}});
{{/for}}

#endif // {{MODULE|sanitize_name}}_MOCKS_H
{{#c_else}}
#else
// Mocks disabled
{{/c_ifdef}}
#endif
```

**Usage:**
```bash
replace_template_placeholders \
    template.tpl \
    output.h \
    --extensions-dir "../mocks/lib/dap_tpl" \
    "MODULE=dap-crypto" \
    "ENABLE_MOCKS=1" \
    "FUNCTIONS=init|int|void
cleanup|void|void
process|char*|const char* data"
```

#### Example 2: Generating Types with Normalization

```bash
# types.tpl
{{#set TYPES=int|char*|dap_list_t*|void*}}
{{#for type in TYPES}}
typedef {{type}} {{type|normalize_name}}_wrapper_t;
{{/for}}
```

**Result:**
```c
typedef int int_wrapper_t;
typedef char* char_PTR_wrapper_t;
typedef dap_list_t* dap_list_t_PTR_wrapper_t;
typedef void* void_PTR_wrapper_t;
```

### Testing

All mocking extensions are tested in `dap-sdk/tests/integration/test-framework/`:

- `test_c_ifdef.sh` - Tests for `c_ifdef`
- `test_return_type_macros.sh` - Tests for return type macro generation
- `test_return_type_macros_dap_tpl.sh` - Tests for pure dap_tpl approach

**Running Tests:**
```bash
cd dap-sdk/tests
./run.sh integration test-framework
```

Or via CTest:
```bash
cd dap-sdk/build
ctest -L test-framework --output-on-failure
```

### Extension Structure

```
mocks/lib/dap_tpl/
├── c_ifdef/
│   ├── definition.awk      # Construct registration
│   ├── tokenizer.awk       # Syntax parsing
│   ├── evaluator.awk       # C code generation
│   └── branch_parser.awk   # elif/else branch parsing
├── c_for/
│   ├── definition.awk
│   ├── tokenizer.awk
│   └── evaluator.awk
├── c_struct/
│   ├── definition.awk
│   ├── tokenizer.awk
│   └── evaluator.awk
├── c_define_chain/
│   ├── definition.awk
│   ├── tokenizer.awk
│   └── evaluator.awk
└── variable_functions.awk  # Variable functions (normalize_name, etc.)
```

