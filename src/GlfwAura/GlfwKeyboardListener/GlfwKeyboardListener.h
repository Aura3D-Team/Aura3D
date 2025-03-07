#ifndef GLFWKEYBOARDLISTENER_H
#define GLFWKEYBOARDLISTENER_H

#pragma once

#include <GLFW/glfw3.h>
#include <unordered_map>
#include "GlfwAura/GlfwKeyAction/GlfwKeyAction.h"

namespace aura3d {

class GlfwKeyboardListener {
public:
    GlfwKeyboardListener(GLFWwindow* window);

    static void addKeyAction(int key, GlfwKeyAction keyAction);

    void setup();
private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    static std::unordered_map<int, GlfwKeyAction> _mapKeyActions;
    GLFWwindow* _window;
};

} // namespace aura

#endif // GLFWKEYBOARDLISTENER_H
