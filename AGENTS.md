# AGENTS.md — open-grn-converter Machine & Agent Contract

> Single source of truth for all autonomous AI agents (Antigravity, Claude Code, OpenCode, Codex)
> and human developers working in `open-grn-converter`.
> 
> Read the **Bootstrap** and **Hard Rules & Red Lines** sections every session before touching any code.

---

## 1. Mission & Scope

**open-grn-converter** is a high-performance, standalone C++20 bidirectional conversion suite between **Granny 1.2b (`.grn`)** binary 3D containers and modern **glTF 2.0 (`.glb` / `.gltf`)** models.

- **Target Container**: Granny 1.2b binary files (`0xCA5E0000`–`0xCA5EFFFF` relational chunks, Section 0 headers, relocation footers).
- **Target Consumer Games**: Classic titles built on the Granny 1.2b engine, notably **Sacred Gold** (2004/2006, `Sacred.exe`).
- **Target Modern Formats**: Official Khronos glTF 2.0 binary (`.glb`) and text (`.gltf`) compliant models compatible with Blender, Windows 3D Viewer, Godot, Unreal Engine, and Unity.
- **Conversion Directions**:
  - `GRN -> GLB`: Extracts meshes, skeletons, skinning weights (`JOINTS_0`, `WEIGHTS_0`), inverse bind matrices (IBM), materials, embedded textures (DXT1, Raw RGB/RGBA, VTex), and animations into self-contained glTF 2.0 models.
  - `GLB -> GRN`: Ingests glTF models, decomposes bone hierarchies and mesh primitives, transforms coordinates (glTF Y-up to Granny Z-up), generates bit-accurate Granny 1.2b relational chunk hierarchies, string tables, Section 0 descriptors, and relocation footers for in-game execution.
- **Dual Interface Modes**:
  - **Modern GUI**: Dear ImGui interface with custom Flat Magic Rune theme, integrated rune icon, drag-and-drop, real-time logging, and batch conversion.
  - **High-Speed CLI**: Headless command-line utility for CI/CD pipelines, automated modding scripts, and batch processing.

---

## 2. Bootstrap Checklist (Read First, Every Session)

Before making any modifications or investigating issues, ground yourself in **verifiable system state**:

1. **Verify the environment**:
   - CMake 3.22+ and MSVC C++20 toolchain (or GCC 12+ / Clang 14+).
   - vcpkg packages installed: `glfw3`, `imgui` (with glfw/opengl3 bindings), `nlohmann-json`, `stb`, `glad`.
2. **Run the baseline test suite**:
   ```bash
   cmake --preset release
   cmake --build --preset release
   ctest --preset release --output-on-failure
   ```
   Confirm all 6 test targets pass (`test_codecs`, `test_parser`, `test_writer`, `test_roundtrip`, `test_texture_hue`, `test_viewer_compat`).
3. **Evidence over assumptions**:
   - **Nothing written in documentation is evidence until re-verified.** Always re-run the specific CLI command, CTest, or validator probe to substantiate any claim.
   - Classify all conclusions strictly:
     - **Confirmed**: Backed by a runnable command or verified automated test in the current session.
     - **Strong inference**: Consistent with format specifications and binary container analysis, pending direct probe.
     - **Hypothesis**: Plausible explanation requiring experimental validation.
     - **Blocked**: Missing required tool, dependency, or hardware capability.
4. **Independent Implementation & Clean Boundary**:
   - External game binaries (`Sacred.exe`) and proprietary viewers (`gr2_viewer.exe`) are strictly **external and read-only**. Never commit proprietary code or binaries to this repository.
   - All parser and serializer code in `src/` must be 100% independent C++20 implementations.

---

## 3. Hard Rules & Red Lines

These rules are non-negotiable across all sessions and agent workflows:

1. **Format Scope: Granny 1.2b ONLY (`.grn`)**:
   - Target container is strictly Granny 1.2b (`0xCA5E` chunk tag domain).
   - There is NO Granny 2 (`granny2.dll`, `.gr2`) format support. Do not import Granny2 SDK headers or assume Granny2 chunk formats.
2. **Architecture: `grn_core.lib` is STATIC, never dynamic (`.dll`)**:
   - `grn_core` must remain a **static library (`.lib`)**.
   - **DO NOT** convert `grn_core` to a shared DLL. Exporting C++20 STL containers (`std::vector`, `std::string`, `std::optional`) across Windows DLL boundaries causes MSVC runtime heap corruption without extensive `__declspec(dllexport)` boilerplate. Static linking allows standalone single-file distribution of `grn_converter.exe`, enables cross-module Link-Time Optimization (LTCG), and allows tests to link without duplicate compilation.
3. **No Proprietary Binaries or Assets Committed**:
   - `gr2_viewer.exe` is proprietary and must remain ignored by git.
   - Commercial game assets (`.grn` files from commercial games) must NOT be committed to git.
   - Synthetic fixtures and open-source models are permitted for testing. All test suites must pass cleanly or skip gracefully when proprietary fixtures are absent.
4. **glTF 2.0 Compliance Gate**:
   - Any modifications to `src/gltf/glb_writer.cpp` must produce `.glb` files that validate with **0 errors** using the bundled Khronos validator:
     ```bash
     .\validator\gltf_validator.exe <output_model.glb>
     ```
5. **No Regression on Warnings or Warnings-as-Errors**:
   - Maintain `/W4` on MSVC and `-Wall -Wextra` on GCC/Clang. Unreferenced variables and signed/unsigned mismatches must be resolved cleanly.

---

## 4. Repository Structure & Subsystem Ownership

```
open-grn-converter/
├── CMakeLists.txt             # Primary C++20 CMake build configuration
├── CMakePresets.json          # Multi-config build presets (release, debug)
├── vcpkg.json                 # Dependency manifest (glfw3, imgui, nlohmann-json, stb, glad)
├── LICENSE                    # MIT License
├── README.md                  # Human user documentation & usage guide
├── AGENTS.md                  # Machine contract & agent guide (this file)
├── .gitignore                 # Build, cache, binary, and proprietary file exclusions
│
├── src/                       # Core C++20 Source Code
│   ├── main.cpp               # App entry point (auto-dispatches GUI vs CLI)
│   ├── cli/                   # Command Line Interface
│   │   ├── cli_main.h         # CLI options parsing & headless runner declarations
│   │   └── cli_main.cpp       # CLI implementation, batch loop, parameter mapping
│   ├── codecs/                # Image & Texture Codecs
│   │   ├── dxt_codec.h/.cpp   # DXT1 (BC1) block decode/encode with 1-bit alpha
│   │   ├── tga_png.h/.cpp     # 32-bit TGA & PNG decode/encode via stb_image
│   │   └── vtex_codec.h/.cpp  # Granny 1.2b compatible VTex texture decompressor & compressor
│   ├── converter/             # Bidirectional Orchestration Pipeline
│   │   ├── options.h          # ConversionOptions structure (scale, z-up, textures, anim)
│   │   ├── converter.h        # High-level convert_grn_to_glb / convert_glb_to_grn API
│   │   └── converter.cpp      # Coordinate transforms, IBM calculations, anim merging
│   ├── core/                  # Granny 1.2b Binary Container Engine
│   │   ├── grn_types.h        # 0xCA5E chunk constants, section headers, GrnModel structures
│   │   ├── grn_parser.h/.cpp  # Recursive binary chunk parser, skeleton/mesh/anim decoding
│   │   └── grn_writer.h/.cpp  # Section builder, chunk serializer, relocation table generation
│   ├── gltf/                  # glTF 2.0 / GLB Importer and Exporter
│   │   ├── glb_reader.h/.cpp  # cgltf-based GLB reader, node hierarchy, skin & weight parsing
│   │   └── glb_writer.h/.cpp  # glTF 2.0 JSON + BIN chunk generator, bufferView packing
│   └── gui/                   # Dear ImGui Desktop Application
│       ├── app.h/.cpp         # GLFW/OpenGL 3.3 window, lifecycle, Flat Magic Rune theme
│       ├── ui_panels.h/.cpp   # Panels: Single File, Batch, Settings, Real-time Log Console
│       ├── file_dialog.h/.cpp # Win32 native file and folder pickers (IFileDialog / GetOpenFileName)
│       └── embedded_icon.h    # Embedded 32x32 / 64x64 magic rune icon pixel buffer
│
├── resources/                 # Application Resources
│   ├── grn_converter.rc       # Windows resource script (icon embedding)
│   ├── icon.ico               # Windows application icon
│   └── icon.png               # PNG application icon
├── third_party/               # Bundled Third-Party Dependencies
│   └── cgltf/                 # Single-file C glTF 2.0 parser & writer (MIT)
├── validator/                 # Official Khronos glTF 2.0 Validator
│   ├── gltf_validator.exe     # Standalone validator binary (Apache 2.0)
│   ├── LICENSE / NOTICES      # Khronos licensing notices
│   └── docs/                  # Validator schemas and configuration templates
├── test_grn/                  # Test Assets Directory (tracked with .gitkeep)
└── tests/                     # CTest Automated Verification Suite
    ├── test_codecs.cpp        # DXT1, TGA, PNG, VTex unit tests
    ├── test_parser.cpp        # GRN binary container chunk parser tests
    ├── test_writer.cpp        # GRN container serializer & relocation tests
    ├── test_roundtrip.cpp     # Bidirectional GRN <-> GLB roundtrip tests
    ├── test_texture_hue.cpp   # Texture color channel transformations & filtering
    └── test_viewer_compat.cpp # Live debugger tests verifying 0 crashes in Granny viewer
```

---

## 5. Technical Specifications

### 5.1 Granny 1.2b Chunk Architecture (`0xCA5E` Tag Domain)

Granny 1.2b files are relational binary containers structured as hierarchical chunk nodes. All chunk tags start with `0xCA5E`:

| Chunk Constant | Hex Value | Description |
|---|---|---|
| `T_FILE_DIRECTORY` | `0xca5e0000` | Root directory container holding all scene descriptors |
| `T_SECTION_FOOTER` | `0xca5e0101` | Section footer containing relocation tables and pointer fixups |
| `T_SECTION_HEADER` | `0xca5e0102` | Section definition header (offset, size, flags) |
| `T_SECTION_PAYLOAD` | `0xca5e0103` | Raw payload memory block for a section |
| `T_STRING_TABLE` | `0xca5e0200` | String table holding bone, mesh, and material identifiers |
| `T_TEXTURE_MAP` | `0xca5e0301` | Texture descriptor chunk (width, height, format, mip levels) |
| `T_TEXTURE_MAP_IMAGE` | `0xca5e0303` | Texture image container chunk |
| `T_TEXTURE_SECTION` | `0xca5e0304` | Texture container section |
| `T_TEXTURE_IMAGE_SECTION` | `0xca5e0305` | Texture image payload section |
| `T_SKELETON` | `0xca5e0505` | Skeleton container chunk |
| `T_BONE` | `0xca5e0506` | Individual bone node (name index, parent index, rest TRS) |
| `T_SKELETON_SECTION` | `0xca5e0507` | Skeleton payload section |
| `T_BONE_SECTION` | `0xca5e0508` | Bone list payload section |
| `T_MESH` | `0xca5e0601` | Mesh container chunk |
| `T_MESH_SECTION` | `0xca5e0602` | Mesh header section |
| `T_MESH_VERTEX_SET` | `0xca5e0603` | Vertex buffer descriptor |
| `T_MESH_VERTEX_SET_SECTION`| `0xca5e0604` | Vertex buffer section |
| `T_MESH_WEIGHTS` | `0xca5e0702` | Vertex skinning influences (bone index + weight) |
| `T_MESH_VERTICES` | `0xca5e0801` | Vertex positions array (`float3` x, y, z) |
| `T_MESH_NORMALS` | `0xca5e0802` | Vertex normals array (`float3` nx, ny, nz) |
| `T_MESH_FIELD` | `0xca5e0803` | Texture coordinate (UV) channel (`float2` u, v) |
| `T_MESH_FIELD_SECTION` | `0xca5e0804` | UV coordinate channel section |
| `T_MESH_TRIANGLES` | `0xca5e0901` | Face indices array (`uint32_t[3]`) |
| `T_HEADER_SPACER` | `0xca5e0a01` | Section spacer/alignment chunk |
| `T_TRANSFORM_CHANNEL` | `0xca5e0b00` | Transform track container |
| `T_FORM` | `0xca5e0c00` | Deformable model form container |
| `T_MATERIAL` | `0xca5e0d00` | Material definition (diffuse color, shininess, shader flags) |
| `T_MATERIAL_SECTION` | `0xca5e0d01` | Material payload section |
| `T_MATERIAL_SIMPLE_DIFFUSE_TEXTURE` | `0xca5e0d03` | Material diffuse texture binding |
| `T_MODEL` | `0xca5e0e00` | Model instance container (links mesh to skeleton) |
| `T_MODEL_SECTION` | `0xca5e0e01` | Model instance payload section |
| `T_RENDER_PASS` | `0xca5e0e02` | Polygon material pass binding |
| `T_ANIMATION` | `0xca5e1200` | Skeletal animation container chunk |
| `T_ANIMATION_HEADER` | `0xca5e1201` | Animation metadata (duration, frame count, FPS) |
| `T_ANIMATION_TRANSFORM_TRACK_SECTION` | `0xca5e1203` | Track channel section |
| `T_ANIMATION_TRANSFORM_TRACK_KEYS` | `0xca5e1204` | Position, quaternion rotation, scale keyframe samples |
| `T_ANIMATION_SECTION` | `0xca5e1205` | Animation payload section |
| `T_NULL_TERMINATOR` | `0xca5effff` | Section terminator sentinel |

### 5.2 Texture Formats (`TextureFormatCode`)

| Code | Enumerator | Memory Layout / Description |
|---|---|---|
| `0` | `RawRGBX` | 32-bit uncompressed RGBX (8 bits/channel, opaque, 4th byte ignored) |
| `1` | `RawRGBA` | 32-bit uncompressed RGBA (8 bits/channel, with transparency) |
| `4` | `VTexOpaque` | Granny 1.2b compatible compressed texture (opaque) |
| `5` | `VTexAlpha` | Granny 1.2b compatible compressed texture (with alpha mask) |
| `6` | `RawRGB24` | 24-bit uncompressed RGB (3 bytes/pixel) |
| `7` | `ExternalRef` | String reference to external loose file (`.tga` or `.png`) |
| `8` | `DXT1` | S3TC / BC1 4x4 block compression (64 bits per 16 pixels, 1-bit alpha) |

### 5.3 glTF 2.0 / GLB Binary Architecture

- **GLB Structure**:
  1. 12-byte header: `magic = 0x46546C67` (`glTF`), `version = 2`, total file byte length.
  2. Chunk 0: Type `0x4E4F534A` (`JSON`), containing scenes, nodes, meshes, skins, accessors, bufferViews, materials, textures, animations.
  3. Chunk 1: Type `0x004E4942` (`BIN\0`), raw contiguous binary buffer holding vertex data, index buffers, IBM matrices, keyframe data, and embedded PNG/JPEG/TGA texture streams.
- **Skeletal Skinning**:
  - `JOINTS_0`: 4-component integer joint indices per vertex (`COMPONENT_UNSIGNED_SHORT` or `COMPONENT_UNSIGNED_BYTE`).
  - `WEIGHTS_0`: 4-component normalized float skin weights per vertex (`float[4]`), sum = 1.0.
  - `inverseBindMatrices`: Array of $4 \times 4$ column-major float matrices satisfying $IBM_i = (WorldBindTransform_i)^{-1}$.

### 5.4 Coordinate Space Transformations

- **Granny 1.2b Standard**: Z-up, right-handed coordinates.
- **glTF 2.0 Standard**: Y-up, right-handed coordinates.
- **Default Conversion ($Z \rightarrow Y$)**:
  $$\begin{bmatrix} X_{gltf} \\ Y_{gltf} \\ Z_{gltf} \end{bmatrix} = \begin{bmatrix} 1 & 0 & 0 \\ 0 & 0 & 1 \\ 0 & -1 & 0 \end{bmatrix} \begin{bmatrix} X_{grn} \\ Y_{grn} \\ Z_{grn} \end{bmatrix} = \begin{bmatrix} X_{grn} \\ Z_{grn} \\ -Y_{grn} \end{bmatrix}$$
- **Quaternion Rotation Conversion**:
  $$q_{gltf} = (x_{grn}, z_{grn}, -y_{grn}, w_{grn})$$
- When `--z-up` is specified, coordinate swizzling is bypassed, preserving native Granny coordinates directly in the glTF output.

---

## 6. Build, Test, and Tooling Reference

### 6.1 Build Presets (CMake Presets)

Build presets are configured in `CMakePresets.json`:

```bash
# Configure Release (MSVC + vcpkg)
cmake --preset release

# Build Release (grn_converter.exe + test suite)
cmake --build --preset release

# Run Automated Test Suite
ctest --preset release --output-on-failure
```

### 6.2 CLI Usage & Options

```bash
# Single model conversion (GRN -> GLB)
.\build\Release\grn_converter.exe character.grn character.glb

# Single model conversion (GLB -> GRN)
.\build\Release\grn_converter.exe model.glb model.grn

# Merge external animation track onto model
.\build\Release\grn_converter.exe hero.grn hero_run.glb --anim run.grn

# Batch process directory recursively
.\build\Release\grn_converter.exe path/to/grn_models/ path/to/output_glb/ --batch

# Scale model to target game height (e.g. 73 units for Seraphim)
.\build\Release\grn_converter.exe mesh.glb mesh.grn --target-height 73.0

# Export loose textures instead of embedding in GLB
.\build\Release\grn_converter.exe model.grn model.glb --no-embed-textures --texture-format png

# Launch Dear ImGui GUI window explicitly
.\build\Release\grn_converter.exe --gui
```

### 6.3 Validation Probes

```bash
# Validate generated GLB against official Khronos specification
.\validator\gltf_validator.exe -a output.glb

# Validate all GLB files in a directory
.\validator\gltf_validator.exe -a path/to/glb_dir/
```

---

## 7. Workflow Roles & Quality Gates

When working on tasks, follow these operational roles:

1. **Researcher / Format Analyst**:
   - Inspect binary structures, chunk headers, and byte offsets.
   - Reference `grn_types.h` and verify chunk layouts against existing Granny 1.2b samples.
2. **Core Developer**:
   - Implement clean, warning-free C++20 code in `src/core/`, `src/gltf/`, `src/codecs/`, or `src/converter/`.
   - Maintain static library linkages for `grn_core`.
   - Keep UI code in `src/gui/` and CLI code in `src/cli/` cleanly decoupled from conversion algorithms.
3. **Verifier / Quality Gate**:
   - Re-run `ctest --preset release` to ensure 100% test pass rate.
   - Verify generated GLB files with `gltf_validator.exe`.
   - Ensure all edge cases (missing textures, models without skeletons, animations without meshes) are handled gracefully without crashes.
