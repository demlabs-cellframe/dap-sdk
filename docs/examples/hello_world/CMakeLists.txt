cmake_minimum_required(VERSION 3.10)
project(dap_sdk_hello_world)

# Найти DAP SDK
find_package(DAP REQUIRED)

# Создать исполняемый файл
add_executable(hello_world main.c)

# Подключить DAP SDK
target_link_libraries(hello_world dap_core)

# Установить свойства
set_target_properties(hello_world PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Установка
install(TARGETS hello_world
    RUNTIME DESTINATION bin
)
