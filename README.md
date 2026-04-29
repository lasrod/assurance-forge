# Assurance Forge - Safety Case Engineering Tool

Assurance Forge is an application that assists Safety Engineers with Safety Case development.
The tool uses SACM (Structured Assurance Case Metamodel).

![Early Screenshot](docs/screenshot/early-screenshot.png)

---

## ✨ Vision

Assurance Forge aims to modernize safety case development by:

- Making safety cases easier to understand and navigate
- Ensuring credibility through SACM compliance
- Enabling AI-assisted improvement of assurance arguments using the SCCG (Safety Case Core Guidelines)
- Removing the burden of diagram maintenance through automatic layout
- Fully open source

---

## 🧭 Core Principles

### Model-driven, not drawing-driven
Users work with assurance content, not diagram layout.

- SACM XML is the source of truth
- Layout is automatically generated
- No manual positioning of nodes

### SACM-first approach
- The tool consumes and produces SACM 2.3 XML
- Internal structures closely follow SACM concepts

### AI as optional assistance
- Users provide their own AI provider
- No vendor lock-in

---

## 🚀 Current Scope (MVP)

- Load SACM XML files
- Visualize assurance cases in a GSN-like structure
- Automatic layout of assurance elements
- Inspect nodes and relationships
- AI-based suggestions (planned)

---

## 🗺️ Roadmap

See docs/ROADMAP.md

---

## 📦 Releases

Pre-built Windows binaries are published on the [Releases page](https://github.com/lasrod/assurance-forge/releases).

1. Download `assurance-forge.<version>-windows-x64.zip` from the latest release.
2. Unzip anywhere.
3. Run `assurance-forge.exe` from the extracted folder.

Each zip contains the executable, the `data/` sample files, `README.md`, and `LICENSE.md`.

Releases are currently Windows x64 only. To build on other platforms, see [Build Instructions](#build-instructions) below.

---
## Requirements

- Windows 10/11, Linux, or macOS
- [CMake](https://cmake.org/) 3.20 or newer
- A C++17 compiler, such as Visual Studio 2022, GCC, or Clang
- [Git](https://git-scm.com/download/win)

The application uses `hello_imgui` with GLFW/OpenGL as its UI backend and Native File Dialog Extended for OS-native file and folder pickers.

## Build Instructions

### 1. Prepare a C++ Build Environment

On Windows, launch "Developer Command Prompt for VS 2022" from the Start menu.

Or in cmd:

```cmd
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

On Linux or macOS, use a shell where CMake and your C++ compiler are available on `PATH`.

### 2. Clone and Initialize Submodules

```bash
git clone <repository-url>
cd assurance-forge
git submodule update --init --recursive
```

### 3. Configure and Build

**Windows** (Visual Studio):

```bash
cmake --preset default
cmake --build --preset release
```

**Linux** (install dependencies first):

```bash
sudo apt-get install xorg-dev libgl1-mesa-dev libglu1-mesa-dev libgtk-3-dev
cmake -B build -DHELLOIMGUI_DOWNLOAD_GLFW_IF_NEEDED=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**macOS**:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 4. Run the Application

**Windows**:

```bash
build\Release\assurance-forge.exe
```

**Linux**:

```bash
./build/assurance-forge
```

**macOS**:

```bash
open build/assurance-forge.app
```

### 5. Run Tests

**Windows**:

```bash
ctest --preset release
```

**Linux / macOS**:

```bash
ctest --test-dir build --output-on-failure
```

Or run the test executable directly:

**Windows**:

```bash
build\Release\tests.exe
```

**Linux / macOS**:

```bash
./build/tests
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

**Build fails on Linux with missing OpenGL or X11 headers**
- Install the OpenGL/X11 development packages shown in the Linux build example

**Application starts but window is blank**
- Ensure your GPU and drivers support OpenGL

**Tests fail to build**
- GoogleTest is fetched automatically; ensure internet connection during first build

## Dependencies

- [hello_imgui](https://github.com/pthom/hello_imgui) - Cross-platform Dear ImGui application runner (MIT License)
- [Dear ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI, supplied by hello_imgui (MIT License)
- [Native File Dialog Extended](https://github.com/btzy/nativefiledialog-extended) - Native file and folder dialogs (Zlib License)
- [PicoSHA2](https://github.com/okdshin/PicoSHA2) - Header-only SHA-256 implementation (MIT License)
- [pugixml](https://github.com/zeux/pugixml) - XML parser (MIT License)
- [GoogleTest](https://github.com/google/googletest) - Testing framework, fetched via CMake (BSD-3-Clause)
- [Noto Sans JP](https://github.com/notofonts/noto-cjk) - Bundled font for Latin and Japanese rendering (SIL Open Font License 1.1)


## Copyright and license
Code and documentation copyright 2026 Jesper Brännström. Code released under the [MIT License](LICENSE.md)