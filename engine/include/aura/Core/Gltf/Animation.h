#ifndef AURA_GLTF_ANIMATION_H
#define AURA_GLTF_ANIMATION_H

#pragma once

#include <span>
#include <vector>

#include "aura/Core/Gltf/Gltf.h"

namespace aura3d
{
class JobSystem;
}

/**
 * @file Animation.h
 * @brief Keyframe playback and skinning for a gltf::Model, on the CPU, so every backend draws the same pose.
 *
 * A frame: animate() poses the nodes, worldMatrices() places them, jointMatrices() turns a skin's joints into
 * deform matrices and skin() applies them to a primitive's bind-pose vertices. aura/Renderer/Model.h strings
 * these together and streams the result to the GPU with IRenderer::updateMesh().
 */
namespace aura3d::gltf
{

/// Sets the TRS of every node @p animation drives to its value at @p seconds, clamped to [0, duration];
/// wrap the time first to loop. Linear rotations take the shorter arc (slerp), cubic splines use the file's tangents.
void animate(std::span<Node> nodes, const Animation &animation, f32 seconds);

/// Deform matrices of @p skin: each joint's world matrix times its inverse bind matrix, so a bind-pose vertex
/// lands in world space. @p world comes from worldMatrices().
void jointMatrices(const Skin &skin, std::span<const glm::mat4> world, std::vector<glm::mat4> &out);

/// @p primitive's bind-pose vertices blended by @p joints into @p out, positions and normals; texture coordinates
/// and colours are copied. @p out holds as many vertices as the primitive. Split across @p jobs when given.
void skin(const Primitive &primitive, std::span<const glm::mat4> joints, std::span<gfx::Vertex3D> out,
          const JobSystem *jobs = nullptr);

} // namespace aura3d::gltf

#endif // AURA_GLTF_ANIMATION_H
