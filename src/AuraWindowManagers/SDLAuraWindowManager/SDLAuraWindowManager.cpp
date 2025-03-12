#include "SDLAuraWindowManager.h"

#include <GLFW/glfw3.h>
#include <SDL2/SDL_vulkan.h>
#include <plog/Log.h>

#include <AuraException/AuraException.h>

namespace aura3d {

SDLAuraWindowManager::SDLAuraWindowManager(WindowDetails windowDetails) :
    _window(nullptr), _windowShouldClose(false),
    _windowDetails(windowDetails), _windowFlags({}),
    _keyboardListener(nullptr)
{
    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw AuraException("Failed to initialize SDL2");
    }

#if defined(USE_OPENGL_API)
    // Set window hints for OpenGL context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#elif defined(USE_VULKAN_API)
    // Not needed if you have a VkInstanceManager
    // if (!SDL_Vulkan_LoadLibrary(nullptr)) {
    //     throw AuraException("Failed to load Vulkan library in SDL.");
    // }
#endif

    PLOG_INFO << "SDL2 initialized.";
}

SDLAuraWindowManager::~SDLAuraWindowManager()
{
    stopEventLoop();
    if (_window) {
        SDL_DestroyWindow(_window);
        PLOG_DEBUG << "SDL2 window destroyed.";
    }
    SDL_Quit();
    PLOG_DEBUG << "SDL2 terminated.";
}

SDL_Window* SDLAuraWindowManager::getWindowInstance()
{
    return _window;
}

WindowFlags* SDLAuraWindowManager::getWindowFlags()
{
    return &_windowFlags;
}

void SDLAuraWindowManager::startEventLoop()
{
    if (!_eventThread.joinable()) {
        _eventThread = std::thread(&SDLAuraWindowManager::eventLoop, this);
    }
}

void SDLAuraWindowManager::stopEventLoop()
{
    if (_eventThread.joinable()) {
        _eventThread.join();
    }
}

void SDLAuraWindowManager::eventLoop()
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type)
        {
            case SDL_QUIT:
                _windowShouldClose = true;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    _windowFlags.resized = true;
                    PLOG_INFO << "Window resized: " << event.window.data1 << " x " << event.window.data2;
                }
                break;
            default:
                // Handle unexpected state, if necessary
                break;
        }

        SDL_Delay(10);
    }
}

void SDLAuraWindowManager::createWindow(const char* windowName)
{
    // Create SDL2 window
    _window = SDL_CreateWindow(
        windowName,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        _windowDetails.width,
        _windowDetails.height,
#ifdef USE_CPU
        SDL_WINDOW_SHOWN // CPU rendering
#elif defined(USE_OPENGL_API)
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
#elif defined(USE_VULKAN_API)
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
#endif
        );

    if (!_window) {
        SDL_Quit();
        throw AuraException("Failed to create SDL2 window");
    }

    _windowFlags = {
        .frame_counter = 0,
        .resized = false
    };

    SDL_SetWindowData(_window, "WindowFlags", &_windowFlags);

    _keyboardListener = std::make_unique<AuraKeyboardListener>(_window);

    _keyboardListener->addKeyAction(SDL_KeyCode::SDLK_ESCAPE, AuraKeyAction(
        [this]() { _windowShouldClose = true; }, // onPress
        nullptr
    ));

    _keyboardListener->addKeyAction(SDL_KeyCode::SDLK_m, AuraKeyAction(
        [this]() { PLOG_INFO << "Metatada key m pressed"; }, // onPress
        [this]() { PLOG_INFO << "Metatada key m released"; } // onRelease
    ));

#ifdef USE_OPENGL_API
    // If OpenGL context is needed, create and initialize OpenGL context
    SDL_GLContext glContext = SDL_GL_CreateContext(_window);
    if (!glContext) {
        SDL_DestroyWindow(_window);
        SDL_Quit();
        throw aura3d::AuraException("Failed to create OpenGL context");
    }

    if (!gladLoadGLLoader((GLADloadfunc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(_window);
        SDL_Quit();
        throw aura3d::AuraException("Failed to load OpenGL functions");
    }

    PLOG_INFO << "OpenGL context created successfully.";
#endif

    PLOG_INFO << "SDL Window created: " << windowName;
}

void SDLAuraWindowManager::process(std::function<void ()>&& actions)
{
    // Main event loop
    while (!_windowShouldClose) {

        actions();  // Execute the actions provided by the user

#ifdef USE_OPENGL_API
        // OpenGL rendering
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        SDL_GL_SwapWindow(_window);  // Swap OpenGL buffers
#endif
        _windowFlags.frame_counter++;
    }
}

std::vector<const char*> SDLAuraWindowManager::getVulkanExtensions() const
{
    std::vector<const char*> sdlExtensions;
    uint32_t sdlExtensionCount = 0;
    const char** exts = {};

    // Get required Vulkan extensions for GLFW
    if (!SDL_Vulkan_GetInstanceExtensions(_window, &sdlExtensionCount, exts)) {
        AuraException("Couldn't extract SDL Vulcan extensions");
    }

    sdlExtensions.assign(exts, exts + sdlExtensionCount);

    return sdlExtensions;
}

}
