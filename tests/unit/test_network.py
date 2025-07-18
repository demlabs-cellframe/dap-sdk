"""
Unit tests for DAP Network modules
Tests network client and server functionality
"""

import pytest
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent))


class TestDapClient:
    """Test cases for DapClient class"""
    
    def test_client_creation(self):
        """Test DapClient creation"""
        from dap.network.client import DapClient
        
        client = DapClient.create_new()
        assert client is not None
    
    def test_client_methods(self):
        """Test DapClient methods"""
        from dap.network.client import DapClient
        
        client = DapClient.create_new()
        assert hasattr(client, 'connect_to')
        assert hasattr(client, 'disconnect')
        assert hasattr(client, 'delete')
        assert callable(client.connect_to)
        assert callable(client.disconnect)
        assert callable(client.delete)
    
    def test_client_properties(self):
        """Test DapClient properties"""
        from dap.network.client import DapClient
        
        client = DapClient.create_new()
        assert hasattr(client, 'handle')
        assert hasattr(client, 'stage')
    
    def test_client_stage_methods(self):
        """Test client stage methods"""
        from dap.network.client import DapClient
        
        client = DapClient.create_new()
        assert hasattr(client, 'go_stage')
        assert hasattr(client, 'get_stage')


class TestDapServer:
    """Test cases for DapServer class"""
    
    def test_server_creation(self):
        """Test DapServer creation"""
        from dap.network.server import DapServer, DapServerType
        
        server = DapServer.create_server("test-server", DapServerType.HTTP)
        assert server is not None
    
    def test_server_methods(self):
        """Test DapServer methods"""
        from dap.network.server import DapServer, DapServerType
        
        server = DapServer.create_server("test-server", DapServerType.HTTP)
        assert hasattr(server, 'listen')
        assert hasattr(server, 'start')
        assert hasattr(server, 'stop')
        assert hasattr(server, 'delete')
        assert callable(server.listen)
        assert callable(server.start)
        assert callable(server.stop)
        assert callable(server.delete)
    
    def test_server_properties(self):
        """Test DapServer properties"""
        from dap.network.server import DapServer, DapServerType
        
        server = DapServer.create_server("test-server", DapServerType.HTTP)
        assert hasattr(server, 'handle')
        assert hasattr(server, 'name')
    
    def test_server_types(self):
        """Test server type constants"""
        from dap.network.server import DapServerType
        
        # Test that server types are defined
        assert hasattr(DapServerType, 'HTTP')
        assert hasattr(DapServerType, 'JSON_RPC')
        assert hasattr(DapServerType, 'TCP')
        assert hasattr(DapServerType, 'WEBSOCKET')


class TestDapStream:
    """Test cases for DapStream class"""
    
    def test_stream_creation(self):
        """Test DapStream creation"""
        from dap.network.stream import DapStream
        
        stream = DapStream.create_new()
        assert stream is not None
    
    def test_stream_methods(self):
        """Test DapStream methods"""
        from dap.network.stream import DapStream
        
        stream = DapStream.create_new()
        assert hasattr(stream, 'write')
        assert hasattr(stream, 'read')
        assert hasattr(stream, 'close')
        assert callable(stream.write)
        assert callable(stream.read)
        assert callable(stream.close)
    
    def test_stream_properties(self):
        """Test DapStream properties"""
        from dap.network.stream import DapStream
        
        stream = DapStream.create_new()
        assert hasattr(stream, 'handle')
        assert hasattr(stream, 'state')


class TestDapHttp:
    """Test cases for HTTP functionality"""
    
    def test_http_client_creation(self):
        """Test HTTP client creation"""
        from dap.network.http import DapHttpClient
        
        http_client = DapHttpClient()
        assert http_client is not None
    
    def test_http_client_methods(self):
        """Test HTTP client methods"""
        from dap.network.http import DapHttpClient
        
        http_client = DapHttpClient()
        assert hasattr(http_client, 'get')
        assert hasattr(http_client, 'post')
        assert hasattr(http_client, 'put')
        assert hasattr(http_client, 'delete')


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"]) 