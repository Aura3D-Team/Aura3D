#ifndef AURA_WINDOW_TEST_UTILS_H
#define AURA_WINDOW_TEST_UTILS_H

#include <string>
#include <utility>
#include <wma/wma.hpp>

namespace aura3d::test
{

class FakeMouse final : public wma::MouseListener
{
  public:
    void move(f64 x, f64 y)
    {
        lastPosition_ = currentPosition_;
        currentPosition_.x = x;
        currentPosition_.y = y;
        dispatchMove(currentPosition_);
    }
    void press(i32 button = wma::MouseButton::WMALeft)
    {
        dispatchButtonPress(button);
    }
    void release(i32 button = wma::MouseButton::WMALeft)
    {
        dispatchButtonRelease(button);
    }

  private:
    void updateCursorState() override
    {
    }
};

class FakeTouch final : public wma::TouchListener
{
  public:
    void down(wma::TouchFingerId finger, f64 x, f64 y)
    {
        dispatchDown({finger, x, y});
    }
    void move(wma::TouchFingerId finger, f64 x, f64 y)
    {
        dispatchMove({finger, x, y});
    }
    void up(wma::TouchFingerId finger, f64 x, f64 y)
    {
        dispatchUp({finger, x, y});
    }
};

class FakeWindow final : public wma::IWindowManager
{
  public:
    int minimizes = 0;
    int maximizes = 0;
    int restores = 0;
    int closes = 0;
    int titleRequests = 0;
    bool maximized = false;
    bool toplevel = true;
    bool created = true;
    bool textInput = false;
    std::string title;
    wma::WindowDetails details{.width = 480, .height = 240, .decorationMode = wma::DecorationMode::ClientSide};
    wma::WindowFlags flags; // Starts focused.
    wma::KeyboardListener keyboard;
    FakeMouse mouse;
    FakeTouch touch;

    //! Backing-store pixels per window unit (Retina), and the display's own scale (an Android phone).
    i32 pixelRatio = 1;
    f32 contentScale = 1.0f;

    wma::HitTest hitTest;

    //! The clipboard this window offers. Off, it is a backend that has none.
    bool clipboardSupported = true;
    //! False: a platform that declines the text, as a Wayland window does before its first key press.
    bool clipboardAccepts = true;
    std::string clipboardText;

    bool isClipboardSupported() const noexcept override
    {
        return clipboardSupported;
    }
    std::string getClipboardText() override
    {
        return clipboardText;
    }
    bool setClipboardText(std::string_view text) override
    {
        if (!clipboardAccepts)
            return false;
        clipboardText.assign(text);
        return true;
    }

    bool setHitTest(wma::HitTest value) override
    {
        hitTest = std::move(value);
        return true;
    }
    bool minimize() noexcept override
    {
        ++minimizes;
        return true;
    }
    bool maximize() noexcept override
    {
        ++maximizes;
        return true;
    }
    bool restore() noexcept override
    {
        ++restores;
        return true;
    }
    bool isMaximized() const noexcept override
    {
        return maximized;
    }
    bool isToplevel() const noexcept override
    {
        return toplevel;
    }
    wma::DecorationMode getDecorationMode() const noexcept override
    {
        return details.decorationMode;
    }
    bool setTitle(const char *value) noexcept override
    {
        if (!value)
            return false;
        title = value;
        ++titleRequests;
        return true;
    }
    void close() noexcept override
    {
        ++closes;
    }
    bool shouldClose() const override
    {
        return closes != 0;
    }
    void createWindow(const char *value) override
    {
        created = true;
        title = value;
    }
    void pollEvents() override
    {
    }
    void *getWindowInstance() override
    {
        return created ? this : nullptr;
    }
    wma::WindowFlags *getWindowFlags() noexcept override
    {
        return &flags;
    }
    const wma::WindowDetails *getWindowDetails() noexcept override
    {
        return &details;
    }
    const std::vector<const char *> getVulkanExtensions() const override
    {
        return {};
    }
    wma::KeyboardListener &getKeyboardListener() noexcept override
    {
        return keyboard;
    }
    wma::MouseListener &getMouseListener() noexcept override
    {
        return mouse;
    }
    wma::TouchListener &getTouchListener() noexcept override
    {
        return touch;
    }
    wma::FramebufferSize getFramebufferSize() noexcept override
    {
        return {details.width * pixelRatio, details.height * pixelRatio};
    }
    f32 getContentScale() noexcept override
    {
        return contentScale;
    }
    void setTextInputEnabled(bool enabled) noexcept override
    {
        textInput = enabled;
    }
    bool isTextInputEnabled() const noexcept override
    {
        return textInput;
    }
    wma::WindowBackend getBackendType() const override
    {
        return wma::WindowBackend::WAYLAND;
    }
    wma::GraphicsAPI getGraphicsAPI() const override
    {
        return wma::GraphicsAPI::CPU;
    }
    wma::WmaCode destroy() override
    {
        created = false;
        return wma::WmaCode::Ok;
    }
};

} // namespace aura3d::test

#endif
