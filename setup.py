#!/usr/bin/env python3
"""
Python DAP SDK Setup

Setup script for building and installing native library components.
Metadata is now defined in pyproject.toml.
"""

from setuptools import setup
import os
import shutil
from pathlib import Path

def build_and_copy_native_library():
    """Find and copy the native library built by CMake."""
    lib_dir = Path("lib")
    
    # Create lib directory if it doesn't exist
    lib_dir.mkdir(exist_ok=True)
    
    # Possible CMake build directories
    cmake_build_dirs = ["build_new", "build", "build_c"]
    
    # Look for the compiled library in various places
    possible_libs = []
    
    # Check lib directory first (CMake output)
    for pattern in ["lib/python_dap*.so*", "lib/*.so"]:
        for lib_file in Path(".").glob(pattern):
            possible_libs.append(lib_file)
    
    # Check CMake build directories
    for build_dir in cmake_build_dirs:
        build_path = Path(build_dir)
        if build_path.exists():
            # Look for the compiled extension module
            for pattern in ["lib/python_dap*.so*", "*.so", "lib/*.so"]:
                for lib_file in build_path.glob(pattern):
                    if "python_dap" in lib_file.name:
                        possible_libs.append(lib_file)
    
    # Find the best library (newest)
    if possible_libs:
        # Sort by modification time, newest first
        best_lib = max(possible_libs, key=lambda p: p.stat().st_mtime)
        
        # Copy to both lib/ directory and dap/ package directory
        target_lib_dir = lib_dir / "python_dap.so"
        target_package_dir = Path("dap") / "python_dap.so"
        
        # Ensure dap directory exists
        Path("dap").mkdir(exist_ok=True)
        
        try:
            # Copy to lib/ directory for backwards compatibility
            shutil.copy2(best_lib, target_lib_dir)
            print(f"✅ Successfully copied {best_lib} to {target_lib_dir}")
            
            # Copy to dap/ package directory for wheel inclusion
            shutil.copy2(best_lib, target_package_dir)
            print(f"✅ Successfully copied {best_lib} to {target_package_dir}")
            return True
        except Exception as e:
            print(f"❌ Failed to copy {best_lib}: {e}")
    
    # Check if library already exists in target locations
    existing_lib = lib_dir / "python_dap.so"
    existing_package = Path("dap") / "python_dap.so"
    
    if existing_lib.exists() or existing_package.exists():
        print(f"✅ Using existing library")
        
        # Ensure both locations have the library
        if existing_lib.exists() and not existing_package.exists():
            Path("dap").mkdir(exist_ok=True)
            shutil.copy2(existing_lib, existing_package)
            print(f"✅ Copied {existing_lib} to package directory")
        elif existing_package.exists() and not existing_lib.exists():
            shutil.copy2(existing_package, existing_lib)
            print(f"✅ Copied {existing_package} to lib directory")
        
        return True
    
    print("❌ Warning: No native library found - package may not work correctly")
    print("   Please run 'cmake --build build' first to compile the extension")
    return False

# Pre-build hook
print("Setting up native library...")
build_and_copy_native_library()

# Setup call with minimal configuration - metadata is in pyproject.toml
setup() 