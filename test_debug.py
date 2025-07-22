#!/usr/bin/env python3
"""
Simple test to debug python_dap module functions
"""

import sys
import os

# Force stderr to be unbuffered
sys.stderr = os.fdopen(sys.stderr.fileno(), 'w', 1)

print("🔍 ОТЛАДКА МОДУЛЯ python_dap")
print("=" * 60)

# Check available files
lib_path = os.path.join(os.getcwd(), "lib", "python_dap.cpython-311-x86_64-linux-gnu.so")
print(f"Библиотека существует: {os.path.exists(lib_path)}")
if os.path.exists(lib_path):
    stat = os.stat(lib_path)
    print(f"Размер файла: {stat.st_size} bytes")
    print(f"Время изменения: {stat.st_mtime}")

# Try import
print("\nИмпортируем python_dap...")
try:
    import python_dap
    print("✅ Импорт успешен")
    
    # Check for our functions
    print(f"\nВсего атрибутов: {len(dir(python_dap))}")
    
    # Look for SDK functions specifically
    sdk_funcs = [attr for attr in dir(python_dap) if 'sdk' in attr.lower()]
    print(f"SDK функции: {sdk_funcs}")
    
    # Look for init functions
    init_funcs = [attr for attr in dir(python_dap) if 'init' in attr.lower()]
    print(f"Init функции (первые 10): {init_funcs[:10]}")
    
    # Test if our new functions exist
    print(f"\nПроверка наших функций:")
    print(f"  dap_sdk_init: {'✅' if hasattr(python_dap, 'dap_sdk_init') else '❌'}")
    print(f"  dap_sdk_deinit: {'✅' if hasattr(python_dap, 'dap_sdk_deinit') else '❌'}")
    
    # Check if module has __file__ attribute
    if hasattr(python_dap, '__file__'):
        print(f"\nФайл модуля: {python_dap.__file__}")
    else:
        print("\nМодуль не имеет атрибута __file__")
        
except Exception as e:
    print(f"❌ Ошибка при импорте: {e}")
    import traceback
    traceback.print_exc()

print("\n" + "=" * 60)
print("ОТЛАДКА ЗАВЕРШЕНА") 