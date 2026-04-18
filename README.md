# Assurance Forge - Safety Case engineering tool

Assurance Forge is an application that assist Safety Engineers with Safety Case development. The tool uses SACM (Structured Assurance Case Metamodel).

## Requirements

- Windows 10/11
- [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) (select "Desktop development with C++", includes CMake)
- [Git](https://git-scm.com/download/win)

Tested with Visual Studio 2022 (v18), CMake 4.2.3, Git 2.53.

## Build Instructions

### 1. Open Developer Command Prompt

Launch "Developer Command Prompt for VS 2022" from the Start menu.

Or in cmd:

```cmd
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

### 2. Clone and Initialize Submodules

```bash
git clone <repository-url>
cd assurance-forge
git submodule update --init --recursive
```

### 3. Configure and Build

```bash
cmake -B build
cmake --build build --config Release
```

### 4. Run the Application

```bash
build\Release\assurance-forge.exe
```

### 5. Run Tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

Or run the test executable directly:

```bash
build\Release\tests.exe
```

## Usage

1. Launch the application
2. Enter the path to a SACM XML file (default: `data/sample.sacm.xml`)
3. Click "Load" to parse and display the assurance case elements
4. Elements are color-coded by type:
   - Green: Claims (Goals)
   - Blue: Argument Reasoning (Strategies)
   - Orange: Artifacts and Evidence

## Sample Data

A minimal sample file is included at `data/sample.sacm.xml`.

For a more comprehensive example, download the Open Autonomy Safety Case:
https://github.com/EdgeCaseResearch/oasc

## Troubleshooting

**"The CXX compiler identification is unknown"**
- Run from Developer Command Prompt for VS 2022, not regular PowerShell/cmd

**Build fails with missing DirectX headers**
- Install Windows SDK via Visual Studio Installer

**Application starts but window is blank**
- Ensure your GPU supports DirectX 11

**Tests fail to build**
- GoogleTest is fetched automatically; ensure internet connection during first build

## Dependencies

- [Dear ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI (MIT License)
- [pugixml](https://github.com/zeux/pugixml) - XML parser (MIT License)
- [GoogleTest](https://github.com/google/googletest) - Testing framework, fetched via CMake (BSD-3-Clause)
