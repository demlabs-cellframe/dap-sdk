# Regression tests for networking modules
add_subdirectory(trans)
# test_stream_keepalive is a standalone-only test: its CMakeLists compiles
# net/stream/stream/dap_stream.c directly (the test #includes the impl) and
# wires json-c only when built from its own directory. Wired into a tree
# build it links against nothing and fails - run it standalone instead:
#   cd tests/regression/net/stream && cmake -B build && cmake --build build
if(FALSE)
    add_subdirectory(stream)
endif()
