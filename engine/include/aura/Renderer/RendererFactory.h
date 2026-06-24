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
     * @brief Constructs and returns a renderer for the given backend.
     * @throws std::runtime_error if the requested backend was not compiled in.
     */
    static std::unique_ptr<IRenderer> create(RendererChoice choice,
                                             const wma::WindowDetails& details);
};

} // namespace aura3d

#endif // RENDERER_FACTORY_H
