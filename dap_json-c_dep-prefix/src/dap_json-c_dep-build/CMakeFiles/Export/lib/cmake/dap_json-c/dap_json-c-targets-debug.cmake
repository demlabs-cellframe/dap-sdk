#----------------------------------------------------------------
# Generated CMake target import file for configuration "debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "dap_json-c::dap_json-c" for configuration "debug"
set_property(TARGET dap_json-c::dap_json-c APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dap_json-c::dap_json-c PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libdap_json-c.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS dap_json-c::dap_json-c )
list(APPEND _IMPORT_CHECK_FILES_FOR_dap_json-c::dap_json-c "${_IMPORT_PREFIX}/lib/libdap_json-c.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
