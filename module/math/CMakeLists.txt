cmake_minimum_required(VERSION 3.12)
project(dap_math)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

# Math module sources
file(GLOB MATH_HEADERS include/*.h)
file(GLOB MATH_SRCS src/*.c)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${MATH_SRCS} HEADERS ${MATH_HEADERS})

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Dependencies
target_link_libraries(${PROJECT_NAME} dap_core)

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${MATH_HEADERS} DESTINATION include/dap/math/)
endif()
