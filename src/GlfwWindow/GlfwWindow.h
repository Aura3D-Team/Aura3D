#ifndef GLFWWINDOW_H
#define GLFWWINDOW_H

#pragma once

#include <GLFW/glfw3.h>
#include <functional>

/**
 * @class GlfwWindow
 *
 * A wrapper class to manage the lifecycle of a GLFW window, designed for use with Vulkan.
 * It initializes GLFW, creates a Vulkan-compatible window, and provides a method to handle
 * the window's main loop with user-defined actions.
 */
class GlfwWindow
{
public:
    /**
     * Parameterized constructor to create a Vulkan-compatible window.
     * Initializes GLFW and creates a window of the specified width and height.
     * Throws an exception if window creation fails.
     *
     * @param width The width of the window.
     * @param height The height of the window.
     * @param windowName The title of the window.
     */
    GlfwWindow(int width, int height, const char* windowName);

    /**
     * Destructor. Cleans up and terminates GLFW.
     */
    ~GlfwWindow();

    /**
     * Returns the underlying GLFW window instance.
     *
     * @return GLFWwindow* A pointer to the GLFW window.
     */
    GLFWwindow* getWindowInstance();

    /**
     * Main loop processing function. Runs the window's event loop and executes user-defined
     * actions passed as a callback function.
     *
     * @param actions A callback function that contains custom actions to perform in the loop.
     */
    void process(const std::function<void ()>& actions);

private:
    /**
     * Helper function to initialize GLFW. Throws an exception if initialization fails or Vulkan is not supported.
     */
    void initializeGLFW();

    GLFWwindow* _window;  ///< The GLFW window instance.
    int _width;           ///< The width of the window.
    int _height;          ///< The height of the window.
};

#endif // GLFWWINDOW_H
