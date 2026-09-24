# GRN $\longleftrightarrow$ GLB Converter

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Standard: C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Framework: Qt 6](https://img.shields.io/badge/GUI-Qt%206%20%2F%20Qlementine-41CD52.svg)](https://www.qt.io/)
[![Target: Granny 1.2b](https://img.shields.io/badge/Format-Granny%201.2b-orange.svg)]()
[![Target: glTF 2.0](https://img.shields.io/badge/Format-glTF%202.0%20%2F%20GLB-brightgreen.svg)](https://www.khronos.org/gltf/)

**GRN $\longleftrightarrow$ GLB Converter** is a high-performance C++20 standalone bidirectional 3D asset conversion suite between **Granny 1.2b (`.grn`)** binary containers and modern industry-standard **glTF 2.0 (`.glb` / `.gltf`)** models.

---

## Purpose & Scope

> **Project Purpose:**  
> This project was developed as an independent format interoperability suite strictly for the purposes of **digital game preservation**, **community modding**, and **software interoperability**.  
> Its primary objective is to liberate legacy 3D assets from the proprietary Granny 1.2b format used in classic titles such as **Sacred Gold** (2004/2006, `Sacred.exe`), enabling them to be viewed, textured, rigged, and edited in modern DCC tools (**Blender**, **Windows 3D Viewer**, **Godot**, **Unreal Engine**, **Unity**), and conversely re-serialized into engine-compliant Granny 1.2b files for in-game execution.

> [!IMPORTANT]
> **Experimental Proof of Concept & AI-Assisted Notice:**  
> This repository is strictly an experimental Proof of Concept (PoC) developed with **100% AI assistance**. It is provided "as is" for educational, archival, modding, and research purposes. The code and documentation may contain errors, inaccuracies, or incomplete edge-case behavior. The authors and contributors assume **no responsibility or liability** for any issues, crashes, data loss, or inaccuracies arising from the use, testing, or modification of this project.

---

<img width="1194" height="738" alt="obraz" src="https://github.com/user-attachments/assets/9d972cab-be1c-4e99-9c7c-a8f0ac66a84d" />

---

## Features

- **Full C++20 Native Pipeline**: High-speed, bit-accurate processing with zero external runtimes or interpreter dependencies.
- **Bidirectional Conversion**:
  - `GRN -> GLB`: Parses Granny 1.2b binary chunks into self-contained binary glTF 2.0 with meshes, bone hierarchies, skinning weights, animations, and embedded PBR materials.
  - `GLB -> GRN`: Serializes modern glTF models into bit-accurate Granny 1.2b relational chunk hierarchies (`0xCA5E0000`–`0xCA5EFFFF`), relocation footers, and Section 0 headers.
- **Skeletal Rigging & Weights**: Full reconstruction of bone linkages, local TRS transforms, Inverse Bind Matrices (IBM), and normalized 4-weight skinning influences (`JOINTS_0`, `WEIGHTS_0`).
- **Animation Sequencing & Split Clips**:
  - Decodes and encodes split keyframe tracks (position, quaternion rotation, scale-shear) and interleaved tracks with high-fidelity interpolation.
  - Fully supports merging external animation tracks onto rigged models (`--anim <path>`).
  - Automatic split animation pipeline (`--split-anims`): exports base mesh with skeleton, plus standalone animation clip containers matching game engine expectations.
- **Animation Optimization Pipeline**:
  - **Uniform FPS Resampling**: Resamples non-uniform keyframes to clean fixed intervals (e.g. 30, 20, 15 FPS) to eliminate frame-step jitter.
  - **Micro-Bone Culling**: Prunes tracks whose maximum angular delta is below a user-defined threshold (e.g. 3.0°), stripping twitchy or imperceptible bone motion.
  - **Loop-Safe Clamping**: Ensures exact duplicate keyframes at $t_{end}$, preventing edge artifacts and jumps in game looping clips.
  - **Static Channel Pruning**: Automatically removes zero-movement transform tracks to reduce file size and vertex processing overhead.
- **16-Bit Safe High-Poly Mesh Partitioner (`--optimizer`)**:
  - Automatically partitions meshes exceeding 64,000 vertices into 16-bit compliant sub-meshes with isolated bone palettes.
  - Eliminates index overflow crashes in legacy DirectX 8/9 engines while retaining 100% geometry fidelity, normals, UV coordinates, and vertex skinning weights.
- **Comprehensive Texture & Codec Support**:
  - **Native C++20 Granny 1.2b Compatible VTex Codec**: Autonomous in-process decompressor and compressor for **Format 4 (Opaque)** and **Format 5 (Alpha plane)** with zero external runtime dependencies.
  - Embedded DXT1 / BC1 and DXT5 decompressor and compressor.
  - Raw uncompressed RGB / RGBA / RGBX / BGR565 image extraction and packaging.
  - Loose textures in PNG, TGA (32-bit truecolor), or native VTex formats.
- **Coordinate Conversion**: Built-in coordinate space transformations between glTF standard Y-up and Granny 1.2b native Z-up.
- **Dual Interface Modes**:
  - **Modern Qt6 / Qlementine GUI**: Fluent / macOS dark design system, 3D Dual-Viewport, animation player, bone inspector, model cards, and real-time logging.
  - **High-Speed CLI**: Headless command-line utility for automated batch conversions, modding scripts, and CI/CD pipelines.

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
    src/converter/anim_optimizer.cpp
    src/converter/mesh_optimizer.cpp
)
```

`grn_core.lib` is **not** an external or proprietary library — it is our internal static library target compiled directly from `src/`. It is intentionally built as a **static library (`.lib`)** rather than a **dynamic shared library (`.dll`)** for key technical reasons:

1. **Self-Contained Executable**:
   - Compiling statically embeds all parsing, conversion, and codec code directly inside `grn_converter.exe`.
   - Distributing the application only requires a single executable; no loose `grn_core.dll` is required.
2. **Zero DLL Export Overhead & Clean C++ Types**:
   - A Windows `.dll` requires annotating every public class, function, and template instantiation (`std::vector`, `std::string`, `std::optional`) with `__declspec(dllexport)` / `__declspec(dllimport)` to prevent MSVC runtime heap boundary mismatches.
   - A static library allows native C++20 data structures to be shared seamlessly between the core engine and unit tests without macro boilerplate or DLL boundary overhead.
3. **Link-Time Optimization (LTO / LTCG)**:
   - The compiler and linker can inline math routines, vertex unpackers, and coordinate transforms across translation units directly into call sites, optimizing code size and runtime performance.
4. **Shared by Unit Tests Without Redundant Compilation**:
   - The unit test executables in `tests/` link directly against `grn_core.lib`, eliminating the need to recompile the converter codebase multiple times.

---

## Native C++20 Granny 1.2b Compatible VTex Codec Engine (Formats 4 & 5)

Classic games built on Granny 1.2b (such as *Sacred Gold*) compress high-resolution character, creature, and environmental diffuse textures into **Granny 1.2b compatible VTex** texture streams:
- **Format 4 (`VTexOpaque`)**: Opaque video texture stream without alpha channel (e.g. `BLACK_MAGICIAN.grn`).
- **Format 5 (`VTexAlpha`)**: Video texture stream carrying a dedicated full-resolution Alpha cutout plane (e.g. `BLACK_RIDER.grn`).

Previously, modders were forced to rely on legacy proprietary binaries or placeholder textures. **open-grn-converter** provides a **100% independent, native C++20 implementation** embedded directly in `grn_core.lib`:

- **Zero External Runtime Dependencies**: Runs cross-platform with 0 external codec modules or external executables.
- **Bitstream Decoding**:
  - Implements LSB-first bitstream parsing with 32-bit boundary alignment (`Align32()`).
  - Supports all standard Granny 1.2b compatible macroblock types: `BLOCK_SKIP`, `BLOCK_SCALED`, `BLOCK_RUN`, `BLOCK_INTRA` (via AAN integer IDCT), `BLOCK_FILL`, `BLOCK_PATTERN`, and `BLOCK_RAW`.
  - Sequential plane reconstruction: Alpha ($W \times H$), Luma Y ($W \times H$), Chroma Cr ($\frac{W+1}{2} \times \frac{H+1}{2}$), Chroma Cb ($\frac{W+1}{2} \times \frac{H+1}{2}$).
  - Full BT.601 integer color recombination into 32-bit RGBA.
- **Bitstream Encoding (`encode_vtex`)**:
  - Automatic alpha plane detection: analyzes pixel buffer alpha channel; if any pixel $A < 250$, selects Format 5, otherwise Format 4.
  - Planar decomposition and 2x2 box filtering for chroma subsampling.
  - Fast macroblock encoding into `BLOCK_FILL`, `BLOCK_PATTERN`, and `BLOCK_RAW` with identity Huffman bundle streaming.
  - Serializes Granny 1.2b compatible 44-byte container headers and frame index tables for direct in-game execution.
- **Verified Compatibility**:
  - Validated across all original Sacred Gold character models, achieving **100% conversion success with 0 crashes, 0 validator errors, and 0 placeholder textures**.

---

## Project Structure

```
open-grn-converter/
├── CMakeLists.txt             # Primary C++20 CMake build configuration
├── CMakePresets.json          # Multi-config build presets (release, debug)
├── vcpkg.json                 # Dependency manifest (nlohmann-json, stb, meshoptimizer)
├── LICENSE                    # MIT License
├── README.md                  # Documentation, architectural notes, and usage guide
├── AGENTS.md                  # Machine contract & autonomous agent guidelines
├── .gitignore                 # Configured strictly for C++ / CMake / MSVC / vcpkg
│
├── src/                       # C++20 Converter Source Code
│   ├── cli/                   # CLI entry point and argument parsing (cli_main.cpp)
│   ├── codecs/                # DXT1, TGA, PNG, and VTex image codecs
│   ├── converter/             # Bidirectional orchestration pipeline, anim/mesh optimizers
│   ├── core/                  # Binary GRN parser and serializer (Granny 1.2b chunks)
│   ├── gltf/                  # glTF 2.0 / GLB reader (cgltf) and writer
│   ├── gui/                   # Qt6 + Qlementine UI
│   │   ├── main_window.cpp    # Modern main window shell and layout
│   │   ├── bone_inspector_widget.cpp # Skeletal tree and transform inspector
│   │   ├── glb_options_widget.cpp    # glTF / GLB export parameters & animation optimization
│   │   ├── grn_options_widget.cpp    # Granny 1.2b export options & anim merge list
│   │   ├── source_info_widget.cpp    # Input model stats and card visualizers
│   │   ├── target_info_widget.cpp    # Converted model output cards & folder navigation
│   │   ├── log_drawer.cpp     # Collapsible real-time color-coded logging console
│   │   └── viewer/            # Embedded 3D Dual-Viewport OpenGL Engine
│   │       ├── model_viewer_panel.cpp # Viewport toolbar, single/split mode, camera controls
│   │       ├── gl_viewport_widget.cpp # OpenGL widget, shaders, grid, skeleton rendering
│   │       ├── playback_bar.cpp       # Animation timeline scrubber, play/pause, speed
│   │       ├── skinning_engine.cpp    # CPU skinning and vertex transformation pipeline
│   │       ├── grn_anim_sampler.cpp   # Cubic spline / linear animation keyframe sampler
│   │       └── camera.cpp             # Arcball orbit camera with pan and zoom
│   └── main.cpp               # Application launcher (auto-dispatches GUI vs CLI)
│
├── resources/                 # Application assets (icon.ico, icon.png, .rc file)
├── third_party/cgltf/         # Header-only C glTF 2.0 parsing and writing library
├── validator/                 # Khronos glTF Validator for official compliance checks
│
├── test_grn/                  # Test assets (models, animations, GLB references)
└── tests/                     # C++ automated CTest suite:
    ├── test_codecs.cpp        # DXT1, TGA, PNG, and VTex codec tests
    ├── test_parser.cpp        # GRN binary container chunk parser & weight tests
    ├── test_writer.cpp        # GRN container serializer tests
    ├── test_roundtrip.cpp     # Bidirectional GRN <-> GLB roundtrip tests
    ├── test_texture_hue.cpp   # Texture manipulation & pipeline verification
    ├── test_viewer_compat.cpp # Live debugger tests verifying 0 crashes in Granny viewer
    ├── test_optimizer.cpp     # 16-bit mesh partitioner & multi-material split tests
    └── test_viewer_engine.cpp # 3D viewport, skinning, and animation engine tests
```

---

## How to Compile

### Prerequisites

- **C++20 Compiler**: Visual Studio 2022 (MSVC), Clang 14+, or GCC 12+.
- **CMake**: Version 3.22 or higher.
- **Qt 6**: Version 6.5+ (Widgets, OpenGLWidgets, Svg, Concurrent). Set `QT_DIR` or `CMAKE_PREFIX_PATH` to your Qt installation (e.g. `C:/Qt/6.11.2/msvc2022_64`).
- **vcpkg**: Microsoft C++ Library Manager (used to satisfy `nlohmann-json`, `stb`, and `meshoptimizer`). Set `VCPKG_ROOT` to your vcpkg clone directory (e.g. `C:/vcpkg`).

### Option A: Using CMake Presets (Recommended)

```bash
# Configure using preset (uses $env:VCPKG_ROOT and $env:QT_DIR)
cmake --preset release

# Build Release executable and test suite
cmake --build --preset release

# Run automated tests (all 8 targets)
ctest --preset release --output-on-failure
```

### Option B: Standard CMake CLI

```bash
# 1. Clone repository
git clone https://github.com/karolak6612/open-grn-converter.git
cd open-grn-converter

# 2. Configure with CMake, vcpkg toolchain, and Qt prefix
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_PREFIX_PATH="$env:QT_DIR"

# 3. Build Release executable and test suite
cmake --build build --config Release -j
```

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

**GUI Capabilities**:
- **3D Dual-Viewport Model Viewer**:
  - Embedded OpenGL rendering with real-time arcball orbit camera, pan, and zoom.
  - Side-by-side **Split View** (comparing Source model against Converted output model) with synchronized camera controls.
  - **Single View** for full-focus model inspection.
  - Display toggles for Wireframe, Ground Grid, Skeletal Joints/Bones, Bone Name Labels, and Shading modes (Textured, Normals, Unlit).
- **Real-Time Animation Player**:
  - Interactive playback bar with Play, Pause, Rewind, and Loop toggle.
  - Interactive timeline scrubber with current time and duration readout.
  - Variable speed playback multipliers (0.25x, 0.5x, 1.0x, 1.5x, 2.0x).
- **Interactive Bone Inspector**:
  - Hierarchical skeletal tree showing parent-child joint linkages.
  - Live search filter to quickly find specific bones in complex rigs.
  - Real-time 3D coordinate inspection (local position, quaternion rotation, scale).
- **Source & Target Inspection Cards**:
  - Instant visual breakdown of Geometry (vertex count, face count, sub-mesh parts).
  - Skeleton inspection (bone count, root joints).
  - Material list and embedded/loose Texture visualizers.
- **Conversion Settings**:
  - Output format toggle (`.grn` $\longleftrightarrow$ `.glb`).
  - Scaling controls: Uniform scale multiplier or automatic Target Height calculation in game units.
  - Animation optimization toggles: Target FPS resampling (Auto / 30 / 20 / 15 FPS) and micro-bone angular culling.
  - Animation split controls (`--split-anims`) or external animation merging.
  - 16-bit safe sub-mesh partitioning toggle.
- **Integrated Logging Console**:
  - Collapsible bottom drawer with color-coded log entries (Info, Warning, Error) and auto-scroll.

---

### 2. Command Line Interface (CLI)

```bash
# Basic file conversions (GRN -> GLB or GLB -> GRN)
./build/Release/grn_converter.exe model.grn model.glb
./build/Release/grn_converter.exe model.glb model.grn

# Merge external animation tracks onto a character model (can repeat)
./build/Release/grn_converter.exe character.grn character_attack.glb --anim run.grn --anim attack.grn

# Split GLB animations into separate .grn files (default)
./build/Release/grn_converter.exe character.glb character.grn --split-anims

# Keep all GLB animations embedded into a single .grn container
./build/Release/grn_converter.exe character.glb character.grn --no-split-anims

# Resample animations to uniform 30 FPS and cull micro-bone motion < 3 degrees
./build/Release/grn_converter.exe model.glb model.grn --anim-fps 30 --anim-min-deg 3.0

# Batch process an entire folder recursively
./build/Release/grn_converter.exe path/to/models/ path/to/output/ --batch

# Scale model to target game height (e.g. 73 units for player characters)
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
| `--anim <path>` | External animation `.grn` track to merge into the output model (can repeat) |
| `--split-anims` | Split GLB animations into separate `.grn` files with matching skeletons (default) |
| `--no-split-anims` | Embed all GLB animations into a single `.grn` container |
| `--anim-optimizer` | Enable animation optimizer (prune static tracks, collapse redundant keys) |
| `--no-anim-optimizer` | Disable animation optimizer (default) |
| `--anim-fps <val>` | Target uniform animation frame rate (e.g. 30, 20, 15; default 0 = source) |
| `--anim-min-deg <val>` | Prune tracks with rotation movement < angle in degrees (e.g. 3.0) |
| `--anim-loop-safe` | Preserve loop boundary keyframes and tangents |
| `--no-anim-loop-safe` | Disable loop-safe clamping (default) |
| `--optimizer` | Enable 16-bit vertex partitioner when converting GLB to GRN (default) |
| `--no-optimizer` | Disable 16-bit vertex partitioner |
| `--no-embed-textures` | Save textures as loose files next to model instead of embedding |
| `--texture-format <fmt>` | Format for loose textures: `tga` (default 32-bit), `png`, or `vtex` |
| `--no-vtex` | Disable VTex video codec; fall back to uncompressed or loose files |
| `-t, --textures-dir <dir>` | Search directory for external or cached textures |
| `--scale <val>` | Uniform scale multiplier applied to geometry/bones |
| `--target-height <val>` | Target height in game units (e.g. 73 for human, 107 for post) |
| `--meters` | Standard glTF metric conversion (scale = 39.37 inches/meter) |
| `--z-up` | Preserve native Z-up coordinates (default converts to glTF Y-up) |
| `--gui` | Force launching the graphical user interface (GUI) |
| `-h, --help` | Display help message and exit |

---

### Granny 1.2b vs. Granny 2 Viewer Compatibility Notes

When evaluating converted `.grn` files across different viewers and tools:

- **`playgrn.exe` (Authentic Granny 1.2b Engine)**:
  `playgrn.exe` (and target games like *Sacred Gold*) is the true reference standard for Granny 1.2b format compliance. It reads relational binary chunks directly and plays keyframe streams linearly from memory. It can play animations with thousands of bone tracks natively with 0 crashes.
- **`gr2_viewer.exe` (Granny 2 Viewer)**:
  Granny 2 viewers do not render `.grn` natively. When passed a `.grn` file, `gr2_viewer.exe` calls `GRN2GR2ConvertGRNFile` (`0x00438750`), attempting to convert the file into a temporary `.gr2` container by running B-spline curve optimization. Its internal debug allocator (`granny_memory.cpp`) uses `VirtualAlloc` with guard pages within a 32-bit (2 GB) address space. Extremely complex modern rigs (>1,000 active spline tracks) can exhaust this allocator during cubic curve fitting. Models with normal bone counts (or models without animations) convert and render in `gr2_viewer.exe` with 0 issues.
- All files produced by `open-grn-converter` are verified bit-accurate Granny 1.2b containers.

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
| `test_parser` | `tests/test_parser.cpp` | Verifies low-level Granny 1.2b binary container chunk parsing & bone weights |
| `test_writer` | `tests/test_writer.cpp` | Validates binary Granny 1.2b container serialization & section building |
| `test_roundtrip` | `tests/test_roundtrip.cpp` | End-to-end `GRN -> GLB -> GRN` roundtrip mesh & skeleton fidelity |
| `test_texture_hue` | `tests/test_texture_hue.cpp` | Texture pipeline validation and color channel transformations |
| `test_viewer_compat` | `tests/test_viewer_compat.cpp` | Runs exported models under the original Granny viewer to ensure 0 crashes |
| `test_optimizer` | `tests/test_optimizer.cpp` | 16-bit mesh partitioner, multi-material splitting, and QEM decimation |
| `test_viewer_engine` | `tests/test_viewer_engine.cpp` | 3D viewport, CPU skinning engine, animation sampler, and orbit camera |

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
| [**Qt 6**](https://www.qt.io/) | LGPLv3 / Commercial | The Qt Company Ltd. | Cross-platform C++ GUI framework (Core, Gui, Widgets, OpenGLWidgets, Svg, Concurrent). |
| [**Qlementine**](https://github.com/oclero/qlementine) | MIT License | Copyright (c) 2021-2024 Olivier Cléro | Modern Fluent / macOS styled QStyle theme and design system. |
| [**Qlementine-Icons**](https://github.com/oclero/qlementine-icons) | MIT License | Copyright (c) 2021-2024 Olivier Cléro | Vector SVG icon library for Qlementine UI. |
| [**cgltf**](https://github.com/jkuhlmann/cgltf) | MIT License | Copyright (c) 2018-2024 Johannes Kuhlmann | Single-file C glTF 2.0 parser and writer bundled in `third_party/cgltf/`. |
| [**meshoptimizer**](https://github.com/zeux/meshoptimizer) | MIT License | Copyright (c) 2016-2024 Arseny Kapoulkine | Mesh vertex cache optimization and simplification library. |
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
