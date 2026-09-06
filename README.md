# GRN $\longleftrightarrow$ GLB Converter

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Standard: C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Target: Granny 1.2b](https://img.shields.io/badge/Format-Granny%201.2b-orange.svg)]()
[![Target: glTF 2.0](https://img.shields.io/badge/Format-glTF%202.0%20%2F%20GLB-brightgreen.svg)](https://www.khronos.org/gltf/)

**GRN $\longleftrightarrow$ GLB Converter** is a high-performance C++20 standalone bidirectional 3D asset conversion suite between **Granny 1.2b (`.grn`)** binary containers and modern industry-standard **glTF 2.0 (`.glb` / `.gltf`)** models.

---

## Purpose & Scope

> **Project Purpose:**  
> This project was developed through offline reverse engineering strictly for the purposes of **digital game preservation**, **community modding**, and **software interoperability**.  
> Its primary objective is to liberate legacy 3D assets from the proprietary Granny 1.2b format used in classic titles such as **Sacred Gold** (2004/2006, `Sacred.exe`), enabling them to be viewed, textured, rigged, and edited in modern DCC tools (**Blender**, **Windows 3D Viewer**, **Godot**, **Unreal Engine**, **Unity**), and conversely re-serialized into engine-compliant Granny 1.2b files for in-game execution.

> [!IMPORTANT]
> **Experimental Proof of Concept & AI-Assisted Notice:**  
> This repository is strictly an experimental Proof of Concept (PoC) developed with **100% AI assistance**. It is provided "as is" for educational, archival, modding, and research purposes. The code and documentation may contain errors, inaccuracies, or incomplete edge-case behavior. The authors and contributors assume **no responsibility or liability** for any issues, crashes, data loss, or inaccuracies arising from the use, testing, or modification of this project.

---

## Features

- **Full C++20 Native Pipeline**: High-speed, bit-accurate processing with zero external runtimes or interpreter dependencies.
- **Bidirectional Conversion**:
  - `GRN -> GLB`: Parses Granny 1.2b binary chunks into self-contained binary glTF 2.0 with meshes, bone hierarchies, skinning weights, animations, and embedded PBR materials.
  - `GLB -> GRN`: Serializes modern glTF models into bit-accurate Granny 1.2b relational chunk hierarchies (`0xCA5E0000`–`0xCA5EFFFF`), relocation footers, and Section 0 headers.
- **Skeletal Rigging & Weights**: Full reconstruction of bone linkages, local TRS transforms, Inverse Bind Matrices (IBM), and normalized 4-weight skinning influences (`JOINTS_0`, `WEIGHTS_0`).
- **Animation Sequencing**: Decodes and encodes split keyframe tracks (position, quaternion rotation, scale-shear) and interleaved tracks with high-fidelity interpolation. Supports merging external animation tracks onto rigged models.
- **Comprehensive Texture & Codec Support**:
  - Embedded DXT1 / BC1 decompressor and compressor.
  - Embedded Bink 1.x video texture decoder with fallback and loose texture support.
  - Raw uncompressed RGB / RGBA / RGBX / BGR565 image extraction and packaging.
  - Loose textures in PNG, TGA (32-bit truecolor), or VTex formats.
- **Coordinate Conversion**: Built-in coordinate space transformations between glTF Y-up and Granny 1.2b Z-up.
- **Dual Interface Modes**:
  - **Modern GUI**: Dear ImGui interface with custom Flat Magic Rune theme, embedded rune icon, real-time logging, and batch support.
  - **Fast CLI**: High-throughput command-line converter for batch processing and automated pipelines.

---

## Architecture: Why `grn_core.lib` (Static Library) instead of `grn_core.dll`?

During compilation, CMake builds the core conversion engine into `grn_core.lib`:

```cmake
add_library(grn_core STATIC
    src/core/grn_parser.cpp
    src/core/grn_writer.cpp
    src/codecs/dxt_codec.cpp
    src/codecs/tga_png.cpp
    src/codecs/vtex_codec.cpp
    src/gltf/glb_reader.cpp
    src/gltf/glb_writer.cpp
    src/converter/converter.cpp
)
```

`grn_core.lib` is **not** an external or proprietary library — it is our internal static library target compiled directly from `src/`. It is intentionally built as a **static library (`.lib`)** rather than a **dynamic shared library (`.dll`)** for key technical reasons:

1. **Self-Contained Standalone Executable**:
   - Compiling statically embeds all parsing, conversion, and codec code directly inside `grn_converter.exe`.
   - Distributing or moving the application only requires a single executable; no loose `grn_core.dll` is required.
2. **Zero DLL Export Overhead & Clean C++ Types**:
   - A Windows `.dll` requires annotating every public class, function, and template instantiation (`std::vector`, `std::string`, `std::optional`) with `__declspec(dllexport)` / `__declspec(dllimport)` to prevent MSVC runtime heap boundary mismatches.
   - A static library allows native C++20 data structures to be shared seamlessly between the core engine and unit tests without macro boilerplate or DLL boundary overhead.
3. **Link-Time Optimization (LTO / LTCG)**:
   - The compiler and linker can inline math routines, vertex unpackers, and coordinate transforms across translation units directly into call sites, optimizing code size and runtime performance.
4. **Shared by Unit Tests Without Redundant Compilation**:
   - The 6 unit test executables in `tests/` link directly against `grn_core.lib`, eliminating the need to recompile the converter codebase 7 separate times.

---

## Project Structure

```
open-grn-converter/
├── CMakeLists.txt             # Primary C++20 CMake build configuration
├── CMakePresets.json          # One-click build presets for Visual Studio & CLI
├── vcpkg.json                 # C++ dependency manifest (glfw, imgui, nlohmann-json, stb, glad)
├── LICENSE                    # MIT License
├── README.md                  # Documentation, architectural notes, and usage guide
├── .gitignore                 # Configured strictly for C++ / CMake / MSVC / vcpkg
│
├── src/                       # C++20 Converter Source Code
│   ├── cli/                   # CLI entry point and argument parsing (cli_main.cpp)
│   ├── codecs/                # DXT1, TGA, PNG, and VTex image codecs
│   ├── converter/             # Bidirectional orchestration pipeline and options
│   ├── core/                  # Binary GRN parser and serializer (Granny 1.2b)
│   ├── gltf/                  # glTF 2.0 / GLB reader (cgltf) and writer
│   ├── gui/                   # Dear ImGui GUI app, Flat Magic Rune theme, icon, and file dialogs
│   └── main.cpp               # Application launcher (auto-detects GUI vs CLI)
│
├── resources/                 # Application assets (icon.ico, icon.png, .rc file)
├── third_party/cgltf/         # Header-only C glTF 2.0 parsing and writing library
├── validator/                 # Khronos glTF Validator for official compliance checks
│
├── test_grn/                  # Sample test assets (models, animations, GLB references)
└── tests/                     # C++ automated CTest suite:
    ├── test_codecs.cpp        # DXT1, TGA, PNG, and VTex codec tests
    ├── test_parser.cpp        # GRN binary container chunk parser tests
    ├── test_writer.cpp        # GRN container serializer tests
    ├── test_roundtrip.cpp     # Bidirectional GRN <-> GLB roundtrip tests
    ├── test_texture_hue.cpp   # Texture manipulation & pipeline verification
    └── test_viewer_compat.cpp # Live debugger tests verifying 0 crashes in Granny viewer
```

---

## How to Compile

### Prerequisites

- **C++20 Compiler**: Visual Studio 2022 / 2026 (MSVC), Clang-cl, or GCC 12+.
- **CMake**: Version 3.22 or higher.
- **vcpkg**: Microsoft C++ Library Manager (used to satisfy `glfw3`, `imgui`, `nlohmann-json`, `stb`, and `glad`).

### Option A: Standard CMake CLI

```bash
# 1. Clone repository
git clone https://github.com/karolak6612/open-grn-converter.git
cd open-grn-converter

# 2. Configure with CMake and vcpkg toolchain
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# 3. Build Release executable and test suite
cmake --build build --config Release -j
```

### Option B: Using CMake Presets

```bash
# Configure using preset
cmake --preset release

# Build using preset
cmake --build --preset release
```

*(Alternatively, open the repository folder in **Visual Studio** or **VS Code** with the CMake Tools extension, select the `release` preset, and click Build).*

The compiled binary will be generated at:
```
build/Release/grn_converter.exe
```

---

## How to Run

The application automatically selects the appropriate mode:
- If run **without arguments** (or launched from Windows Explorer by double-clicking), it opens the **Graphical User Interface (GUI)**.
- If run **with command-line arguments**, it runs the high-speed **Command Line Interface (CLI)**.

### 1. Graphical User Interface (GUI)

Launch `grn_converter.exe` directly or pass `--gui`:
```bash
./build/Release/grn_converter.exe
```

**GUI Features**:
- **Single Model Mode**: Convert an individual `.grn` or `.glb` file with drag-and-drop or browse buttons.
- **Animation Merge**: Select an optional animation `.grn` file (e.g. `WOGG_ATTACK_STAB_A.grn`) to embed animation tracks directly into the exported `.glb`.
- **Batch Processing Mode**: Recursively convert an entire folder of `.grn` or `.glb` files with real-time progress.
- **Scaling Controls**: Adjust a uniform scaling factor or specify target height in Sacred game units (e.g. `73.0` for player characters, `107.0` for street posts).
- **Coordinate Conversion**: Built-in toggle for Y-up (glTF standard) $\longleftrightarrow$ Z-up (Granny/Sacred standard).
- **Real-Time Log Console**: Color-coded conversion logs and progress tracking.

---

### 2. Command Line Interface (CLI)

```bash
# Basic file conversions
./build/Release/grn_converter.exe model.grn model.glb
./build/Release/grn_converter.exe model.glb model.grn

# Merge external animation track onto a character model
./build/Release/grn_converter.exe character.grn character_attack.glb --anim attack.grn

# Batch process an entire folder recursively
./build/Release/grn_converter.exe path/to/models/ path/to/output/ --batch

# Scale model to target game height (e.g. 73 units for Seraphim)
./build/Release/grn_converter.exe hero.glb hero.grn --target-height 73.0

# Export loose textures instead of embedding
./build/Release/grn_converter.exe model.grn model.glb --no-embed-textures --texture-format png

# View full options reference
./build/Release/grn_converter.exe --help
```

#### CLI Options Reference

| Option | Description |
|---|---|
| `-b, --batch` | Batch process all compatible files in the input folder recursively |
| `--anim <path>` | External animation `.grn` track to merge into the output model |
| `--no-embed-textures` | Save textures as loose files next to model instead of embedding |
| `--texture-format <fmt>` | Format for loose textures: `tga` (default 32-bit), `png`, or `vtex` |
| `--no-vtex` | Disable VTex video codec; fall back to uncompressed or loose files |
| `-t, --textures-dir <dir>` | Search directory for external textures |
| `--scale <val>` | Uniform scale multiplier applied to geometry/bones |
| `--target-height <val>` | Target height in game units (e.g. 73 for character, 107 for post) |
| `--meters` | Standard glTF metric conversion (scale = 39.37 inches/meter) |
| `--z-up` | Preserve native Z-up coordinates (default converts to glTF Y-up) |
| `--gui` | Force launching the Dear ImGui GUI window |
| `-h, --help` | Display help message and exit |

---

## How to Run Tests

Run the automated CTest suite from the build folder:
```bash
ctest --test-dir build -C Release --output-on-failure
```

### Test Suite Overview

| Test Name | Source File | Description |
|---|---|---|
| `test_codecs` | `tests/test_codecs.cpp` | Validates DXT1/BC1, TGA, PNG, and VTex image codecs |
| `test_parser` | `tests/test_parser.cpp` | Verifies low-level Granny 1.2b binary container chunk parsing |
| `test_writer` | `tests/test_writer.cpp` | Validates binary Granny 1.2b container serialization |
| `test_roundtrip` | `tests/test_roundtrip.cpp` | End-to-end `GRN -> GLB -> GRN` roundtrip mesh & skeleton fidelity |
| `test_texture_hue` | `tests/test_texture_hue.cpp` | Texture pipeline validation and color channel transformations |
| `test_viewer_compat` | `tests/test_viewer_compat.cpp` | Runs exported models under the original Granny viewer to ensure 0 crashes |

---

### Important: `test_viewer_compat` and Granny Viewer

> [!NOTE]
> **Granny Viewer (`gr2_viewer.exe`) is proprietary software and is NOT included in this repository.**

- **Automatic Skipping**: If `gr2_viewer.exe` is not present, `test_viewer_compat` will **automatically detect its absence, display `[SKIP]`, and exit cleanly with return code 0**. The entire test suite will still report **100% passed**.
- **Enabling Live Viewer Testing (Optional)**: If you possess a legitimate copy of `gr2_viewer.exe`, you can enable full live viewer verification by placing `gr2_viewer.exe` into:
  - The repository root (`./gr2_viewer.exe`), OR
  - The binary output folder (`./build/Release/gr2_viewer.exe`).
- When present, `test_viewer_compat` launches the viewer under the Windows Debugging API (`DEBUG_PROCESS`), monitors the process for memory access violations or unhandled exceptions, and confirms that our converted models render cleanly with zero crashes.
- `.gitignore` is explicitly configured to ignore `gr2_viewer.exe` to prevent proprietary binaries from ever being committed.

---

## Third-Party Software & Licenses

This project incorporates or links against the following third-party open-source libraries:

| Library | License | Author / Copyright | Description |
|---|---|---|---|
| [**cgltf**](https://github.com/jkuhlmann/cgltf) | MIT License | Copyright (c) 2018-2024 Johannes Kuhlmann | Single-file C glTF 2.0 parser and writer bundled in `third_party/cgltf/`. |
| [**Dear ImGui**](https://github.com/ocornut/imgui) | MIT License | Copyright (c) 2014-2026 Omar Cornut | Immediate mode graphical user interface library used for the GUI frontend. |
| [**GLFW**](https://github.com/glfw/glfw) | zlib/libpng License | Copyright (c) 2002-2006 Marcus Geelnard, 2006-2019 Camilla Löwy | Window creation, OpenGL context initialization, and input handling. |
| [**GLAD**](https://github.com/Dav1dde/glad) | MIT / Apache 2.0 | Copyright (c) 2013-2020 David Herberth | Multi-language Vulkan/GL/GLES/EGL loader generator. |
| [**nlohmann/json**](https://github.com/nlohmann/json) | MIT License | Copyright (c) 2013-2025 Niels Lohmann | Modern JSON for C++ used for glTF chunk generation and metadata. |
| [**stb**](https://github.com/nothings/stb) | MIT / Public Domain | Copyright (c) 2017 Sean Barrett | `stb_image.h` and `stb_image_write.h` image decoding and encoding routines. |
| [**glTF-Validator**](https://github.com/KhronosGroup/glTF-Validator) | Apache License 2.0 | Copyright (c) 2016-2024 The Khronos Group Inc. | Command-line tool and JSON schemas in `validator/` for official glTF 2.0 specification validation. |

Individual license notices for bundled components can be found in their respective source files or under `validator/LICENSE` and `validator/NOTICES`.

---

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## Disclaimer & Trademark Notice

This repository is an independent, open-source experimental research and digital preservation project. It is **not** affiliated with, associated with, sponsored by, endorsed by, or in any way officially connected with **RAD Game Tools**, **Epic Games, Inc.**, **Ascaron Entertainment**, or any of their subsidiaries or affiliates.

All trademarks, registered trademarks, service marks, trade names, and brand names referenced in this repository are the property of their respective owners. Any reference to specific third-party games, formats, engines, or company names is made solely for the purposes of identification, technical description, software interoperability, community modding, and digital game preservation.
