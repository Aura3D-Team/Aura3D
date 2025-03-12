#include "AuraKeyboardListener.h"

#include <plog/Log.h>

namespace aura3d {

std::unordered_map<int, AuraKeyAction> AuraKeyboardListener::_mapKeyActions;


AuraKeyboardListener::AuraKeyboardListener(WindowAPI* window) : _window(window)
{
#ifdef SDL_WINDOW_MANAGER
    SDL_SetWindowData(window, "AuraKeyboardListener", this);
#else
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, AuraKeyboardListener::keyCallback);
#endif
}

void AuraKeyboardListener::addKeyAction(int key, AuraKeyAction keyAction) {
    _mapKeyActions[key] = std::move(keyAction);
}

void AuraKeyboardListener::keyCallback(SDL_KeyboardEvent keyEvent)
{
    auto it = _mapKeyActions.find(keyEvent.keysym.sym);
    if (it != _mapKeyActions.end()) {
        if (keyEvent.state == SDL_PRESSED) {
            it->second.onPressAction();
        } else if (keyEvent.state == SDL_RELEASED) {
            it->second.onReleaseAction();
        }
    }
}

void AuraKeyboardListener::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
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
