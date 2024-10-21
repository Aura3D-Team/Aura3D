#include "GlfwWindow.h"
#include "VkException/VkException.h"
#include "plog/Log.h"

GlfwWindow::GlfwWindow(const int width, const int height, const char* windowName)
    : _window(nullptr), _width(width), _height(height)
{
    initializeGLFW();

    // Create a Vulkan-compatible window
    _window = glfwCreateWindow(_width, _height, windowName, nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        throw VkException("Failed to create GLFW window");
    }

    PLOG_INFO << "Created window: " << windowName;
}

GlfwWindow::~GlfwWindow()
{
    if (_window) {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
    PLOG_INFO << "GLFW terminated";
}

GLFWwindow* GlfwWindow::getWindowInstance() {
    return _window;
}

void GlfwWindow::initializeGLFW()
{
    if (!glfwInit()) {
        throw VkException("Failed to initialize GLFW");
    }

    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw VkException("Vulkan not supported by GLFW");
    }

    PLOG_INFO << "GLFW initialized and Vulkan supported.";

    // Set required GLFW window hints for Vulkan, as this project does not use OpenGL
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
}

void GlfwWindow::process(const std::function<void ()>& actions)
{
    while (!glfwWindowShouldClose(_window)) {
        // Clear the screen (optional, may be used for OpenGL or Vulkan)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Execute user-defined actions for each frame
        actions();


        glfwSwapBuffers(_window);

        // Poll for window events (e.g., input, window close, resize)
        glfwPollEvents();
    }

    PLOG_INFO << "Window event loop ended.";
}
