#!/usr/bin/env python3
"""
🎯 Демонстрация единого конструктора DapSign

Показывает все режимы работы единого конструктора __init__.
"""

from dap.crypto import (
    DapSign, DapSignType, DapSignMetadata,
    DapCryptoKey, DapKeyType,
    quick_sign, quick_verify, quick_multi_sign,
    quick_composite_sign, quick_aggregated_sign,
    get_recommended_signature_types, get_deprecated_signature_types
)

def demo_single_constructor_modes():
    """Демонстрация всех режимов единого конструктора"""
    print("🎯 Единый конструктор DapSign - все режимы:")
    print("=" * 50)
    
    data = "Тестовые данные для подписи".encode('utf-8')
    
    # Создание ключей
    dilithium_key = DapCryptoKey(DapKeyType.DILITHIUM)
    falcon_key = DapCryptoKey(DapKeyType.FALCON)
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    mixed_keys = [dilithium_key, falcon_key]
    
    print("1️⃣  Режим 1: DapSign(data, key=key) - Одиночная подпись")
    single_sign = DapSign(data, key=dilithium_key)
    print(f"   Создана: {single_sign.sign_type.value}")
    print(f"   Квантово-защищённая: {single_sign.is_quantum_secure()}")
    
    print()
    print("2️⃣  Режим 2: DapSign(data, keys=keys) - Мультиподпись (авто)")
    
    # Смешанные ключи → COMPOSITE
    composite_auto = DapSign(data, keys=mixed_keys)
    print(f"   Смешанные ключи → {composite_auto.sign_type.value}")
    
    # Все CHIPMUNK → CHIPMUNK (агрегированная)
    aggregated_auto = DapSign(data, keys=chipmunk_keys)
    print(f"   Все CHIPMUNK → {aggregated_auto.sign_type.value} (агрегированная)")
    
    print()
    print("3️⃣  Режим 3: DapSign(data, keys=keys, sign_type=type) - Явный тип")
    composite_explicit = DapSign(data, keys=mixed_keys, sign_type=DapSignType.COMPOSITE)
    print(f"   Явно COMPOSITE → {composite_explicit.sign_type.value}")
    
    chipmunk_explicit = DapSign(data, keys=chipmunk_keys, sign_type=DapSignType.CHIPMUNK)
    print(f"   Явно CHIPMUNK → {chipmunk_explicit.sign_type.value}")
    
    print()
    print("4️⃣  Режим 4: DapSign(handle=handle, sign_type=type) - Обертка")
    # Создаем handle для демонстрации
    wrapped_sign = DapSign(handle=single_sign.handle, sign_type=single_sign.sign_type, keys=single_sign.keys)
    print(f"   Обертка над handle → {wrapped_sign.sign_type.value}")
    
    print()

def demo_real_dap_types():
    """Демонстрация реальных типов DAP SDK"""
    print("🔐 Реальные типы подписей DAP SDK:")
    print("=" * 40)
    
    data = "Тест типов".encode('utf-8')
    
    print("📋 Поддерживаемые типы:")
    for sign_type in DapSignType:
        metadata = DapSignMetadata.get_metadata(sign_type)
        status = "✅ Рекомендуется" if metadata.get('quantum_secure') and not metadata.get('deprecated') else \
                 "⚠️  Устарел" if metadata.get('deprecated') else \
                 "🧪 Экспериментальный"
        print(f"   {sign_type.value:12} - {status}")
    
    print()
    print("🚀 Рекомендуемые (квантово-защищённые):")
    recommended = get_recommended_signature_types()
    for t in recommended:
        print(f"   • {t.value}")
    
    print()
    print("🗑️  Устаревшие (квантово-уязвимые):")
    deprecated = get_deprecated_signature_types()
    for t in deprecated:
        print(f"   • {t.value}")
    
    print()

def demo_auto_detection_logic():
    """Демонстрация логики автоопределения"""
    print("🧠 Умная логика автоопределения типов:")
    print("=" * 45)
    
    data = "Логика определения".encode('utf-8')
    
    print("📝 Правила:")
    print("   • Один ключ → тип подписи = тип ключа")
    print("   • Несколько ключей, все CHIPMUNK → CHIPMUNK (агрегированная)")
    print("   • Несколько ключей, смешанные → COMPOSITE")
    print()
    
    test_cases = [
        ("DILITHIUM ключ", DapCryptoKey(DapKeyType.DILITHIUM)),
        ("FALCON ключ", DapCryptoKey(DapKeyType.FALCON)),
        ("PICNIC ключ", DapCryptoKey(DapKeyType.PICNIC)),
        ("BLISS ключ (устарел)", DapCryptoKey(DapKeyType.BLISS)),
        ("CHIPMUNK ключ", DapCryptoKey(DapKeyType.CHIPMUNK)),
    ]
    
    for description, key in test_cases:
        try:
            signature = DapSign(data, key=key)
            deprecated_status = " (УСТАРЕЛ)" if signature.is_deprecated() else ""
            print(f"✅ {description} → {signature.sign_type.value}{deprecated_status}")
        except Exception as e:
            print(f"❌ {description}: {e}")
    
    print()
    print("🔗 Мультиподписи:")
    
    # Различные комбинации ключей
    multi_test_cases = [
        ("2 CHIPMUNK ключа", [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]),
        ("3 CHIPMUNK ключа", [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(3)]),
        ("DILITHIUM + FALCON", [DapCryptoKey(DapKeyType.DILITHIUM), DapCryptoKey(DapKeyType.FALCON)]),
        ("PICNIC + BLISS", [DapCryptoKey(DapKeyType.PICNIC), DapCryptoKey(DapKeyType.BLISS)]),
        ("CHIPMUNK + DILITHIUM", [DapCryptoKey(DapKeyType.CHIPMUNK), DapCryptoKey(DapKeyType.DILITHIUM)]),
    ]
    
    for description, keys in multi_test_cases:
        try:
            signature = DapSign(data, keys=keys)
            aggregated = " (агрегированная)" if signature.supports_aggregation() else ""
            print(f"✅ {description} → {signature.sign_type.value}{aggregated}")
        except Exception as e:
            print(f"❌ {description}: {e}")
    
    print()

def demo_universal_verification():
    """Демонстрация универсальной верификации"""
    print("✅ Универсальная верификация через единый конструктор:")
    print("=" * 60)
    
    data = "Данные для проверки".encode('utf-8')
    
    # Создаем подписи разными способами
    key = DapCryptoKey(DapKeyType.DILITHIUM)
    keys = [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.PICNIC)]
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(2)]
    
    # Все через единый конструктор
    single = DapSign(data, key=key)
    composite = DapSign(data, keys=keys)
    aggregated = DapSign(data, keys=chipmunk_keys)
    
    print("🔑 Создание и верификация:")
    print(f"   Одиночная: {single.sign_type.value} → верификация: {single.verify(data)}")
    print(f"   Композиционная: {composite.sign_type.value} → верификация: {composite.verify(data)}")
    print(f"   Агрегированная: {aggregated.sign_type.value} → верификация: {aggregated.verify(data)}")
    
    print()
    print("⚡ Quick функции тоже используют единый конструктор:")
    
    quick_single = quick_sign(data, key=key)
    quick_multi = quick_multi_sign(data, keys)
    quick_comp = quick_composite_sign(data, keys)
    quick_agg = quick_aggregated_sign(data, chipmunk_keys)
    
    print(f"   quick_sign() → {quick_single.sign_type.value}")
    print(f"   quick_multi_sign() → {quick_multi.sign_type.value}")
    print(f"   quick_composite_sign() → {quick_comp.sign_type.value}")
    print(f"   quick_aggregated_sign() → {quick_agg.sign_type.value}")
    
    print()

def demo_constructor_flexibility():
    """Демонстрация гибкости конструктора"""
    print("🎛️  Гибкость единого конструктора:")
    print("=" * 40)
    
    data = "Гибкий конструктор".encode('utf-8')
    key = DapCryptoKey(DapKeyType.DILITHIUM)
    keys = [DapCryptoKey(DapKeyType.FALCON), DapCryptoKey(DapKeyType.PICNIC)]
    
    print("📝 Различные способы вызова:")
    
    # Позиционные аргументы
    sig1 = DapSign(data, key)
    print(f"   DapSign(data, key) → {sig1.sign_type.value}")
    
    # Именованные аргументы
    sig2 = DapSign(data=data, key=key)
    print(f"   DapSign(data=data, key=key) → {sig2.sign_type.value}")
    
    sig3 = DapSign(data, keys=keys)
    print(f"   DapSign(data, keys=keys) → {sig3.sign_type.value}")
    
    # Явное указание типа
    sig4 = DapSign(data, keys=keys, sign_type=DapSignType.COMPOSITE)
    print(f"   DapSign(data, keys=keys, sign_type=COMPOSITE) → {sig4.sign_type.value}")
    
    # Один ключ как список
    sig5 = DapSign(data, keys=[key])
    print(f"   DapSign(data, keys=[single_key]) → {sig5.sign_type.value}")
    
    print()
    print("🎯 Все варианты работают через один __init__ метод!")
    
    print()

def main():
    """Главная функция демонстрации"""
    print("🎯 Демонстрация единого конструктора DapSign")
    print("=" * 60)
    print()
    
    try:
        demo_single_constructor_modes()
        demo_real_dap_types()
        demo_auto_detection_logic()
        demo_universal_verification()
        demo_constructor_flexibility()
        
        print("🎉 Единый конструктор работает идеально!")
        print("    Все операции через один __init__ метод с умным определением параметров!")
        
    except Exception as e:
        print(f"💥 Ошибка во время демонстрации: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main() 