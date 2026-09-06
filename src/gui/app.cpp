/**
 * @file app.cpp
 * @brief Implementation of the GLFW + OpenGL3 + Dear ImGui application loop.
 */

#include "app.h"
#include "embedded_icon.h"
#include "../codecs/tga_png.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>

namespace grn {

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "[GLFW Error " << error << "]: " << (description ? description : "unknown") << std::endl;
}

static void apply_flat_magic_rune_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Window & Frame Geometry
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);

    // Dark Charcoal / Obsidian Foundations (matching the rune background)
    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.90f, 0.84f, 1.00f); // Warm ivory parchment
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.52f, 0.50f, 0.45f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.11f, 0.11f, 0.12f, 1.00f); // #1C1D1F
    colors[ImGuiCol_ChildBg]               = ImVec4(0.09f, 0.09f, 0.10f, 1.00f); // #17181A
    colors[ImGuiCol_PopupBg]               = ImVec4(0.13f, 0.13f, 0.14f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.42f, 0.34f, 0.16f, 0.55f); // Warm antique brass border
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Controls & Frame Backgrounds
    colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.16f, 0.18f, 1.00f); // #292A2E
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.26f, 0.23f, 0.15f, 0.80f); // Subtle gold tint on hover
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.34f, 0.28f, 0.17f, 0.90f);

    // Title & Menu Bar
    colors[ImGuiCol_TitleBg]               = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.20f, 0.17f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.09f, 0.09f, 0.10f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.09f, 0.09f, 0.10f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.42f, 0.34f, 0.16f, 0.70f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.60f, 0.48f, 0.20f, 0.85f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.78f, 0.62f, 0.25f, 1.00f);

    // Buttons (Matte Gold -> Radiant Gold)
    colors[ImGuiCol_Button]                = ImVec4(0.42f, 0.34f, 0.16f, 0.75f); // Muted antique gold
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.64f, 0.51f, 0.20f, 0.90f); // Radiant gold hover
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.82f, 0.66f, 0.25f, 1.00f); // Bright active gold

    // Headers (Selectables, CollapsingHeaders)
    colors[ImGuiCol_Header]                = ImVec4(0.38f, 0.30f, 0.14f, 0.65f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.56f, 0.44f, 0.18f, 0.85f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.72f, 0.58f, 0.22f, 1.00f);

    // Sliders, Checkmarks, Grabs
    colors[ImGuiCol_CheckMark]             = ImVec4(0.92f, 0.76f, 0.28f, 1.00f); // Bright rune gold checkmark
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.74f, 0.60f, 0.24f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.92f, 0.76f, 0.30f, 1.00f);

    // Separators
    colors[ImGuiCol_Separator]             = ImVec4(0.38f, 0.30f, 0.14f, 0.55f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.60f, 0.48f, 0.20f, 0.80f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.80f, 0.64f, 0.25f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab]                   = ImVec4(0.18f, 0.16f, 0.12f, 0.85f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.56f, 0.44f, 0.18f, 0.85f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.42f, 0.34f, 0.15f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.12f, 0.11f, 0.10f, 0.90f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.26f, 0.21f, 0.13f, 1.00f);

    // Plots & Tables
    colors[ImGuiCol_PlotLines]             = ImVec4(0.75f, 0.60f, 0.24f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.95f, 0.78f, 0.32f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = ImVec4(0.65f, 0.52f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.85f, 0.68f, 0.26f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.18f, 0.17f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.42f, 0.34f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.28f, 0.23f, 0.14f, 0.60f);

    // Selection & Navigation
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.65f, 0.52f, 0.20f, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(0.95f, 0.78f, 0.30f, 0.90f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.85f, 0.68f, 0.26f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.08f, 0.08f, 0.10f, 0.70f);
}

App::App() = default;

App::~App() {
    shutdown();
}

bool App::init_window() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // OpenGL 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(1024, 640, "GRN <-> GLB Converter", nullptr, nullptr);
    if (!window_) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Set application window icon from embedded Flat Magic Rune
    auto icon_data = decode_image_memory(EMBEDDED_ICON_PNG, EMBEDDED_ICON_PNG_SIZE);
    if (icon_data && !icon_data->pixels.empty()) {
        GLFWimage icon_img;
        icon_img.width = static_cast<int>(icon_data->width);
        icon_img.height = static_cast<int>(icon_data->height);
        icon_img.pixels = icon_data->pixels.data();
        glfwSetWindowIcon(window_, 1, &icon_img);
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vertical sync

    // Load OpenGL function pointers using GLAD
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD OpenGL loader" << std::endl;
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        return false;
    }

    // Set user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, App::drop_callback);

    // Initialize Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Visual theme setup: Flat Magic Rune (Antique Gold + Obsidian Charcoal)
    apply_flat_magic_rune_theme();

    // Initialize platform and renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    initialized_ = true;
    return true;
}

void App::drop_callback(GLFWwindow* window, int count, const char** paths) {
    if (count > 0 && paths && paths[0]) {
        auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
        if (app) {
            app->get_panels().set_input_path(paths[0]);
            app->get_panels().add_log(std::string("Dropped path: ") + paths[0], LogEntry::Level::Info);
        }
    }
}

int App::run() {
    if (!init_window()) {
        return 1;
    }

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Start Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render UI panels
        panels_.render();

        // Render Dear ImGui
        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.09f, 0.09f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    shutdown();
    return 0;
}

void App::shutdown() {
    if (initialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        initialized_ = false;
    }

    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    glfwTerminate();
}

} // namespace grn
