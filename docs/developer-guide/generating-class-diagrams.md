# Generating Class Diagrams

Assurance Forge uses `clang-uml` to generate selected C++ class diagrams.

The goal is not to generate one huge diagram for the whole codebase. Instead, diagrams should focus on specific subsystems.

## Install documentation dependencies

```powershell
python -m pip install -r requirements-docs.txt
```

## Serve documentation locally

```powershell
python -m mkdocs serve
```

Then open the local URL printed by MkDocs.

## Build documentation

```powershell
python -m mkdocs build --strict
```

## Generate compile database

`clang-uml` needs a CMake compilation database.

On Windows, use the Ninja generator:

```powershell
cmake -S . -B build-docs -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-docs
```

This should create:

```text
build-docs/compile_commands.json
```

## Generate Mermaid class diagrams

```powershell
clang-uml -g mermaid
```

Generated Mermaid files should appear under:

```text
docs/diagrams/generated/
```

## Diagram Strategy

Prefer small diagrams such as:

- Problem manager
- Review system
- SACM import/export
- Core domain model
- UI panels

Avoid repository-wide diagrams because they become unreadable quickly.