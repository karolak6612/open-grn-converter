cmake_minimum_required(VERSION 3.22)

project(grn_converter
    VERSION 1.0.0
    DESCRIPTION "GRN to glTF 2.0 / GLB bidirectional converter with Dear ImGui GUI and CLI"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

add_compile_definitions(GRN_SOURCE_CODE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Find external dependencies via VCPKG
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Core Conversion and Codec Library
# ---------------------------------------------------------------------------
set(GRN_CORE_SOURCES
    src/core/grn_parser.cpp
    src/core/grn_writer.cpp
    src/codecs/dxt_codec.cpp
    src/codecs/tga_png.cpp
    src/codecs/vtex_codec.cpp
    src/gltf/glb_reader.cpp
    src/gltf/glb_writer.cpp
    src/converter/converter.cpp
)

set(GRN_CORE_HEADERS
    src/core/grn_types.h
    src/core/grn_parser.h
    src/core/grn_writer.h
    src/codecs/dxt_codec.h
    src/codecs/tga_png.h
    src/codecs/vtex_codec.h
    src/gltf/glb_reader.h
    src/gltf/glb_writer.h
    src/converter/options.h
    src/converter/converter.h
)

add_library(grn_core STATIC
    ${GRN_CORE_SOURCES}
    ${GRN_CORE_HEADERS}
)

target_include_directories(grn_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
        ${Stb_INCLUDE_DIR}
)

target_link_libraries(grn_core
    PUBLIC
        nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# Main Application Executable (CLI + GUI)
# ---------------------------------------------------------------------------
set(GRN_APP_SOURCES
    src/main.cpp
    src/cli/cli_main.cpp
    src/gui/app.cpp
    src/gui/ui_panels.cpp
    src/gui/file_dialog.cpp
)

set(GRN_APP_HEADERS
    src/cli/cli_main.h
    src/gui/app.h
    src/gui/ui_panels.h
    src/gui/file_dialog.h
)

if(WIN32)
    set(GRN_APP_RESOURCES resources/grn_converter.rc)
endif()

add_executable(grn_converter
    ${GRN_APP_SOURCES}
    ${GRN_APP_HEADERS}
    ${GRN_APP_RESOURCES}
)

target_include_directories(grn_converter
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Stb_INCLUDE_DIR}
)

target_link_libraries(grn_converter
    PRIVATE
        grn_core
        glfw
        imgui::imgui
        glad::glad
        OpenGL::GL
)

if(WIN32)
    set_target_properties(grn_converter PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    if(MSVC)
        target_link_options(grn_converter PRIVATE "/ENTRY:mainCRTStartup")
    endif()
    target_link_libraries(grn_converter
        PRIVATE
            comdlg32
            shell32
            ole32
    )
endif()

# Deploy test_grn to binary running folder
add_custom_command(TARGET grn_converter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/test_grn"
        "$<TARGET_FILE_DIR:grn_converter>/test_grn"
    COMMENT "Deploying test_grn folder to binary output directory"
)

# ---------------------------------------------------------------------------
# Automated Testing Suite (CTest)
# ---------------------------------------------------------------------------
enable_testing()

macro(add_grn_test test_name test_source)
    add_executable(${test_name} ${test_source})
    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${Stb_INCLUDE_DIR}
    )
    target_link_libraries(${test_name}
        PRIVATE
            grn_core
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

add_grn_test(test_codecs tests/test_codecs.cpp)
add_grn_test(test_parser tests/test_parser.cpp)
# Deploy gr2_viewer.exe to binary running folder if present in source folder
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gr2_viewer.exe")
    add_custom_command(TARGET grn_converter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/gr2_viewer.exe"
            "$<TARGET_FILE_DIR:grn_converter>/gr2_viewer.exe"
        COMMENT "Deploying gr2_viewer.exe to binary output directory"
    )
endif()

add_grn_test(test_writer tests/test_writer.cpp)
add_grn_test(test_roundtrip tests/test_roundtrip.cpp)
add_grn_test(test_texture_hue tests/test_texture_hue.cpp)
add_grn_test(test_viewer_compat tests/test_viewer_compat.cpp)
