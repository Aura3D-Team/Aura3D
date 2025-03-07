#include "GlfwWindowManager.h"

#include <plog/Log.h>
#include <VkAura/VkException/VkException.h>

#include <GlfwAura/GlfwKeyboardListener/GlfwKeyboardListener.h>

namespace aura3d {

// Callback for framebuffer size change (resizing the window)
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
#if defined(GLFW_INCLUDE_VULKAN)
    WindowFlags* windowFlags = static_cast<WindowFlags*>(glfwGetWindowUserPointer(window));
    windowFlags->resized = true;
#elif defined(GLFW_INCLUDE_OPENGL)
    glViewport(0, 0, width, height);
#endif
}

GlfwWindowManager::GlfwWindowManager(WindowDetails windowDetails)
    : _window(nullptr), _windowDetails(windowDetails), _windowFlags({}), _keyboardListener(nullptr)
{
    // Initialize GLFW
    if (!glfwInit()) {
        throw aura3d::VkException("Failed to initialize GLFW");
    }

    // Check for Vulkan support in GLFW
    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw aura3d::VkException("Vulkan is not supported by GLFW");
    }

    // Set window hints for OpenGL/Vulkan context
    glfwWindowHint(GLFW_RESIZABLE, _windowDetails.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

// Initialize GLFW window for Vulkan or OpenGL
#if defined(GLFW_INCLUDE_VULKAN)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    PLOG_INFO << "GLFW initialized with Vulkan support.";
#elif defined(GLFW_INCLUDE_OPENGL)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Optional, may not be necessary
    PLOG_INFO << "GLFW initialized with OpenGL support.";
#endif
}

GlfwWindowManager::~GlfwWindowManager()
{
    if (_window) {
        glfwDestroyWindow(_window);
        PLOG_DEBUG << "GLFW window destroyed.";
    }
    glfwTerminate();
    PLOG_DEBUG << "GLFW terminated";
}

GLFWwindow* GlfwWindowManager::getWindowInstance()
{
    return _window;
}

void GlfwWindowManager::createGlfwWindowManager(const char* windowName)
{
    // Create GLFW window
    _window = glfwCreateWindow(_windowDetails.width, _windowDetails.height, windowName, nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        throw aura3d::VkException("Failed to create GLFW window");
    }

#ifdef GLFW_INCLUDE_OPENGL
    // If using OpenGL, make the context current and load OpenGL functions
    glfwMakeContextCurrent(_window);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        glfwDestroyWindow(_window);
        glfwTerminate();
        throw VkException("Failed to load OpenGL functions for GLFW window");
    }
#endif

    glfwSetFramebufferSizeCallback(_window, framebuffer_size_callback);

    _windowFlags = {
        .resized = false,
    };

    glfwSetWindowUserPointer(_window, &_windowFlags);

    _keyboardListener = std::make_unique<GlfwKeyboardListener>(_window);

    PLOG_INFO << "Created window and listeners: " << windowName;
}

void GlfwWindowManager::process(std::function<void()>&& actions)
{
    int count = 0;
    // Main event loop
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents(); // Poll for events (input, window resize, etc.)

        count++;

        actions();  // Execute the actions provided by the user

#ifdef GLFW_INCLUDE_OPENGL
        // OpenGL rendering
        glfwSwapBuffers(_window);  // Swap buffers (draw to the window)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);  // GL Clear buffers
#endif
    }

    PLOG_INFO << "Window event loop ended.";
    PLOG_INFO << count << " frames processed!";
}

WindowFlags* GlfwWindowManager::getWindowFlags()
{
    return &_windowFlags;
}

std::vector<const char*> GlfwWindowManager::getGlfwVulkanExtensions() const
{
    std::vector<const char*> glfwExtensions;
    uint32_t glfwExtensionCount = 0;

    // Get required Vulkan extensions for GLFW
    const char** exts = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (exts != nullptr) {
        glfwExtensions.assign(exts, exts + glfwExtensionCount);
    }

    return glfwExtensions;
}

}  // namespace aura3d
