#ifndef AURAKEYBOARDLISTENER_H
#define AURAKEYBOARDLISTENER_H

#pragma once

#include <GLFW/glfw3.h>
#include <SDL2/SDL.h>
#include <unordered_map>
#include "AuraWindowManagers/AuraKeyAction/AuraKeyAction.h"
#include "AuraWindowManagers/CommonWindow.hpp"

namespace aura3d {

class AuraKeyboardListener {
public:
    AuraKeyboardListener(WindowAPI* window);

    static void addKeyAction(int key, AuraKeyAction keyAction);

    static void keyCallback(SDL_KeyboardEvent keyEvent);

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    static std::unordered_map<int, AuraKeyAction> _mapKeyActions;

    WindowAPI* _window;
};

} // namespace aura

#endif // AURAKEYBOARDLISTENER_H
