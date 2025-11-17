# DAP SDK Test Framework Documentation

This directory contains comprehensive documentation for the DAP SDK Test Framework.

## Structure

```
docs/
├── parts_en/                    # English documentation parts
│   ├── 01_title.md             # Title page and metadata
│   ├── 02_overview.md          # Overview and introduction
│   ├── 03_quickstart.md        # Quick start guide
│   ├── 04_api_reference.md     # API reference
│   ├── 05_examples.md          # Code examples
│   ├── 06_glossary.md          # Glossary of terms
│   └── 07_troubleshooting.md   # Troubleshooting guide
│
├── parts_ru/                    # Russian documentation parts
│   ├── 01_title.md             # Титульная страница
│   ├── 02_overview.md          # Обзор и введение
│   ├── 03_quickstart.md        # Быстрый старт
│   ├── 04_api_reference.md     # Справочник API
│   ├── 05_examples.md          # Примеры кода
│   ├── 06_glossary.md          # Глоссарий
│   └── 07_troubleshooting.md   # Решение проблем
│
├── internal/                    # Internal documentation
│   ├── README.md               # Internal docs index
│   ├── mock_tpl_extensions_en.md # dap_tpl extensions (EN)
│   ├── mock_tpl_extensions_ru.md # dap_tpl extensions (RU)
│   └── DAP_MOCK_ASYNC.md       # Async mock implementation
│
├── build_guide.sh               # Build script (combines parts → PDF)
├── README.md                    # This file
│
└── Generated files (after build):
    ├── DAP_TEST_FRAMEWORK_GUIDE_EN.pdf  # English PDF (auto-generated)
    └── DAP_TEST_FRAMEWORK_GUIDE_RU.pdf  # Russian PDF (auto-generated)
```

## Building Documentation

### Prerequisites

Install pandoc and LaTeX:

**Ubuntu/Debian:**
```bash
sudo apt-get install pandoc texlive-latex-base texlive-fonts-recommended texlive-latex-extra
```

**macOS:**
```bash
brew install pandoc basictex
```

**Arch Linux:**
```bash
sudo pacman -S pandoc texlive-core texlive-fontsextra
```

### Build PDF

```bash
cd docs
./build_guide.sh
```

**Output:**
```
✅ DAP_TEST_FRAMEWORK_GUIDE_EN.pdf (English, ~30 pages)
✅ DAP_TEST_FRAMEWORK_GUIDE_RU.pdf (Russian, ~30 pages)
```

### Build Individual Language

**English only:**
```bash
pandoc parts_en/*.md -o guide_en.pdf --pdf-engine=pdflatex --toc
```

**Russian only:**
```bash
pandoc parts_ru/*.md -o guide_ru.pdf --pdf-engine=xelatex --toc -V mainfont="DejaVu Sans"
```

## Documentation Philosophy

### Modular Structure

Each part is self-contained and focused:

- **01_title.md** - Document metadata, version, copyright
- **02_overview.md** - High-level introduction, "why use this?"
- **03_quickstart.md** - Get started in 5-15 minutes
- **04_api_reference.md** - Complete API documentation
- **05_examples.md** - Real-world code examples
- **06_glossary.md** - Terms and definitions
- **07_troubleshooting.md** - Common issues and solutions

**Internal Documentation (`internal/`):**
- **mock_tpl_extensions_en.md** / **mock_tpl_extensions_ru.md** - dap_tpl extensions for mocking (internal)
- **DAP_MOCK_ASYNC.md** - Async mock execution (internal implementation)

### Benefits

1. **Easy to Maintain** - Update one part without touching others
2. **Reusable** - Parts can be included in other docs
3. **Version Control** - Clear diffs when updating specific sections
4. **Flexible Output** - Combine parts differently for different audiences

## Adding New Content

### Add New Part

1. Create new markdown file in `parts_en/` and `parts_ru/`
2. Name it with number prefix: `08_new_section.md`
3. Content will be included automatically in alphabetical order
4. Run `./build_guide.sh` to rebuild

### Update Existing Part

1. Edit the specific part file
2. Run `./build_guide.sh` to rebuild PDFs
3. Commit changes to git

## Online Viewing

For quick preview without PDF generation, read parts in order:

**English:**
```bash
cd parts_en
cat *.md | less
```

**Russian:**
```bash
cd parts_ru
cat *.md | less
```

## Automated Build

The `build_guide.sh` script:

1. Finds all `.md` files in `parts_en/` and `parts_ru/`
2. Sorts them alphabetically
3. Combines into single markdown file
4. Converts to PDF using pandoc
5. Cleans up temporary files
6. Reports file sizes

**Features:**
- ✅ Automatic part discovery
- ✅ Error handling
- ✅ Dependency checking
- ✅ Progress reporting
- ✅ Size reporting

## Contributing

When updating documentation:

1. Follow existing structure
2. Keep parts focused and concise
3. Add code examples for new features
4. Update both EN and RU versions
5. Rebuild and verify PDFs
6. All code in docs must compile

## See Also

- `../README.md` - Test framework main README
- `../mocks/README.md` - Mock framework documentation
- `../mocks/AUTOWRAP.md` - Auto-wrapper system
- `internal/` - Internal documentation (mock framework architecture, dap_tpl extensions)
- `../dap_tpl/docs/DAP_TPL_GUIDE.md` - dap_tpl template system guide

---

**Last Updated:** 2025-10-27  
**Maintained by:** Cellframe Core Team

