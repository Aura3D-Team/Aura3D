#ifndef AURA_RENDERER_MODEL_H
#define AURA_RENDERER_MODEL_H

#pragma once

#include <span>
#include <vector>

#include "aura/Core/Gltf/Gltf.h"
#include "aura/Renderer/IRenderer.h"

namespace aura3d
{

class JobSystem;

/**
 * @brief A gltf::Model on one renderer: a mesh per primitive, a material per glTF material and a texture per
 *        glTF texture and colour space it is sampled in.
 *
 * @code
 *   auto model = gltf::load("robot.glb");
 *   ModelHandles handles = uploadModel(renderer, *model);
 *   // per frame:
 *   gltf::animate(model->nodes, model->animations[0], std::fmod(t, model->animations[0].duration));
 *   const auto world = gltf::worldMatrices(*model);
 *   updateSkins(renderer, *model, handles, world);       // between passes
 *   items.clear();
 *   appendDrawItems(*model, handles, world, placement, items);
 *   renderer.drawMeshes(items);                          // in a pass
 * @endcode
 */
struct ModelHandles
{
    struct Part
    {
        MeshHandle mesh;
        MaterialHandle material;
    };
    std::vector<std::vector<Part>> meshes; //! Per glTF mesh, per primitive.
    std::vector<gfx::Vertex3D> skinned;    //! updateSkins()'s scratch.
    std::vector<glm::mat4> joints;         //! updateSkins()'s scratch.
};

[[nodiscard]] ModelHandles uploadModel(IRenderer &renderer, const gltf::Model &model);

/// One draw item per primitive of every node with a mesh, placed by @p world under @p placement. A skinned mesh
/// ignores its node's transform, as glTF says: its joints already put it in the model's space.
void appendDrawItems(const gltf::Model &model, const ModelHandles &handles, std::span<const glm::mat4> world,
                     const glm::mat4 &placement, std::vector<IRenderer::DrawItem> &out);

/// Deforms every skinned mesh to @p world's joints and streams it with IRenderer::updateMesh(), so call it where
/// that may be called: between beginFrame() and the pass that draws the model.
void updateSkins(IRenderer &renderer, const gltf::Model &model, ModelHandles &handles, std::span<const glm::mat4> world,
                 const JobSystem *jobs = nullptr);

} // namespace aura3d

#endif // AURA_RENDERER_MODEL_H
