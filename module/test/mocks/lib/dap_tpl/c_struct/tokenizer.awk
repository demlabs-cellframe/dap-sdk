# C_STRUCT Construct Tokenizer
# Parses c_struct constructs: {{#c_struct StructName}}...{{/c_struct}}

@include "core/token_tree.awk"
@include "core/tokenizer_helpers.awk"

function parse_c_struct_construct(marker_text, pos, start_line) {
    return tokenize_simple_construct(marker_text, "c_struct", 8, "c_struct", start_line)
}

