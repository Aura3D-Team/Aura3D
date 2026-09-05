#ifndef AURA_UI_UIVIEW_H
#define AURA_UI_UIVIEW_H

#pragma once

#include <string>

#include "aura/UI/Backend/DrawListRenderer.h"
#include "aura/UI/Text/TextEngine.h"
#include "aura/UI/UIRoot.h"

/**
 * @file UIView.h
 * @brief AuraUI wired to a renderer and a window: the whole toolkit in one
 *        object.
 *
 * @ref UIRoot is deliberately platform-free. This is the piece that is not: it
 * owns the text engine, the draw list and the GPU backend, and feeds the tree
 * from wma. Use it when the UI is drawn by Aura3D into a wma window; drive a
 * UIRoot yourself for anything else (a test, an offscreen render, another
 * windowing layer).
 *
 * @code
 * aui::UIView ui(renderer, {.fontPath = "resources/fonts/Inter.ttf"});
 * ui.attachInput(*renderer.getWindowManager());
 *
 * auto& page = ui.root().setContent<Column>();
 * page.layout().padding = Thickness::all(24.0f);
 * page.add<Label>("AuraShell").setFontSize(32.0f);
 * page.add<Button>("Launch").clicked.connect([] { launch(); });
 *
 * renderer.run([&] {
 *     renderer.beginRenderPass();
 *     ui.render(deltaSeconds);
 *     renderer.endRenderPass();
 * });
 * @endcode
 */

namespace aura3d {
class IRenderer;
} // namespace aura3d

namespace aura3d::ui {

/// Construction parameters for a @ref UIView.
struct UIViewDesc {
    /// Path to a .ttf/.otf. Empty, or a file that fails to load, falls back to
    /// the engine's embedded bitmap font -- so a UI always draws.
    std::string fontPath;

    u32 glyphPageSize = 1024; //! Edge length of each glyph page, in texels.
    u32 maxFontSizes = 6;     //! Distinct rasterization sizes kept resident.

    Theme theme{};
};

/**
 * @class UIView
 * @brief A widget tree, drawn through an @ref IRenderer and fed from wma.
 *
 * @note Neither copyable nor movable: wma callbacks capture @c this.
 */
class UIView {
public:
    explicit UIView(IRenderer& renderer, const UIViewDesc& desc = UIViewDesc{});
    ~UIView();

    UIView(const UIView&) = delete;
    UIView& operator=(const UIView&) = delete;

    /**
     * @brief Subscribes to @p window's pointer, keyboard, text and touch input.
     *
     * Bindings land in each listener's currently active input context, so push
     * a dedicated one first if the application already binds the same devices
     * there. Text input is enabled and disabled automatically, following
     * whether a field has focus -- which is what raises and lowers a soft
     * keyboard on Android.
     *
     * @param window Must outlive this object.
     */
    void attachInput(wma::IWindowManager& window);
    void detachInput();

    /**
     * @brief Runs the frame: animations, layout, recording and submission.
     *
     * Call between beginRenderPass() and endRenderPass(), after the scene --
     * the UI composites over it with straight alpha and no depth test.
     *
     * Only what changed is redone. A tree with nothing animating and no input
     * re-submits the vertex buffers it already built, so an idle UI costs one
     * draw call and no CPU work.
     */
    void render(f32 deltaSeconds);

    [[nodiscard]] UIRoot& root() noexcept { return _root; }
    [[nodiscard]] const UIRoot& root() const noexcept { return _root; }

    [[nodiscard]] Theme& theme() noexcept { return _root.theme(); }
    [[nodiscard]] AtlasTextShaper& shaper() noexcept { return _shaper; }

    /// @{
    /// The last frame's output, for profiling and for tests. One draw call is
    /// the expected number for a UI that draws no images.
    [[nodiscard]] const DrawList& drawList() const noexcept { return _list; }
    [[nodiscard]] usize drawCallCount() const noexcept { return _backend.batchCount(); }
    /// @}

private:
    /// Matches the tree to the window: logical size, and the device-pixel
    /// ratio the framebuffer implies.
    void _syncSurface();

    /// Reads the cursor and dispatches a move if it went anywhere. Polled
    /// rather than bound, so the UI stays correct while the application owns
    /// the move callback (a camera, usually).
    void _syncPointer();

    IRenderer* _renderer = nullptr;

    //! Declared before _root and _backend, both of which hold a reference to
    //! it for their whole lives.
    AtlasTextShaper _shaper;

    UIRoot _root;
    DrawList _list;
    DrawListRenderer _backend;

    wma::IWindowManager* _window = nullptr;
    wma::MouseListener* _mouse = nullptr;

    glm::vec2 _pointer{0.0f};
    bool _pointerKnown = false;

    //! Set while a finger is down: touch drives the pointer instead of the
    //! mouse, whose position on a touch device is stale or invented.
    bool _touchActive = false;
    wma::TouchFingerId _finger = 0;
    bool _windowFocused = true;
    std::shared_ptr<void> _inputLifetime;
};

} // namespace aura3d::ui

#endif // AURA_UI_UIVIEW_H
