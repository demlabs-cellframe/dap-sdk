#!/usr/bin/env python3
"""
Python DAP SDK Setup

Direct Python bindings and wrappers over DAP SDK (dap-sdk) functions.
This package provides low-level access to dap_* functions while maintaining
pythonic interfaces.
"""

from setuptools import setup, find_packages
import os
import subprocess
import shutil
from pathlib import Path

def read_file(filename):
    """Read file content."""
    try:
        with open(os.path.join(os.path.dirname(__file__), filename), 'r', encoding='utf-8') as f:
            return f.read()
    except FileNotFoundError:
        return ""

def get_version():
    """Get version from __init__.py."""
    try:
        with open('dap/__init__.py', 'r') as f:
            for line in f:
                if line.startswith('__version__'):
                    return line.split('=')[1].strip().strip('"\'')
    except FileNotFoundError:
        pass
    return "1.0.0"

def build_and_copy_native_library():
    """Build the native library using CMake and copy to Python package."""
    build_dir = Path("build_c")
    lib_dir = Path("lib")
    
    # Create lib directory if it doesn't exist
    lib_dir.mkdir(exist_ok=True)
    
    # Build using CMake if build directory exists
    if build_dir.exists():
        try:
            print("Building native library with CMake...")
            subprocess.run(["make", "python_dap", "-j4"], 
                          cwd=build_dir, check=True)
            
            # Copy the built library to lib directory  
            built_lib = build_dir / "lib" / "python_dap.so"
            target_lib = lib_dir / "python_dap.so"
            
            if built_lib.exists():
                shutil.copy2(built_lib, target_lib)
                print(f"Successfully copied {built_lib} to {target_lib}")
                return True
            else:
                print(f"Warning: Built library not found at {built_lib}")
                
        except subprocess.CalledProcessError as e:
            print(f"Make failed: {e}")
        except FileNotFoundError:
            print("Make not found - please install build tools")
    
    # Check if library already exists
    existing_lib = lib_dir / "python_dap.so"
    if existing_lib.exists():
        print(f"Using existing library: {existing_lib}")
        return True
    
    print("Warning: No native library found - package may not work correctly")
    return False

# Pre-build hook
print("Setting up native library...")
build_and_copy_native_library()

setup(
    name="python-dap",
    version=get_version(),
    description="Python bindings for DAP SDK",
    long_description=read_file("README.md"),
    long_description_content_type="text/markdown",
    author="Demlabs Team",
    author_email="support@demlabs.net",
    url="https://gitlab.demlabs.net/dap/python-dap",
    project_urls={
        "Bug Reports": "https://gitlab.demlabs.net/dap/python-dap/-/issues",
        "Source": "https://gitlab.demlabs.net/dap/python-dap",
        "Documentation": "https://docs.demlabs.net/python-dap/",
    },
    
    packages=find_packages(),
    package_data={
        'dap': ['py.typed'],
        '': ['lib/*.so']  # Include .so files
    },
    include_package_data=True,
    
    python_requires=">=3.8",
    install_requires=[
        # Core dependencies will be added when we know the actual requirements
    ],
    
    extras_require={
        'dev': [
            'pytest>=6.0',
            'pytest-cov',
            'black',
            'isort',
            'mypy',
            'flake8',
        ],
        'docs': [
            'sphinx>=4.0',
            'sphinx-rtd-theme',
            'myst-parser',
        ],
    },
    
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: System :: Distributed Computing",
        "Topic :: Security :: Cryptography",
    ],
    
    keywords="dap sdk blockchain demlabs crypto network",
    
    zip_safe=False,
) 