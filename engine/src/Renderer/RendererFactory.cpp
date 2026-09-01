#include "aura/Renderer/RendererFactory.h"

#ifdef AURA_HAS_VULKAN
#  include "aura/Renderer/Vulkan/VulkanRenderer.h"
#endif
#ifdef AURA_HAS_OPENGL
#  include "aura/Renderer/OpenGL/OpenGLRenderer.h"
#endif
#ifdef AURA_HAS_METAL
#  include "aura/Renderer/Metal/MetalRenderer.h"
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
 *
 * METAL sits beside VULKAN because it is the same kind of backend -- an explicit,
 * modern GPU API -- but it is platform-exclusive in both directions: it exists
 * only on Apple, where VULKAN and OPENGL do not. A linear chain therefore cannot
 * express "prefer the native backend", which is why resolve() consults
 * defaultChoice() before walking this list at all.
 */
constexpr std::array<RendererChoice, 4> kFallbackChain = {
    RendererChoice::VULKAN,
    RendererChoice::METAL,
    RendererChoice::OPENGL,
    RendererChoice::SOFTWARE,
};

} // namespace

Camera::ClipSpace RendererFactory::clipSpaceFor(RendererChoice choice) noexcept
{
    switch (choice)
    {
    case RendererChoice::VULKAN:
    case RendererChoice::METAL:
        return Camera::ClipSpace::Vulkan;

    case RendererChoice::OPENGL:
    case RendererChoice::SOFTWARE:
        return Camera::ClipSpace::OpenGL;
    }

    //! Unreachable for any enumerator -- the switch is exhaustive, so adding a
    //! backend without answering here is a -Wswitch warning, not a silent
    //! fall-through to the wrong convention.
    return Camera::ClipSpace::OpenGL;
}

RendererChoice RendererFactory::defaultChoice() noexcept
{
    /*
     * Compile-time platform priority:
     *
     *   WASM (Emscripten)  → OPENGL  (maps to WebGL2)
     *   macOS / iOS        → METAL   (Apple's OpenGL is deprecated and capped at
     *                                 4.1, and Vulkan exists there only through
     *                                 MoltenVK, which this engine does not use)
     *   Android            → VULKAN  (modern mobile path)
     *   Desktop            → VULKAN  → OPENGL  → SOFTWARE
     */
#if defined(__EMSCRIPTEN__)
    return RendererChoice::OPENGL;
#elif defined(__APPLE__) && defined(AURA_HAS_METAL)
    return RendererChoice::METAL;
#elif defined(__ANDROID__)
    return RendererChoice::VULKAN;
#elif defined(AURA_HAS_VULKAN)
    return RendererChoice::VULKAN;
#elif defined(AURA_HAS_OPENGL)
    return RendererChoice::OPENGL;
#elif defined(AURA_HAS_METAL)
    return RendererChoice::METAL;
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

    case RendererChoice::METAL:
#ifdef AURA_HAS_METAL
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

    /*
     * The platform's own preference outranks the linear chain below. Without
     * this, a settings.json that still reads "vulkan" -- the committed default --
     * would degrade past METAL on an Apple build and land on the software
     * rasteriser, since METAL sits above OPENGL/SOFTWARE in kFallbackChain and is
     * never reached when walking downwards from VULKAN.
     */
    const RendererChoice preferred = defaultChoice();
    if (preferred != choice && isAvailable(preferred))
        return preferred;

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

    std::unique_ptr<IRenderer> renderer;

    switch (resolved)
    {
#ifdef AURA_HAS_VULKAN
    case RendererChoice::VULKAN:
        renderer = std::make_unique<vk::VulkanRenderer>(details);
        break;
#endif

#ifdef AURA_HAS_OPENGL
    case RendererChoice::OPENGL:
        renderer = std::make_unique<gl::OpenGLRenderer>(details);
        break;
#endif

#ifdef AURA_HAS_METAL
    case RendererChoice::METAL:
        renderer = std::make_unique<mtl::MetalRenderer>(details);
        break;
#endif

#ifdef AURA_HAS_CPU
    case RendererChoice::SOFTWARE:
        renderer = std::make_unique<cpu::CPURenderer>(details);
        break;
#endif

    default:
        //! Only reachable when every backend was switched off at configure time,
        //! which leaves nothing to degrade to.
        throw std::runtime_error(
            "RendererFactory: no renderer backend was compiled into this build "
            "(enable at least one of AURA_ENABLE_VULKAN/OPENGL/METAL/CPU)");
    }

    /*
     * Applied here rather than left to the caller because this is the single
     * point every renderer is constructed through. A projection built for the
     * wrong depth range does not fail -- it z-fights -- so the one thing that
     * must not be possible is constructing a backend and forgetting.
     */
    Camera::setClipSpace(clipSpaceFor(resolved));

    return renderer;
}

} // namespace aura3d
