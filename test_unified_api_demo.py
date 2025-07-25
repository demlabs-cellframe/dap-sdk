#!/usr/bin/env python3
"""
🚀 Демонстрация унифицированного API системы подписей DAP SDK

Показывает новые возможности умных конструкторов с автоопределением типа.
"""

from dap.crypto import (
    DapSign, DapSignType, DapSignMetadata,
    DapCryptoKey, DapKeyType,
    quick_sign, quick_verify, quick_multi_sign, 
    quick_composite_sign, quick_aggregated_sign,
    get_recommended_signature_types
)

def demo_auto_detection():
    """Демонстрация автоматического определения типа подписи"""
    print("🧠 Автоматическое определение типа подписи:")
    print("=" * 50)
    
    data = "Тестовые данные для подписи".encode('utf-8')
    
    # Создание ключей
    dilithium_key = DapCryptoKey(DapKeyType.DILITHIUM)
    falcon_key = DapCryptoKey(DapKeyType.FALCON)
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(3)]
    mixed_keys = [dilithium_key, falcon_key]
    
    print("1️⃣  Одиночная подпись - тип определяется по ключу:")
    single_sign = DapSign.create(data, key=dilithium_key)
    print(f"   Ключ: {dilithium_key.key_type.value} → Подпись: {single_sign.sign_type.value}")
    
    print()
    print("2️⃣  Мультиподпись - тип определяется по составу ключей:")
    
    # Смешанные ключи → COMPOSITE
    composite_sign = DapSign.create(data, keys=mixed_keys)
    print(f"   Смешанные ключи → {composite_sign.sign_type.value}")
    
    # Все ключи CHIPMUNK → CHIPMUNK (агрегированная)
    chipmunk_sign = DapSign.create(data, keys=chipmunk_keys)
    print(f"   Все CHIPMUNK → {chipmunk_sign.sign_type.value} (агрегированная)")
    
    print()
    print("3️⃣  Возможности автоматически определяются:")
    print(f"   Одиночная - мультиподпись: {single_sign.supports_multi_signature()}")
    print(f"   Композиционная - агрегация: {composite_sign.supports_aggregation()}")
    print(f"   CHIPMUNK - агрегация: {chipmunk_sign.supports_aggregation()}")
    
    print()

def demo_simplified_constructors():
    """Демонстрация упрощенных конструкторов"""
    print("🎯 Упрощенные конструкторы без лишних параметров:")
    print("=" * 55)
    
    data = "Документ для подписи".encode('utf-8')
    
    # Создание ключей
    key = DapCryptoKey(DapKeyType.DILITHIUM)
    keys = [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.DILITHIUM)]
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    
    print("📝 Единый конструктор DapSign.create():")
    
    # Один ключ → одиночная подпись
    single = DapSign.create(data, key=key)
    print(f"   create(data, key=key) → {single.sign_type.value}")
    
    # Несколько ключей → автоматически определяется тип
    multi = DapSign.create(data, keys=keys)
    print(f"   create(data, keys=mixed) → {multi.sign_type.value}")
    
    agg = DapSign.create(data, keys=chipmunk_keys)
    print(f"   create(data, keys=chipmunk) → {agg.sign_type.value}")
    
    print()
    print("🎯 Явные конструкторы для точного контроля:")
    
    # Явное указание типа
    explicit_composite = DapSign.create_composite(data, keys)
    print(f"   create_composite() → {explicit_composite.sign_type.value}")
    
    explicit_agg = DapSign.create_aggregated(data, chipmunk_keys)
    print(f"   create_aggregated() → {explicit_agg.sign_type.value}")
    
    print()

def demo_universal_verification():
    """Демонстрация универсальной верификации"""
    print("✅ Универсальная верификация - один метод для всех:")
    print("=" * 55)
    
    data = "Данные для верификации".encode('utf-8')
    
    # Создание подписей
    key = DapCryptoKey(DapKeyType.DILITHIUM)
    keys = [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.DILITHIUM)]
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    
    # Создание подписей
    single = DapSign.create(data, key=key)
    composite = DapSign.create(data, keys=keys)
    aggregated = DapSign.create(data, keys=chipmunk_keys)
    
    print("🔑 Все подписи проверяются одним методом verify():")
    
    # Автоматическое использование сохранённых ключей
    print(f"   Одиночная: {single.verify(data)}")
    print(f"   Композиционная: {composite.verify(data)}")
    print(f"   Агрегированная: {aggregated.verify(data)}")
    
    print()
    print("⚡ Можно явно передать ключи для проверки:")
    print(f"   Одиночная с явным ключом: {single.verify(data, key=key)}")
    print(f"   Мультиподпись с явными ключами: {composite.verify(data, keys=keys)}")
    
    print()

def demo_simplified_quick_functions():
    """Демонстрация упрощенных quick функций"""
    print("⚡ Упрощенные quick функции:")
    print("=" * 35)
    
    data = "Быстрые подписи".encode('utf-8')
    
    # Создание ключей
    key = DapCryptoKey(DapKeyType.DILITHIUM)
    keys = [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.DILITHIUM)]
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    
    print("1️⃣  Универсальная quick_sign() без лишних параметров:")
    
    # Одиночная подпись
    single = quick_sign(data, key=key)
    print(f"   quick_sign(data, key=key) → {single.sign_type.value}")
    
    # Мультиподпись - автоопределение
    multi = quick_sign(data, keys=keys)
    print(f"   quick_sign(data, keys=mixed) → {multi.sign_type.value}")
    
    # Агрегированная - автоопределение
    agg = quick_sign(data, keys=chipmunk_keys)
    print(f"   quick_sign(data, keys=chipmunk) → {agg.sign_type.value}")
    
    print()
    print("2️⃣  Специализированные функции:")
    
    # Специализированные функции
    composite = quick_composite_sign(data, keys)
    print(f"   quick_composite_sign() → {composite.sign_type.value}")
    
    aggregated = quick_aggregated_sign(data, chipmunk_keys)
    print(f"   quick_aggregated_sign() → {aggregated.sign_type.value}")
    
    print()
    print("3️⃣  Универсальная верификация:")
    
    # Все проверяются одинаково
    print(f"   Все подписи проверяются: quick_verify(signature, data)")
    print(f"   Одиночная: {quick_verify(single, data)}")
    print(f"   Композиционная: {quick_verify(composite, data)}")
    print(f"   Агрегированная: {quick_verify(aggregated, data)}")
    
    print()

def demo_smart_detection_logic():
    """Демонстрация логики умного определения типов"""
    print("🧠 Логика умного определения типов:")
    print("=" * 40)
    
    data = "Тест логики определения".encode('utf-8')
    
    print("📋 Правила автоопределения:")
    print("   • Один ключ → тип подписи = тип ключа")
    print("   • Несколько ключей, все CHIPMUNK → CHIPMUNK (агрегированная)")
    print("   • Несколько ключей, смешанные → COMPOSITE")
    print()
    
    # Тестирование различных сценариев
    test_cases = [
        ("DILITHIUM ключ", DapCryptoKey(DapKeyType.DILITHIUM), None),
        ("FALCON ключ", DapCryptoKey(DapKeyType.FALCON), None),
        ("Смешанные ключи", None, [DapCryptoKey(DapKeyType.DILITHIUM), DapCryptoKey(DapKeyType.FALCON)]),
        ("Только CHIPMUNK", None, [DapCryptoKey(DapKeyType.CHIPMUNK), DapCryptoKey(DapKeyType.CHIPMUNK)]),
        ("Один CHIPMUNK в списке", None, [DapCryptoKey(DapKeyType.CHIPMUNK)]),
    ]
    
    for description, key, keys in test_cases:
        try:
            if key:
                signature = DapSign.create(data, key=key)
            else:
                signature = DapSign.create(data, keys=keys)
            
            print(f"✅ {description}:")
            print(f"   → {signature.sign_type.value}")
            print(f"   Мультиподпись: {signature.supports_multi_signature()}")
            print(f"   Агрегация: {signature.supports_aggregation()}")
            print()
        except Exception as e:
            print(f"❌ {description}: {e}")
            print()

def main():
    """Главная функция демонстрации"""
    print("🚀 Демонстрация упрощенного API с автоопределением типов")
    print("=" * 70)
    print()
    
    try:
        demo_auto_detection()
        demo_simplified_constructors()
        demo_universal_verification()
        demo_simplified_quick_functions()
        demo_smart_detection_logic()
        
        print("🎉 Демонстрация упрощенного API завершена успешно!")
        
    except Exception as e:
        print(f"💥 Ошибка во время демонстрации: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main() 