#!/bin/bash
# Generate PDF from Markdown using pandoc
# Install: sudo apt-get install pandoc texlive-latex-base texlive-fonts-recommended

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check pandoc
if ! command -v pandoc &> /dev/null; then
    echo "Error: pandoc not found"
    echo "Install: sudo apt-get install pandoc texlive-latex-base"
    exit 1
fi

echo "Generating PDFs from Markdown..."

# English version
if [ -f "DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_EN.md" ]; then
    echo "Converting English guide..."
    pandoc DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_EN.md \
        -o DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_EN.pdf \
        --pdf-engine=pdflatex \
        --toc \
        --toc-depth=3 \
        --number-sections \
        -V geometry:margin=2cm \
        -V fontsize=11pt \
        -V documentclass=article \
        -V colorlinks=true
    echo "✅ Created: DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_EN.pdf"
fi

# Russian version  
if [ -f "DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_RU.md" ]; then
    echo "Converting Russian guide..."
    pandoc DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_RU.md \
        -o DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_RU.pdf \
        --pdf-engine=xelatex \
        --toc \
        --toc-depth=3 \
        --number-sections \
        -V geometry:margin=2cm \
        -V fontsize=11pt \
        -V documentclass=article \
        -V mainfont="DejaVu Sans" \
        -V colorlinks=true
    echo "✅ Created: DAP_TEST_FRAMEWORK_COMPLETE_GUIDE_RU.pdf"
fi

echo ""
echo "✅ PDF generation complete!"
echo ""
echo "Generated files:"
ls -lh *.pdf 2>/dev/null || echo "No PDF files found"

