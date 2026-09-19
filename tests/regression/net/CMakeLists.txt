# Regression tests for networking modules
add_subdirectory(trans)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(stream)
endif()
