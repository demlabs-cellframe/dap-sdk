"""
Unit tests for DAP Network modules
Tests network client and server functionality
"""

import pytest
import sys
import os

# Add the parent directory to the Python path to import test_init_helper
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from test_init_helper import init_test_dap_sdk


class TestDapClient:
    """Test cases for DapClient functionality using real DAP SDK functions"""
    
    def test_client_creation(self):
        """Test client creation using real DAP SDK"""
        import python_dap as dap
        
        # Проверяем что client функции доступны
        assert hasattr(dap, 'dap_client_new')
        assert hasattr(dap, 'dap_client_delete')
        assert callable(dap.dap_client_new)
        assert callable(dap.dap_client_delete)
    
    def test_client_methods(self):
        """Test client methods using real DAP SDK"""
        import python_dap as dap
        
        # Проверяем основные client функции
        client_funcs = ['dap_client_connect_to', 'dap_client_disconnect',
                       'dap_client_go_stage', 'dap_client_get_stage']
        
        for func_name in client_funcs:
            assert hasattr(dap, func_name), f"Missing client function: {func_name}"
            assert callable(getattr(dap, func_name)), f"Function not callable: {func_name}"
    
    def test_client_properties(self):
        """Test client stage constants"""
        import python_dap as dap
        
        # Проверяем client stage константы
        client_stages = ['DAP_CLIENT_STAGE_BEGIN', 'DAP_CLIENT_STAGE_ESTABLISHED',
                        'DAP_CLIENT_STAGE_DISCONNECTED', 'DAP_CLIENT_STAGE_ERROR',
                        'DAP_CLIENT_STAGE_ENC_INIT', 'DAP_CLIENT_STAGE_STREAM_CTL']
        
        for stage in client_stages:
            assert hasattr(dap, stage), f"Missing client stage: {stage}"
    
    def test_client_stage_methods(self):
        """Test client stage functionality"""
        import python_dap as dap
        
        # Проверяем stage management функции
        stage_funcs = ['dap_client_go_stage', 'dap_client_get_stage']
        
        for func_name in stage_funcs:
            assert hasattr(dap, func_name), f"Missing stage function: {func_name}"
            assert callable(getattr(dap, func_name)), f"Function not callable: {func_name}"


class TestDapServer:
    """Test cases for DapServer functionality using real DAP SDK functions"""
    
    def test_server_creation(self):
        """Test server creation using real DAP SDK"""
        import python_dap as dap
        
        # Проверяем что server функции доступны
        assert hasattr(dap, 'dap_server_new')
        assert hasattr(dap, 'dap_server_delete')
        assert callable(dap.dap_server_new)
        assert callable(dap.dap_server_delete)
        
        # Проверяем server инициализацию
        assert hasattr(dap, 'server_init')
        assert callable(dap.server_init)
    
    def test_server_methods(self):
        """Test server methods using real DAP SDK"""
        import python_dap as dap
        
        # Проверяем основные server функции
        server_funcs = ['dap_server_start', 'dap_server_stop', 'dap_server_listen',
                       'server_start', 'server_stop', 'server_listen']
        
        for func_name in server_funcs:
            assert hasattr(dap, func_name), f"Missing server function: {func_name}"
            assert callable(getattr(dap, func_name)), f"Function not callable: {func_name}"
    
    def test_server_properties(self):
        """Test server management functions"""
        import python_dap as dap
        
        # Проверяем server management функции
        management_funcs = ['server_get_all', 'server_delete', 'server_deinit']
        
        for func_name in management_funcs:
            assert hasattr(dap, func_name), f"Missing server management function: {func_name}"
            assert callable(getattr(dap, func_name)), f"Function not callable: {func_name}"
    
    def test_server_types(self):
        """Test server type constants"""
        import python_dap as dap
        
        # Проверяем server type константы
        server_types = ['DAP_SERVER_TYPE_HTTP', 'DAP_SERVER_TYPE_JSON_RPC', 
                       'DAP_SERVER_TYPE_TCP', 'DAP_SERVER_TYPE_WEBSOCKET']
        
        for server_type in server_types:
            assert hasattr(dap, server_type), f"Missing server type: {server_type}"


class TestDapStream:
    """Test cases for DapStream functionality using real DAP SDK functions"""
    
    def test_stream_creation(self):
        """Test stream session creation using real DAP SDK"""
        import python_dap as dap
        
        # Проверяем что функции stream доступны
        assert hasattr(dap, 'dap_stream_ch_new')
        assert hasattr(dap, 'dap_stream_ch_delete')
        assert callable(dap.dap_stream_ch_new)
        assert callable(dap.dap_stream_ch_delete)
    
    def test_stream_methods(self):
        """Test stream channel methods"""
        import python_dap as dap
        
        # Проверяем доступность основных stream функций
        stream_funcs = ['dap_stream_ch_pkt_write', 'dap_stream_ch_pkt_send', 
                       'dap_stream_ch_add_notifier', 'dap_stream_ch_del_notifier']
        
        for func_name in stream_funcs:
            assert hasattr(dap, func_name), f"Missing stream function: {func_name}"
            assert callable(getattr(dap, func_name)), f"Function not callable: {func_name}"
    
    def test_stream_properties(self):
        """Test stream constants and state properties"""
        import python_dap as dap
        
        # Проверяем stream состояния
        stream_states = ['DAP_STREAM_STATE_NEW', 'DAP_STREAM_STATE_CONNECTED', 
                        'DAP_STREAM_STATE_LISTENING', 'DAP_STREAM_STATE_CLOSED',
                        'DAP_STREAM_STATE_ERROR']
        
        for state in stream_states:
            assert hasattr(dap, state), f"Missing stream state: {state}"


class TestDapHttp:
    """Test cases for HTTP functionality using real DAP SDK"""
    
    def test_http_client_creation(self):
        """Test HTTP client creation using real DAP SDK"""
        import python_dap as dap
        
        # Проверяем что HTTP функции доступны
        assert hasattr(dap, 'dap_http_client_new')
        assert hasattr(dap, 'dap_http_client_delete')
        assert callable(dap.dap_http_client_new)
        assert callable(dap.dap_http_client_delete)
        
        # Проверяем инициализацию
        assert hasattr(dap, 'dap_http_client_init')
        assert callable(dap.dap_http_client_init)
    
    def test_http_client_methods(self):
        """Test HTTP client methods using real DAP SDK"""
        import python_dap as dap
        
        # Проверяем основные HTTP функции
        http_funcs = ['dap_http_client_request', 'dap_http_client_request_ex',
                     'dap_http_simple_request', 'dap_http_client_set_timeout',
                     'dap_http_client_get_response_code', 'dap_http_client_get_response_data']
        
        for func_name in http_funcs:
            assert hasattr(dap, func_name), f"Missing HTTP function: {func_name}"
            assert callable(getattr(dap, func_name)), f"Function not callable: {func_name}"
    
    def test_http_constants(self):
        """Test HTTP constants and method types"""
        import python_dap as dap
        
        # Проверяем HTTP методы
        http_methods = ['DAP_HTTP_METHOD_GET', 'DAP_HTTP_METHOD_POST', 
                       'DAP_HTTP_METHOD_PUT', 'DAP_HTTP_METHOD_DELETE',
                       'DAP_HTTP_METHOD_HEAD', 'DAP_HTTP_METHOD_OPTIONS']
        
        for method in http_methods:
            assert hasattr(dap, method), f"Missing HTTP method: {method}"
            
        # Проверяем HTTP статусы  
        http_statuses = ['DAP_HTTP_STATUS_OK', 'DAP_HTTP_STATUS_NOT_FOUND', 
                        'DAP_HTTP_STATUS_SERVER_ERROR']
        
        for status in http_statuses:
            assert hasattr(dap, status), f"Missing HTTP status: {status}"


if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"]) 