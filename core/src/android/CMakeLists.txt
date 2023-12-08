cmake_minimum_required(VERSION 3.10)

project (dap_core_android C)

file(GLOB CORE_ANDROID_SRCS *.c)
file(GLOB CORE_ANDROID_HEADERS *.h)

add_library(${PROJECT_NAME} STATIC ${CORE_ANDROID_SRCS} ${CORE_ANDROID_HEADERS})

target_include_directories(${PROJECT_NAME} INTERFACE .)


