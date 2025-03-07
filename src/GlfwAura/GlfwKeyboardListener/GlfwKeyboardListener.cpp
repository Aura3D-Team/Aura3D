#include "GlfwKeyboardListener.h"

#include <plog/Log.h>

namespace aura3d {

std::unordered_map<int, GlfwKeyAction> GlfwKeyboardListener::_mapKeyActions;

GlfwKeyboardListener::GlfwKeyboardListener(GLFWwindow* window) : _window(window)
{
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, GlfwKeyboardListener::keyCallback);

    setup();
}

void GlfwKeyboardListener::addKeyAction(int key, GlfwKeyAction keyAction) {
    _mapKeyActions[key] = std::move(keyAction);
}

void GlfwKeyboardListener::setup() {
    addKeyAction(GLFW_KEY_ESCAPE, GlfwKeyAction(
        [this]() { glfwSetWindowShouldClose(_window, true); }, // onPress
        nullptr // No action on release
    ));

    addKeyAction(GLFW_KEY_M, aura3d::GlfwKeyAction(
        []() { PLOG_INFO << "OnPress: Metadata"; },
        []() { PLOG_INFO << "OnRelease: Metadata"; }
    ));
}

void GlfwKeyboardListener::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto it = _mapKeyActions.find(key);
    if (it != _mapKeyActions.end()) {
        if (action == GLFW_PRESS) {
            it->second.onPressAction();
        } else if (action == GLFW_RELEASE) {
            it->second.onReleaseAction();
        }
    }
}

} // namespace aura
