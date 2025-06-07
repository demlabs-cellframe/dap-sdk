#!/usr/bin/env python3
"""
Smart Layered Context CLI Tool
Инструмент для управления шаблонами, генерации проектов и работы с контекстной системой
"""

import argparse
import json
import os
import sys
import shutil
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional
import subprocess


class SLCManager:
    """Основной класс для управления Smart Layered Context системой"""
    
    def __init__(self, base_path: str = "."):
        self.base_path = Path(base_path)
        self.modules_path = self.base_path / "modules"
        self.context_path = self.base_path / "context.reflection"
        self.templates_path = self.base_path / "tools" / "templates"
        
    def list_templates(self, category: Optional[str] = None) -> Dict[str, List[str]]:
        """Показать доступные шаблоны"""
        templates = {
            "languages": [],
            "methodologies": [], 
            "tools": [],
            "projects": []
        }
        
        if not self.modules_path.exists():
            return templates
            
        for category_path in self.modules_path.iterdir():
            if category_path.is_dir():
                category_name = category_path.name
                if category_name not in templates:
                    templates[category_name] = []
                    
                for template_file in category_path.glob("**/*.json"):
                    relative_path = template_file.relative_to(category_path)
                    templates[category_name].append(str(relative_path))
        
        if category:
            return {category: templates.get(category, [])}
        return templates
    
    def get_template_info(self, template_path: str) -> Optional[Dict]:
        """Получить информацию о шаблоне"""
        full_path = self.modules_path / template_path
        if not full_path.exists():
            return None
            
        try:
            with open(full_path, 'r', encoding='utf-8') as f:
                template_data = json.load(f)
                return {
                    "name": template_data.get("template_info", {}).get("name", "Unknown"),
                    "description": template_data.get("template_info", {}).get("description", "No description"),
                    "version": template_data.get("version", "1.0.0"),
                    "domain": template_data.get("domain", "unknown"),
                    "applicability": template_data.get("template_info", {}).get("applicability", "Unknown"),
                    "target_projects": template_data.get("template_info", {}).get("target_projects", [])
                }
        except Exception as e:
            return {"error": f"Failed to read template: {e}"}
    
    def search_templates(self, query: str) -> Dict[str, List[str]]:
        """Поиск шаблонов по ключевым словам"""
        results = {}
        templates = self.list_templates()
        
        for category, template_list in templates.items():
            matching_templates = []
            for template_path in template_list:
                template_info = self.get_template_info(f"{category}/{template_path}")
                if template_info and not template_info.get("error"):
                    # Поиск в названии, описании и target_projects
                    search_text = (
                        template_info.get("name", "") + " " +
                        template_info.get("description", "") + " " +
                        " ".join(template_info.get("target_projects", []))
                    ).lower()
                    
                    if query.lower() in search_text:
                        matching_templates.append(template_path)
            
            if matching_templates:
                results[category] = matching_templates
        
        return results
    
    def create_project_from_template(self, template_path: str, project_name: str, output_dir: str = ".") -> bool:
        """Создать проект на основе шаблона"""
        template_info = self.get_template_info(template_path)
        if not template_info or template_info.get("error"):
            print(f"❌ Template not found or invalid: {template_path}")
            return False
        
        output_path = Path(output_dir) / project_name
        if output_path.exists():
            print(f"❌ Directory already exists: {output_path}")
            return False
        
        try:
            # Создаем структуру проекта
            output_path.mkdir(parents=True, exist_ok=True)
            
            # Копируем и адаптируем шаблон
            self._generate_project_structure(template_info, output_path, project_name)
            
            print(f"✅ Project '{project_name}' created successfully in {output_path}")
            print(f"📋 Template: {template_info['name']}")
            print(f"🎯 Domain: {template_info['domain']}")
            
            return True
            
        except Exception as e:
            print(f"❌ Failed to create project: {e}")
            if output_path.exists():
                shutil.rmtree(output_path)
            return False
    
    def _generate_project_structure(self, template_info: Dict, output_path: Path, project_name: str):
        """Генерация структуры проекта на основе шаблона"""
        domain = template_info.get("domain", "unknown")
        
        # Создаем базовую структуру
        (output_path / "src").mkdir(exist_ok=True)
        (output_path / "docs").mkdir(exist_ok=True)
        (output_path / "tests").mkdir(exist_ok=True)
        
        # README.md
        readme_content = self._generate_readme(template_info, project_name)
        with open(output_path / "README.md", "w", encoding="utf-8") as f:
            f.write(readme_content)
        
        # Специфичные для домена файлы
        if "python" in domain:
            self._create_python_project_files(output_path, project_name)
        elif "javascript" in domain:
            self._create_javascript_project_files(output_path, project_name)
        elif "obsidian" in domain:
            self._create_obsidian_project_files(output_path, project_name)
        elif "documentation" in domain:
            self._create_documentation_project_files(output_path, project_name)
        
        # .gitignore
        gitignore_content = self._generate_gitignore(domain)
        with open(output_path / ".gitignore", "w", encoding="utf-8") as f:
            f.write(gitignore_content)
    
    def _generate_readme(self, template_info: Dict, project_name: str) -> str:
        """Генерация README.md на основе шаблона"""
        return f"""# {project_name}

## Description
{template_info.get('description', 'Project created using Smart Layered Context template')}

## Template Information
- **Template**: {template_info.get('name', 'Unknown')}
- **Domain**: {template_info.get('domain', 'unknown')}
- **Version**: {template_info.get('version', '1.0.0')}

## Target Use Cases
{chr(10).join(f"- {project}" for project in template_info.get('target_projects', []))}

## Getting Started

### Prerequisites
Please refer to the template documentation for specific prerequisites.

### Installation
```bash
# Add installation instructions here
```

### Usage
```bash
# Add usage examples here
```

## Contributing
Please read the template guidelines for contribution standards.

## License
[Add license information]

---
*Generated using Smart Layered Context v2.1.0*
"""
    
    def _create_python_project_files(self, output_path: Path, project_name: str):
        """Создание специфичных для Python файлов"""
        # pyproject.toml
        pyproject_content = f"""[build-system]
requires = ["setuptools>=61.0", "wheel"]
build-backend = "setuptools.build_meta"

[project]
name = "{project_name.replace('_', '-')}"
version = "0.1.0"
description = "Python project created with Smart Layered Context"
authors = [
    {{name = "Your Name", email = "your.email@example.com"}},
]
dependencies = []

[project.optional-dependencies]
dev = [
    "pytest>=7.0",
    "black>=22.0",
    "isort>=5.0",
    "ruff>=0.1.0",
    "mypy>=1.0",
]

[tool.black]
line-length = 88
target-version = ['py39']

[tool.isort]
profile = "black"

[tool.ruff]
line-length = 88
target-version = "py39"

[tool.mypy]
python_version = "3.9"
strict = true
"""
        with open(output_path / "pyproject.toml", "w") as f:
            f.write(pyproject_content)
        
        # src структура
        src_path = output_path / "src" / project_name.replace('-', '_')
        src_path.mkdir(parents=True, exist_ok=True)
        
        # __init__.py
        with open(src_path / "__init__.py", "w") as f:
            f.write(f'"""\\n{project_name} - Created with Smart Layered Context\\n"""\\n\\n__version__ = "0.1.0"\\n')
        
        # main.py
        main_content = f'''"""
Main module for {project_name}
"""

def main():
    """Main entry point"""
    print("Hello from {project_name}!")

if __name__ == "__main__":
    main()
'''
        with open(src_path / "main.py", "w") as f:
            f.write(main_content)
    
    def _create_javascript_project_files(self, output_path: Path, project_name: str):
        """Создание специфичных для JavaScript файлов"""
        # package.json
        package_json = {
            "name": project_name.replace('_', '-'),
            "version": "0.1.0",
            "description": "JavaScript project created with Smart Layered Context",
            "main": "src/index.js",
            "scripts": {
                "start": "node src/index.js",
                "test": "jest",
                "lint": "eslint src/",
                "format": "prettier --write src/"
            },
            "devDependencies": {
                "eslint": "^8.0.0",
                "prettier": "^3.0.0",
                "jest": "^29.0.0"
            }
        }
        
        with open(output_path / "package.json", "w") as f:
            json.dump(package_json, f, indent=2)
        
        # src/index.js
        index_content = f'''/**
 * {project_name} - Main entry point
 * Created with Smart Layered Context
 */

console.log("Hello from {project_name}!");

module.exports = {{}};
'''
        with open(output_path / "src" / "index.js", "w") as f:
            f.write(index_content)
    
    def _create_obsidian_project_files(self, output_path: Path, project_name: str):
        """Создание Obsidian vault структуры"""
        # .obsidian folder
        obsidian_path = output_path / ".obsidian"
        obsidian_path.mkdir(exist_ok=True)
        
        # Core plugins
        core_plugins = {
            "file-explorer": True,
            "global-search": True,
            "switcher": True,
            "graph": True,
            "backlink": True,
            "outgoing-link": True,
            "tag-pane": True,
            "page-preview": True,
            "templates": True,
            "note-composer": True,
            "command-palette": True
        }
        
        with open(obsidian_path / "core-plugins.json", "w") as f:
            json.dump(core_plugins, f, indent=2)
        
        # Folder structure
        folders = ["01. Explore", "02. Learn", "03. Develop", "Resources", "Templates"]
        for folder in folders:
            (output_path / folder).mkdir(exist_ok=True)
        
        # Index note
        index_content = f"""# {project_name}

Welcome to your {project_name} knowledge vault!

## Structure

- **[[01. Explore]]** - High-level overview and orientation
- **[[02. Learn]]** - Detailed concepts and understanding  
- **[[03. Develop]]** - Practical implementation guides
- **[[Resources]]** - Reference materials and links
- **[[Templates]]** - Note templates for consistency

## Quick Start

1. Start by exploring the structure above
2. Use `[[double brackets]]` to create linked notes
3. Use tags like #concept #todo #important for organization
4. Leverage the graph view to see connections

---
*Created with Smart Layered Context v2.1.0*
"""
        with open(output_path / f"{project_name}.md", "w") as f:
            f.write(index_content)
    
    def _create_documentation_project_files(self, output_path: Path, project_name: str):
        """Создание структуры документации"""
        # Docs structure
        docs_folders = ["getting-started", "guides", "reference", "api"]
        for folder in docs_folders:
            (output_path / "docs" / folder).mkdir(parents=True, exist_ok=True)
        
        # mkdocs.yml для MkDocs
        mkdocs_config = f"""site_name: {project_name}
site_description: Documentation for {project_name}

nav:
  - Home: index.md
  - Getting Started:
    - Quick Start: getting-started/quickstart.md
    - Installation: getting-started/installation.md
  - Guides:
    - User Guide: guides/user-guide.md
    - Developer Guide: guides/developer-guide.md
  - Reference:
    - API Reference: reference/api.md
    - Configuration: reference/config.md

theme:
  name: material
  palette:
    - scheme: default
      primary: blue
      accent: blue
      toggle:
        icon: material/brightness-7
        name: Switch to dark mode
    - scheme: slate
      primary: blue
      accent: blue
      toggle:
        icon: material/brightness-4
        name: Switch to light mode

plugins:
  - search
  - mkdocstrings

markdown_extensions:
  - pymdownx.highlight
  - pymdownx.superfences
  - admonition
  - pymdownx.details
"""
        with open(output_path / "mkdocs.yml", "w") as f:
            f.write(mkdocs_config)
        
        # Index page
        index_content = f"""# {project_name}

Welcome to the {project_name} documentation!

## Quick Links

- [Quick Start](getting-started/quickstart.md)
- [Installation Guide](getting-started/installation.md)
- [User Guide](guides/user-guide.md)
- [API Reference](reference/api.md)

## About

This documentation was created using Smart Layered Context templates for maximum efficiency and consistency.

---
*Generated with Smart Layered Context v2.1.0*
"""
        with open(output_path / "docs" / "index.md", "w") as f:
            f.write(index_content)
    
    def _generate_gitignore(self, domain: str) -> str:
        """Генерация .gitignore на основе домена"""
        base_gitignore = """# IDE
.vscode/
.idea/
*.swp
*.swo

# OS
.DS_Store
Thumbs.db

# Logs
*.log
logs/

# Temporary files
*.tmp
*.temp
"""
        
        if "python" in domain:
            base_gitignore += """
# Python
__pycache__/
*.py[cod]
*$py.class
*.so
.Python
build/
develop-eggs/
dist/
downloads/
eggs/
.eggs/
lib/
lib64/
parts/
sdist/
var/
wheels/
*.egg-info/
.installed.cfg
*.egg

# Virtual environments
venv/
env/
ENV/

# Testing
.pytest_cache/
.coverage
htmlcov/
"""
        
        if "javascript" in domain:
            base_gitignore += """
# Node.js
node_modules/
npm-debug.log*
yarn-debug.log*
yarn-error.log*
.npm
.yarn-integrity

# Build outputs
dist/
build/
.next/
.nuxt/

# Testing
coverage/
.nyc_output/
"""
        
        if "obsidian" in domain:
            base_gitignore += """
# Obsidian
.obsidian/workspace
.obsidian/workspace.json
.obsidian/cache/
.obsidian/plugins/
.obsidian/community-plugins.json
"""
        
        return base_gitignore.strip()
    
    def validate_system(self) -> Dict[str, bool]:
        """Проверка целостности системы"""
        checks = {
            "modules_directory_exists": self.modules_path.exists(),
            "context_directory_exists": self.context_path.exists(),
            "templates_available": len(self.list_templates()) > 0,
            "reflection_system_active": (self.context_path / ".context").exists()
        }
        
        return checks
    
    def update_context_system(self):
        """Обновление контекстной системы"""
        if not self.context_path.exists():
            print("❌ Context system not found")
            return False
        
        try:
            # Обновляем timestamp
            current_time = datetime.now().isoformat() + "Z"
            
            task_file = self.context_path / ".context" / "tasks" / "current_meta_task.json"
            if task_file.exists():
                with open(task_file, 'r', encoding='utf-8') as f:
                    task_data = json.load(f)
                
                task_data["last_updated"] = current_time
                
                with open(task_file, 'w', encoding='utf-8') as f:
                    json.dump(task_data, f, indent=2, ensure_ascii=False)
                
                print("✅ Context system updated")
                return True
        except Exception as e:
            print(f"❌ Failed to update context system: {e}")
            return False


def main():
    parser = argparse.ArgumentParser(description="Smart Layered Context CLI Tool")
    subparsers = parser.add_subparsers(dest="command", help="Available commands")
    
    # List command
    list_parser = subparsers.add_parser("list", help="List available templates")
    list_parser.add_argument("--category", "-c", help="Filter by category")
    
    # Info command  
    info_parser = subparsers.add_parser("info", help="Get template information")
    info_parser.add_argument("template", help="Template path (e.g., languages/python/python_development.json)")
    
    # Search command
    search_parser = subparsers.add_parser("search", help="Search templates")
    search_parser.add_argument("query", help="Search query")
    
    # Create command
    create_parser = subparsers.add_parser("create", help="Create project from template")
    create_parser.add_argument("template", help="Template path")
    create_parser.add_argument("name", help="Project name")
    create_parser.add_argument("--output", "-o", default=".", help="Output directory")
    
    # Validate command
    validate_parser = subparsers.add_parser("validate", help="Validate system integrity")
    
    # Update command
    update_parser = subparsers.add_parser("update", help="Update context system")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return
    
    # Initialize manager
    slc = SLCManager()
    
    if args.command == "list":
        templates = slc.list_templates(args.category)
        print("📋 Available Templates:")
        print("=" * 50)
        
        for category, template_list in templates.items():
            if template_list:
                print(f"\n🗂️  {category.upper()}")
                print("-" * 30)
                for template in template_list:
                    print(f"   📄 {template}")
    
    elif args.command == "info":
        info = slc.get_template_info(args.template)
        if info and not info.get("error"):
            print(f"📄 Template Information")
            print("=" * 50)
            print(f"Name: {info['name']}")
            print(f"Description: {info['description']}")
            print(f"Version: {info['version']}")
            print(f"Domain: {info['domain']}")
            print(f"Applicability: {info['applicability']}")
            print(f"\nTarget Projects:")
            for project in info['target_projects']:
                print(f"  • {project}")
        else:
            print(f"❌ Template not found or invalid: {args.template}")
    
    elif args.command == "search":
        results = slc.search_templates(args.query)
        print(f"🔍 Search Results for '{args.query}':")
        print("=" * 50)
        
        if not results:
            print("No templates found matching your query.")
        else:
            for category, templates in results.items():
                print(f"\n🗂️  {category.upper()}")
                print("-" * 30)
                for template in templates:
                    print(f"   📄 {template}")
    
    elif args.command == "create":
        success = slc.create_project_from_template(args.template, args.name, args.output)
        if success:
            print(f"\n🎉 Next steps:")
            print(f"   cd {Path(args.output) / args.name}")
            print(f"   # Follow template-specific setup instructions")
    
    elif args.command == "validate":
        checks = slc.validate_system()
        print("🔧 System Validation:")
        print("=" * 50)
        
        all_good = True
        for check, status in checks.items():
            status_icon = "✅" if status else "❌"
            print(f"{status_icon} {check.replace('_', ' ').title()}")
            if not status:
                all_good = False
        
        if all_good:
            print("\n🎉 System is healthy!")
        else:
            print("\n⚠️  Some issues detected. Please check your installation.")
    
    elif args.command == "update":
        slc.update_context_system()


if __name__ == "__main__":
    main() 