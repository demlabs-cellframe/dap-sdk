# ============================================================================
# DAP SDK Package Detection and Naming
# ============================================================================
# Detects OS distribution and version for proper package naming
# Usage: include(cmake/PackageDetection.cmake)
# Output variables:
#   DAP_OS_NAME        - OS name (debian, ubuntu, arch, fedora, etc)
#   DAP_OS_VERSION     - OS version (13.1, 22.04, etc)
#   DAP_PACKAGE_SUFFIX - Package suffix for CPack (debian13.1, ubuntu22.04, etc)
#   DAP_PACKAGE_ARCH   - Architecture (x86_64, aarch64, etc)

# Detect architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(DAP_PACKAGE_ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(DAP_PACKAGE_ARCH "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7")
    set(DAP_PACKAGE_ARCH "armv7")
else()
    set(DAP_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Detect OS distribution and version
if(EXISTS "/etc/os-release")
    file(READ "/etc/os-release" OS_RELEASE)
    
    # Extract ID (distribution name)
    string(REGEX MATCH "ID=([a-zA-Z0-9_-]+)" _ ${OS_RELEASE})
    set(DAP_OS_ID "${CMAKE_MATCH_1}")
    string(TOLOWER "${DAP_OS_ID}" DAP_OS_NAME)
    
    # Extract VERSION_ID
    string(REGEX MATCH "VERSION_ID=\"?([0-9.]+)\"?" _ ${OS_RELEASE})
    set(DAP_OS_VERSION "${CMAKE_MATCH_1}")
    
elseif(EXISTS "/etc/debian_version")
    # Debian system without os-release (older systems)
    file(READ "/etc/debian_version" DEBIAN_VERSION)
    string(STRIP "${DEBIAN_VERSION}" DAP_OS_VERSION)
    set(DAP_OS_NAME "debian")
    
else()
    # Generic Linux
    set(DAP_OS_NAME "linux")
    set(DAP_OS_VERSION "unknown")
endif()

# Special handling for specific distributions
if(DAP_OS_NAME STREQUAL "ubuntu")
    # Ubuntu: use full version (22.04, 20.04, etc)
    set(DAP_PACKAGE_SUFFIX "ubuntu${DAP_OS_VERSION}")
    
elseif(DAP_OS_NAME STREQUAL "debian")
    # Debian: use major.minor version from /etc/debian_version
    set(DAP_PACKAGE_SUFFIX "debian${DAP_OS_VERSION}")
    
elseif(DAP_OS_NAME STREQUAL "linuxmint" OR DAP_OS_NAME STREQUAL "mint")
    # Linux Mint
    set(DAP_PACKAGE_SUFFIX "mint${DAP_OS_VERSION}")
    
elseif(DAP_OS_NAME STREQUAL "arch" OR DAP_OS_NAME STREQUAL "archlinux")
    # Arch Linux (rolling release, no version)
    set(DAP_PACKAGE_SUFFIX "arch")
    
elseif(DAP_OS_NAME STREQUAL "fedora")
    # Fedora: use version number
    set(DAP_PACKAGE_SUFFIX "fedora${DAP_OS_VERSION}")
    
elseif(DAP_OS_NAME STREQUAL "centos")
    # CentOS: use version number
    set(DAP_PACKAGE_SUFFIX "centos${DAP_OS_VERSION}")
    
elseif(DAP_OS_NAME STREQUAL "rhel" OR DAP_OS_NAME STREQUAL "redhat")
    # Red Hat Enterprise Linux
    set(DAP_PACKAGE_SUFFIX "rhel${DAP_OS_VERSION}")
    
elseif(DAP_OS_NAME STREQUAL "gentoo")
    # Gentoo (rolling release, no version)
    set(DAP_PACKAGE_SUFFIX "gentoo")
    
elseif(DAP_OS_NAME STREQUAL "opensuse" OR DAP_OS_NAME STREQUAL "suse")
    # openSUSE
    set(DAP_PACKAGE_SUFFIX "opensuse${DAP_OS_VERSION}")
    
elseif(DAP_OS_NAME STREQUAL "alpine")
    # Alpine Linux
    set(DAP_PACKAGE_SUFFIX "alpine${DAP_OS_VERSION}")
    
else()
    # Generic Linux with version if available
    if(DAP_OS_VERSION AND NOT DAP_OS_VERSION STREQUAL "unknown")
        set(DAP_PACKAGE_SUFFIX "${DAP_OS_NAME}${DAP_OS_VERSION}")
    else()
        set(DAP_PACKAGE_SUFFIX "linux")
    endif()
endif()

# Add debug suffix for debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DAP_DEBUG_SUFFIX "-dbg")
else()
    set(DAP_DEBUG_SUFFIX "")
endif()

# Final package name components
set(DAP_PACKAGE_OS_SUFFIX "${DAP_PACKAGE_SUFFIX}_${DAP_PACKAGE_ARCH}")
set(DAP_PACKAGE_FULL_SUFFIX "${DAP_PACKAGE_OS_SUFFIX}${DAP_DEBUG_SUFFIX}")

# Display detected information
message(STATUS "==================================================")
message(STATUS "DAP Package Detection:")
message(STATUS "  OS Name:          ${DAP_OS_NAME}")
message(STATUS "  OS Version:       ${DAP_OS_VERSION}")
message(STATUS "  Architecture:     ${DAP_PACKAGE_ARCH}")
message(STATUS "  Package Suffix:   ${DAP_PACKAGE_OS_SUFFIX}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "  Debug Build:      YES (-dbg suffix)")
endif()
message(STATUS "  Full Suffix:      ${DAP_PACKAGE_FULL_SUFFIX}")
message(STATUS "==================================================")

# Export for parent scope (if included from subdirectory)
set(DAP_OS_NAME "${DAP_OS_NAME}" PARENT_SCOPE)
set(DAP_OS_VERSION "${DAP_OS_VERSION}" PARENT_SCOPE)
set(DAP_PACKAGE_SUFFIX "${DAP_PACKAGE_SUFFIX}" PARENT_SCOPE)
set(DAP_PACKAGE_ARCH "${DAP_PACKAGE_ARCH}" PARENT_SCOPE)
set(DAP_PACKAGE_OS_SUFFIX "${DAP_PACKAGE_OS_SUFFIX}" PARENT_SCOPE)
set(DAP_DEBUG_SUFFIX "${DAP_DEBUG_SUFFIX}" PARENT_SCOPE)
set(DAP_PACKAGE_FULL_SUFFIX "${DAP_PACKAGE_FULL_SUFFIX}" PARENT_SCOPE)
