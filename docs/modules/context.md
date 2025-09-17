# DAP Context Module (context/)

## Overview

The `context` module provides a centralized system for managing project context, including metadata, development standards, code templates, and navigation helpers. This module contains no executable code and acts as an information hub for developers and tools.

## 🎯 Purpose

The context module solves the following tasks:

- ✅ **Centralized storage of project metadata**
- ✅ **Standardization of development practices**
- ✅ **Provision of code templates**
- ✅ **Navigation helpers** for tools
- ✅ **Quality standards documentation**

## 📁 Module structure

```
context/
├── context.json              # Main project context
├── project_standards.json    # Project standards
├── coding_guidelines.json    # Coding guidelines
├── code_templates.json       # Code templates
├── structure.json            # Project structure
├── index.json               # Module index
├── modules/                 # Module configurations
│   ├── core.json
│   ├── crypto.json
│   ├── net.json
│   └── other.json
├── scripts/                 # Helper scripts
│   ├── load_full_context.sh
│   ├── load_module.sh
│   └── validate_context.sh
├── human_docs/              # Developer documentation
│   ├── architecture_guide.md
│   ├── coding_standards.md
│   ├── testing_guide.md
│   ├── security_practices.md
│   └── deployment_guide.md
└── tests/                   # Context tests
    └── validate_context.sh
```

## 📋 Key files

### `context.json` - Main project context

```json
{
  "version": "1.0",
  "created": "2025-01-03T15:00:00Z",
  "updated": "2025-06-05T14:00:00Z",
  "project": {
    "name": "DAP SDK",
    "description": "Decentralized Application Platform Software Development Kit",
    "repository": "dap-sdk.dev",
    "focus_area": "Quantum-resistant cryptography and blockchain infrastructure"
  }
}
```

**Purpose:**
- Define core project metadata
- Links to related context files
- Information about the tech stack
- Navigation helpers

### `project_standards.json` - Project standards

```json
{
  "documentation": {
    "language": "English for documentation and code",
    "structure": [
      "Title",
      "Description (## Description)",
      "Module Structure (## Module structure)",
      "Main Components",
      "Usage Examples (## Usage examples)",
      "Implementation Notes (## Implementation notes)",
      "See Also (## See also)"
    ]
  },
  "quality_requirements": {
    "code_examples": {
      "must_compile": true,
      "memory_safe": true,
      "follow_conventions": true,
      "include_error_handling": true
    }
  }
}
```

**Purpose:**
- Define documentation standards
- Code quality requirements
- Localization rules
- Acceptance criteria

### `coding_guidelines.json` - Coding guidelines

```json
{
  "naming_conventions": {
    "functions": {
      "prefix": "dap_",
      "style": "snake_case",
      "examples": ["dap_common_init()", "dap_config_get()"]
    },
    "variables": {
      "prefixes": {
        "local": "l_",
        "argument": "a_",
        "static": "s_",
        "global": "g_"
      }
    }
  },
  "memory_management": {
    "allocation": {
      "preferred": ["DAP_NEW", "DAP_NEW_Z", "DAP_MALLOC"],
      "always_check": true
    }
  }
}
```

**Purpose:**
- Coding style standardization
- Naming rules
- Safe memory management
- Logging standards

## 🔧 Usage

### Automatic context loading

```bash
# Load full project context
./context/scripts/load_full_context.sh

# Load a specific module context
./context/scripts/load_module.sh crypto

# Validate context
./context/scripts/validate_context.sh
```

### IDE integration

```json
// VS Code settings
{
  "dap.context.autoLoad": true,
  "dap.context.files": [
    "context/context.json",
    "context/coding_guidelines.json",
    "context/project_standards.json"
  ],
  "dap.coding.style": "context/coding_guidelines.json"
}
```

### Working with code templates

```json
// code_templates.json
{
  "function_template": {
    "prefix": "dap_func",
    "body": [
      "/**",
      " * @brief ${1:Brief description}",
      " * @param ${2:param_name} ${3:Parameter description}",
      " * @return ${4:Return description}",
      " */",
      "int dap_${5:function_name}(${6:parameters}) {",
      "    ${0:// Implementation}",
      "}"
    ]
  }
}
```

## 📚 Developer documentation

### `human_docs/architecture_guide.md`
Architecture guide for DAP SDK:
- System overview
- Component interaction
- Design principles
- Scalability

### `human_docs/coding_standards.md`
Detailed coding standards:
- Style and formatting
- Code documentation
- Testing
- Code review

### `human_docs/testing_guide.md`
Testing guide:
- Unit testing
- Integration testing
- Performance profiling
- Test automation

### `human_docs/security_practices.md`
Security practices:
- Secure coding
- Security audit
- Vulnerability protection
- Cryptographic practices

### `human_docs/deployment_guide.md`
Deployment guide:
- Build and install
- Configuration
- Monitoring
- Updates

## 🧪 Context validation

### Automatic validation

```bash
# Full context validation
./context/tests/validate_context.sh

# Validate specific components
./context/tests/validate_structure.sh
./context/tests/validate_standards.sh
./context/tests/validate_guidelines.sh
```

### Manual checks

```bash
# Validate JSON structure
python3 -m json.tool context/context.json

# Validate links
./scripts/validate_links.py context/

# Check compliance with standards
./scripts/check_standards.py src/ context/coding_guidelines.json
```

## 🔄 Tooling integration

### Git integration

```bash
# Pre-commit hooks for standards checks
#!/bin/bash
# .git/hooks/pre-commit

# Check coding guidelines compliance
./context/scripts/check_coding_standards.py

# Validate project structure
./context/scripts/validate_project_structure.py

# Validate documentation
./context/scripts/validate_documentation.py
```

### CI/CD integration

```yaml
# .github/workflows/context-validation.yml
name: Context Validation
on: [push, pull_request]

jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    - name: Validate Context
      run: ./context/tests/validate_context.sh
    - name: Check Coding Standards
      run: ./context/scripts/check_standards.py src/
    - name: Validate Documentation
      run: ./context/scripts/validate_docs.py docs/
```

### Интеграция с IDE

```json
// .vscode/settings.json
{
  "dap.context.enabled": true,
  "dap.context.path": "./context",
  "editor.codeActionsOnSave": {
    "source.fixAll": true
  },
  "editor.formatOnSave": true,
  "C_Cpp.clang_format_fallbackStyle": "file",
  "C_Cpp.clang_format_style": "context/coding_guidelines.json"
}
```

## 📊 Monitoring and statistics

### Metrics collection

```bash
# Project statistics
./context/scripts/project_stats.sh

# Standards compliance analysis
./context/scripts/standards_compliance.py

# Code quality metrics
./context/scripts/code_quality_metrics.py
```

### Reports

```json
// Example standards compliance report
{
  "timestamp": "2025-01-06T10:00:00Z",
  "project": "DAP SDK",
  "standards_compliance": {
    "coding_guidelines": {
      "score": 95.2,
      "total_files": 245,
      "compliant_files": 233,
      "violations": [
        {
          "file": "src/module.c",
          "line": 42,
          "rule": "naming_convention",
          "message": "Function name should use snake_case"
        }
      ]
    },
    "documentation": {
      "score": 87.8,
      "coverage": "92.3%",
      "missing_docs": 12
    }
  }
}
```

## 🔧 Context management

### Update context

```bash
# Update versions
./context/scripts/update_versions.sh

# Sync with repository
./context/scripts/sync_context.sh

# Archive old versions
./context/scripts/archive_context.sh
```

### Create a new module

```bash
# Create new module configuration
./context/scripts/create_module.sh my_module

# Add to index
./context/scripts/update_index.sh

# Validate
./context/tests/validate_context.sh
```

## 🎯 Best practices

### Organizing context

1. **Centralization** - keep context in one place
2. **Versioning** - track changes
3. **Validation** - automated correctness checks
4. **Documentation** - detailed descriptions

### Working with teams

```bash
# Onboard new developers
./context/scripts/onboard_developer.sh new_dev@example.com

# Standards audit
./context/scripts/audit_standards.sh --team=all

# Generate quality reports
./context/scripts/generate_quality_report.sh --period=monthly
```

### Continuous improvement

```json
// Quality improvement plan
{
  "continuous_improvement": {
    "code_quality": {
      "target_score": 95,
      "current_score": 92.3,
      "actions": [
        "Implement automated code review",
        "Add performance benchmarks",
        "Enhance error handling"
      ]
    },
    "documentation": {
      "target_coverage": 100,
      "current_coverage": 92.3,
      "actions": [
        "Complete API documentation",
        "Add usage examples",
        "Create troubleshooting guides"
      ]
    }
  }
}
```

## 📈 Metrics and KPIs

### Quality metrics

- **Coding standards compliance:** >95%
- **Documentation coverage:** >90%
- **Successful builds rate:** >98%
- **Average code review time:** <2 hours

### Performance metrics

- **Context load time:** <1 second
- **Validation time:** <30 seconds
- **Context size:** <10 MB
- **Module count:** Auto-detected

## 🚨 Troubleshooting

### Common issues

#### Issue: Context does not load

```bash
# Check file structure
ls -la context/

# Validate JSON files
python3 -c "import json; json.load(open('context/context.json'))"

# Check permissions
chmod +x context/scripts/*.sh
```

#### Issue: Coding standards violations

```bash
# Auto-fix
./context/scripts/auto_fix_standards.sh

# Manual check
./context/scripts/check_standards.py --fix src/module.c
```

#### Issue: Outdated context

```bash
# Update to latest version
./context/scripts/update_context.sh

# Sync with repository
./context/scripts/sync_from_repo.sh
```

## 🔗 Integration with external tools

### Jira/Confluence integration

```json
// Integration configuration
{
  "jira_integration": {
    "endpoint": "https://company.atlassian.net",
    "project_key": "DAP",
    "documentation_space": "DAPSDK",
    "auto_sync": true,
    "sync_interval": "1h"
  }
}
```

### SonarQube integration

```xml
<!-- sonar-project.properties -->
sonar.projectKey=dap-sdk
sonar.projectName=DAP SDK
sonar.projectVersion=2.3.0
sonar.sources=src/
sonar.tests=test/
sonar.sourceEncoding=UTF-8
sonar.coverage.exclusions=**/test/**,**/examples/**
sonar.cpd.exclusions=**/generated/**
```

### GitLab CI integration

```yaml
# .gitlab-ci.yml
stages:
  - validate
  - build
  - test
  - deploy

validate_context:
  stage: validate
  script:
    - ./context/tests/validate_context.sh
    - ./context/scripts/check_standards.py src/

build:
  stage: build
  script:
    - mkdir build && cd build
    - cmake .. -DENABLE_CONTEXT_VALIDATION=ON
    - make -j$(nproc)
```

## 📚 Additional resources

### Documentation
- [Architecture guide](human_docs/architecture_guide.md)
- [Coding standards](human_docs/coding_standards.md)
- [Testing guide](human_docs/testing_guide.md)
- [Security practices](human_docs/security_practices.md)

### Tools
- [Context management scripts](scripts/)
- [Code templates](code_templates.json)
- [Configuration examples](examples/)

### Community
- [Developers forum](https://forum.cellframe.net)
- [Telegram chat](https://t.me/cellframe_dev)
- [GitHub Issues](https://github.com/cellframe/libdap/issues)

---

## 🎯 Conclusion

The `context` module is a central component of the DAP SDK development ecosystem. It ensures:

- ✅ **Standardization** of development processes
- ✅ **Centralized** metadata management
- ✅ **Automation** of quality checks
- ✅ **Integration** with development tools
- ✅ **Continuous improvement** of code quality

**🚀 Proper setup and use of the context module ensures high quality and consistency of DAP SDK development!**



