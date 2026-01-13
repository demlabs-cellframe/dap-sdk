# C_IFDEF Construct Branch Parser
# Parses c_elif/c_else branches within c_ifdef constructs
# Uses generic branch parser for simplified implementation

@include "core/token_tree.awk"
@include "core/construct_registry.awk"
@include "core/tokenizer_core.awk"
@include "core/generic_branch_parser.awk"

# Parse c_ifdef construct with c_elif/c_else branches
function parse_c_ifdef_branches(content, token_id, start_line) {
    parse_branches_generic(content, token_id, "c_ifdef", start_line)
}

