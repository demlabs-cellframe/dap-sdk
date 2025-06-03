# BigInt Test Project Structure

## Directory Structure
```
dap-sdk/core/test/bigint/
├── doc/
│   ├── progress.md
│   ├── project_structure.md
│   └── mcp.md
├── prompt_eng_add.cc
├── bigint_logic_tests.cc
└── CMakeLists.txt
```

## Test Files Overview

### prompt_eng_add.cc
- **Purpose**: Compares addition operations between Boost and Libdap implementations
- **Key Components**:
  - `BigIntAdditionTest` class
  - Helper functions for conversion between formats
  - Parameterized tests for different limb sizes
  - Edge case testing framework

### bigint_logic_tests.cc
- **Purpose**: Tests logical operations on big integers
- **Key Components**:
  - `BigIntLogicTest` class
  - Test cases for AND, OR, XOR operations
  - Support for different limb sizes
  - Edge case testing

## Dependencies
- Google Test Framework
- Boost Multiprecision Library
- Libdap Core Library

## Build System
- CMake-based build system
- Integration with main CELLFRAME-SDK build process
- Test execution through CTest

## Test Categories
1. **Unit Tests**
   - Individual operation testing
   - Edge case verification
   - Error handling

2. **Integration Tests**
   - Cross-library comparison
   - Performance benchmarking
   - Memory management

3. **Regression Tests**
   - Known issue verification
   - Bug fix validation
   - Compatibility testing 