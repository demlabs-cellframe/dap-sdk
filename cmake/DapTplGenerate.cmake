# DapTplGenerate.cmake
# General-purpose template code generation for DAP SDK using dap_tpl.
#
# Provides:
#   dap_tpl_generate(TEMPLATE t OUTPUT o [ARGS key=val ...])
#
# Unlike DapArchCodegen.cmake, this macro does NOT add architecture guards
# or inject arch-specific variables. It is a plain dap_tpl wrapper suitable
# for any kind of code generation: type-parametric headers, config files, etc.
#
# Requires DAP_TPL_DIR to be set before include().

if(NOT DEFINED DAP_TPL_DIR)
    message(FATAL_ERROR "DAP_TPL_DIR must be set before including DapTplGenerate.cmake")
endif()

set(_DAP_TPL_CODEGEN_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/dap_tpl_codegen.sh")
if(NOT EXISTS "${_DAP_TPL_CODEGEN_SCRIPT}")
    message(FATAL_ERROR "dap_tpl_codegen.sh not found at ${_DAP_TPL_CODEGEN_SCRIPT}")
endif()

# ============================================================================
# dap_tpl_generate(TEMPLATE t OUTPUT o [ARGS key=val ...])
#
# Process a single .tpl template through dap_tpl and write the result.
# KEY=VALUE pairs in ARGS are forwarded as template variables.
# KEY=@filepath pairs inline the file content into the variable.
# ============================================================================
function(dap_tpl_generate)
    cmake_parse_arguments(ARG "" "TEMPLATE;OUTPUT" "ARGS" ${ARGN})

    if(NOT ARG_TEMPLATE)
        message(FATAL_ERROR "dap_tpl_generate: TEMPLATE is required")
    endif()
    if(NOT ARG_OUTPUT)
        message(FATAL_ERROR "dap_tpl_generate: OUTPUT is required")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            "DAP_TPL_DIR=${DAP_TPL_DIR}"
            "DAP_TPL_TEMPLATE=${ARG_TEMPLATE}"
            "DAP_TPL_OUTPUT=${ARG_OUTPUT}"
            bash "${_DAP_TPL_CODEGEN_SCRIPT}" ${ARG_ARGS}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE  _err
    )

    if(NOT _rc EQUAL 0)
        message(WARNING "[DapTplGenerate] Failed to generate ${ARG_OUTPUT}:")
        message(WARNING "  exit code: ${_rc}")
        message(WARNING "  stdout: ${_out}")
        message(WARNING "  stderr: ${_err}")
    endif()
endfunction()
