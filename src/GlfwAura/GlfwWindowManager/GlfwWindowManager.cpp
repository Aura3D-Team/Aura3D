#include "GlfwWindowManager.h"
#include "VkAura/VkException/VkException.h"
#include "plog/Log.h"

namespace aura3d {

GlfwWindowManager::GlfwWindowManager(const int width, const int height, bool resizable)
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


    glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

#if defined(GLFW_INCLUDE_VULKAN)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#elif defined(GLFW_INCLUDE_OPENGL)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE)
#endif

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

#ifdef GLFW_INCLUDE_OPENGL
    glfwMakeContextCurrent(_window);
#endif

    if (!_window) {
        glfwTerminate();
        throw VkException("Failed to create GLFW window");
    }

    PLOG_INFO << "Created window: " << windowName;
}

void GlfwWindowManager::process(const std::function<void ()>& actions)
{
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents();
        actions();
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

}
