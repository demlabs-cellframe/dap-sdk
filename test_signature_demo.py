#!/usr/bin/env python3
"""
🔐 Демонстрация единой системы подписей DAP SDK

Показывает возможности новой системы с метаданными и расширенными функциями.
"""

from dap.crypto import (
    DapSign, DapSignType, DapSignMetadata,
    DapCryptoKey, DapKeyType,
    get_recommended_signature_types,
    get_deprecated_signature_types,
    check_signature_compatibility,
    quick_sign, quick_multi_sign
)

def demo_signature_metadata():
    """Демонстрация метаданных подписей"""
    print("🔍 Анализ типов подписей DAP SDK:")
    print("=" * 50)
    
    # Рекомендуемые типы подписей
    recommended = get_recommended_signature_types()
    print(f"✅ Рекомендуемые типы ({len(recommended)}):")
    for sign_type in recommended:
        metadata = DapSignMetadata.get_metadata(sign_type)
        print(f"  • {sign_type.value}: квантово-защищённая={metadata.get('quantum_secure')}, "
              f"мульти={metadata.get('multi_signature')}, агрегация={metadata.get('aggregated')}")
    
    print()
    
    # Устаревшие типы подписей
    deprecated = get_deprecated_signature_types()
    print(f"⚠️  Устаревшие типы ({len(deprecated)}):")
    for sign_type in deprecated:
        metadata = DapSignMetadata.get_metadata(sign_type)
        print(f"  • {sign_type.value}: квантово-уязвимая, "
              f"размер ключа={metadata.get('key_size')} бит")
    
    print()

def demo_single_signatures():
    """Демонстрация обычных подписей"""
    print("✍️  Демонстрация обычных подписей:")
    print("=" * 40)
    
    data = "Важные данные для подписи".encode('utf-8')
    
    # Создаем ключи разных типов
    dilithium_key = DapCryptoKey(DapKeyType.DILITHIUM)
    falcon_key = DapCryptoKey(DapKeyType.FALCON)
    
    # Создаем подписи
    dilithium_sign = quick_sign(dilithium_key, data, DapSignType.DILITHIUM)
    falcon_sign = quick_sign(falcon_key, data, DapSignType.FALCON)
    
    # Показываем информацию о подписях
    for name, signature in [("DILITHIUM", dilithium_sign), ("FALCON", falcon_sign)]:
        print(f"📝 {name} подпись:")
        print(f"   Квантово-защищённая: {signature.is_quantum_secure()}")
        print(f"   Устаревшая: {signature.is_deprecated()}")
        print(f"   Поддерживает мультиподпись: {signature.supports_multi_signature()}")
        print(f"   Поддерживает агрегацию: {signature.supports_aggregation()}")
        print()

def demo_multi_signatures():
    """Демонстрация мультиподписей"""
    print("👥 Демонстрация мультиподписей:")
    print("=" * 35)
    
    data = "Документ для мультиподписи".encode('utf-8')
    
    # Создаем несколько ключей
    keys = [
        DapCryptoKey(DapKeyType.DILITHIUM),
        DapCryptoKey(DapKeyType.FALCON),
        DapCryptoKey(DapKeyType.CHIPMUNK)
    ]
    
    # Композиционная мультиподпись
    try:
        composite_sign = quick_multi_sign(keys, data, DapSignType.COMPOSITE)
        print("✅ Композиционная мультиподпись создана")
        print(f"   Тип: {composite_sign.sign_type.value}")
        print(f"   Количество ключей: {len(composite_sign.keys)}")
        print(f"   Проверка: {composite_sign.verify_multi_signature(data)}")
    except Exception as e:
        print(f"❌ Ошибка композиционной подписи: {e}")
    
    print()
    
    # Агрегированная подпись (только с CHIPMUNK ключами)
    chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(3)]
    
    try:
        aggregated_sign = quick_multi_sign(chipmunk_keys, data, DapSignType.CHIPMUNK, aggregated=True)
        print("✅ Агрегированная подпись создана")
        print(f"   Тип: {aggregated_sign.sign_type.value}")
        print(f"   Количество ключей: {len(aggregated_sign.keys)}")
        print(f"   Поддерживает агрегацию: {aggregated_sign.supports_aggregation()}")
        print(f"   Проверка: {aggregated_sign.verify_multi_signature(data)}")
    except Exception as e:
        print(f"❌ Ошибка агрегированной подписи: {e}")

def demo_compatibility_check():
    """Демонстрация проверки совместимости"""
    print("🔧 Проверка совместимости типов подписей:")
    print("=" * 45)
    
    test_types = [DapSignType.DILITHIUM, DapSignType.CHIPMUNK, DapSignType.RSA]
    
    for sign_type in test_types:
        compatibility = check_signature_compatibility(sign_type)
        print(f"📋 {sign_type.value}:")
        for feature, supported in compatibility.items():
            icon = "✅" if supported else "❌"
            print(f"   {icon} {feature}")
        print()

def main():
    """Главная функция демонстрации"""
    print("🚀 Демонстрация единой системы подписей DAP SDK")
    print("=" * 60)
    print()
    
    try:
        demo_signature_metadata()
        demo_single_signatures()
        demo_multi_signatures()
        demo_compatibility_check()
        
        print("🎉 Демонстрация завершена успешно!")
        
    except Exception as e:
        print(f"💥 Ошибка во время демонстрации: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main() 