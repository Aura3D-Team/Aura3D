#ifndef RENDERER_FACTORY_H
#define RENDERER_FACTORY_H

#pragma once

#include <memory>
#include "aura/Renderer/IRenderer.h"

namespace aura3d {

/**
 * @brief Factory that instantiates the appropriate IRenderer for the current platform.
 *
 * Backend priority (highest to lowest) when no explicit choice is requested:
 *   WASM  : OPENGL  (WebGL2 via Emscripten)
 *   Android: VULKAN (modern mobile GPU path)
 *   Desktop: VULKAN → OPENGL → SOFTWARE
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
     * Walks the VULKAN → OPENGL → SOFTWARE chain starting at @p choice and
     * returns the first compiled-in entry. When @p choice is available it is
     * returned unchanged. Purely a query: nothing is constructed or logged.
     */
    static RendererChoice resolve(RendererChoice choice) noexcept;

    /**
     * @brief Constructs a renderer, degrading gracefully to an available backend.
     *
     * If @p choice was not compiled in, logs a warning and falls back along
     * VULKAN → OPENGL → SOFTWARE rather than throwing.
     *
     * @throws std::runtime_error only if no backend at all was compiled in.
     */
    static std::unique_ptr<IRenderer> create(RendererChoice choice,
                                             const wma::WindowDetails& details);
};

} // namespace aura3d

#endif // RENDERER_FACTORY_H
