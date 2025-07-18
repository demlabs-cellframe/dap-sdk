# Python DAP Tests

Comprehensive test suite for Python DAP SDK bindings.

## Test Structure

```
tests/
├── unit/                 # Unit tests for individual modules
│   ├── test_core.py     # Core module tests (Dap, DapSystem, DapLogging, DapTime)
│   ├── test_config.py   # Configuration module tests
│   ├── test_crypto.py   # Cryptography module tests
│   └── test_network.py  # Network module tests
├── integration/          # Integration tests
│   ├── test_core_integration.py    # Core subsystem integration
│   └── test_network_integration.py  # Network client-server integration
├── regression/           # Regression tests
│   └── test_no_fallbacks.py  # Ensure no fallback implementations
├── performance/          # Performance tests
└── functional/           # End-to-end functional tests
```

## Running Tests

### Prerequisites

1. Build python_dap.so:
```bash
cd build_c
cmake .. -DBUILD_PYTHON_DAP=ON
make
```

2. Install test dependencies:
```bash
pip install pytest pytest-timeout pytest-cov
```

### Run All Tests
```bash
pytest
```

### Run Specific Test Categories

```bash
# Unit tests only
pytest unit/

# Integration tests
pytest integration/

# Regression tests
pytest regression/

# With coverage
pytest --cov=dap --cov-report=html

# Run specific test file
pytest unit/test_core.py

# Run specific test class
pytest unit/test_core.py::TestDapCore

# Run specific test method
pytest unit/test_core.py::TestDapCore::test_dap_singleton
```

### Test Markers

Tests are marked with categories:

```bash
# Run only unit tests
pytest -m unit

# Run network-related tests
pytest -m network

# Skip slow tests
pytest -m "not slow"

# Run tests that require build
pytest -m requires_build
```

## Test Guidelines

### Unit Tests
- Test individual classes and functions in isolation
- Mock external dependencies
- Fast execution (< 1 second per test)
- Located in `unit/` directory

### Integration Tests
- Test interaction between modules
- May use real DAP SDK functions
- Medium execution time (< 30 seconds per test)
- Located in `integration/` directory

### Regression Tests
- Prevent reintroduction of fixed bugs
- Test for absence of fallback implementations
- Verify critical functionality remains intact
- Located in `regression/` directory

### Performance Tests
- Benchmark critical operations
- Compare against baseline performance
- Located in `performance/` directory

### Functional Tests
- End-to-end scenarios
- Test complete workflows
- May take longer to execute
- Located in `functional/` directory

## Writing New Tests

1. Choose appropriate test category
2. Create test file with `test_` prefix
3. Use descriptive test names
4. Add appropriate markers
5. Include docstrings explaining test purpose
6. Use pytest fixtures for common setup

Example:
```python
import pytest
from dap.core.dap import Dap

class TestExample:
    """Test cases for example functionality"""
    
    @pytest.fixture
    def dap_instance(self):
        """Provide initialized DAP instance"""
        dap = Dap()
        dap.init()
        yield dap
        dap.deinit()
    
    @pytest.mark.unit
    def test_example_feature(self, dap_instance):
        """Test specific feature behavior"""
        result = dap_instance.some_method()
        assert result is not None
```

## Common Issues

### ImportError: No module named 'python_dap'
- Solution: Build python_dap.so first (see Prerequisites)

### Test Timeout
- Some tests have timeouts to prevent hanging
- Adjust timeout in pytest.ini if needed

### Fallback Implementation Errors
- Ensure all Python modules use real C bindings
- Check regression tests pass 