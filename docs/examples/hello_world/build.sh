#!/bin/bash

# Скрипт сборки DAP SDK Hello World примера с использованием CMake

set -e

echo "=== DAP SDK Hello World Example Build Script ==="
echo

# Создание директории сборки
echo "📁 Creating build directory..."
mkdir -p build
cd build

# Конфигурация с CMake
echo "⚙️  Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo
echo "🔨 Building..."
make -j$(nproc)

echo
echo "✅ Build completed successfully!"
echo
echo "📍 Executable location: $(pwd)/dap_hello_world"
echo
echo "🚀 To run the example:"
echo "   cd build && ./dap_hello_world"
echo
echo "📦 To install system-wide:"
echo "   sudo make install"
echo
echo "🧹 To clean build files:"
echo "   rm -rf build/"



