#!/usr/bin/env python3
"""
Пример использования python-dap в составе плагина
Демонстрирует новую гибкую систему инициализации DAP SDK
"""

import python_dap
from dap.network.client import DapClient

def example_plugin_main():
    """Пример использования python-dap как плагина"""
    
    print("🔌 Пример инициализации python-dap в составе плагина")
    
    # ========================================================================
    # НОВАЯ ПЛАГИННАЯ API: Гибкая инициализация с параметрами
    # ========================================================================
    
    try:
        # Вариант 1: Полная кастомная инициализация (для production плагинов)
        print("\n1️⃣  Полная кастомная инициализация:")
        python_dap.dap_sdk_init(
            app_name="my_cellframe_plugin",           # Название приложения
            working_dir="/opt/cellframe-node",        # Рабочая директория
            config_dir="/opt/cellframe-node/etc",     # Директория конфигов
            temp_dir="/tmp/cellframe_plugin",         # Временная директория
            log_file="/var/log/cellframe_plugin.log", # Файл логов
            events_threads=2,                         # Количество потоков events
            events_timeout=15000,                     # Timeout events (мс)
            debug_mode=True                           # Режим отладки
        )
        print("✅ Кастомная инициализация успешна!")
        
        # Тестирование функциональности
        print("\n🔗 Тестирую создание клиента...")
        client = DapClient.create_new()
        print(f"✅ Client создан с handle: {client._client_handle}")
        
        # Очистка
        python_dap.dap_sdk_deinit()
        print("✅ DAP SDK деинициализован")
        
    except Exception as e:
        print(f"❌ Ошибка в полной инициализации: {e}")
    
    try:
        # Вариант 2: Минимальная инициализация (для простых плагинов)
        print("\n2️⃣  Минимальная инициализация с defaults:")
        python_dap.dap_sdk_init(
            app_name="simple_plugin",     # Только имя
            working_dir="/opt/dap"        # Только рабочая директория
            # Остальные параметры = default значения
        )
        print("✅ Минимальная инициализация успешна!")
        
        # Тестирование
        client = DapClient.create_new()
        print(f"✅ Client создан с handle: {client._client_handle}")
        
        # Очистка
        python_dap.dap_sdk_deinit()
        print("✅ DAP SDK деинициализован")
        
    except Exception as e:
        print(f"❌ Ошибка в минимальной инициализации: {e}")
    
    try:
        # Вариант 3: Полностью default инициализация (backwards compatibility)
        print("\n3️⃣  Default инициализация (backwards compatibility):")
        python_dap.dap_sdk_init()  # Все параметры по умолчанию
        print("✅ Default инициализация успешна!")
        
        # Тестирование
        client = DapClient.create_new()
        print(f"✅ Client создан с handle: {client._client_handle}")
        
        python_dap.dap_sdk_deinit()
        print("✅ DAP SDK деинициализован")
        
    except Exception as e:
        print(f"❌ Ошибка в default инициализации: {e}")

def plugin_architecture_example():
    """
    Пример интеграции в архитектуру плагинов CellFrame Node
    """
    print("\n" + "="*60)
    print("🏗️  АРХИТЕКТУРА ПЛАГИНОВ CELLFRAME NODE")
    print("="*60)
    
    # Симуляция инициализации плагина главной нодой
    cellframe_node_working_dir = "/opt/cellframe-node"
    plugin_name = "python_analytics_plugin"
    
    print(f"📂 CellFrame Node рабочая директория: {cellframe_node_working_dir}")
    print(f"🔌 Загружаемый плагин: {plugin_name}")
    
    try:
        # Инициализация по стандартам CellFrame Node
        python_dap.dap_sdk_init(
            app_name=plugin_name,
            working_dir=cellframe_node_working_dir,
            config_dir=f"{cellframe_node_working_dir}/etc",
            temp_dir=f"{cellframe_node_working_dir}/var/tmp",
            log_file=f"{cellframe_node_working_dir}/var/log/{plugin_name}.log",
            events_threads=1,  # Один поток для плагина
            events_timeout=10000,
            debug_mode=False   # Production режим
        )
        
        print("✅ Плагин инициализирован в архитектуре CellFrame Node")
        
        # Имитация работы плагина
        print("\n🔄 Плагин выполняет свои задачи...")
        client = DapClient.create_new()
        print(f"  └─ DAP Client готов: {client._client_handle}")
        
        # Деинициализация при выгрузке плагина
        python_dap.dap_sdk_deinit()
        print("✅ Плагин корректно деинициализирован")
        
    except Exception as e:
        print(f"❌ Ошибка в архитектуре плагинов: {e}")

if __name__ == "__main__":
    example_plugin_main()
    plugin_architecture_example()
    
    print("\n" + "="*60)
    print("🎉 ДЕМОНСТРАЦИЯ ЗАВЕРШЕНА!")
    print("💡 Теперь python-dap готов к использованию в плагинах!")
    print("="*60) 