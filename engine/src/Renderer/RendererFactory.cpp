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

#include <array>
#include <stdexcept>

#include "aura/aura.h"

namespace aura3d {

namespace {

/*
 * Degradation order shared by resolve() and create(). Starting from the
 * requested backend we walk towards progressively more portable ones, so a
 * missing Vulkan driver lands on OpenGL and finally on the always-portable
 * software rasteriser.
 */
constexpr std::array<RendererChoice, 3> kFallbackChain = {
    RendererChoice::VULKAN,
    RendererChoice::OPENGL,
    RendererChoice::SOFTWARE,
};

} // namespace

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

bool RendererFactory::isAvailable(RendererChoice choice) noexcept
{
    switch (choice)
    {
    case RendererChoice::VULKAN:
#ifdef AURA_HAS_VULKAN
        return true;
#else
        return false;
#endif

    case RendererChoice::OPENGL:
#ifdef AURA_HAS_OPENGL
        return true;
#else
        return false;
#endif

    case RendererChoice::SOFTWARE:
#ifdef AURA_HAS_CPU
        return true;
#else
        return false;
#endif
    }
    return false;
}

RendererChoice RendererFactory::resolve(RendererChoice choice) noexcept
{
    if (isAvailable(choice))
        return choice;

    //! Continue the chain from the requested entry downwards.
    bool reached = false;
    for (RendererChoice candidate : kFallbackChain) 
    {
        if (candidate == choice)
            reached = true;

        if (reached && isAvailable(candidate))
            return candidate;
    }

    //! The request sits outside the chain (or nothing below it exists):
    //! accept anything that was compiled in.
    for (RendererChoice candidate : kFallbackChain) 
    {
        if (isAvailable(candidate))
            return candidate;
    }

    return choice;
}

std::unique_ptr<IRenderer> RendererFactory::create(RendererChoice choice,
                                                   const wma::WindowDetails& details)
{
    const RendererChoice resolved = resolve(choice);

    if (resolved != choice) 
    {
        INK_WARN << "RendererFactory: backend '" << RendererChoiceToString(choice)
                 << "' was not compiled into this build; falling back to '"
                 << RendererChoiceToString(resolved) << "'";
    }

    switch (resolved)
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
        //! Only reachable when every backend was switched off at configure time,
        //! which leaves nothing to degrade to.
        throw std::runtime_error(
            "RendererFactory: no renderer backend was compiled into this build "
            "(enable at least one of AURA_ENABLE_VULKAN/OPENGL/CPU)");
    }
}

} // namespace aura3d
