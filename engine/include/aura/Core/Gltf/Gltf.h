#ifndef AURA_GLTF_H
#define AURA_GLTF_H

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "aura/Core/AuraCore.h"
#include "aura/Core/ImageLoader/ImageLoader.h"
#include "aura/Renderer/Texture.h"

/**
 * @file Gltf.h
 * @brief glTF 2.0 (.gltf with external or data-URI buffers, and .glb) into plain CPU data.
 *
 * Everything an engine needs to draw and animate a file: triangle meshes in gfx::Vertex3D, metallic-roughness
 * materials, decoded images, the node hierarchy, skins and animations. Nothing here touches a renderer;
 * aura/Renderer/Model.h uploads a Model and draws it.
 *
 * Indices refer into the Model's own vectors, -1 meaning none, as in the file.
 */
namespace aura3d::gltf
{

struct Texture
{
    i32 image = -1;
    SamplerDesc sampler{};
};

/// glTF's metallic-roughness material; a factor multiplies its texture when both are present.
struct Material
{
    glm::vec4 baseColor{1.0f};
    f32 metallic = 1.0f;
    f32 roughness = 1.0f;
    glm::vec3 emissive{0.0f};
    i32 baseColorTexture = -1;
    i32 metallicRoughnessTexture = -1;
    i32 normalTexture = -1;
    i32 emissiveTexture = -1;
};

struct Primitive
{
    gfx::Mesh3D mesh;
    i32 material = -1;
    /// Per vertex, parallel to mesh.vertices; empty for a primitive no skin deforms.
    std::vector<glm::uvec4> joints;
    std::vector<glm::vec4> weights;
};

struct Mesh
{
    std::string name;
    std::vector<Primitive> primitives;
};

/// A node's transform is always TRS here; a file's "matrix" is decomposed on load so animation can drive it.
struct Node
{
    std::string name;
    i32 parent = -1;
    std::vector<i32> children;
    i32 mesh = -1;
    i32 skin = -1;
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

struct Skin
{
    std::vector<i32> joints;                    //! Nodes.
    std::vector<glm::mat4> inverseBindMatrices; //! One per joint; identity when the file has none.
};

enum class Interpolation : u8
{
    Linear,
    Step,
    CubicSpline,
};

enum class Path : u8
{
    Translation,
    Rotation,
    Scale,
};

/// One animated property of one node. Values are xyz or a quaternion's xyzw; a cubic spline stores
/// in-tangent, value, out-tangent per key.
struct Channel
{
    i32 node = -1;
    Path path = Path::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<f32> times;
    std::vector<glm::vec4> values;
};

struct Animation
{
    std::string name;
    std::vector<Channel> channels;
    f32 duration = 0.0f; //! The last key of any channel.
};

struct Model
{
    std::vector<ImageData> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;
    std::vector<i32> roots; //! The default scene's root nodes; every parentless node when it names none.
    std::vector<Skin> skins;
    std::vector<Animation> animations;
};

/// Reads @p path, a .gltf or .glb; std::nullopt (logged) when it is missing or malformed.
[[nodiscard]] std::optional<Model> load(const std::filesystem::path &path);
/// The same from bytes already in memory; external buffers and images resolve against @p baseDirectory.
[[nodiscard]] std::optional<Model> parse(std::span<const std::byte> bytes, const std::filesystem::path &baseDirectory);

/// @p node's TRS as one matrix.
[[nodiscard]] glm::mat4 localMatrix(const Node &node) noexcept;
/// Every node's world matrix, parents before children, from the nodes' current TRS.
[[nodiscard]] std::vector<glm::mat4> worldMatrices(const Model &model);

} // namespace aura3d::gltf

#endif // AURA_GLTF_H
