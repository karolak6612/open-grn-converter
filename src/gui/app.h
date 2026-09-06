/**
 * @file app.h
 * @brief Main application window and runtime manager for the Dear ImGui GUI.
 */

#pragma once

#include "ui_panels.h"

struct GLFWwindow;

namespace grn {

/**
 * @class App
 * @brief Manages the GLFW window, OpenGL context, and Dear ImGui render loop.
 */
class App {
public:
    /**
     * @brief Constructs the application object.
     */
    App();

    /**
     * @brief Destructs the application object and cleans up GUI resources.
     */
    ~App();

    /**
     * @brief Initializes the window, graphics context, and enters the main loop.
     * @return 0 on success, non-zero on error.
     */
    int run();

    /**
     * @brief Returns the active UI panels instance.
     * @return Reference to UiPanels.
     */
    UiPanels& get_panels() { return panels_; }

    /**
     * @brief Static callback for GLFW drag-and-drop file events.
     * @param window The GLFW window receiving the drop.
     * @param count The number of dropped paths.
     * @param paths Array of UTF-8 encoded file/folder paths.
     */
    static void drop_callback(GLFWwindow* window, int count, const char** paths);

private:
    GLFWwindow* window_ = nullptr;
    UiPanels panels_;
    bool initialized_ = false;

    /**
     * @brief Initializes GLFW and the OpenGL context.
     * @return True if initialized successfully, false otherwise.
     */
    bool init_window();

    /**
     * @brief Shuts down Dear ImGui and GLFW subsystems.
     */
    void shutdown();
};

} // namespace grn
