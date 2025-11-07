# Replace generation section markers with generated content
# First file (ARGV[1]): processed sections file with format: type|start_line|end_line|generated_content
# Second file (ARGV[2]): template file to process
# Output: template file with markers replaced by generated content

BEGIN {
    # Read processed sections file first
    # Handle multi-line generated_content by reading until next section or EOF
    current_section = ""
    current_start = ""
    current_end = ""
    current_content = ""
    in_content = 0
    
    while ((getline line < ARGV[1]) > 0) {
        # Check if this is a new section header
        if (line ~ /^(AWK_GEN|SH_GEN)\|/) {
            # Save previous section if any
            if (current_start != "") {
                # Convert start_line to number and store replacement
                start_num = current_start + 0
                replacements[start_num] = current_content
            }
            
            # Parse new section header
            pos1 = index(line, "|")
            if (pos1 == 0) continue
            
            pos2 = index(substr(line, pos1 + 1), "|")
            if (pos2 == 0) continue
            pos2 = pos2 + pos1
            
            pos3 = index(substr(line, pos2 + 1), "|")
            if (pos3 == 0) continue
            pos3 = pos3 + pos2
            
            current_section = substr(line, 1, pos1 - 1)
            current_start = substr(line, pos1 + 1, pos2 - pos1 - 1)
            current_end = substr(line, pos2 + 1, pos3 - pos2 - 1)
            current_content = substr(line, pos3 + 1)
            in_content = 1
        } else if (in_content) {
            # Continue accumulating content for current section
            current_content = current_content "\n" line
        }
    }
    # Save last section
    if (current_start != "") {
        start_num = current_start + 0
        replacements[start_num] = current_content
    }
    close(ARGV[1])
    
    # Debug: print what we loaded
    # for (start in replacements) {
    #     print "DEBUG: replacement[" start "] = " replacements[start] > "/dev/stderr"
    # }
    
    # Now read and process template file (ARGV[2])
    line_num = 0
    while ((getline line < ARGV[2]) > 0) {
        line_num++
        
        # Check if this line should be replaced
        replaced = 0
        for (start in replacements) {
            if ((start + 0) == line_num) {
                # Replace with generated content
                print replacements[start]
                replaced = 1
                break
            }
        }
        
        if (!replaced) {
            # Check if this line matches a marker pattern
            if (index(line, "{{AWK:") == 1 || index(line, "{{#/bin/sh:") == 1) {
                # Marker found - check if we have replacement for this line number
                for (start in replacements) {
                    if ((start + 0) == line_num) {
                        print replacements[start]
                        replaced = 1
                        break
                    }
                }
                # If no replacement, skip marker line
                if (!replaced) {
                    # Marker but no replacement - skip (shouldn't happen normally)
                    # Don't print the marker line
                }
            } else {
                # Regular line - print as is
                print line
            }
        }
    }
    close(ARGV[2])
    
    # Exit - we've processed everything
    exit
}

# This block should never execute since we process everything in BEGIN
{
    print
}
