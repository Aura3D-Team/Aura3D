#ifndef RENDERER_FACTORY_H
#define RENDERER_FACTORY_H

#pragma once

#include <memory>

#include "aura/Core/Camera/Camera.h"
#include "aura/Renderer/IRenderer.h"

namespace aura3d {

/**
 * @brief Factory that instantiates the appropriate IRenderer for the current platform.
 *
 * Backend priority (highest to lowest) when no explicit choice is requested:
 *   WASM        : OPENGL  (WebGL2 via Emscripten)
 *   macOS / iOS : METAL   (the only native GPU API Apple still ships)
 *   Android     : VULKAN  (modern mobile GPU path)
 *   Desktop     : VULKAN → OPENGL → SOFTWARE
 */
class RendererFactory {
public:
    /**
     * @brief Returns the best backend available at compile time.
     *
     * CMake controls which backends are compiled in via AURA_HAS_* defines.
     * This lets the engine pick a sensible default without reading any config.
     */
    static RendererChoice defaultChoice() noexcept;

    /**
     * @brief Reports whether @p choice was compiled into this build.
     *
     * Lets application code query capability before committing to a backend,
     * instead of discovering the answer from a failed create().
     */
    static bool isAvailable(RendererChoice choice) noexcept;

    /**
     * @brief Maps @p choice onto the closest backend that is actually available.
     *
     * When @p choice is available it is returned unchanged. Otherwise
     * defaultChoice() is tried first -- so a config still asking for "vulkan" on
     * an Apple build lands on METAL rather than degrading past it -- and only
     * then is the VULKAN → METAL → OPENGL → SOFTWARE chain walked from @p choice
     * downwards. Purely a query: nothing is constructed or logged.
     */
    static RendererChoice resolve(RendererChoice choice) noexcept;

    /**
     * @brief The clip-space convention @p choice's projections must target.
     *
     * Only the depth range differs between the backends -- every one of them is
     * fed a Y-up GLM projection (Vulkan gets there through a negative-height
     * viewport; see VkGraphicsPipelineManager) -- so this is purely a [0,1] vs
     * [-1,1] question. Vulkan and Metal want [0,1]; OpenGL and the software
     * rasteriser want [-1,1].
     *
     * Lives here, beside the rest of the per-backend knowledge, because it is a
     * property of the backend rather than of whatever happens to be starting one
     * up. create() applies it for you -- this is exposed for code that needs the
     * answer without constructing anything.
     *
     * @note Getting it wrong does not fail loudly: the scene renders and then
     *       z-fights against its own rasteriser.
     */
    [[nodiscard]] static Camera::ClipSpace clipSpaceFor(RendererChoice choice) noexcept;

    /**
     * @brief Constructs a renderer, degrading gracefully to an available backend.
     *
     * If @p choice was not compiled in, logs a warning and falls back as
     * resolve() describes rather than throwing.
     *
     * Also points Camera at the new backend's clip-space convention
     * (@ref clipSpaceFor). That is a process-wide side effect rather than
     * something the caller opts into, and deliberately so: this is the one place
     * every renderer in the engine is born, so doing it here is what stops a
     * caller that constructs a backend directly -- without going through
     * @c Engine -- from silently rendering a z-fighting scene.
     *
     * @throws std::runtime_error only if no backend at all was compiled in.
     */
    static std::unique_ptr<IRenderer> create(RendererChoice choice,
                                             const wma::WindowDetails& details);
};

} // namespace aura3d

#endif // RENDERER_FACTORY_H
