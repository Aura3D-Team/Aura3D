#ifdef SDL_WINDOW_MANAGER

#include "SDLAuraWindowManager.h"

#include <chrono>
#include <thread>
#include <SDL2/SDL_vulkan.h>

#include <AuraException/AuraException.h>
#ifdef USE_CPU
#include <CpuAura/CpuFrameBufferManager.h>
#endif

#include <ink/ink.hpp>

namespace aura3d {

SDLAuraWindowManager::SDLAuraWindowManager(WindowDetails windowDetails) :
    _window(nullptr), _windowShouldClose(false),
    _windowDetails(windowDetails), _windowFlags({}),
    _keyboardListener(nullptr)
{
    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        throw AuraException("Failed to initialize SDL: " + std::string(SDL_GetError()));
    }

#if defined(USE_OPENGL_API)
    // Set window hints for OpenGL context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_f64BUFFER, 1);
#elif defined(USE_VULKAN_API)
    // Not needed if you have a VkInstanceManager
    if (SDL_Vulkan_LoadLibrary(nullptr) != VK_SUCCESS) {
        throw AuraException("Failed to load Vulkan library in SDL.");
    }
#endif

    INK_INFO << "SDL2 initialized.";
}

SDLAuraWindowManager::~SDLAuraWindowManager()
{
    if (_window) {
        SDL_DestroyWindow(_window);
        INK_DEBUG << "SDL2 window destroyed.";
    }
    SDL_Quit();
    INK_DEBUG << "SDL2 terminated.";
}

SDL_Window* SDLAuraWindowManager::getWindowInstance()
{
    return _window;
}

WindowFlags* SDLAuraWindowManager::getWindowFlags()
{
    return &_windowFlags;
}

WindowDetails* SDLAuraWindowManager::getWindowDetails()
{
    return &_windowDetails;
}

void SDLAuraWindowManager::eventLoop(SDL_Event& event)
{
    while (SDL_PollEvent(&event)) {
        switch (event.type)
        {
            case SDL_QUIT:
                _windowShouldClose = true;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    _windowFlags.resized = true;
                    int newWidth = event.window.data1;
                    int newHeight = event.window.data2;
                    _windowDetails.width = newWidth;
                    _windowDetails.height = newHeight;
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                _keyboardListener->keyCallback(event.key);
                break;
            default:
                // Handle unexpected state, if necessary
                break;
        }
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
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN// CPU rendering
#elif defined(USE_OPENGL_API)
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
#elif defined(USE_VULKAN_API)
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
#endif
        );

    INK_ASSERT_MSG(_window != nullptr, "Failed to create SDL2 Window!");

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
        [this]() { INK_INFO << "Metatada key m pressed"; }, // onPress
        [this]() { INK_INFO << "Metatada key m released"; } // onRelease
    ));

#ifdef USE_OPENGL_API
    // If OpenGL context is needed, create and initialize OpenGL context
    SDL_GLContext glContext = SDL_GL_CreateContext(_window);
    if (!glContext) {
        SDL_DestroyWindow(_window);
        SDL_Quit();
        throw aura3d::AuraException("Failed to create OpenGL context");
    }

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(_window);
        SDL_Quit();
        throw aura3d::AuraException("Failed to load OpenGL functions");
    }

    INK_INFO << "OpenGL context created successfully.";
#endif

    INK_INFO << "SDL Window created: " << windowName;
}

void SDLAuraWindowManager::process(std::function<void ()>&& actions)
{
    SDL_Event event;

    const std::chrono::milliseconds frameTime(1000 / _windowDetails.targetFPS);
    // Main event loop
    while (!_windowShouldClose) {
        std::chrono::time_point startTime = std::chrono::high_resolution_clock::now();

        eventLoop(event);

        if (_windowDetails.resizable)
        {
            _windowDetails.resizable = false;
            continue;
        }

        actions();  // Execute the actions provided by the user

#ifdef USE_OPENGL_API
        // OpenGL rendering
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        SDL_GL_SwapWindow(_window);  // Swap OpenGL buffers
#endif
        _windowFlags.frame_counter++;

        std::chrono::time_point endTime = std::chrono::high_resolution_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        if (elapsedTime < frameTime) {
            std::this_thread::sleep_for(frameTime - elapsedTime);
        }
    }
}

std::vector<const char*> SDLAuraWindowManager::getVulkanExtensions() const
{
    unsigned int count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(_window, &count, nullptr)) {
        throw AuraException("Failed to get Vulkan extension count: " + std::string(SDL_GetError()));
    }

    std::vector<const char*> extensions(count);
    if (!SDL_Vulkan_GetInstanceExtensions(_window, &count, extensions.data())) {
        throw AuraException("Failed to get Vulkan extensions: " + std::string(SDL_GetError()));
    }

    return extensions;
}

}

#endif
