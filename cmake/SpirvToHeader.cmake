# SpirvToHeader.cmake — convert a .spv file to a C header with uint32_t array
# Called by SpirvEmbed.cmake via cmake -P
#
# Variables: SPV_FILE, OUTPUT_FILE, VAR_NAME

file(READ "${SPV_FILE}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" SPV_HEX_LEN)

set(HEADER "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\nstatic const uint32_t ${VAR_NAME}[] = {\n")

math(EXPR NUM_WORDS "${SPV_HEX_LEN} / 8")
set(IDX 0)
set(LINE "    ")
set(WORDS_PER_LINE 0)

while(IDX LESS SPV_HEX_LEN)
    math(EXPR END "${IDX} + 8")
    if(END GREATER SPV_HEX_LEN)
        break()
    endif()
    string(SUBSTRING "${SPV_HEX}" ${IDX} 8 WORD_HEX)

    # SPIR-V is little-endian uint32_t, file bytes are in file order.
    # CMake file(READ HEX) gives bytes left-to-right, so we need to
    # swap byte pairs to get the correct uint32_t hex literal.
    string(SUBSTRING "${WORD_HEX}" 0 2 B0)
    string(SUBSTRING "${WORD_HEX}" 2 2 B1)
    string(SUBSTRING "${WORD_HEX}" 4 2 B2)
    string(SUBSTRING "${WORD_HEX}" 6 2 B3)
    set(WORD_LE "0x${B3}${B2}${B1}${B0}")

    math(EXPR NEXT_IDX "${IDX} + 8")
    if(NEXT_IDX LESS SPV_HEX_LEN)
        string(APPEND LINE "${WORD_LE}, ")
    else()
        string(APPEND LINE "${WORD_LE}")
    endif()

    math(EXPR WORDS_PER_LINE "${WORDS_PER_LINE} + 1")
    if(WORDS_PER_LINE EQUAL 6)
        string(APPEND HEADER "${LINE}\n")
        set(LINE "    ")
        set(WORDS_PER_LINE 0)
    endif()

    set(IDX ${NEXT_IDX})
endwhile()

if(WORDS_PER_LINE GREATER 0)
    string(APPEND HEADER "${LINE}\n")
endif()

string(APPEND HEADER "};\nstatic const size_t ${VAR_NAME}_size = sizeof(${VAR_NAME});\n")

file(WRITE "${OUTPUT_FILE}" "${HEADER}")
