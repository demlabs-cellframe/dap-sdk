#!/bin/bash
# Setup script for Windows x86 (32-bit) cross-compilation and wine testing on Debian/Ubuntu
# Usage: sudo ./scripts/deb-test-win32-setup.sh
#
# This script installs and configures:
# - MinGW-w64 i686 toolchain for cross-compilation
# - Wine32 for running Windows executables
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

# Install MinGW-w64 i686 toolchain
install_mingw() {
    log_step "Installing MinGW-w64 i686 cross-compiler..."
    
    case $DISTRO in
        debian|ubuntu|linuxmint|pop)
            apt-get update
            apt-get install -y \
                gcc-mingw-w64-i686 \
                g++-mingw-w64-i686 \
                mingw-w64-tools \
                binutils-mingw-w64-i686
            ;;
        fedora)
            dnf install -y \
                mingw32-gcc \
                mingw32-gcc-c++ \
                mingw32-binutils
            ;;
        arch|manjaro)
            pacman -Sy --noconfirm mingw-w64-gcc
            ;;
        *)
            log_error "Unsupported distribution: $DISTRO"
            log_error "Please install MinGW-w64 i686 toolchain manually"
            exit 1
            ;;
    esac
    
    # Verify installation
    if command -v i686-w64-mingw32-gcc &> /dev/null; then
        log_info "MinGW-w64 i686 installed: $(i686-w64-mingw32-gcc --version | head -1)"
    else
        log_error "MinGW-w64 i686 installation failed"
        exit 1
    fi
}

# Install Wine32
install_wine() {
    log_step "Installing Wine32 for running Windows executables..."
    
    case $DISTRO in
        debian|ubuntu|linuxmint|pop)
            # Enable 32-bit architecture
            dpkg --add-architecture i386
            apt-get update
            apt-get install -y \
                wine32 \
                winetricks
            ;;
        fedora)
            dnf install -y wine.i686 winetricks
            ;;
        arch|manjaro)
            # Enable multilib repository first
            pacman -Sy --noconfirm wine winetricks
            ;;
        *)
            log_error "Unsupported distribution: $DISTRO"
            log_error "Please install Wine32 manually"
            exit 1
            ;;
    esac
    
    # Verify installation
    if command -v wine &> /dev/null; then
        log_info "Wine32 installed: $(wine --version)"
    else
        log_error "Wine32 installation failed"
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
    
    # Create Wine configuration hints
    cat > /etc/profile.d/wine-test-32.sh << 'EOF'
# Wine32 configuration for automated testing
export WINEDEBUG=-all
export WINEPREFIX=$HOME/.wine-test-32
export WINEARCH=win32
# Disable GUI prompts
export WINEDLLOVERRIDES="winemenubuilder.exe=d"
EOF
    
    log_info "Wine32 configured. Source /etc/profile.d/wine-test-32.sh or re-login"
}

# Print summary and usage instructions
print_summary() {
    echo ""
    echo "=============================================="
    log_info "Windows x86 cross-compilation setup complete!"
    echo "=============================================="
    echo ""
    log_info "Installed components:"
    echo "  - MinGW-w64 i686: $(i686-w64-mingw32-gcc --version | head -1)"
    echo "  - Wine32: $(wine --version)"
    echo ""
    log_info "Usage:"
    echo "  1. Build for Windows x86:"
    echo "     ./scripts/test-win32-cross.sh"
    echo ""
    echo "  2. Run tests with Wine:"
    echo "     WINEARCH=win32 wine build-cross-win32/tests/bin/test_unit_dap_json.exe"
    echo ""
    echo "  3. Run all Windows tests:"
    echo "     ./scripts/test-win32-wine.sh  (after building)"
    echo ""
    log_info "Environment configured in: /etc/profile.d/wine-test-32.sh"
    log_info "Logout and login again, or run: source /etc/profile.d/wine-test-32.sh"
}

# Main
main() {
    echo "=============================================="
    echo "DAP SDK Windows x86 Cross-Compilation Setup"
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
