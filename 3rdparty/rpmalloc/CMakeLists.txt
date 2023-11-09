project(rpmalloc C)

add_definitions("-DENABLE_RPMALLOC")

add_library(${PROJECT_NAME} STATIC rpmalloc.h rpnew.h malloc.c rpmalloc.c)
target_link_libraries(${PROJECT_NAME} pthread)

target_include_directories(${PROJECT_NAME} INTERFACE rpmalloc.h)
message("-- rpmalloc")
