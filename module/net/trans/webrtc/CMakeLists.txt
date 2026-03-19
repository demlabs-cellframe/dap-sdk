cmake_minimum_required(VERSION 3.10)
project(dap_trans_webrtc C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

if(DAP_WASM)
    set(WEBRTC_SRCS dap_net_trans_webrtc_wasm.c)
else()
    set(WEBRTC_SRCS
        dap_net_trans_webrtc_native.c
        dap_net_trans_webrtc_signaling.c
    )
endif()

file(GLOB WEBRTC_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${WEBRTC_SRCS} HEADERS ${WEBRTC_HDRS})

target_include_directories(${PROJECT_NAME} PUBLIC include)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_io dap_net_common dap_net_trans)

if(DAP_WASM)
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_crypto dap_json dap_stream dap_stream_ch dap_session)
else()
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_crypto dap_stream dap_stream_ch dap_session dap_http_server)

    find_library(LIBDATACHANNEL_LIB datachannel)
    find_path(LIBDATACHANNEL_INCLUDE rtc/rtc.h)
    if(LIBDATACHANNEL_LIB AND LIBDATACHANNEL_INCLUDE)
        target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_HAVE_LIBDATACHANNEL)
        target_include_directories(${PROJECT_NAME} PRIVATE ${LIBDATACHANNEL_INCLUDE})
        dap_link_libraries(${PROJECT_NAME} INTERFACE ${LIBDATACHANNEL_LIB})
        message(STATUS "WebRTC: libdatachannel found")
    else()
        message(STATUS "WebRTC: libdatachannel NOT found — native WebRTC transport disabled (signaling-only)")
    endif()
endif()

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${WEBRTC_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/net/trans/webrtc/
            ARCHIVE DESTINATION lib/dap/net/trans/webrtc/
            PUBLIC_HEADER DESTINATION include/dap/net/trans/webrtc/
    )
endif()
