cmake_minimum_required(VERSION 3.10)
project (dap_http_client)

set(HTTP_SRCS FILES *.c)
set(HTTP_HDRS FILES include/*.h)

if(WIN32)
  include_directories(../../../../os/win32/)
endif()

add_library(${PROJECT_NAME} STATIC ${HTTP_SRCS} ${HTTP_HDRS})

