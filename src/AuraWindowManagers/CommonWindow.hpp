#ifndef COMMMONWINDOW_HPP
#define COMMMONWINDOW_HPP

#pragma once

#include <SDL2/SDL.h>
#include <GLFW/glfw3.h>
#include <cstdint>

namespace aura3d {
    struct WindowDetails {
        int width;
        int height;
        bool resizable;
    };

    struct WindowFlags {
        uint64_t frame_counter;
        bool resized;
    };

#ifdef SDL_WINDOW_MANAGER
    typedef SDL_Window WindowAPI;
#else
    typedef GLFWwindow WindowAPI;
#endif

    // Callbacks for framebuffer size change (resizing the window)
    inline void framebuffer_size_callback(GLFWwindow* window, int width, int height)
    {
#if defined(GLFW_INCLUDE_VULKAN)
        WindowFlags* windowFlags = static_cast<WindowFlags*>(glfwGetWindowUserPointer(window));
        windowFlags->resized = true;
#elif defined(GLFW_INCLUDE_OPENGL)
        glViewport(0, 0, width, height);
#endif
    }
}

#endif
