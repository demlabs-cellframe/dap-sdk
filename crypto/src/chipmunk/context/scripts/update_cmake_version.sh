#!/bin/bash

# Скрипт для автоматической замены версии CMake с 3.0 на 3.10 
# в файлах CMakeLists.txt в директории python-cellframe

find ./python-cellframe -name "CMakeLists.txt" -exec grep -l "cmake_minimum_required(VERSION 3\.0)" {} \; | while read file; do
    echo "Updating $file..."
    sed -i 's/cmake_minimum_required(VERSION 3\.0)/cmake_minimum_required(VERSION 3.10)/g' "$file"
done

# Также обрабатываем случай, когда project идет перед cmake_minimum_required
find ./python-cellframe -name "CMakeLists.txt" -exec grep -l "project.*\ncmake_minimum_required(VERSION 3\.0)" {} \; | while read file; do
    echo "Fixing order in $file..."
    sed -i '/^project.*/{N;s/\(^project.*\)\n\(cmake_minimum_required.*\)/cmake_minimum_required(VERSION 3.10)\n\1/g}' "$file"
done

echo "Update completed!" 