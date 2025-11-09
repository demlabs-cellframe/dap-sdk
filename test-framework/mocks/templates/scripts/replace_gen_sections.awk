# Replace generation section markers with generated content
# First file (ARGV[1]): processed sections file with format: type|start_line|end_line|generated_content
# Second file (ARGV[2]): original template file (used to determine line ranges)
# Third file (ARGV[3]): template file to modify (if not provided, use ARGV[2])
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
                # Convert start_line and end_line to numbers and store replacement
                start_num = current_start + 0
                end_num = current_end + 0
                replacements[start_num] = current_content
                replacement_ends[start_num] = end_num
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
        end_num = current_end + 0
        replacements[start_num] = current_content
        replacement_ends[start_num] = end_num
    }
    close(ARGV[1])
    
    # Determine which file to process
    # ARGV[2] is original template (for line ranges)
    # ARGV[3] is temp file to modify (if provided)
    template_for_ranges = ARGV[2]
    template_to_process = (ARGC > 3) ? ARGV[3] : ARGV[2]
    
    # Debug: print what we loaded
    debug_file = "/tmp/replace_gen_debug.txt"
    print "DEBUG: Loaded " length(replacements) " replacements" > debug_file
    for (start in replacements) {
        print "DEBUG: replacement[" start "] ends at " replacement_ends[start] > debug_file
    }
    print "DEBUG: template_to_process = " template_to_process > debug_file
    print "DEBUG: template_for_ranges = " template_for_ranges > debug_file
    print "DEBUG: ARGC = " ARGC > debug_file
    for (i = 1; i < ARGC; i++) {
        print "DEBUG: ARGV[" i "] = " ARGV[i] > debug_file
    }
    close(debug_file)
    
           # Read postproc sections from original template to get line ranges to skip
           # These sections should be completely removed (not replaced)
           postproc_line_num = 0
           while ((getline line < template_for_ranges) > 0) {
               postproc_line_num++
               if (line ~ /{{postproc:{{AWK:/) {
                   # Found postproc section start - mark it for removal
                   postproc_start = postproc_line_num
                   in_postproc = 1
                   postproc_brace_count = 2  # {{postproc:{{AWK:
               } else if (in_postproc) {
                   # Count braces to find end of postproc section
                   for (i = 1; i <= length(line); i++) {
                       char = substr(line, i, 1)
                       if (char == "{") {
                           postproc_brace_count++
                       } else if (char == "}") {
                           postproc_brace_count--
                           if (postproc_brace_count == 0) {
                               # End of postproc section
                               postproc_ends[postproc_start] = postproc_line_num
                               in_postproc = 0
                               break
                           }
                       }
                   }
               }
           }
           close(template_for_ranges)
           
           # Now read and process template file
           # Use template_to_process for reading (temp file), but line numbers match template_for_ranges
           line_num = 0
           skip_until = 0
           in_postproc_content = 0
           postproc_content_brace_count = 0
           
           output_lines = 0
           while ((getline line < template_to_process) > 0) {
               line_num++
               
               # Check if we're in a range that should be skipped (replaced section)
               if (skip_until > 0 && line_num <= skip_until) {
                   # Skip this line - it's part of a section that was replaced
                   # Reset skip_until if we've reached the end
                   if (line_num == skip_until) {
                       skip_until = 0
                   }
                   continue
               }
               
               # Check if this line is part of a postproc section that should be removed
               skip_postproc = 0
               for (start in postproc_ends) {
                   start_num = start + 0
                   end_num = postproc_ends[start] + 0
                   if (line_num >= start_num && line_num <= end_num) {
                       skip_postproc = 1
                       break
                   }
               }
               
               # Also check if line contains AWK code that looks like postproc content
               # This handles cases where markers were removed but content remained
               if (!skip_postproc && !in_postproc_content) {
                   if (line ~ /^# Post-process:/ || line ~ /^# This allows/ || line ~ /^# First, print/ || line ~ /^# Then append/ || line ~ /^# Read and append/) {
                       # Start of postproc content block
                       in_postproc_content = 1
                       postproc_content_brace_count = 0
                       skip_postproc = 1
                   }
               }
               
               if (in_postproc_content) {
                   # Count braces to find end of AWK code block
                   for (i = 1; i <= length(line); i++) {
                       char = substr(line, i, 1)
                       if (char == "{") {
                           postproc_content_brace_count++
                       } else if (char == "}") {
                           postproc_content_brace_count--
                           if (postproc_content_brace_count < 0) {
                               # End of AWK code block (closing braces)
                               in_postproc_content = 0
                               postproc_content_brace_count = 0
                               skip_postproc = 1
                               break
                           }
                       }
                   }
                   # Skip lines inside postproc content
                   skip_postproc = 1
               }
               
               if (skip_postproc) {
                   continue
               }
               
               # Check if this line should be replaced
               replaced = 0
               for (start in replacements) {
                   start_num = start + 0
                   end_num = replacement_ends[start] + 0
                   if (line_num == start_num) {
                       # Replace with generated content
                       print replacements[start]
                       output_lines++
                       replaced = 1
                       # Skip all lines until end_line (inclusive)
                       skip_until = end_num
                       break
                   }
               }
               
               if (!replaced) {
                   # Check if this line matches a marker pattern
                   if (index(line, "{{AWK:") == 1 || index(line, "{{#/bin/sh:") == 1 || index(line, "{{postproc:") == 1) {
                       # Marker found - check if we have replacement for this line number
                       for (start in replacements) {
                           start_num = start + 0
                           end_num = replacement_ends[start] + 0
                           if (line_num == start_num) {
                               print replacements[start]
                               replaced = 1
                               skip_until = end_num
                               break
                           }
                       }
                       # If no replacement, skip marker line (it's a postproc section that should be removed)
                       if (!replaced) {
                           # Marker but no replacement - skip (postproc section)
                           # Don't print the marker line
                       }
                   } else {
                       # Regular line - print as is
                       print line
                       output_lines++
                   }
               }
           }
           close(template_to_process)
    
    # Debug: print output stats
    print "DEBUG: Processed " line_num " lines, output " output_lines " lines" > debug_file
    close(debug_file)
    
    # Exit - we've processed everything
    exit
}

# This block should never execute since we process everything in BEGIN
{
    print
}
