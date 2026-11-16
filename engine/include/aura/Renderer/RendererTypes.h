#ifndef RENDERER_TYPES_H
#define RENDERER_TYPES_H

#pragma once

#include <cctype>
#include <string>

#include "aura/Core/AuraCore.h"

/**
 * @brief List of available graphics backend renderers.
 * Uses X-Macros for synchronized enum and string conversions.
 */
#define RENDERER_LIST                                                                                                  \
    X(SOFTWARE)                                                                                                        \
    X(OPENGL)                                                                                                          \
    X(VULKAN)                                                                                                          \
    X(METAL)

namespace aura3d
{

/**
 * @brief Strongly-typed enum representing the chosen graphics API backend.
 */
enum class RendererChoice
{
#define X(name) name,
    RENDERER_LIST
#undef X
};

/**
 * @brief Converts a string representation to a RendererChoice enum.
 * * Case-insensitive. Also maps "CPU" to RendererChoice::SOFTWARE.
 * * @param[in] s The input string to parse.
 * @param[out] out The destination enum variable.
 * @return true If the string matches a known backend, false otherwise.
 */
inline bool RendererChoiceFromString(const std::string &s, RendererChoice &out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

#define X(name)                                                                                                        \
    if (up == #name)                                                                                                   \
    {                                                                                                                  \
        out = RendererChoice::name;                                                                                    \
        return true;                                                                                                   \
    }
    RENDERER_LIST
#undef X

    if (up == "CPU")
    {
        out = RendererChoice::SOFTWARE;
        return true;
    }

    return false;
}

/**
 * @brief Converts a RendererChoice enum value to its exact string representation.
 * * @param[in] c The renderer enum value.
 * @return const char* A static string literal matching the enum identifier, or "UNKNOWN".
 */
inline const char *RendererChoiceToString(RendererChoice c)
{
    switch (c)
    {
    case RendererChoice::SOFTWARE:
        return "SOFTWARE";
    case RendererChoice::OPENGL:
        return "OPENGL";
    case RendererChoice::VULKAN:
        return "VULKAN";
    case RendererChoice::METAL:
        return "METAL";
    }
    return "UNKNOWN";
}

/// What a backend supports beyond the API every backend has.
enum class RendererCaps : u8
{
    None = 0,
    Pbr = 1u << 0u,           //!< Material maps, metallic-roughness shading, point and spot lights.
    Instancing = 1u << 1u,    //!< drawMeshInstanced() is one instanced draw rather than a loop.
    RenderTargets = 1u << 2u, //!< createRenderTarget().
    Compute = 1u << 3u,       //!< Buffers, dispatch() and drawMeshInstancedIndirect(); software runs ComputeDesc::cpu.
    CustomPipelines = 1u << 4u, //!< createPipeline() and drawPipeline(): your own shaders.
};
INK_ENUM_FLAGS(RendererCaps) // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

} // namespace aura3d

#endif // RENDERER_TYPES_H
