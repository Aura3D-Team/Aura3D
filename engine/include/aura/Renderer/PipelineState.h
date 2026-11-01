#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#pragma once

#include <ink/ink_base.hpp>

namespace aura3d
{

//! The fixed-function switches every GPU backend's pipelines share.
enum class PipelineState : u8
{
    None = 0,
    DepthTest = 1u << 0u,     //!< Enables the depth test, and writes unless DepthWrite is clear.
    DepthWrite = 1u << 1u,    //!< Ignored without DepthTest.
    AlphaBlend = 1u << 2u,    //!< src*srcAlpha + dst*(1-srcAlpha).
    CullBackFaces = 1u << 3u, //!< Discards clockwise-wound back faces.
};
INK_ENUM_FLAGS(PipelineState) // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

} // namespace aura3d

#endif // PIPELINE_STATE_H
