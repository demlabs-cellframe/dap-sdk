#!/bin/bash

# Обновление версий CMake на 3.10 во всех файлах проекта
# Исключаем сторонние библиотеки (3rdparty) и Android (для него требуется повышенная версия)

# Обновление всех версий 3.0
echo "Обновление версий 3.0 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.0)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.0)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.0.2
echo "Обновление версий 3.0.2 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.0\.2)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.0\.2)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.1
echo "Обновление версий 3.1 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.1)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.1)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.2
echo "Обновление версий 3.2 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.2)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.2)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.4.1
echo "Обновление версий 3.4.1 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.4\.1)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.4\.1)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 2.8
echo "Обновление версий 2.8 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 2\.8)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 2\.8)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.8.2
echo "Обновление версий 3.8.2 на 3.10..."
find . -name "CMakeLists.txt" -o -name "*.cmake" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.8\.2)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.8\.2)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.9
echo "Обновление версий 3.9 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.9)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.9)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.12
echo "Обновление версий 3.12 на 3.10..."
find . -name "CMakeLists.txt" -o -name "*.cmake" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.12)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.12)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.13
echo "Обновление версий 3.13 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.13)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.13)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Обновление версии 3.22.1
echo "Обновление версий 3.22.1 на 3.10..."
find . -name "CMakeLists.txt" ! -path "*/3rdparty/*" ! -path "*/lib/*" ! -path "*/sources/android/*" -exec grep -l "cmake_minimum_required(VERSION 3\.22\.1)" {} \; | while read file; do
    echo "Обновление файла: $file"
    sed -i 's/cmake_minimum_required(VERSION 3\.22\.1)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Восстановление версии для Android
if [ -f "sources/android/CMakeLists.txt" ]; then
    echo "Восстановление повышенной версии для Android..."
    sed -i 's/cmake_minimum_required(VERSION 3\.10)/cmake_minimum_required(VERSION 3.22.1)/g' "sources/android/CMakeLists.txt"
fi

echo "Обновление версий CMake завершено." 