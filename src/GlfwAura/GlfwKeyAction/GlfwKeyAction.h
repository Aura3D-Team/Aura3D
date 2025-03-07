#ifndef GLFWKEYACTION_H
#define GLFWKEYACTION_H

#pragma once

#include <functional>

namespace aura3d {

class GlfwKeyAction {
public:
    GlfwKeyAction(std::function<void()> onPress = nullptr, std::function<void()> onRelease = nullptr)
        : _onPress(std::move(onPress)), _onRelease(std::move(onRelease)) {}

    void onPressAction() {
        if (_onPress) _onPress();
    }

    void onReleaseAction() {
        if (_onRelease) _onRelease();
    }

private:
    std::function<void()> _onPress;
    std::function<void()> _onRelease;
};

} // namespace aura

#endif // GLFWKEYACTION_H
