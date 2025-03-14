#ifdef SDL_WINDOW_MANAGER

#ifndef SDLAURAWINDOWMANAGER_H
#define SDLAURAWINDOWMANAGER_H

#pragma once

#include <SDL2/SDL.h>
#include <glad/gl.h>
#include <vector>
#include <functional>
#include <thread>

#include <AuraWindowManagers/AuraKeyboardListener/AuraKeyboardListener.h>
#include <AuraWindowManagers/CommonWindow.hpp>

namespace aura3d {

/**
 * @class SDLAuraWindowManager
 *
 * A wrapper class to manage the lifecycle of an SDL2 window, designed for use with both Vulkan and OpenGL.
 * It initializes SDL2, creates a Vulkan/OpenGL-compatible window, and provides a method to handle
 * the window's main loop with user-defined actions.
 */
class SDLAuraWindowManager
{
public:
    /**
     * @brief Create a Vulkan/OpenGL-compatible window and manage its resources.
     *
     * Initializes SDL2 and creates a window of the specified width and height.
     * Throws an exception if window creation fails.
     *
     * @param windowDetails The window configuration details.
     */
    SDLAuraWindowManager(WindowDetails windowDetails);

    /**
     * @brief Cleans up and terminates SDL2.
     */
    ~SDLAuraWindowManager();

    /**
     * @brief Returns the SDL_Window instance.
     *
     * @return SDL_Window* A pointer to the SDL_Window.
     */
    SDL_Window* getWindowInstance();

    /**
     * @brief Main loop processing function. Runs the window's event loop and executes user-defined
     * actions passed as a callback function.
     *
     * @param actions A callback function that contains custom actions to perform in the loop.
     */
    void process(std::function<void ()>&& actions);

    /**
     * @brief Create an SDL2 window for Vulkan or OpenGL.
     *
     * @param windowName The title of the window.
     */
    void createWindow(const char* windowName);

    /**
     * @brief Retrieves Vulkan required extensions for surface creation.
     */
    std::vector<const char*> getVulkanExtensions() const;

    WindowFlags* getWindowFlags();

private:
    SDL_Window* _window;  ///< The SDL_Window instance.
    bool _windowShouldClose;

    void eventLoop(SDL_Event& event);

    WindowDetails _windowDetails;
    WindowFlags _windowFlags;

    std::unique_ptr<AuraKeyboardListener> _keyboardListener;
};
} // namespace aura3d

#endif // SDLAuraWindowManager_H

#endif
