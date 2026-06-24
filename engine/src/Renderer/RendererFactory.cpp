#include "aura/Renderer/RendererFactory.h"

#ifdef AURA_HAS_VULKAN
#  include "aura/Renderer/Vulkan/VulkanRenderer.h"
#endif
#ifdef AURA_HAS_OPENGL
#  include "aura/Renderer/OpenGL/OpenGLRenderer.h"
#endif
#ifdef AURA_HAS_CPU
#  include "aura/Renderer/Software/CPURenderer.h"
#endif

#include <stdexcept>

namespace aura3d {

RendererChoice RendererFactory::defaultChoice() noexcept
{
    /*
     * Compile-time platform priority:
     *
     *   WASM (Emscripten)  → OPENGL  (maps to WebGL2)
     *   Android            → VULKAN  (modern mobile path)
     *   Desktop            → VULKAN  → OPENGL  → SOFTWARE
     */
#if defined(__EMSCRIPTEN__)
    return RendererChoice::OPENGL;
#elif defined(__ANDROID__)
    return RendererChoice::VULKAN;
#elif defined(AURA_HAS_VULKAN)
    return RendererChoice::VULKAN;
#elif defined(AURA_HAS_OPENGL)
    return RendererChoice::OPENGL;
#else
    return RendererChoice::SOFTWARE;
#endif
}

std::unique_ptr<IRenderer> RendererFactory::create(RendererChoice choice,
                                                    const wma::WindowDetails& details)
{
    switch (choice)
    {
#ifdef AURA_HAS_VULKAN
    case RendererChoice::VULKAN:
        return std::make_unique<vk::VulkanRenderer>(details);
#endif

#ifdef AURA_HAS_OPENGL
    case RendererChoice::OPENGL:
        return std::make_unique<gl::OpenGLRenderer>(details);
#endif

#ifdef AURA_HAS_CPU
    case RendererChoice::SOFTWARE:
        return std::make_unique<cpu::CPURenderer>(details);
#endif

    default:
        throw std::runtime_error(
            std::string("RendererFactory: backend '") +
            RendererChoiceToString(choice) +
            "' was not compiled into this build");
    }
}

} // namespace aura3d
