cmake_minimum_required(VERSION 3.10)

project (dap_core_darwin)

file(GLOB CORE_DARWIN_SRCS *.c)
file(GLOB CORE_DARWIN_HEADERS *.h)

if(MACOS)
    file(GLOB CORE_MACOS_SRCS macos/*.c)
    file(GLOB CORE_MACOS_HEADERS macos/*.h)
endif()

add_library(${PROJECT_NAME} STATIC ${CORE_DARWIN_SRCS} ${CORE_DARWIN_HEADERS}
    ${CORE_MACOS_SRCS} ${CORE_MACOS_HEADERS})

target_link_libraries(${PROJECT_NAME} dap_core)

# Add include directories for macOS-specific headers
if(MACOS)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/macos)
endif()

# Add macOS frameworks for network monitoring, CPU and memory monitoring
if(MACOS)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(SYSTEMCONFIGURATION_FRAMEWORK SystemConfiguration)
    target_link_libraries(${PROJECT_NAME} ${COREFOUNDATION_FRAMEWORK} ${SYSTEMCONFIGURATION_FRAMEWORK})
endif()

