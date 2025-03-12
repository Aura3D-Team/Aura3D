#include "GlfwAuraWindowManager.h"

#include <plog/Log.h>
#include <AuraException/AuraException.h>

namespace aura3d {

GlfwAuraWindowManager::GlfwAuraWindowManager(WindowDetails windowDetails) :
    _window(nullptr), _windowDetails(windowDetails),
    _windowFlags({}), _keyboardListener(nullptr)
{
    // Initialize GLFW
    if (!glfwInit()) {
        throw aura3d::AuraException("Failed to initialize GLFW");
    }

    // Check for Vulkan support in GLFW
    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw aura3d::AuraException("Vulkan is not supported by GLFW");
    }

    // Set window hints for OpenGL/Vulkan context
    glfwWindowHint(GLFW_RESIZABLE, _windowDetails.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

// Initialize GLFW window for Vulkan or OpenGL
#if defined(USE_VULKAN_API)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    PLOG_INFO << "GLFW initialized with Vulkan support.";
#elif defined(USE_OPENGL_API)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Optional, may not be necessary
    PLOG_INFO << "GLFW initialized with OpenGL support.";
#endif
}

GlfwAuraWindowManager::~GlfwAuraWindowManager()
{
    if (_window) {
        glfwDestroyWindow(_window);
        PLOG_DEBUG << "GLFW window destroyed.";
    }
    glfwTerminate();
    PLOG_DEBUG << "GLFW terminated";
}

GLFWwindow* GlfwAuraWindowManager::getWindowInstance()
{
    return _window;
}

void GlfwAuraWindowManager::createWindow(const char* windowName)
{
    // Create GLFW window
    _window = glfwCreateWindow(_windowDetails.width, _windowDetails.height, windowName, nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        throw aura3d::AuraException("Failed to create GLFW window");
    }

#ifdef USE_OPENGL_API
    // If using OpenGL, make the context current and load OpenGL functions
    glfwMakeContextCurrent(_window);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        glfwDestroyWindow(_window);
        glfwTerminate();
        throw AuraException("Failed to load OpenGL functions for GLFW window");
    }
#endif

    glfwSetFramebufferSizeCallback(_window, framebuffer_size_callback);

    _windowFlags = {
        .frame_counter = 0,
        .resized = false
    };

    glfwSetWindowUserPointer(_window, &_windowFlags);

    // KeyboardListener setup
    _keyboardListener = std::make_unique<AuraKeyboardListener>(static_cast<GLFWwindow*>(_window));

    _keyboardListener->addKeyAction(GLFW_KEY_ESCAPE, AuraKeyAction(
        [this]() { glfwSetWindowShouldClose(_window, true); }, // onPress
        nullptr
    ));

    PLOG_INFO << "Created window and listeners: " << windowName;
}

void GlfwAuraWindowManager::process(std::function<void()>&& actions)
{
    int count = 0;
    // Main event loop
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents(); // Poll for events (input, window resize, etc.)

        count++;

        actions();  // Execute the actions provided by the user

#ifdef USE_OPENGL_API
        // OpenGL rendering
        glfwSwapBuffers(_window);  // Swap buffers (draw to the window)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);  // GL Clear buffers
#endif
    }

    PLOG_INFO << "Window event loop ended.";
    PLOG_INFO << count << " frames processed!";
}

WindowFlags* GlfwAuraWindowManager::getWindowFlags()
{
    return &_windowFlags;
}

std::vector<const char*> GlfwAuraWindowManager::getVulkanExtensions() const
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
