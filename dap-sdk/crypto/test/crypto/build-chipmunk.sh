#!/bin/bash

# Устанавливаем режим отладки и останавливаемся при ошибках
set -ex

# Получаем абсолютный путь директории скрипта
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$( cd "$DIR/../../../.." && pwd )"

# Компилируем с флагами отладки
gcc -g \
    -I$DIR/../../include \
    -I$DIR/../../../core/include \
    -I$ROOT_DIR/build-debug/dap-sdk/crypto/include \
    -I$DIR/../../../../test-framework \
    -o $ROOT_DIR/build-debug/bin/chipmunk_test \
    $DIR/chipmunk_test_main.c \
    $DIR/dap_enc_chipmunk_test.c \
    $DIR/../../src/dap_enc_chipmunk.c \
    -L$ROOT_DIR/build-debug/bin \
    -L$ROOT_DIR/build-debug/dap-sdk/crypto/lib \
    -L$ROOT_DIR/build-debug/dap-sdk/core/lib \
    -ldap_test -ldap_core -ldap_crypto -lm

# Добавляем библиотеки в путь поиска
export LD_LIBRARY_PATH=$ROOT_DIR/build-debug/bin:$ROOT_DIR/build-debug/dap-sdk/crypto/lib:$ROOT_DIR/build-debug/dap-sdk/core/lib:$LD_LIBRARY_PATH

# Запускаем программу
$ROOT_DIR/build-debug/bin/chipmunk_test 