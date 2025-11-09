# Parse template constructs: if/else/for/set/awk
# Supports:
#   {{#if VAR}}...{{/if}}
#   {{#if VAR}}...{{else}}...{{/if}}
#   {{#for item in ARRAY}}...{{/for}}
#   {{#set VAR=value}}...{{/set}}
#   {{VAR}} - variable output
#   {{#awk:...}} - inline AWK script
#   Nested constructs are now supported via construct stack
# Output format: type|start_line|end_line|condition/var/script|content

BEGIN {
    # Stack for nested constructs
    stack_size = 0
    line_num = 0
}

{
    line_num++
    line = $0
}

# Helper function to push construct onto stack
function push_construct(type, condition, start, brace_cnt) {
    stack_size++
    stack[stack_size, "type"] = type
    stack[stack_size, "condition"] = condition
    stack[stack_size, "start_line"] = start
    stack[stack_size, "brace_count"] = brace_cnt
    stack[stack_size, "content"] = ""
    stack[stack_size, "in_else"] = 0
    stack[stack_size, "else_start_line"] = 0
    stack[stack_size, "else_content"] = ""
}

# Helper function to pop construct from stack
function pop_construct() {
    if (stack_size > 0) {
        stack_size--
    }
}

# Helper function to get current construct type
function current_type() {
    if (stack_size > 0) {
        return stack[stack_size, "type"]
    }
    return ""
}

# Helper function to add content to current construct
function add_content(text) {
    if (stack_size > 0) {
        if (stack[stack_size, "in_else"]) {
            if (stack[stack_size, "else_content"] != "") {
                stack[stack_size, "else_content"] = stack[stack_size, "else_content"] "\n" text
            } else {
                stack[stack_size, "else_content"] = text
            }
        } else {
            if (stack[stack_size, "content"] != "") {
                stack[stack_size, "content"] = stack[stack_size, "content"] "\n" text
            } else {
                stack[stack_size, "content"] = text
            }
        }
    }
}

# Match {{#if VAR}}
/{{#if / {
        # Extract condition: {{#if VAR}}
        if (match(line, /{{#if[ \t]+([^}]+)}}/)) {
        condition = substr(line, RSTART + 6, RLENGTH - 8)
        gsub(/^[ \t]+|[ \t]+$/, "", condition)  # trim
            # Remove matched part from line
        remaining = line
        sub(/{{#if[ \t]+[^}]+}}/, "", remaining)
        # Push construct onto stack
        push_construct("if", condition, line_num, 2)
        if (remaining != "") {
            add_content(remaining)
        }
        next
        } else {
            # Multi-line if - extract condition from this line
            if (match(line, /{{#if[ \t]+([^}]+)/)) {
            condition = substr(line, RSTART + 6, RLENGTH - 6)
            gsub(/^[ \t]+|[ \t]+$/, "", condition)
            push_construct("if", condition, line_num, 2)
        next
        }
    }
}

# Match {{#for VAR in ARRAY}}
/{{#for / {
        # Extract: {{#for item in ARRAY}}
        if (match(line, /{{#for[ \t]+([^}]+)}}/)) {
        condition = substr(line, RSTART + 7, RLENGTH - 9)
        gsub(/^[ \t]+|[ \t]+$/, "", condition)
        remaining = line
        sub(/{{#for[ \t]+[^}]+}}/, "", remaining)
        push_construct("for", condition, line_num, 2)
        if (remaining != "") {
            add_content(remaining)
        }
        next
    } else {
        if (match(line, /{{#for[ \t]+([^}]+)/)) {
            condition = substr(line, RSTART + 7, RLENGTH - 7)
            gsub(/^[ \t]+|[ \t]+$/, "", condition)
            push_construct("for", condition, line_num, 2)
            next
        }
    }
}

# Match {{#set VAR=value}}
/{{#set / {
        # Extract: {{#set VAR=value}}
        if (match(line, /{{#set[ \t]+([^}]+)}}/)) {
        condition = substr(line, RSTART + 7, RLENGTH - 9)
        gsub(/^[ \t]+|[ \t]+$/, "", condition)
        remaining = line
        sub(/{{#set[ \t]+[^}]+}}/, "", remaining)
        push_construct("set", condition, line_num, 2)
        if (remaining != "") {
            add_content(remaining)
        }
        next
    } else {
        if (match(line, /{{#set[ \t]+([^}]+)/)) {
            condition = substr(line, RSTART + 7, RLENGTH - 7)
            gsub(/^[ \t]+|[ \t]+$/, "", condition)
            push_construct("set", condition, line_num, 2)
            next
        }
    }
}

# Match {{#include FILE}} - self-contained directive (no closing tag needed)
/{{#include / {
    # Extract: {{#include FILE}} - single line directive, output immediately
    if (match(line, /{{#include[ \t]+([^}]+)}}/)) {
        condition = substr(line, RSTART + 10, RLENGTH - 12)
        gsub(/^[ \t]+|[ \t]+$/, "", condition)
        remaining = line
        sub(/{{#include[ \t]+[^}]+}}/, "", remaining)
        # Output immediately as self-contained construct (no closing tag)
        print "include|" line_num "|" line_num "|" condition "|" remaining
        if (remaining != "") {
            # Process remaining content on same line
            $0 = remaining
            # Don't use next - continue processing remaining content
        } else {
            next
        }
    } else {
        # Multi-line include (shouldn't happen, but handle it)
        if (match(line, /{{#include[ \t]+([^}]+)/)) {
            condition = substr(line, RSTART + 10, RLENGTH - 10)
            gsub(/^[ \t]+|[ \t]+$/, "", condition)
            # Output as single-line construct
            print "include|" line_num "|" line_num "|" condition "|"
            next
        }
    }
}

# Match {{#awk:...}}
/{{#awk:/ {
        # Extract: {{#awk:...}}
        if (match(line, /{{#awk:(.*)}}/)) {
        condition = substr(line, RSTART + 8, RLENGTH - 10)
        remaining = line
        sub(/{{#awk:[^}]+}}/, "", remaining)
        # AWK script is complete on one line - output immediately
        print "awk|" line_num "|" line_num "|" condition "|" remaining
            next
        } else {
            # Multi-line AWK
            if (match(line, /{{#awk:(.*)/)) {
            condition = substr(line, RSTART + 8, RLENGTH - 8)
            push_construct("awk", condition, line_num, 2)
        next
        }
    }
}

# Match {{else}}
/{{else}}/ {
    if (stack_size > 0 && stack[stack_size, "type"] == "if" && !stack[stack_size, "in_else"]) {
        stack[stack_size, "in_else"] = 1
        stack[stack_size, "else_start_line"] = line_num
        remaining = line
        sub(/{{else}}/, "", remaining)
        if (remaining != "") {
            stack[stack_size, "else_content"] = remaining
        }
        next
    }
}

# Match closing }} for AWK constructs
/^}}$/ {
    if (stack_size > 0 && stack[stack_size, "type"] == "awk") {
        # Get construct data
        type = stack[stack_size, "type"]
        condition = stack[stack_size, "condition"]
        start = stack[stack_size, "start_line"]
        content = stack[stack_size, "content"]
        
        # Replace newlines in content with special marker
        gsub(/\n/, "\001", content)
        print type "|" start "|" line_num "|" condition "|" content
        
        # Pop construct from stack
        pop_construct()
        next
    }
}

# Match {{/if}}, {{/for}}, {{/set}}, {{/include}}
/{{(\/if|\/for|\/set|\/include)}}/ {
    if (stack_size > 0) {
        # Determine which construct type this closing tag matches
        closing_type = ""
        if (match(line, /{{\/(if|for|set|include)}}/)) {
            closing_type = substr(line, RSTART + 3, RLENGTH - 5)
        }
        
        # Check if closing tag matches current construct type
        current = current_type()
        if (closing_type == current) {
            # Count braces in closing tag
            brace_count = stack[stack_size, "brace_count"]
        for (i = 1; i <= length(line); i++) {
            char = substr(line, i, 1)
            if (char == "{") brace_count++
            else if (char == "}") brace_count--
        }
        
        if (brace_count == 0) {
                # End of construct - remove closing tag from line
                remaining = line
                sub(/{{(\/if|\/for|\/set|\/include)}}.*$/, "", remaining)
                if (remaining != "") {
                    add_content(remaining)
                }
                
                # Get construct data
                type = stack[stack_size, "type"]
                condition = stack[stack_size, "condition"]
                start = stack[stack_size, "start_line"]
                content = stack[stack_size, "content"]
                in_else = stack[stack_size, "in_else"]
                else_start = stack[stack_size, "else_start_line"]
                else_content = stack[stack_size, "else_content"]
                
                # Replace newlines in content with special marker
                gsub(/\n/, "\001", content)
                if (type == "if" && in_else) {
                gsub(/\n/, "\001", else_content)
                    print type "|" start "|" else_start "|" condition "|" content
                    print "else|" else_start "|" line_num "||" else_content
                } else {
                    print type "|" start "|" line_num "|" condition "|" content
                }
                
                # Pop construct from stack
                pop_construct()
                next
            } else {
                # Update brace count and add to content
                stack[stack_size, "brace_count"] = brace_count
                add_content(line)
                next
            }
        } else {
            # Closing tag doesn't match current construct - it's for a nested construct
            # Add it to content of current construct
            add_content(line)
            next
        }
    }
}

# Match {{VAR}} - simple variable output
/{{[^#\/][^}]*}}/ {
    if (stack_size == 0) {
        # Extract all variables from line
        pos = 1
        while (match(substr(line, pos), /{{[^#\/][^}]*}}/)) {
            var_name = substr(line, pos + RSTART + 1, RLENGTH - 3)
            gsub(/^[ \t]+|[ \t]+$/, "", var_name)
            print "var|" line_num "|" line_num "|" var_name "||"
            pos = pos + RSTART + RLENGTH - 1
        }
    }
}

# Default action: accumulate content into current construct
{
    if (stack_size > 0) {
        add_content(line)
        next
    }
}

END {
    # Output any remaining constructs on stack
    while (stack_size > 0) {
        type = stack[stack_size, "type"]
        condition = stack[stack_size, "condition"]
        start = stack[stack_size, "start_line"]
        content = stack[stack_size, "content"]
        in_else = stack[stack_size, "in_else"]
        else_start = stack[stack_size, "else_start_line"]
        else_content = stack[stack_size, "else_content"]
        
        # Replace newlines in content with special marker
        gsub(/\n/, "\001", content)
        if (type == "if" && in_else) {
            gsub(/\n/, "\001", else_content)
            print type "|" start "|" else_start "|" condition "|" content
            print "else|" else_start "|" line_num "||" else_content
        } else {
            print type "|" start "|" line_num "|" condition "|" content
        }
        
        pop_construct()
    }
}

