cmake_minimum_required(VERSION 3.22)

project(grn_converter
    VERSION 1.0.0
    DESCRIPTION "GRN to glTF 2.0 / GLB bidirectional converter with Qt6 Qlementine GUI and CLI"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Qt MOC, RCC, UIC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

add_compile_definitions(GRN_SOURCE_CODE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Fallback Qt6 prefix path
if(WIN32 AND NOT DEFINED CMAKE_PREFIX_PATH AND EXISTS "C:/Qt/6.11.2/msvc2022_64")
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.2/msvc2022_64")
endif()

# Find external dependencies
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent OpenGL OpenGLWidgets)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)

# Qlementine & Qlementine-Icons (Modern Fluent/macOS QStyle)
include(FetchContent)
set(QLEMENTINE_SANDBOX OFF CACHE BOOL "" FORCE)
set(QLEMENTINE_SHOWCASE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    qlementine
    GIT_REPOSITORY "https://github.com/oclero/qlementine.git"
    GIT_TAG "master"
)
FetchContent_Declare(
    qlementine-icons
    GIT_REPOSITORY "https://github.com/oclero/qlementine-icons.git"
    GIT_TAG "master"
)
FetchContent_MakeAvailable(qlementine qlementine-icons)

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
    src/converter/mesh_optimizer.cpp
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
    src/converter/mesh_optimizer.h
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
        meshoptimizer::meshoptimizer
)

# ---------------------------------------------------------------------------
# Main Application Executable (CLI + Qt6 GUI)
# ---------------------------------------------------------------------------
set(GRN_APP_SOURCES
    src/main.cpp
    src/cli/cli_main.cpp
    src/gui/main_window.cpp
    src/gui/conversion_worker.cpp
    src/gui/grn_options_widget.cpp
    src/gui/glb_options_widget.cpp
    src/gui/log_drawer.cpp
    src/gui/viewer/camera.cpp
    src/gui/viewer/grn_anim_sampler.cpp
    src/gui/viewer/skinning_engine.cpp
    src/gui/viewer/shader_gl.cpp
    src/gui/viewer/mesh_gl.cpp
    src/gui/viewer/playback_bar.cpp
    src/gui/viewer/viewport_widget.cpp
    src/gui/viewer/model_viewer_panel.cpp
    src/gui/target_info_widget.cpp
    src/gui/source_info_widget.cpp
)

set(GRN_APP_HEADERS
    src/cli/cli_main.h
    src/gui/main_window.h
    src/gui/conversion_worker.h
    src/gui/grn_options_widget.h
    src/gui/glb_options_widget.h
    src/gui/log_drawer.h
    src/gui/section_card.h
    src/gui/gui_utils.h
    src/gui/source_info_widget.h
    src/gui/target_info_widget.h
    src/gui/viewer/camera.h
    src/gui/viewer/grn_anim_sampler.h
    src/gui/viewer/skinning_engine.h
    src/gui/viewer/shader_gl.h
    src/gui/viewer/mesh_gl.h
    src/gui/viewer/playback_bar.h
    src/gui/viewer/viewport_widget.h
    src/gui/viewer/model_viewer_panel.h
)

set(GRN_APP_RESOURCES
    resources/themes.qrc
)
if(WIN32)
    list(APPEND GRN_APP_RESOURCES resources/grn_converter.rc)
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
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::Concurrent
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        qlementine
        qlementine-icons
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
            shell32
            ole32
    )

    # Automatically deploy Qt DLLs and plugins using windeployqt
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "C:/Qt/6.11.2/msvc2022_64/bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET grn_converter POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-compiler-runtime
                --no-opengl-sw
                --no-translations
                "$<TARGET_FILE:grn_converter>"
            COMMENT "Deploying Qt libraries via windeployqt"
        )
    endif()
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
add_grn_test(test_optimizer tests/test_optimizer.cpp)

add_executable(test_viewer_engine
    tests/test_viewer_engine.cpp
    src/gui/viewer/camera.cpp
    src/gui/viewer/grn_anim_sampler.cpp
    src/gui/viewer/skinning_engine.cpp
)
target_include_directories(test_viewer_engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Stb_INCLUDE_DIR}
)
target_link_libraries(test_viewer_engine
    PRIVATE
        grn_core
        Qt6::Core
        Qt6::Gui
)
add_test(NAME test_viewer_engine COMMAND test_viewer_engine)

