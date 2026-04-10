# SpirvEmbed.cmake — compile GLSL → SPIR-V → C header (embedded bytecode)
#
# Usage:
#   spirv_embed(
#       SHADER  path/to/shader.comp
#       OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/shader_spv.h
#       VARNAME shader_spirv_data
#   )
#
# Produces a header with:
#   static const uint32_t <VARNAME>[] = { ... };
#   static const size_t <VARNAME>_size = sizeof(<VARNAME>);

function(spirv_embed)
    cmake_parse_arguments(SE "" "SHADER;OUTPUT;VARNAME" "" ${ARGN})

    get_filename_component(SE_SHADER_ABS "${SE_SHADER}" ABSOLUTE)
    get_filename_component(SE_OUTPUT_DIR "${SE_OUTPUT}" DIRECTORY)
    get_filename_component(SE_SHADER_NAME "${SE_SHADER}" NAME)

    set(SE_SPV_FILE "${SE_OUTPUT_DIR}/${SE_SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT "${SE_SPV_FILE}"
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V "${SE_SHADER_ABS}" -o "${SE_SPV_FILE}"
        DEPENDS "${SE_SHADER_ABS}"
        COMMENT "Compiling GLSL → SPIR-V: ${SE_SHADER_NAME}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${SE_OUTPUT}"
        COMMAND ${CMAKE_COMMAND}
            -DSPV_FILE=${SE_SPV_FILE}
            -DOUTPUT_FILE=${SE_OUTPUT}
            -DVAR_NAME=${SE_VARNAME}
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SpirvToHeader.cmake"
        DEPENDS "${SE_SPV_FILE}"
        COMMENT "Embedding SPIR-V → C header: ${SE_VARNAME}"
        VERBATIM
    )
endfunction()
