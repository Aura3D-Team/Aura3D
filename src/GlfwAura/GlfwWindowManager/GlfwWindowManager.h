#ifndef GLFWWINDOWMANAGER_H
#define GLFWWINDOWMANAGER_H

#pragma once

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <functional>
#include <memory>

#include <GlfwAura/GlfwKeyboardListener/GlfwKeyboardListener.h>

namespace aura3d {

struct WindowDetails {
    int width;
    int height;
    bool resizable;
};

struct WindowFlags {
    bool resized;
};

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

/**
 * @class GlfwWindowManager
 *
 * A wrapper class to manage the lifecycle of a GLFW window, designed for use with Vulkan.
 * It initializes GLFW, creates a Vulkan-compatible window, and provides a method to handle
 * the window's main loop with user-defined actions.
 */
class GlfwWindowManager
{
public:
    /**
     * @brief Create a Vulkan-compatible window and manage it resources.
     *
     * Initializes GLFW and creates a window of the specified width and height.
     * Throws an exception if window creation fails.
     *
     * @param width The width of the window.
     * @param height The height of the window.
     */
    GlfwWindowManager(WindowDetails windowDetails);

    /**
     * @brief Cleans up and terminates GLFW.
     */
    ~GlfwWindowManager();

    /**
     * @brief Returns the underlying GLFW window instance.
     *
     * @return GlfwWindowManager* A pointer to the GLFW window.
     */
    GLFWwindow* getWindowInstance();

    /**
     * @brief Main loop processing function. Runs the window's event loop and executes user-defined
     * actions passed as a callback function.
     *
     * @param actions A callback function that contains custom actions to perform in the loop.
     */
    void process(std::function<void ()>&& actions);

    /**
     * @brief Create a cross-plataform Vulkan-compatible window
     *
     * @param windowName The title of the window.
     */
    void createGlfwWindowManager(const char* windowName);

    /**
     * @brief Retrives Vulkan required extensions for surface creation.
     */
    std::vector<const char*> getGlfwVulkanExtensions() const;

    WindowFlags* getWindowFlags();

private:
    GLFWwindow* _window;  ///< The GLFW window instance.

    WindowDetails _windowDetails;

    WindowFlags _windowFlags;
    std::unique_ptr<GlfwKeyboardListener> _keyboardListener;
};

}

#endif // GLFWWINDOWMANAGER_H
