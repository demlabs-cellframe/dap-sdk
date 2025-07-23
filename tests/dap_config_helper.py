#!/usr/bin/env python3
"""
🔧 DAP Configuration Helper

Utilities for creating isolated application environments for DAP SDK.
Supports both testing and production application configurations.
Each application can have its own directory structure and config files.
"""

import os
import tempfile
import shutil
from pathlib import Path
from typing import Dict, Any, Optional
import pytest


class DapApplicationEnvironment:
    """
    Isolated application environment for DAP SDK
    
    Creates directory structure and configuration files for applications,
    ensuring complete isolation between different applications or test runs.
    """
    
    def __init__(self, app_name: str = "dap_app", is_test: bool = False):
        """
        Initialize application environment
        
        Args:
            app_name: Name of the application
            is_test: If True, creates temporary test environment
                    If False, creates persistent application environment
        """
        self.app_name = app_name
        self.is_test = is_test
        self.temp_root = None
        self.paths = {}
        self._created_dirs = []
        
    def setup(self, base_dir: str = None) -> Dict[str, str]:
        """
        Create application environment and return paths for DAP SDK initialization
        
        Args:
            base_dir: Base directory for application (if None, uses temp for tests)
        
        Returns:
            Dictionary with paths for dap_sdk_init:
            - app_name: Application name
            - working_dir: Application working directory  
            - config_dir: Application config directory
            - temp_dir: Application temp directory
            - log_file: Application log file path
        """
        if self.is_test or base_dir is None:
            # Create unique temporary directory for tests
            self.temp_root = tempfile.mkdtemp(prefix=f"{self.app_name}_")
            app_root = self.temp_root
            self._created_dirs.append(self.temp_root)
        else:
            # Use provided base directory for persistent applications
            app_root = os.path.join(base_dir, self.app_name)
            os.makedirs(app_root, exist_ok=True)
        
        # Create subdirectories
        self.paths = {
            'app_name': self.app_name,
            'working_dir': app_root,
            'config_dir': os.path.join(app_root, 'etc'),
            'temp_dir': os.path.join(app_root, 'tmp'),
            'log_file': os.path.join(app_root, 'var', 'log', f'{self.app_name}.log'),
        }
        
        # Create all required directories
        for dir_key in ['config_dir', 'temp_dir']:
            os.makedirs(self.paths[dir_key], exist_ok=True)
            
        # Create log directory
        log_dir = os.path.dirname(self.paths['log_file'])
        os.makedirs(log_dir, exist_ok=True)
        
        # Create application configuration files
        self._create_app_configs()
        
        return self.paths
    
    def _create_app_configs(self):
        """Create configuration files for the application"""
        config_dir = self.paths['config_dir']
        
        # Create main dap.conf
        dap_conf_path = os.path.join(config_dir, 'dap.conf')
        with open(dap_conf_path, 'w') as f:
            config_type = "testing" if self.is_test else "production"
            f.write(f"""# DAP Configuration for {self.app_name}
[global]
app_name={self.app_name}
working_dir={self.paths['working_dir']}
temp_dir={self.paths['temp_dir']}
log_file={self.paths['log_file']}
environment={config_type}

[log]
level=DEBUG
file={self.paths['log_file']}
console=true

[application]
name={self.app_name}
isolation={str(self.is_test).lower()}
""")
        
        # Create network config
        network_conf_path = os.path.join(config_dir, 'network.conf')
        with open(network_conf_path, 'w') as f:
            if self.is_test:
                # Test network configuration
                f.write(f"""# Network Configuration for {self.app_name} (Test Mode)
[network]
enabled=false
listen_port=0
test_mode=true
""")
            else:
                # Production network configuration
                f.write(f"""# Network Configuration for {self.app_name}
[network]
enabled=true
listen_port=8089
test_mode=false
auto_discovery=true
""")
            
        print(f"✅ Application configs created for {self.app_name} in {config_dir}")
    
    def get_dap_init_params(self, **overrides) -> Dict[str, Any]:
        """
        Get parameters for dap_sdk_init function
        
        Args:
            **overrides: Override any default parameters
        
        Returns:
            Dictionary with all parameters for dap_sdk_init
        """
        defaults = {
            'app_name': self.paths['app_name'],
            'working_dir': self.paths['working_dir'],
            'config_dir': self.paths['config_dir'],
            'temp_dir': self.paths['temp_dir'],
            'log_file': self.paths['log_file'],
            'events_threads': 2 if self.is_test else 4,
            'events_timeout': 5000,
            'debug_mode': self.is_test
        }
        
        # Apply overrides
        defaults.update(overrides)
        return defaults
    
    def cleanup(self):
        """Clean up application environment (only for test environments)"""
        if self.is_test and self.temp_root and os.path.exists(self.temp_root):
            try:
                shutil.rmtree(self.temp_root)
                print(f"✅ Cleaned up test environment for {self.app_name}: {self.temp_root}")
            except OSError as e:
                print(f"⚠️  Warning: Could not fully clean up {self.temp_root}: {e}")
        
        for dir_path in self._created_dirs:
            if os.path.exists(dir_path):
                try:
                    shutil.rmtree(dir_path)
                except OSError:
                    pass
    
    def __enter__(self):
        """Context manager entry"""
        self.setup()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.cleanup()
