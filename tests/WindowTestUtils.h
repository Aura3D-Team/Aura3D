#ifndef AURA_WINDOW_TEST_UTILS_H
#define AURA_WINDOW_TEST_UTILS_H

#include <functional>
#include <string>
#include <utility>
#include <vector>
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

    //! Runs on every pollEvents(): the events a scripted loop test feeds the window.
    std::function<void(FakeWindow &)> onPoll;

    //! Off, lockFramebuffer() has nothing to give, as a surface that is not ready. On, it hands out a surface
    //! of the framebuffer's size, and each presentFramebuffer() is counted.
    bool presentable = false;
    int presents = 0;
    std::vector<u32> surface;
    //! Off: the platform has taken the surface away (an Android app in the background).
    bool surfaceAvailable = true;
    //! The loop's idle sleeps and its buffer swaps. A wait returns at once and delivers nothing: the next
    //! pollEvents() is where a script's events arrive.
    int waits = 0;
    int swaps = 0;

    //! What a backend does when the platform resizes the window.
    void resize(i32 width, i32 height)
    {
        details.width = width;
        details.height = height;
        flags.markResized();
    }

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
        if (onPoll)
            onPoll(*this);
    }
    void waitEvents(int) override
    {
        ++waits;
    }
    void swapBuffers() override
    {
        ++swaps;
    }
    bool isSurfaceAvailable() const override
    {
        return surfaceAvailable;
    }
    wma::SoftwareFramebuffer lockFramebuffer() override
    {
        if (!presentable)
            return {};
        const wma::FramebufferSize size = getFramebufferSize();
        surface.resize(static_cast<usize>(size.width) * static_cast<usize>(size.height));
        return {surface.data(), size.width, size.height, size.width * static_cast<i32>(sizeof(u32))};
    }
    void presentFramebuffer() override
    {
        ++presents;
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
