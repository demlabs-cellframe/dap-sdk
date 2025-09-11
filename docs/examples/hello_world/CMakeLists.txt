cmake_minimum_required(VERSION 3.10)
project(dap_hello_world C)

# Пути к DAP SDK (относительно директории примера)
set(DAP_SDK_ROOT "../../../../..")

# Создать исполняемый файл
add_executable(${PROJECT_NAME}
    main.c
)

# Включить директории DAP SDK
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DAP_SDK_ROOT}/core/include
    ${DAP_SDK_ROOT}/crypto/include
    ${DAP_SDK_ROOT}/io/include
    ${DAP_SDK_ROOT}/net/server/json_rpc/include
)

# Найти и подключить библиотеки
find_library(DAP_CORE_LIBRARY dap_core PATHS ${DAP_SDK_ROOT}/build/core)
find_library(DAP_COMMON_LIBRARY dap_common PATHS ${DAP_SDK_ROOT}/build/core)
find_library(DAP_TIME_LIBRARY dap_time PATHS ${DAP_SDK_ROOT}/build/core)

if(DAP_CORE_LIBRARY AND DAP_COMMON_LIBRARY)
    target_link_libraries(${PROJECT_NAME}
        ${DAP_COMMON_LIBRARY}
        ${DAP_CORE_LIBRARY}
        pthread
        m
    )
else()
    # Если библиотеки не найдены, попробуем собрать с исходников
    message(WARNING "Pre-built DAP libraries not found, trying to build from source")

    # Добавить исходные файлы DAP SDK
    target_sources(${PROJECT_NAME} PRIVATE
        ${DAP_SDK_ROOT}/core/src/dap_common.c
        ${DAP_SDK_ROOT}/core/src/dap_time.c
        ${DAP_SDK_ROOT}/core/src/dap_strfuncs.c
    )

    target_link_libraries(${PROJECT_NAME}
        pthread
        m
    )
endif()

# Опции компиляции
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O2
    -g
    -DDAP_OS_LINUX
)

# Установка
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Вывод информации о сборке
message(STATUS "DAP SDK Hello World Example configured:")
message(STATUS "  DAP SDK Root: ${DAP_SDK_ROOT}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  make          - Build the example")
message(STATUS "  make install  - Install to ${CMAKE_INSTALL_PREFIX}/bin")
message(STATUS "  make clean    - Clean build files")