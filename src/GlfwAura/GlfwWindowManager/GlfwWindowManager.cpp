#include "GlfwWindowManager.h"
#include "VkAura/VkException/VkException.h"
#include "plog/Log.h"

GlfwWindowManager::GlfwWindowManager(const int width, const int height)
    : _window(nullptr), _width(width), _height(height)
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

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // Disabled for now
}

GlfwWindowManager::~GlfwWindowManager()
{
    if (_window) {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
    PLOG_DEBUG << "GLFW terminated";
}

GLFWwindow* GlfwWindowManager::getWindowInstance() {
    return _window;
}

void GlfwWindowManager::createGlfwWindowManager(const char* windowName)
{
    _window = glfwCreateWindow(_width, _height, windowName, nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        throw VkException("Failed to create GLFW window");
    }

    PLOG_INFO << "Created window: " << windowName;
}

void GlfwWindowManager::process(const std::function<void ()>& actions)
{
    while (!glfwWindowShouldClose(_window)) {
        // Clear the screen (optional, may be used for OpenGL or Vulkan)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Execute user-defined actions for each frame
        actions();

        // Poll for window events (e.g., input, window close, resize)
        glfwPollEvents();
    }

    PLOG_INFO << "Window event loop ended.";
}

std::vector<const char*> GlfwWindowManager::getGlfwVulkanExtensions() const {
    std::vector<const char*> glfwExtensions;
    uint32_t glfwExtensionCount = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    glfwExtensions.assign(exts, exts+glfwExtensionCount);
    return glfwExtensions;
}
