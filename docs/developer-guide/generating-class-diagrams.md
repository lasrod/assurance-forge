# Generating Class Diagrams

Assurance Forge uses `clang-uml` to generate selected C++ class diagrams.

The goal is not to generate one huge diagram for the whole codebase. Instead, diagrams should focus on specific subsystems.

## Install documentation dependencies

```powershell
python -m pip install -r requirements-docs.txt
```

## Download Mermaid JS

The docs site vendors Mermaid rather than loading it from a CDN. Download the pinned version once before serving or building locally:

```powershell
curl -fsSL https://cdn.jsdelivr.net/npm/mermaid@10.9.1/dist/mermaid.min.js `
     -o docs/javascripts/mermaid.min.js
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

## Install clang-uml

`clang-uml` is a standalone C++ tool and is not included in `requirements-docs.txt`.

- **Windows**: download the MSI installer from the [clang-uml releases page](https://github.com/bkryza/clang-uml/releases).
- **macOS**: `brew install clang-uml`
- **Ubuntu/Debian**: use the author's PPA:

  ```bash
  sudo add-apt-repository -y ppa:bkryza/clang-uml
  sudo apt-get update
  sudo apt-get install -y clang-uml
  ```

## Initialize submodules

The top-level CMake configure hard-errors when `external/safety-case-core-guidelines/dist/sccg.full.yaml` is missing. Initialize all submodules before generating the compile database:

```powershell
git submodule update --init --recursive
```

If the SCCG submodule is present but the generated `dist/sccg.full.yaml` file is missing, regenerate the SCCG distribution:

```powershell
Push-Location external/safety-case-core-guidelines
python -m pip install -r requirements.txt
python scripts/build_dist.py
Pop-Location
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

These generated `.mmd` files are documentation assets for the architecture section. When they change, review and commit the updated outputs.

## Diagram Strategy

Prefer small diagrams such as:

- Problem manager
- Parser, SACM, and tree model
- Project storage
- Controllers
- UI panels and panel callbacks
- Review, proposal, problems, and AI service

Avoid repository-wide diagrams because they become unreadable quickly.