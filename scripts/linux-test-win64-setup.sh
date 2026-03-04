#!/bin/bash
# Setup script for Windows x64 cross-compilation and wine testing on Debian/Ubuntu
# Usage: sudo ./scripts/deb-test-win64-setup.sh
#
# This script installs and configures:
# - MinGW-w64 x86_64 toolchain for cross-compilation
# - Wine64 for running Windows executables
# - Required development libraries

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (sudo)"
        exit 1
    fi
}

# Detect distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
        VERSION=$VERSION_ID
        log_info "Detected: $DISTRO $VERSION"
    else
        log_error "Cannot detect distribution. /etc/os-release not found."
        exit 1
    fi
}

# Install MinGW-w64 x86_64 toolchain
install_mingw() {
    log_step "Installing MinGW-w64 x86_64 cross-compiler..."
    
    case $DISTRO in
        debian|ubuntu|linuxmint|pop)
            apt-get update
            apt-get install -y \
                gcc-mingw-w64-x86-64 \
                g++-mingw-w64-x86-64 \
                mingw-w64-tools \
                binutils-mingw-w64-x86-64
            ;;
        fedora)
            dnf install -y \
                mingw64-gcc \
                mingw64-gcc-c++ \
                mingw64-binutils
            ;;
        arch|manjaro)
            pacman -Sy --noconfirm mingw-w64-gcc
            ;;
        *)
            log_error "Unsupported distribution: $DISTRO"
            log_error "Please install MinGW-w64 x86_64 toolchain manually"
            exit 1
            ;;
    esac
    
    # Verify installation
    if command -v x86_64-w64-mingw32-gcc &> /dev/null; then
        log_info "MinGW-w64 x86_64 installed: $(x86_64-w64-mingw32-gcc --version | head -1)"
    else
        log_error "MinGW-w64 x86_64 installation failed"
        exit 1
    fi
}

# Install Wine64
install_wine() {
    log_step "Installing Wine64 for running Windows executables..."
    
    case $DISTRO in
        debian|ubuntu|linuxmint|pop)
            # Enable 32-bit architecture for wine dependencies
            dpkg --add-architecture i386 2>/dev/null || true
            apt-get update
            apt-get install -y \
                wine64 \
                wine32 \
                winetricks
            ;;
        fedora)
            dnf install -y wine winetricks
            ;;
        arch|manjaro)
            pacman -Sy --noconfirm wine winetricks
            ;;
        *)
            log_error "Unsupported distribution: $DISTRO"
            log_error "Please install Wine manually"
            exit 1
            ;;
    esac
    
    # Verify installation
    if command -v wine64 &> /dev/null; then
        log_info "Wine64 installed: $(wine64 --version)"
    else
        log_error "Wine64 installation failed"
        exit 1
    fi
}

# Install additional development dependencies
install_deps() {
    log_step "Installing additional build dependencies..."
    
    case $DISTRO in
        debian|ubuntu|linuxmint|pop)
            apt-get install -y \
                cmake \
                make \
                git \
                pkg-config
            ;;
        fedora)
            dnf install -y \
                cmake \
                make \
                git \
                pkgconf
            ;;
        arch|manjaro)
            pacman -Sy --noconfirm \
                cmake \
                make \
                git \
                pkg-config
            ;;
    esac
}

# Configure Wine for non-interactive use
configure_wine() {
    log_step "Configuring Wine for non-interactive testing..."
    
    # Set Wine to not show GUI dialogs
    export WINEDEBUG=-all
    export DISPLAY=
    
    # Create Wine configuration hints
    cat > /etc/profile.d/wine-test.sh << 'EOF'
# Wine configuration for automated testing
export WINEDEBUG=-all
export WINEPREFIX=$HOME/.wine-test
# Disable GUI prompts
export WINEDLLOVERRIDES="winemenubuilder.exe=d"
EOF
    
    log_info "Wine configured. Source /etc/profile.d/wine-test.sh or re-login"
}

# Print summary and usage instructions
print_summary() {
    echo ""
    echo "=============================================="
    log_info "Windows x64 cross-compilation setup complete!"
    echo "=============================================="
    echo ""
    log_info "Installed components:"
    echo "  - MinGW-w64 x86_64: $(x86_64-w64-mingw32-gcc --version | head -1)"
    echo "  - Wine64: $(wine64 --version)"
    echo ""
    log_info "Usage:"
    echo "  1. Build for Windows:"
    echo "     ./scripts/test-win64-cross.sh"
    echo ""
    echo "  2. Run tests with Wine:"
    echo "     wine64 build-cross-win64/tests/bin/test_unit_dap_json.exe"
    echo ""
    echo "  3. Run all Windows tests:"
    echo "     ./scripts/test-win64-wine.sh  (after building)"
    echo ""
    log_info "Environment configured in: /etc/profile.d/wine-test.sh"
    log_info "Logout and login again, or run: source /etc/profile.d/wine-test.sh"
}

# Main
main() {
    echo "=============================================="
    echo "DAP SDK Windows x64 Cross-Compilation Setup"
    echo "=============================================="
    echo ""
    
    check_root
    detect_distro
    install_deps
    install_mingw
    install_wine
    configure_wine
    print_summary
}

main "$@"
