"""
DAP SDK Python Bindings
Proper Python bindings for DAP SDK with full crypto support
"""

from setuptools import setup, find_packages

setup(
    name="python-dap",
    version="3.0.0",
    description="DAP SDK Python Bindings",
    long_description=__doc__,
    author="Demlabs",
    author_email="support@demlabs.net",
    url="https://gitlab.demlabs.net/dap/python-dap",
    packages=find_packages(),
    package_data={
        "dap": ["*.pyi", "py.typed"],
        "dap.crypto": ["*.pyi"],
    },
    python_requires=">=3.7",
    install_requires=[
        "typing-extensions>=4.0.0",
    ],
    extras_require={
        "test": [
            "pytest>=6.0.0",
            "pytest-cov>=2.0.0",
            "pytest-xdist>=2.0.0",
        ],
        "dev": [
            "black>=21.0.0",
            "isort>=5.0.0",
            "mypy>=0.900",
            "pylint>=2.0.0",
        ],
    },
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: GNU Affero General Public License v3",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: Security :: Cryptography",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    keywords="dap sdk crypto blockchain",
    project_urls={
        "Documentation": "https://docs.demlabs.net/python-dap/",
        "Source": "https://gitlab.demlabs.net/dap/python-dap",
        "Tracker": "https://gitlab.demlabs.net/dap/python-dap/issues",
    },
) 