#include "aura/Core/Gltf/Gltf.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include "aura/Core/Files.h"
#include "aura/aura.h"

namespace aura3d::gltf
{
namespace
{

using json = nlohmann::json;

constexpr u32 kGlbMagic = 0x46546C67u; // "glTF"
constexpr u32 kChunkJson = 0x4E4F534Au;
constexpr u32 kChunkBin = 0x004E4942u;
//! What one accessor may expand to: a hostile count must not allocate the machine away.
constexpr usize kMaxAccessorBytes = INK_GIB_TO_BYTES(1);

struct Document
{
    const json &root;
    std::vector<std::vector<std::byte>> buffers;
};

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    u32 value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

[[nodiscard]] std::optional<std::vector<std::byte>> decodeBase64(std::string_view text)
{
    const auto value = [](char c) -> i32
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3);
    u32 bits = 0;
    i32 count = 0;
    for (const char c : text)
    {
        if (c == '=')
            break;
        const i32 digit = value(c);
        if (digit < 0)
            return std::nullopt;
        bits = (bits << 6u) | static_cast<u32>(digit);
        count += 6;
        if (count >= 8)
        {
            count -= 8;
            out.push_back(static_cast<std::byte>((bits >> static_cast<u32>(count)) & 0xFFu));
        }
    }
    return out;
}

//! A data: URI or a file next to the model, percent-escapes decoded.
[[nodiscard]] std::optional<std::vector<std::byte>> resolveUri(const std::string &uri,
                                                               const std::filesystem::path &base)
{
    if (uri.starts_with("data:"))
    {
        const usize comma = uri.find(',');
        if (comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos)
            return std::nullopt;
        return decodeBase64(std::string_view(uri).substr(comma + 1));
    }
    std::string path;
    for (usize i = 0; i < uri.size(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.size())
        {
            path.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        }
        else
            path.push_back(uri[i]);
    }
    return readFileBytes(base / path);
}

[[nodiscard]] u32 componentCount(const std::string &type) noexcept
{
    if (type == "SCALAR")
        return 1;
    if (type == "VEC2")
        return 2;
    if (type == "VEC3")
        return 3;
    if (type == "VEC4" || type == "MAT2")
        return 4;
    if (type == "MAT3")
        return 9;
    return type == "MAT4" ? 16 : 0;
}

[[nodiscard]] u32 componentSize(i32 type) noexcept
{
    switch (type)
    {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

template <typename T> [[nodiscard]] T load(const std::byte *at) noexcept
{
    T value{};
    std::memcpy(&value, at, sizeof(T));
    return value;
}

[[nodiscard]] f32 readComponent(const std::byte *at, i32 type, bool normalized) noexcept
{
    switch (type)
    {
    case 5120:
        return normalized ? std::max(static_cast<f32>(load<i8>(at)) / 127.0f, -1.0f) : static_cast<f32>(load<i8>(at));
    case 5121:
        return normalized ? static_cast<f32>(load<u8>(at)) / 255.0f : static_cast<f32>(load<u8>(at));
    case 5122:
        return normalized ? std::max(static_cast<f32>(load<i16>(at)) / 32767.0f, -1.0f)
                          : static_cast<f32>(load<i16>(at));
    case 5123:
        return normalized ? static_cast<f32>(load<u16>(at)) / 65535.0f : static_cast<f32>(load<u16>(at));
    case 5125:
        return static_cast<f32>(load<u32>(at));
    default:
        return load<f32>(at);
    }
}

[[nodiscard]] u32 readIndex(const std::byte *at, i32 type) noexcept
{
    return type == 5121 ? load<u8>(at) : type == 5123 ? load<u16>(at) : load<u32>(at);
}

/// Hands @p elements elements of @p elementSize bytes from buffer view @p viewIndex to @p sink, after checking
/// every one lies inside the view and its buffer.
template <typename Sink>
[[nodiscard]] bool readView(const Document &document, i32 viewIndex, usize offset, usize elementSize, usize elements,
                            Sink &&sink)
{
    const json &views = document.root.at("bufferViews");
    if (viewIndex < 0 || static_cast<usize>(viewIndex) >= views.size())
        return false;
    const json &view = views[static_cast<usize>(viewIndex)];
    const auto bufferIndex = view.at("buffer").get<usize>();
    if (bufferIndex >= document.buffers.size())
        return false;
    const std::vector<std::byte> &buffer = document.buffers[bufferIndex];
    const usize viewOffset = view.value("byteOffset", usize{0});
    const usize viewLength = view.at("byteLength").get<usize>();
    const usize stride = view.value("byteStride", elementSize);
    if (elements == 0)
        return true;
    //! Each test subtracts what the previous one proved fits, so none can wrap.
    if (elementSize == 0 || stride < elementSize || viewOffset > buffer.size() ||
        viewLength > buffer.size() - viewOffset || offset > viewLength || elementSize > viewLength - offset ||
        elements - 1 > (viewLength - offset - elementSize) / stride)
        return false;
    const std::byte *base = buffer.data() + viewOffset + offset;
    for (usize i = 0; i < elements; ++i)
        sink(i, base + i * stride);
    return true;
}

/// Accessor @p index as floats, @p components per element, sparse substitutions applied; empty on any error.
[[nodiscard]] std::vector<f32> readAccessor(const Document &document, i32 index, u32 &components)
{
    const json &accessors = document.root.at("accessors");
    if (index < 0 || static_cast<usize>(index) >= accessors.size())
        return {};
    const json &accessor = accessors[static_cast<usize>(index)];
    const auto count = accessor.at("count").get<usize>();
    components = componentCount(accessor.at("type").get<std::string>());
    const i32 type = accessor.at("componentType").get<i32>();
    const bool normalized = accessor.value("normalized", false);
    const u32 size = componentSize(type);
    if (components == 0 || size == 0 || count == 0 || count > kMaxAccessorBytes / (components * sizeof(f32)))
        return {};

    std::vector<f32> out(count * components, 0.0f);
    const auto store = [&](usize element, const std::byte *at)
    {
        for (u32 c = 0; c < components; ++c)
            out[element * components + c] = readComponent(at + static_cast<usize>(c) * size, type, normalized);
    };
    if (accessor.contains("bufferView") &&
        !readView(document, accessor["bufferView"].get<i32>(), accessor.value("byteOffset", usize{0}),
                  static_cast<usize>(components) * size, count, store))
        return {};

    if (const auto sparse = accessor.find("sparse"); sparse != accessor.end())
    {
        const auto replaced = sparse->at("count").get<usize>();
        const json &indices = sparse->at("indices");
        const json &values = sparse->at("values");
        const i32 indexType = indices.at("componentType").get<i32>();
        if (replaced > count || componentSize(indexType) == 0)
            return {};
        std::vector<u32> targets(replaced);
        if (!readView(document, indices.at("bufferView").get<i32>(), indices.value("byteOffset", usize{0}),
                      componentSize(indexType), replaced,
                      [&](usize i, const std::byte *at)
                      {
                          targets[i] = readIndex(at, indexType);
                      }))
            return {};
        bool inRange = true;
        if (!readView(document, values.at("bufferView").get<i32>(), values.value("byteOffset", usize{0}),
                      static_cast<usize>(components) * size, replaced,
                      [&](usize i, const std::byte *at)
                      {
                          if (targets[i] >= count)
                          {
                              inRange = false;
                              return;
                          }
                          store(targets[i], at);
                      }) ||
            !inRange)
            return {};
    }
    return out;
}

[[nodiscard]] std::vector<u32> readIndices(const Document &document, i32 index)
{
    const json &accessor = document.root.at("accessors").at(static_cast<usize>(index));
    const auto count = accessor.at("count").get<usize>();
    const i32 type = accessor.at("componentType").get<i32>();
    if ((type != 5121 && type != 5123 && type != 5125) || count > kMaxAccessorBytes / sizeof(u32) ||
        !accessor.contains("bufferView"))
        return {};
    std::vector<u32> out(count);
    if (!readView(document, accessor["bufferView"].get<i32>(), accessor.value("byteOffset", usize{0}),
                  componentSize(type), count,
                  [&](usize i, const std::byte *at)
                  {
                      out[i] = readIndex(at, type);
                  }))
        return {};
    return out;
}

[[nodiscard]] TextureFilter filterOf(i32 mode, TextureFilter fallback) noexcept
{
    //! 9728 NEAREST, 9729 LINEAR; the mipmap variants name the texel filter first.
    switch (mode)
    {
    case 9728:
    case 9984:
    case 9986:
        return TextureFilter::Nearest;
    case 9729:
    case 9985:
    case 9987:
        return TextureFilter::Linear;
    default:
        return fallback;
    }
}

[[nodiscard]] TextureWrap wrapOf(i32 mode) noexcept
{
    return mode == 33071 ? TextureWrap::ClampToEdge : mode == 33648 ? TextureWrap::MirroredRepeat : TextureWrap::Repeat;
}

[[nodiscard]] i32 textureIndex(const json &material, const char *key)
{
    const auto info = material.find(key);
    return info == material.end() ? -1 : info->value("index", -1);
}

//! Triangle lists from a list, strip or fan; empty for points and lines, which a lit mesh cannot use.
[[nodiscard]] std::vector<u32> triangulate(std::vector<u32> indices, i32 mode)
{
    if (mode == 4)
    {
        indices.resize(indices.size() / 3 * 3);
        return indices;
    }
    std::vector<u32> triangles;
    for (usize i = 2; i < indices.size() && (mode == 5 || mode == 6); ++i)
    {
        if (mode == 6)
            triangles.insert(triangles.end(), {indices[0], indices[i - 1], indices[i]});
        else if (i % 2 == 0)
            triangles.insert(triangles.end(), {indices[i - 2], indices[i - 1], indices[i]});
        else
            triangles.insert(triangles.end(), {indices[i - 1], indices[i - 2], indices[i]});
    }
    return triangles;
}

[[nodiscard]] std::optional<Primitive> readPrimitive(const Document &document, const json &source)
{
    const json &attributes = source.at("attributes");
    const auto attribute = [&](const char *name, u32 wanted, std::vector<f32> &out) -> bool
    {
        const auto found = attributes.find(name);
        if (found == attributes.end())
            return true;
        u32 components = 0;
        out = readAccessor(document, found->get<i32>(), components);
        return !out.empty() && (components == wanted || (wanted == 4 && components == 3));
    };

    std::vector<f32> positions, normals, uvs, colours, joints, weights;
    u32 colourComponents = 4;
    if (!attributes.contains("POSITION") || !attribute("POSITION", 3, positions) || !attribute("NORMAL", 3, normals) ||
        !attribute("TEXCOORD_0", 2, uvs) || !attribute("JOINTS_0", 4, joints) || !attribute("WEIGHTS_0", 4, weights))
        return std::nullopt;
    if (attributes.contains("COLOR_0"))
    {
        colours = readAccessor(document, attributes["COLOR_0"].get<i32>(), colourComponents);
        if (colours.empty() || (colourComponents != 3 && colourComponents != 4))
            return std::nullopt;
    }
    const usize vertexCount = positions.size() / 3;
    const auto sized = [&](const std::vector<f32> &values, usize width)
    {
        return values.empty() || values.size() == vertexCount * width;
    };
    if (!sized(normals, 3) || !sized(uvs, 2) || !sized(colours, colourComponents) || !sized(joints, 4) ||
        !sized(weights, 4) || joints.empty() != weights.empty())
        return std::nullopt;

    std::vector<u32> indices(vertexCount);
    if (source.contains("indices"))
        indices = readIndices(document, source["indices"].get<i32>());
    else
        std::iota(indices.begin(), indices.end(), 0u);
    indices = triangulate(std::move(indices), source.value("mode", 4));
    if (indices.empty() || std::ranges::any_of(indices,
                                               [&](u32 i)
                                               {
                                                   return i >= vertexCount;
                                               }))
        return std::nullopt;

    //! Without normals the spec asks for flat shading, so every triangle gets vertices of its own.
    const bool flat = normals.empty();
    const std::vector<u32> from = flat ? indices : std::vector<u32>{};
    const usize outCount = flat ? indices.size() : vertexCount;
    Primitive primitive;
    primitive.material = source.value("material", -1);
    primitive.mesh.vertices.resize(outCount);
    if (!joints.empty())
    {
        primitive.joints.resize(outCount);
        primitive.weights.resize(outCount);
    }
    for (usize v = 0; v < outCount; ++v)
    {
        const usize s = flat ? from[v] : v;
        gfx::Vertex3D &vertex = primitive.mesh.vertices[v];
        vertex.pos = {positions[s * 3], positions[s * 3 + 1], positions[s * 3 + 2]};
        vertex.texCoord = uvs.empty() ? glm::vec2{0.0f} : glm::vec2{uvs[s * 2], uvs[s * 2 + 1]};
        vertex.color = glm::vec4{1.0f};
        for (u32 c = 0; c < colourComponents && !colours.empty(); ++c)
            vertex.color[static_cast<glm::length_t>(c)] = colours[s * colourComponents + c];
        vertex.normal = flat ? glm::vec3{0.0f} : glm::vec3{normals[s * 3], normals[s * 3 + 1], normals[s * 3 + 2]};
        if (!joints.empty())
        {
            primitive.joints[v] = {joints[s * 4], joints[s * 4 + 1], joints[s * 4 + 2], joints[s * 4 + 3]};
            primitive.weights[v] = {weights[s * 4], weights[s * 4 + 1], weights[s * 4 + 2], weights[s * 4 + 3]};
        }
    }
    if (flat)
    {
        std::iota(indices.begin(), indices.end(), 0u);
        for (usize t = 0; t + 2 < outCount; t += 3)
        {
            auto &vertices = primitive.mesh.vertices;
            const glm::vec3 n =
                glm::cross(vertices[t + 1].pos - vertices[t].pos, vertices[t + 2].pos - vertices[t].pos);
            const f32 length = glm::length(n);
            const glm::vec3 normal = length > 0.0f ? n / length : glm::vec3{0.0f, 1.0f, 0.0f};
            vertices[t].normal = vertices[t + 1].normal = vertices[t + 2].normal = normal;
        }
    }
    primitive.mesh.indices = std::move(indices);
    return primitive;
}

[[nodiscard]] std::optional<ImageData> readImage(const Document &document, const json &source,
                                                 const std::filesystem::path &base)
{
    std::vector<std::byte> encoded;
    if (const auto uri = source.find("uri"); uri != source.end())
    {
        auto bytes = resolveUri(uri->get<std::string>(), base);
        if (!bytes)
            return std::nullopt;
        encoded = std::move(*bytes);
    }
    else
    {
        const json &view = document.root.at("bufferViews").at(source.at("bufferView").get<usize>());
        const auto length = view.at("byteLength").get<usize>();
        encoded.resize(length);
        if (!readView(document, source["bufferView"].get<i32>(), 0, length, 1,
                      [&](usize, const std::byte *at)
                      {
                          std::memcpy(encoded.data(), at, length);
                      }))
            return std::nullopt;
    }
    if (encoded.empty() || encoded.size() > static_cast<usize>(std::numeric_limits<int>::max()))
        return std::nullopt;
    ImageData image;
    u8 *decoded =
        stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(encoded.data()), static_cast<int>(encoded.size()),
                              &image.width, &image.height, &image.channels, STBI_rgb_alpha);
    if (!decoded)
        return std::nullopt;
    image.channels = 4;
    image.pixels.assign(decoded, decoded + image.getBufferSize());
    stbi_image_free(decoded);
    return image;
}

[[nodiscard]] Node readNode(const json &source)
{
    Node node;
    node.name = source.value("name", "");
    node.mesh = source.value("mesh", -1);
    node.skin = source.value("skin", -1);
    node.children = source.value("children", std::vector<i32>{});
    if (const auto matrix = source.find("matrix"); matrix != source.end())
    {
        const auto values = matrix->get<std::vector<f32>>();
        if (values.size() == 16)
        {
            glm::mat4 m;
            std::memcpy(&m[0][0], values.data(), sizeof(m));
            node.translation = glm::vec3(m[3]);
            node.scale = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
            //! A mirrored matrix keeps its handedness in a negative x scale.
            if (glm::determinant(glm::mat3(m)) < 0.0f)
                node.scale.x = -node.scale.x;
            const glm::mat3 rotation{glm::vec3(m[0]) / node.scale.x, glm::vec3(m[1]) / node.scale.y,
                                     glm::vec3(m[2]) / node.scale.z};
            node.rotation = glm::normalize(glm::quat_cast(rotation));
        }
        return node;
    }
    if (const auto t = source.value("translation", std::vector<f32>{}); t.size() == 3)
        node.translation = {t[0], t[1], t[2]};
    if (const auto r = source.value("rotation", std::vector<f32>{}); r.size() == 4)
        node.rotation = glm::quat{r[3], r[0], r[1], r[2]};
    if (const auto s = source.value("scale", std::vector<f32>{}); s.size() == 3)
        node.scale = {s[0], s[1], s[2]};
    return node;
}

[[nodiscard]] std::optional<Model> build(const json &root, std::vector<std::byte> glbBinary,
                                         const std::filesystem::path &base)
{
    Document document{root, {}};
    bool binaryTaken = false;
    for (const json &buffer : root.value("buffers", json::array()))
    {
        if (const auto uri = buffer.find("uri"); uri != buffer.end())
        {
            auto bytes = resolveUri(uri->get<std::string>(), base);
            if (!bytes)
            {
                INK_ERROR << "glTF: buffer " << uri->get<std::string>() << " could not be read";
                return std::nullopt;
            }
            document.buffers.push_back(std::move(*bytes));
        }
        else if (!binaryTaken)
        {
            //! Only the first buffer may be the .glb's BIN chunk.
            document.buffers.push_back(std::exchange(glbBinary, {}));
            binaryTaken = true;
        }
        else
        {
            INK_ERROR << "glTF: a second buffer without a uri";
            return std::nullopt;
        }
    }

    Model model;
    for (const json &image : root.value("images", json::array()))
    {
        auto decoded = readImage(document, image, base);
        if (!decoded)
            INK_WARN << "glTF: an image could not be decoded; its textures sample white";
        model.images.push_back(decoded ? std::move(*decoded) : ImageData{});
    }
    const json samplers = root.value("samplers", json::array());
    for (const json &texture : root.value("textures", json::array()))
    {
        Texture out;
        out.image = texture.value("source", -1);
        if (const i32 index = texture.value("sampler", -1); index >= 0 && static_cast<usize>(index) < samplers.size())
        {
            const json &sampler = samplers[static_cast<usize>(index)];
            const i32 minFilter = sampler.value("minFilter", 9987);
            out.sampler.magFilter = filterOf(sampler.value("magFilter", 9729), TextureFilter::Linear);
            out.sampler.minFilter = filterOf(minFilter, TextureFilter::Linear);
            out.sampler.mipFilter =
                minFilter == 9984 || minFilter == 9985 ? TextureFilter::Nearest : TextureFilter::Linear;
            out.sampler.wrapU = wrapOf(sampler.value("wrapS", 10497));
            out.sampler.wrapV = wrapOf(sampler.value("wrapT", 10497));
        }
        model.textures.push_back(out);
    }
    for (const json &material : root.value("materials", json::array()))
    {
        Material out;
        const json pbr = material.value("pbrMetallicRoughness", json::object());
        if (const auto factor = pbr.value("baseColorFactor", std::vector<f32>{}); factor.size() == 4)
            out.baseColor = {factor[0], factor[1], factor[2], factor[3]};
        out.metallic = pbr.value("metallicFactor", 1.0f);
        out.roughness = pbr.value("roughnessFactor", 1.0f);
        out.baseColorTexture = textureIndex(pbr, "baseColorTexture");
        out.metallicRoughnessTexture = textureIndex(pbr, "metallicRoughnessTexture");
        out.normalTexture = textureIndex(material, "normalTexture");
        out.emissiveTexture = textureIndex(material, "emissiveTexture");
        if (const auto factor = material.value("emissiveFactor", std::vector<f32>{}); factor.size() == 3)
            out.emissive = {factor[0], factor[1], factor[2]};
        if (const auto extensions = material.find("extensions"); extensions != material.end())
            if (const auto strength = extensions->find("KHR_materials_emissive_strength");
                strength != extensions->end())
                out.emissive *= strength->value("emissiveStrength", 1.0f);
        model.materials.push_back(out);
    }
    for (const json &mesh : root.value("meshes", json::array()))
    {
        Mesh out;
        out.name = mesh.value("name", "");
        for (const json &primitive : mesh.at("primitives"))
        {
            auto read = readPrimitive(document, primitive);
            if (!read)
            {
                INK_ERROR << "glTF: mesh '" << out.name << "' has a primitive with missing or out-of-range data";
                return std::nullopt;
            }
            out.primitives.push_back(std::move(*read));
        }
        model.meshes.push_back(std::move(out));
    }
    for (const json &node : root.value("nodes", json::array()))
        model.nodes.push_back(readNode(node));
    const auto nodeCount = static_cast<i32>(model.nodes.size());
    for (i32 parent = 0; parent < nodeCount; ++parent)
        for (const i32 child : model.nodes[static_cast<usize>(parent)].children)
        {
            if (child < 0 || child >= nodeCount || model.nodes[static_cast<usize>(child)].parent != -1 ||
                child == parent)
            {
                INK_ERROR << "glTF: node " << parent << " has an invalid or shared child " << child;
                return std::nullopt;
            }
            model.nodes[static_cast<usize>(child)].parent = parent;
        }
    for (const Node &node : model.nodes)
        if ((node.mesh >= static_cast<i32>(model.meshes.size())) ||
            (node.skin >= 0 && static_cast<usize>(node.skin) >= root.value("skins", json::array()).size()))
        {
            INK_ERROR << "glTF: node '" << node.name << "' names a mesh or skin that does not exist";
            return std::nullopt;
        }

    const json scenes = root.value("scenes", json::array());
    const i32 scene = root.value("scene", scenes.empty() ? -1 : 0);
    if (scene >= 0 && static_cast<usize>(scene) < scenes.size())
        model.roots = scenes[static_cast<usize>(scene)].value("nodes", std::vector<i32>{});
    else
        for (i32 i = 0; i < nodeCount; ++i)
            if (model.nodes[static_cast<usize>(i)].parent == -1)
                model.roots.push_back(i);
    std::erase_if(model.roots,
                  [&](i32 node)
                  {
                      return node < 0 || node >= nodeCount;
                  });

    for (const json &skin : root.value("skins", json::array()))
    {
        Skin out;
        out.joints = skin.at("joints").get<std::vector<i32>>();
        if (std::ranges::any_of(out.joints,
                                [&](i32 joint)
                                {
                                    return joint < 0 || joint >= nodeCount;
                                }))
            return std::nullopt;
        out.inverseBindMatrices.assign(out.joints.size(), glm::mat4{1.0f});
        if (skin.contains("inverseBindMatrices"))
        {
            u32 components = 0;
            const std::vector<f32> values = readAccessor(document, skin["inverseBindMatrices"].get<i32>(), components);
            if (components != 16 || values.size() != out.joints.size() * 16)
                return std::nullopt;
            std::memcpy(&out.inverseBindMatrices[0][0][0], values.data(), values.size() * sizeof(f32));
        }
        model.skins.push_back(std::move(out));
    }

    for (const json &animation : root.value("animations", json::array()))
    {
        Animation out;
        out.name = animation.value("name", "");
        const json &animationSamplers = animation.at("samplers");
        for (const json &channel : animation.at("channels"))
        {
            const json &target = channel.at("target");
            const std::string path = target.at("path").get<std::string>();
            if (path == "weights")
                continue; //! Morph targets are not imported, so neither are their weights.
            Channel read;
            read.node = target.value("node", -1);
            read.path = path == "rotation" ? Path::Rotation : path == "scale" ? Path::Scale : Path::Translation;
            const json &sampler = animationSamplers.at(channel.at("sampler").get<usize>());
            const std::string interpolation = sampler.value("interpolation", "LINEAR");
            read.interpolation = interpolation == "STEP"          ? Interpolation::Step
                                 : interpolation == "CUBICSPLINE" ? Interpolation::CubicSpline
                                                                  : Interpolation::Linear;
            u32 timeComponents = 0, valueComponents = 0;
            read.times = readAccessor(document, sampler.at("input").get<i32>(), timeComponents);
            const std::vector<f32> values = readAccessor(document, sampler.at("output").get<i32>(), valueComponents);
            const usize perKey = read.interpolation == Interpolation::CubicSpline ? 3 : 1;
            const u32 wanted = read.path == Path::Rotation ? 4 : 3;
            if (read.node < 0 || read.node >= nodeCount || timeComponents != 1 || valueComponents != wanted ||
                read.times.empty() || values.size() != read.times.size() * perKey * wanted ||
                !std::ranges::is_sorted(read.times))
            {
                INK_ERROR << "glTF: animation '" << out.name << "' has a malformed channel";
                return std::nullopt;
            }
            read.values.resize(values.size() / wanted);
            for (usize i = 0; i < read.values.size(); ++i)
                for (u32 c = 0; c < wanted; ++c)
                    read.values[i][static_cast<glm::length_t>(c)] = values[i * wanted + c];
            out.duration = std::max(out.duration, read.times.back());
            out.channels.push_back(std::move(read));
        }
        model.animations.push_back(std::move(out));
    }
    return model;
}

} // namespace

std::optional<Model> parse(std::span<const std::byte> bytes, const std::filesystem::path &baseDirectory)
{
    try
    {
        std::span<const std::byte> text = bytes;
        std::vector<std::byte> binary;
        if (bytes.size() >= 12 && readU32(bytes, 0) == kGlbMagic)
        {
            //! Header (magic, version, length), then a JSON chunk and an optional BIN chunk.
            const usize length = std::min<usize>(readU32(bytes, 8), bytes.size());
            text = {};
            for (usize offset = 12; offset + 8 <= length;)
            {
                const usize chunkLength = readU32(bytes, offset);
                const u32 type = readU32(bytes, offset + 4);
                if (chunkLength > length - offset - 8)
                    break;
                const std::span<const std::byte> chunk = bytes.subspan(offset + 8, chunkLength);
                if (type == kChunkJson && text.empty())
                    text = chunk;
                else if (type == kChunkBin && binary.empty())
                    binary.assign(chunk.begin(), chunk.end());
                offset += 8 + chunkLength;
            }
        }
        const auto *begin = reinterpret_cast<const char *>(text.data());
        const json root = json::parse(begin, begin + text.size(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            INK_ERROR << "glTF: not a glTF document";
            return std::nullopt;
        }
        if (!root.value("asset", json::object()).value("version", "").starts_with("2."))
        {
            INK_ERROR << "glTF: only version 2 is supported";
            return std::nullopt;
        }
        return build(root, std::move(binary), baseDirectory);
    }
    catch (const std::exception &error)
    {
        //! nlohmann throws on a field of the wrong type; that is a malformed file, not a crash.
        INK_ERROR << "glTF: malformed document: " << error.what();
        return std::nullopt;
    }
}

std::optional<Model> load(const std::filesystem::path &path)
{
    const auto bytes = readFileBytes(path);
    if (!bytes)
    {
        INK_ERROR << "glTF: cannot read " << path.string();
        return std::nullopt;
    }
    auto model = parse(*bytes, path.parent_path());
    if (!model)
        INK_ERROR << "glTF: failed to load " << path.string();
    return model;
}

glm::mat4 localMatrix(const Node &node) noexcept
{
    glm::mat4 matrix = glm::mat4_cast(node.rotation);
    matrix[0] *= node.scale.x;
    matrix[1] *= node.scale.y;
    matrix[2] *= node.scale.z;
    matrix[3] = glm::vec4(node.translation, 1.0f);
    return matrix;
}

std::vector<glm::mat4> worldMatrices(const Model &model)
{
    std::vector<glm::mat4> world(model.nodes.size(), glm::mat4{1.0f});
    std::vector<i32> stack;
    for (usize i = 0; i < model.nodes.size(); ++i)
        if (model.nodes[i].parent == -1)
            stack.push_back(static_cast<i32>(i));
    //! Parents are unique and checked on load, so every node is reached once, from its root.
    while (!stack.empty())
    {
        const auto index = static_cast<usize>(stack.back());
        stack.pop_back();
        const Node &node = model.nodes[index];
        world[index] =
            node.parent == -1 ? localMatrix(node) : world[static_cast<usize>(node.parent)] * localMatrix(node);
        stack.insert(stack.end(), node.children.begin(), node.children.end());
    }
    return world;
}

} // namespace aura3d::gltf
