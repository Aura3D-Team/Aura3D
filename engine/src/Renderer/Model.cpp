#include "aura/Renderer/Model.h"

#include <map>
#include <utility>

#include "aura/Core/Gltf/Animation.h"

namespace aura3d
{

ModelHandles uploadModel(IRenderer &renderer, const gltf::Model &model)
{
    //! A glTF texture can be sampled as colour (sRGB) and as data (linear), which are different GPU textures.
    std::map<std::pair<i32, bool>, TextureHandle> textures;
    const auto texture = [&](i32 index, bool srgb) -> TextureHandle
    {
        if (index < 0 || static_cast<usize>(index) >= model.textures.size())
            return {};
        const auto key = std::pair{index, srgb};
        if (const auto found = textures.find(key); found != textures.end())
            return found->second;
        const gltf::Texture &source = model.textures[static_cast<usize>(index)];
        TextureHandle handle;
        if (source.image >= 0 && static_cast<usize>(source.image) < model.images.size())
        {
            const ImageData &image = model.images[static_cast<usize>(source.image)];
            if (!image.pixels.empty())
                handle = renderer.createTexture({.width = static_cast<u32>(image.width),
                                                 .height = static_cast<u32>(image.height),
                                                 .format = srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm,
                                                 .sampler = source.sampler},
                                                std::as_bytes(std::span(image.pixels)));
        }
        textures.emplace(key, handle);
        return handle;
    };

    //! glTF's default material, and the albedo every material without a base colour map needs: an invalid
    //! handle would keep whatever texture the previous draw bound.
    const TextureHandle white = renderer.createSolidColorTexture(255, 255, 255);
    std::vector<MaterialHandle> materials;
    for (const gltf::Material &source : model.materials)
    {
        const TextureHandle albedo = texture(source.baseColorTexture, true);
        materials.push_back(
            renderer.createMaterial({.albedo = isValidHandle(albedo) ? albedo : white,
                                     .tint = source.baseColor,
                                     .roughness = source.roughness,
                                     .metallic = source.metallic,
                                     .normalMap = texture(source.normalTexture, false),
                                     .metallicRoughness = texture(source.metallicRoughnessTexture, false),
                                     .emissiveMap = texture(source.emissiveTexture, true),
                                     .emissive = source.emissive}));
    }
    MaterialHandle fallback;

    ModelHandles handles;
    for (const gltf::Mesh &mesh : model.meshes)
    {
        auto &parts = handles.meshes.emplace_back();
        for (const gltf::Primitive &primitive : mesh.primitives)
        {
            MaterialHandle material;
            if (primitive.material >= 0 && static_cast<usize>(primitive.material) < materials.size())
                material = materials[static_cast<usize>(primitive.material)];
            else
            {
                if (!isValidHandle(fallback))
                    fallback = renderer.createMaterial({.albedo = white, .metallic = 1.0f});
                material = fallback;
            }
            parts.push_back({renderer.createMesh(primitive.mesh), material});
        }
    }
    return handles;
}

void appendDrawItems(const gltf::Model &model, const ModelHandles &handles, std::span<const glm::mat4> world,
                     const glm::mat4 &placement, std::vector<IRenderer::DrawItem> &out)
{
    for (usize n = 0; n < model.nodes.size() && n < world.size(); ++n)
    {
        const gltf::Node &node = model.nodes[n];
        if (node.mesh < 0 || static_cast<usize>(node.mesh) >= handles.meshes.size())
            continue;
        const glm::mat4 transform = node.skin >= 0 ? placement : placement * world[n];
        for (const ModelHandles::Part &part : handles.meshes[static_cast<usize>(node.mesh)])
            out.push_back({part.mesh, part.material, transform});
    }
}

void updateSkins(IRenderer &renderer, const gltf::Model &model, ModelHandles &handles, std::span<const glm::mat4> world,
                 const JobSystem *jobs)
{
    for (const gltf::Node &node : model.nodes)
    {
        if (node.skin < 0 || node.mesh < 0 || static_cast<usize>(node.skin) >= model.skins.size() ||
            static_cast<usize>(node.mesh) >= handles.meshes.size())
            continue;
        gltf::jointMatrices(model.skins[static_cast<usize>(node.skin)], world, handles.joints);
        const gltf::Mesh &mesh = model.meshes[static_cast<usize>(node.mesh)];
        for (usize p = 0; p < mesh.primitives.size(); ++p)
        {
            const gltf::Primitive &primitive = mesh.primitives[p];
            if (primitive.joints.empty())
                continue;
            handles.skinned.resize(primitive.mesh.vertices.size());
            gltf::skin(primitive, handles.joints, handles.skinned, jobs);
            renderer.updateMesh(handles.meshes[static_cast<usize>(node.mesh)][p].mesh, handles.skinned);
        }
    }
}

} // namespace aura3d
