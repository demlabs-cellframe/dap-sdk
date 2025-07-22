file(REMOVE_RECURSE
  "libdap_sdk.a"
  "libdap_sdk.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/dap_sdk.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
