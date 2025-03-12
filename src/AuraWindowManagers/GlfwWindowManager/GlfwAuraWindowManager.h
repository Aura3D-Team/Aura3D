#ifndef GLFWAURAWINDOWMANAGER_H
#define GLFWAURAWINDOWMANAGER_H

#pragma once

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <functional>
#include <memory>

#include <AuraWindowManagers/AuraKeyboardListener/AuraKeyboardListener.h>
#include <AuraWindowManagers/CommonWindow.hpp>

namespace aura3d {

/**
 * @class GlfwAuraWindowManager
 *
 * A wrapper class to manage the lifecycle of a GLFW window, designed for use with Vulkan.
 * It initializes GLFW, creates a Vulkan-compatible window, and provides a method to handle
 * the window's main loop with user-defined actions.
 */
class GlfwAuraWindowManager
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
    GlfwAuraWindowManager(WindowDetails windowDetails);

    /**
     * @brief Cleans up and terminates GLFW.
     */
    ~GlfwAuraWindowManager();

    /**
     * @brief Returns the underlying GLFW window instance.
     *
     * @return GlfwAuraWindowManager* A pointer to the GLFW window.
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
    void createWindow(const char* windowName);

    /**
     * @brief Retrives Vulkan required extensions for surface creation.
     */
    std::vector<const char*> getVulkanExtensions() const;

    WindowFlags* getWindowFlags();

private:
    GLFWwindow* _window;  ///< The GLFW window instance.

    WindowDetails _windowDetails;

    WindowFlags _windowFlags;
    std::unique_ptr<AuraKeyboardListener> _keyboardListener;
};

}

#endif // GlfwAuraWindowManager_H
