#!/usr/bin/env python3
"""
🚀 Демонстрация унифицированного API системы подписей DAP SDK

Показывает новые возможности умных конструкторов и единого интерфейса.
"""

from dap.crypto import (
    DapSign, DapSignType, DapSignMetadata,
    DapCryptoKey, DapKeyType,
    quick_sign, quick_verify, quick_multi_sign, 
    quick_composite_sign, quick_aggregated_sign,
    get_recommended_signature_types
)

def demo_unified_constructors():
    """Демонстрация унифицированных конструкторов"""
    print("🔧 Умные конструкторы DapSign:")
    print("=" * 40)
    
    data = "Тестовые данные для подписи".encode('utf-8')
    
    # Создание ключей
    dilithium_key = DapCryptoKey(DapKeyType.DILITHIUM)
    falcon_key = DapCryptoKey(DapKeyType.FALCON)
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(3)]
    
    print("1️⃣  Автоматическое определение типа подписи:")
    
    # Единый конструктор - одиночная подпись
    single_sign = DapSign.create(data, key=dilithium_key)
    print(f"   Одиночная подпись: {single_sign.sign_type.value}")
    print(f"   Квантово-защищённая: {single_sign.is_quantum_secure()}")
    
    # Единый конструктор - мультиподпись (композиционная)
    multi_keys = [dilithium_key, falcon_key]
    composite_sign = DapSign.create(data, keys=multi_keys)
    print(f"   Мультиподпись: {composite_sign.sign_type.value}")
    print(f"   Поддерживает мультиподпись: {composite_sign.supports_multi_signature()}")
    
    # Единый конструктор - агрегированная подпись
    agg_sign = DapSign.create(data, keys=chipmunk_keys, aggregated=True)
    print(f"   Агрегированная: {agg_sign.sign_type.value}")
    print(f"   Поддерживает агрегацию: {agg_sign.supports_aggregation()}")
    
    print()

def demo_specific_constructors():
    """Демонстрация специализированных конструкторов"""
    print("🎯 Специализированные конструкторы:")
    print("=" * 40)
    
    data = "Документ для мультиподписи".encode('utf-8')
    
    # Создание ключей
    keys = [DapCryptoKey(DapKeyType.DILITHIUM), DapCryptoKey(DapKeyType.FALCON)]
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    
    # Композиционная мультиподпись
    composite = DapSign.create_composite(data, keys)
    print(f"📝 Композиционная: {composite.sign_type.value}")
    print(f"   Ключей: {len(composite.keys)}")
    
    # Агрегированная подпись
    aggregated = DapSign.create_aggregated(data, chipmunk_keys)
    print(f"🔗 Агрегированная: {aggregated.sign_type.value}")
    print(f"   Ключей: {len(aggregated.keys)}")
    print(f"   Поддерживает агрегацию: {aggregated.supports_aggregation()}")
    
    print()

def demo_universal_verification():
    """Демонстрация универсальной верификации"""
    print("✅ Универсальная верификация:")
    print("=" * 35)
    
    data = "Данные для верификации".encode('utf-8')
    
    # Создание подписей
    key = DapCryptoKey(DapKeyType.DILITHIUM)
    keys = [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.DILITHIUM)]
    
    # Одиночная подпись
    single_sign = DapSign.create(data, key=key)
    single_valid = single_sign.verify(data)  # Использует сохранённый ключ
    print(f"🔑 Одиночная подпись валидна: {single_valid}")
    
    # Композиционная мультиподпись
    composite_sign = DapSign.create_composite(data, keys)
    composite_valid = composite_sign.verify(data)  # Использует сохранённые ключи
    print(f"👥 Композиционная подпись валидна: {composite_valid}")
    
    # Агрегированная подпись
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    agg_sign = DapSign.create_aggregated(data, chipmunk_keys)
    agg_valid = agg_sign.verify(data)  # Автоматическая верификация
    print(f"🔗 Агрегированная подпись валидна: {agg_valid}")
    
    print()

def demo_unified_quick_functions():
    """Демонстрация унифицированных quick функций"""
    print("⚡ Унифицированные quick функции:")
    print("=" * 40)
    
    data = "Быстрые подписи".encode('utf-8')
    
    # Создание ключей
    key = DapCryptoKey(DapKeyType.DILITHIUM)
    keys = [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.DILITHIUM)]
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    
    print("1️⃣  Универсальная quick_sign:")
    
    # Одиночная подпись
    single = quick_sign(data, key=key)
    print(f"   Одиночная: {single.sign_type.value}")
    
    # Мультиподпись через универсальную функцию
    multi = quick_sign(data, keys=keys)
    print(f"   Мультиподпись: {multi.sign_type.value}")
    
    # Агрегированная через универсальную функцию
    agg = quick_sign(data, keys=chipmunk_keys, aggregated=True)
    print(f"   Агрегированная: {agg.sign_type.value}")
    
    print()
    print("2️⃣  Специализированные quick функции:")
    
    # Специализированные функции
    composite = quick_composite_sign(data, keys)
    print(f"   Композиционная: {composite.sign_type.value}")
    
    aggregated = quick_aggregated_sign(data, chipmunk_keys)
    print(f"   Агрегированная: {aggregated.sign_type.value}")
    
    print()
    print("3️⃣  Универсальная верификация:")
    
    # Универсальная верификация
    print(f"   Одиночная валидна: {quick_verify(single, data)}")
    print(f"   Композиционная валидна: {quick_verify(composite, data)}")
    print(f"   Агрегированная валидна: {quick_verify(aggregated, data)}")
    
    print()

def demo_capability_checking():
    """Демонстрация проверки возможностей"""
    print("🔍 Универсальная проверка возможностей:")
    print("=" * 45)
    
    data = "Тест возможностей".encode('utf-8')
    
    # Создаем разные типы подписей
    signatures = [
        ("Одиночная DILITHIUM", DapSign.create(data, key=DapCryptoKey(DapKeyType.DILITHIUM))),
        ("Композиционная", DapSign.create_composite(data, [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.DILITHIUM)])),
        ("Агрегированная CHIPMUNK", DapSign.create_aggregated(data, [DapCryptoKey(DapKeyType.CHIPMUNK), DapCryptoKey(DapKeyType.CHIPMUNK)]))
    ]
    
    capabilities = ['quantum_secure', 'multi_signature', 'aggregated', 'ring_signature', 'zero_knowledge', 'deprecated']
    
    for name, signature in signatures:
        print(f"📋 {name}:")
        for cap in capabilities:
            status = "✅" if signature.check_capability(cap) else "❌"
            print(f"   {status} {cap}")
        print()

def main():
    """Главная функция демонстрации"""
    print("🚀 Демонстрация унифицированного API системы подписей")
    print("=" * 65)
    print()
    
    try:
        demo_unified_constructors()
        demo_specific_constructors()
        demo_universal_verification()
        demo_unified_quick_functions()
        demo_capability_checking()
        
        print("🎉 Демонстрация унифицированного API завершена успешно!")
        
    except Exception as e:
        print(f"💥 Ошибка во время демонстрации: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main() 