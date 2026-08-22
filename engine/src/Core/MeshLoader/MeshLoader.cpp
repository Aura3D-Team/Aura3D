#include "aura/Core/MeshLoader/MeshLoader.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "aura/aura.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace aura3d {
namespace {
constexpr float kPi = 3.14159265358979323846f;

struct VertexKey {
    int position = -1;
    int normal = -1;
    int texcoord = -1;

    bool operator==(const VertexKey& other) const noexcept
    {
        return position == other.position &&
               normal == other.normal &&
               texcoord == other.texcoord;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const noexcept
    {
        size_t hash = std::hash<int>{}(key.position);
        hash ^= std::hash<int>{}(key.normal) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.texcoord) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        return hash;
    }
};

void generateNormals(gfx::Mesh3D& mesh)
{
    for (auto& vertex : mesh.vertices)
        vertex.normal = glm::vec3(0.0f);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const u32 i0 = mesh.indices[i + 0];
        const u32 i1 = mesh.indices[i + 1];
        const u32 i2 = mesh.indices[i + 2];

        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
            continue;

        const glm::vec3& p0 = mesh.vertices[i0].pos;
        const glm::vec3& p1 = mesh.vertices[i1].pos;
        const glm::vec3& p2 = mesh.vertices[i2].pos;

        const glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);

        mesh.vertices[i0].normal += faceNormal;
        mesh.vertices[i1].normal += faceNormal;
        mesh.vertices[i2].normal += faceNormal;
    }

    for (auto& vertex : mesh.vertices)
    {
        const float length = glm::length(vertex.normal);
        vertex.normal = (length > 0.0f) ? vertex.normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

} // namespace

gfx::Mesh3D MeshLoader::loadOBJ(const std::string& path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const bool loaded = tinyobj::LoadObj(
        &attrib, &shapes, &materials, &warn, &err, path.c_str(),
        /*mtl_basedir=*/nullptr, /*triangulate=*/true);

    if (!warn.empty())
        INK_WARN << "MeshLoader: " << path << ": " << warn;

    if (!loaded || !err.empty())
    {
        INK_WARN << "MeshLoader: failed to load '" << path << "': "
                 << (err.empty() ? "unknown error" : err)
                 << "; substituting the cube fallback";
        return createCube();
    }

    gfx::Mesh3D mesh;
    std::unordered_map<VertexKey, u32, VertexKeyHash> uniqueVertices;

    const bool fileHasNormals = !attrib.normals.empty();

    for (const auto& shape : shapes)
    {
        for (const auto& index : shape.mesh.indices)
        {
            VertexKey key;
            key.position = index.vertex_index;
            key.normal = index.normal_index;
            key.texcoord = index.texcoord_index;

            auto it = uniqueVertices.find(key);
            if (it != uniqueVertices.end())
            {
                mesh.indices.push_back(it->second);
                continue;
            }

            gfx::Vertex3D vertex{};

            if (index.vertex_index >= 0)
            {
                const size_t base = static_cast<size_t>(index.vertex_index) * 3;
                if (base + 2 < attrib.vertices.size())
                {
                    vertex.pos = {attrib.vertices[base + 0],
                                  attrib.vertices[base + 1],
                                  attrib.vertices[base + 2]};
                }
            }

            if (index.normal_index >= 0)
            {
                const size_t base = static_cast<size_t>(index.normal_index) * 3;
                if (base + 2 < attrib.normals.size())
                {
                    vertex.normal = {attrib.normals[base + 0],
                                     attrib.normals[base + 1],
                                     attrib.normals[base + 2]};
                }
            }

            if (index.texcoord_index >= 0)
            {
                const size_t base = static_cast<size_t>(index.texcoord_index) * 2;
                if (base + 1 < attrib.texcoords.size())
                {
                    vertex.texCoord = {attrib.texcoords[base + 0],
                                       1.0f - attrib.texcoords[base + 1]};
                }
            }

            vertex.color = glm::vec4(1.0f);
            if (index.vertex_index >= 0)
            {
                const size_t base = static_cast<size_t>(index.vertex_index) * 3;
                if (base + 2 < attrib.colors.size())
                {
                    vertex.color = {attrib.colors[base + 0],
                                    attrib.colors[base + 1],
                                    attrib.colors[base + 2],
                                    1.0f};
                }
            }

            const auto newIndex = static_cast<u32>(mesh.vertices.size());
            uniqueVertices.emplace(key, newIndex);
            mesh.vertices.push_back(vertex);
            mesh.indices.push_back(newIndex);
        }
    }

    if (mesh.empty())
    {
        INK_WARN << "MeshLoader: '" << path
                 << "' contained no geometry; substituting the cube fallback";
        return createCube();
    }

    if (!fileHasNormals)
    {
        INK_DEBUG << "MeshLoader: '" << path << "' has no normals; generating them";
        generateNormals(mesh);
    }

    INK_INFO << "MeshLoader: loaded '" << path << "' ("
             << mesh.vertices.size() << " vertices, "
             << mesh.indices.size() / 3 << " triangles)";

    return mesh;
}

gfx::Mesh3D MeshLoader::createCube()
{
    gfx::Mesh3D mesh;

    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };

    const Face faces[6] = {
        {{0.0f, 0.0f, 1.0f},
         {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},

        {{0.0f, 0.0f, -1.0f},
         {{0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}}},

        {{-1.0f, 0.0f, 0.0f},
         {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}}},

        {{1.0f, 0.0f, 0.0f},
         {{0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}},

        {{0.0f, 1.0f, 0.0f},
         {{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}}},

        {{0.0f, -1.0f, 0.0f},
         {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}}},
    };

    const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (const Face& face : faces)
    {
        const auto base = static_cast<u32>(mesh.vertices.size());

        for (int i = 0; i < 4; ++i)
        {
            gfx::Vertex3D vertex{};
            vertex.pos = face.corners[i];
            vertex.texCoord = uvs[i];
            vertex.color = glm::vec4(1.0f);
            vertex.normal = face.normal;
            mesh.vertices.push_back(vertex);
        }

        mesh.indices.insert(mesh.indices.end(),
                            {base + 0, base + 1, base + 2,
                             base + 2, base + 3, base + 0});
    }

    return mesh;
}

gfx::Mesh3D MeshLoader::createPlane()
{
    gfx::Mesh3D mesh;

    const glm::vec3 corners[4] = {
        {-0.5f, 0.0f,  0.5f},
        { 0.5f, 0.0f,  0.5f},
        { 0.5f, 0.0f, -0.5f},
        {-0.5f, 0.0f, -0.5f},
    };
    const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (int i = 0; i < 4; ++i)
    {
        gfx::Vertex3D vertex{};
        vertex.pos = corners[i];
        vertex.texCoord = uvs[i];
        vertex.color = glm::vec4(1.0f);
        vertex.normal = {0.0f, 1.0f, 0.0f};
        mesh.vertices.push_back(vertex);
    }

    mesh.indices = {0, 1, 2, 2, 3, 0};
    return mesh;
}

gfx::Mesh3D MeshLoader::createSphere(int subdivisions)
{
    gfx::Mesh3D mesh;

    const int level   = std::max(subdivisions, 1);
    const int stacks  = std::max(2, 8 * level);
    const int sectors = std::max(3, 16 * level);
    constexpr float radius = 0.5f;

    for (int i = 0; i <= stacks; ++i)
    {
        const float phi = kPi * 0.5f - static_cast<float>(i) * kPi / static_cast<float>(stacks);
        const float y = radius * std::sin(phi);
        const float ringRadius = radius * std::cos(phi);

        for (int j = 0; j <= sectors; ++j)
        {
            const float theta = static_cast<float>(j) * 2.0f * kPi / static_cast<float>(sectors);

            gfx::Vertex3D vertex{};
            vertex.pos = {ringRadius * std::cos(theta), y, ringRadius * std::sin(theta)};
            vertex.normal = glm::normalize(vertex.pos);
            vertex.texCoord = {static_cast<float>(j) / static_cast<float>(sectors),
                               static_cast<float>(i) / static_cast<float>(stacks)};
            vertex.color = glm::vec4(1.0f);
            mesh.vertices.push_back(vertex);
        }
    }

    for (int i = 0; i < stacks; ++i)
    {
        u32 k1 = static_cast<u32>(i * (sectors + 1));
        u32 k2 = k1 + static_cast<u32>(sectors) + 1;

        for (int j = 0; j < sectors; ++j, ++k1, ++k2)
        {
            if (i != 0) {
                mesh.indices.insert(mesh.indices.end(), {k1, k1 + 1, k2});
            }
            if (i != stacks - 1) {
                mesh.indices.insert(mesh.indices.end(), {k1 + 1, k2 + 1, k2});
            }
        }
    }

    return mesh;
}

} // namespace aura3d
