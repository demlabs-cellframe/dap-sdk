#!/bin/bash
# DAP SDK Test Framework - Documentation Builder
# Combines multiple markdown parts into single comprehensive PDF

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "DAP SDK Test Framework - Doc Builder"
echo "========================================="
echo ""

# Check pandoc
if ! command -v pandoc &> /dev/null; then
    echo "❌ Error: pandoc not found"
    echo ""
    echo "Install on Debian/Ubuntu:"
    echo "  sudo apt-get install pandoc texlive-latex-base texlive-fonts-recommended texlive-latex-extra"
    echo ""
    echo "Install on macOS:"
    echo "  brew install pandoc basictex"
    echo ""
    exit 1
fi

echo "✅ pandoc found: $(pandoc --version | head -1)"
echo ""

# Function to build guide
build_guide() {
    local lang=$1
    local parts_dir=$2
    local output_name=$3
    local pdf_engine=$4
    local extra_opts=$5
    
    echo "Building $lang guide..."
    echo "  Parts directory: $parts_dir"
    echo "  Output: $output_name"
    
    if [ ! -d "$parts_dir" ]; then
        echo "  ⚠️  Directory $parts_dir not found, skipping"
        return
    fi
    
    # Count parts (exclude _combined.md)
    local part_count=$(find "$parts_dir" -name "*.md" ! -name "_combined.md" | wc -l)
    if [ "$part_count" -eq 0 ]; then
        echo "  ⚠️  No markdown files found in $parts_dir, skipping"
        return
    fi
    
    echo "  Found $part_count parts"
    
    # Combine parts in order
    local combined="${parts_dir}/_combined.md"
    echo "  Combining parts..."
    
    > "$combined"  # Clear file
    
    for part in $(find "$parts_dir" -name "*.md" ! -name "_combined.md" | sort); do
        echo "    + $(basename $part)"
        cat "$part" >> "$combined"
        echo "" >> "$combined"
        echo "" >> "$combined"
    done
    
    echo "  Combined size: $(wc -l < "$combined") lines"
    
    # Generate PDF
    echo "  Generating PDF with $pdf_engine..."
    
    if [ -n "$extra_opts" ]; then
        pandoc "$combined" \
            -o "$output_name" \
            --pdf-engine="$pdf_engine" \
            --toc \
            --toc-depth=3 \
            --number-sections \
            -V geometry:margin=2.5cm \
            -V fontsize=11pt \
            -V documentclass=article \
            -V papersize=a4 \
            -V colorlinks=true \
            -V linkcolor=blue \
            -V urlcolor=blue \
            -V toccolor=black \
            -V mainfont="DejaVu Sans" \
            -V monofont="DejaVu Sans Mono"
    else
        pandoc "$combined" \
            -o "$output_name" \
            --pdf-engine="$pdf_engine" \
            --toc \
            --toc-depth=3 \
            --number-sections \
            -V geometry:margin=2.5cm \
            -V fontsize=11pt \
            -V documentclass=article \
            -V papersize=a4 \
            -V colorlinks=true \
            -V linkcolor=blue \
            -V urlcolor=blue \
            -V toccolor=black
    fi
    
    if [ -f "$output_name" ]; then
        local size=$(du -h "$output_name" | cut -f1)
        echo "  ✅ Created: $output_name ($size)"
    else
        echo "  ❌ Failed to create PDF"
        return 1
    fi
    
    # Cleanup combined file
    rm -f "$combined"
}

echo "========================================="
echo "Building English Guide"
echo "========================================="
echo ""

build_guide \
    "English" \
    "parts_en" \
    "DAP_TEST_FRAMEWORK_GUIDE_EN.pdf" \
    "xelatex" \
    "yes"

echo ""
echo "========================================="
echo "Building Russian Guide"
echo "========================================="
echo ""

build_guide \
    "Russian" \
    "parts_ru" \
    "DAP_TEST_FRAMEWORK_GUIDE_RU.pdf" \
    "xelatex" \
    "yes"

echo ""
echo "========================================="
echo "Build Complete!"
echo "========================================="
echo ""

# List generated PDFs
if ls *.pdf 1> /dev/null 2>&1; then
    echo "Generated PDFs:"
    ls -lh *.pdf | awk '{print "  " $9 " (" $5 ")"}'
else
    echo "⚠️  No PDF files generated"
fi

echo ""
echo "To rebuild: ./build_guide.sh"
echo ""

