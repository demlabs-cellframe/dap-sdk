#!/bin/bash
# Build combined markdown files (without PDF generation)
# Useful for preview or when pandoc is not available

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "DAP SDK Test Framework - Markdown Builder"
echo "========================================="
echo ""

# Build English
echo "Building English combined markdown..."
if [ -d "parts_en" ]; then
    > DAP_TEST_FRAMEWORK_GUIDE_EN.md
    for part in $(ls parts_en/*.md | sort); do
        echo "  + $(basename $part)"
        cat "$part" >> DAP_TEST_FRAMEWORK_GUIDE_EN.md
        echo "" >> DAP_TEST_FRAMEWORK_GUIDE_EN.md
        echo "" >> DAP_TEST_FRAMEWORK_GUIDE_EN.md
    done
    lines=$(wc -l < DAP_TEST_FRAMEWORK_GUIDE_EN.md)
    echo "  ✅ Created: DAP_TEST_FRAMEWORK_GUIDE_EN.md ($lines lines)"
else
    echo "  ⚠️  parts_en/ not found"
fi

echo ""

# Build Russian
echo "Building Russian combined markdown..."
if [ -d "parts_ru" ]; then
    > DAP_TEST_FRAMEWORK_GUIDE_RU.md
    for part in $(ls parts_ru/*.md | sort); do
        echo "  + $(basename $part)"
        cat "$part" >> DAP_TEST_FRAMEWORK_GUIDE_RU.md
        echo "" >> DAP_TEST_FRAMEWORK_GUIDE_RU.md
        echo "" >> DAP_TEST_FRAMEWORK_GUIDE_RU.md
    done
    lines=$(wc -l < DAP_TEST_FRAMEWORK_GUIDE_RU.md)
    echo "  ✅ Created: DAP_TEST_FRAMEWORK_GUIDE_RU.md ($lines lines)"
else
    echo "  ⚠️  parts_ru/ not found"
fi

echo ""
echo "========================================="
echo "Build Complete!"
echo "========================================="
echo ""

echo "Generated files:"
ls -lh DAP_TEST_FRAMEWORK_GUIDE_*.md 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'

echo ""
echo "To generate PDFs (requires pandoc):"
echo "  ./build_guide.sh"
echo ""
echo "To preview:"
echo "  less DAP_TEST_FRAMEWORK_GUIDE_EN.md"
echo "  less DAP_TEST_FRAMEWORK_GUIDE_RU.md"
echo ""

