# DAP SDK - Documentation

## Overview

DAP SDK (Decentralized Application Platform Software Development Kit) is a toolkit for building decentralized applications with quantum-resistant cryptography and blockchain infrastructure.

## Documentation structure

- [System architecture](./architecture.md) - Overall architecture and principles
- [Modules](./modules/) - Documentation for individual modules
- [API Reference](./api/) - API reference
- [Examples](./examples/) - Practical code examples
- [Scripts](./scripts/) - Scripts to run MCP servers

## Quick start

### Installation

```bash
# Clone the repository
git clone <repository-url>
cd dap-sdk

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### MCP server for AI integration

DAP SDK includes a Model Context Protocol (MCP) server for integration with AI systems:

```bash
# Run MCP server
./docs/scripts/start_mcp_servers.sh start dap_sdk

# Or run all MCP servers
./docs/scripts/start_mcp_servers.sh start all
```

The MCP server provides tools for:
- Analyzing cryptographic algorithms
- Exploring networking modules
- Inspecting the build system
- Searching code examples
- Analyzing security-related functions

### Basic usage

```c
#include "dap_common.h"
#include "dap_crypto.h"

int main() {
    // Initialize DAP SDK
    dap_init();

    // Your code here

    // Cleanup resources
    dap_deinit();
    return 0;
}
```

## Main modules

### Core
- **Path**: `core/`
- **Description**: Core functionality of DAP SDK
- **Components**: Common utilities, platform-specific implementations
- **Documentation**: [Detailed description](./modules/core.md)

### Crypto
- **Path**: `crypto/`
- **Description**: Cryptographic components and algorithms
- **Algorithms**: Kyber, Falcon, SPHINCS+, Dilithium, Bliss, Chipmunk
- **Documentation**: [Cryptographic modules](./modules/crypto.md)

### Net
- **Path**: `net/`
- **Description**: Networking components and communication
- **Servers**: HTTP, JSON-RPC, DNS, Encryption, Notification
- **Documentation**: [Network architecture](./modules/net.md)

### Global DB
- **Path**: `global-db/`
- **Description**: Data management system
- **Drivers**: MDBX, PostgreSQL, SQLite

## Requirements

- **Compiler**: GCC 7.0+ or Clang 5.0+
- **CMake**: 3.10+
- **Dependencies**:
  - libmdbx
  - json-c
  - OpenSSL (optional)

## License

GNU General Public License v3.0

## Support

- **Documentation**: [Wiki](https://wiki.demlabs.net)
- **Issues**: [GitLab Issues](https://gitlab.demlabs.net/dap/dap-sdk/-/issues)
- **Community**: [Telegram](https://t.me/cellframe)
