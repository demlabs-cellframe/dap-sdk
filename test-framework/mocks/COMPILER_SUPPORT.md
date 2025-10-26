# Compiler and Linker Support for Mock Framework

## Quick Reference

### ✅ Fully Supported (Recommended)

| Platform | Compiler | Linker | Command Example |
|----------|----------|--------|-----------------|
| **Linux** | GCC | GNU ld | `gcc -Wl,--wrap=func test.c` |
| **Linux** | Clang | GNU ld / LLD | `clang -Wl,--wrap=func test.c` |
| **macOS** | Clang | LLD (Xcode) | `clang -Wl,--wrap=func test.c` |
| **Windows** | MinGW-w64 | GNU ld | `gcc -Wl,--wrap=func test.c` |
| **Windows** | Clang | LLD | `clang -Wl,--wrap=func test.c` |

### ❌ Not Supported

| Platform | Compiler | Reason | Alternative |
|----------|----------|--------|-------------|
| **Windows** | MSVC (cl.exe) | `link.exe` doesn't support `--wrap` | Use MinGW-w64 |

## Detailed Information

### 1. GCC + GNU ld (Linux)

**Status:** ✅ **Fully supported (PRIMARY)**

```bash
# Install (if needed)
sudo apt install build-essential  # Debian/Ubuntu
sudo yum install gcc              # RHEL/CentOS

# Usage
gcc test.c -Wl,--wrap=my_function -o test
```

**Pros:**
- Default Linux compiler
- Best documentation and support
- Most tested configuration
- Standard in Cellframe SDK

---

### 2. Clang + GNU ld/LLD (Linux/macOS)

**Status:** ✅ **Fully supported**

```bash
# Install (if needed)
sudo apt install clang  # Linux
# macOS: built-in with Xcode

# Usage
clang test.c -Wl,--wrap=my_function -o test
```

**Pros:**
- Better error messages
- Faster compilation
- Default on macOS

---

### 3. MinGW-w64 + GNU ld (Windows)

**Status:** ✅ **Fully supported (Windows PRIMARY)**

```bash
# Install via MSYS2 (https://www.msys2.org/)
pacman -S mingw-w64-x86_64-gcc

# Usage
gcc test.c -Wl,--wrap=my_function -o test.exe
```

**CMake setup:**
```cmake
# In CMakeLists.txt
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "GNU")
    # MinGW detected, mock tests will work
endif()
```

---

### 4. MSVC + link.exe (Windows)

**Status:** ❌ **NOT SUPPORTED**

**Why:**
- `link.exe` has no `--wrap` equivalent
- `/ALTERNATENAME` is not compatible (different semantics)

**Solution for MSVC users:**

Use MinGW for test builds:
```cmake
if(BUILD_TESTS AND MSVC)
    message(WARNING "MSVC detected. Install MinGW-w64 for mock tests.")
    message(WARNING "Download: https://www.msys2.org/")
endif()
```

---

## CMake Automatic Detection

DAP Mock AutoWrap automatically handles compiler differences:

```cmake
# From DAPMockAutoWrap.cmake
if(CMAKE_C_COMPILER_ID MATCHES "MSVC" OR CMAKE_C_SIMULATE_ID MATCHES "MSVC")
    message(WARNING "MSVC does not support --wrap.")
    message(WARNING "Please use MinGW/Clang for mock testing.")
else()
    # GCC, Clang, MinGW - unified syntax
    target_link_options(test PRIVATE "-Wl,--wrap=${FUNCTION}")
endif()
```

**Detection variables:**
- `CMAKE_C_COMPILER_ID`: `GNU`, `Clang`, `MSVC`, `Intel`
- `CMAKE_C_SIMULATE_ID`: For clang-cl
- `CMAKE_LINKER`: Path to linker

---

## Platform-Specific Notes

### Linux
✅ Works out of the box with GCC or Clang (standard install)

**Verify:**
```bash
ld --help | grep wrap
# Output: --wrap SYMBOL    Use wrapper functions for SYMBOL
```

### macOS
✅ Works with Xcode's clang (standard install)

**Verify:**
```bash
ld -v 2>&1 | head -1
# Output: @(#)PROGRAM:ld  PROJECT:ld-...
```

### Windows

**MinGW (recommended):**
```bash
# Check MinGW installation
gcc --version | grep MinGW
# Output: gcc.exe (MinGW-W64 x86_64-...) ...

ld --help | grep wrap
# Output: --wrap SYMBOL
```

**MSVC (not supported):**
```cmd
cl /?
link /? | findstr "ALTERNATENAME"
REM Shows /ALTERNATENAME but it's incompatible
```

---

## Testing Your Setup

Create a simple test to verify `--wrap` works:

```c
// test_wrap.c
#include <stdio.h>

int real_function() { 
    return 42; 
}

int __wrap_real_function() { 
    return 999;  // Mock returns different value
}

int main() {
    printf("Result: %d\n", real_function());
    return 0;
}
```

Compile and run:
```bash
gcc test_wrap.c -Wl,--wrap=real_function -o test_wrap
./test_wrap
```

**Expected output:** `Result: 999` (wrapped)  
**If you see:** `Result: 42` → `--wrap` not working!

---

## Recommendations by Use Case

| Your Situation | Recommended Setup | Why |
|----------------|-------------------|-----|
| Linux development | GCC + GNU ld | Default, best tested |
| macOS development | Clang (Xcode) | Built-in, works great |
| Windows + MinGW | GCC (MinGW-w64) | Full compatibility |
| Windows + Visual Studio | MinGW for tests only | MSVC for production, MinGW for tests |
| CI/CD (Linux) | GCC or Clang | Both in standard images |
| CI/CD (Windows) | MinGW or Clang | Avoid MSVC for test builds |

---

## Troubleshooting

### "undefined reference to __wrap_function"
**Problem:** Wrapper not defined  
**Solution:** Create wrapper using `DAP_MOCK_WRAPPER_*` macros

### "warning: --wrap is not supported"  
**Problem:** Wrong linker (probably MSVC)  
**Solution:** Install MinGW-w64 and reconfigure CMake

### Wrapper not called (real function runs)
**Problem:** `--wrap` option not passed to linker  
**Solution:** 
```cmake
target_link_options(test PRIVATE "-Wl,--wrap=function_name")
```

### Windows: "gcc: command not found"
**Problem:** MinGW not in PATH  
**Solution:** 
```bash
# Add to PATH (PowerShell)
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
```

---

## Cellframe SDK Policy

**Primary platforms:**
- **Linux**: GCC (tests + production)
- **macOS**: Clang (tests + production)
- **Windows**: 
  - MinGW for development/tests
  - MSVC for production builds (tests disabled)

**CI/CD:**
- Linux: GCC (Ubuntu/Debian runners)
- macOS: Clang (macOS runners)
- Windows: MinGW-w64 (custom runners)

---

## See Also

- [AUTOWRAP.md](AUTOWRAP.md) - Auto-wrapper system docs
- [README.md](README.md) - Mock framework docs  
- GNU ld manual: https://sourceware.org/binutils/docs/ld/Options.html
- MinGW-w64: https://www.mingw-w64.org/
- MSYS2 (MinGW installer): https://www.msys2.org/
- LLD linker: https://lld.llvm.org/
