cmake_minimum_required(VERSION 3.10)
project (dap_core)

add_definitions("-D_GNU_SOURCE") 

if(UNIX)
  file(GLOB CORE_HEADERS 
      include/*.h 
      include/unix/*.h 
      include/unix/linux/*.h 
  )

  file(GLOB CORE_SRCS
    src/*.c
    src/etc/*.c
    src/rpmalloc/*.c
    src/unix/*.c
    src/unix/linux/*.c
    src/rpmalloc/*.c
  )
endif()


if(WIN32)

file(GLOB CORE_SRCS
    src/*.c
    src/etc/*.c
    src/rpmalloc/*.c
    src/win32/*.c
)

  file(GLOB CORE_HEADERS 
      include/*.h 
      src/win32/*.h 
  )

endif()

add_library(${PROJECT_NAME} STATIC ${CORE_SRCS} ${CORE_HEADERS})

#This paths will be used by project-dependent project libraries
target_include_directories(${PROJECT_NAME} INTERFACE include/ src/rpmalloc/)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)


if(WIN32)
  include_directories(include/)
endif()

if(UNIX)
    add_subdirectory(src/unix)
    set(CORE_LIBRARIES dap_core_unix)
    if(DARWIN)
        add_subdirectory(src/darwin)
        set(CORE_LIBRARIES ${CORE_LIBRARIES} dap_core_darwin)
    else()
        set(CORE_LIBRARIES ${CORE_LIBRARIES} rt)
    endif()
    if(ANDROID)
        add_subdirectory(src/android)
        set(CORE_LIBRARIES ${CORE_LIBRARIES} dap_core_android)
    endif()
endif()

if (WIN32)
    add_subdirectory(src/win32)
    set(CORE_LIBRARIES dap_core_win32)
endif()

if (BUILD_WITH_ZIP)
    set(CORE_LIBRARIES ${CORE_LIBRARIES} zip)
    add_definitions ("-DDAP_BUILD_WITH_ZIP")
endif()

target_link_libraries(${PROJECT_NAME} ${CORE_LIBRARIES})

if(BUILD_DAP_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${CORE_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/core
            ARCHIVE DESTINATION lib/dap/core
            PUBLIC_HEADER DESTINATION include/dap/core/
    )


    INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    #if (DAP_OS_LINUX)
    INSTALL(DIRECTORY src/unix/linux/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    INSTALL(DIRECTORY src/unix/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    #endif()

endif()