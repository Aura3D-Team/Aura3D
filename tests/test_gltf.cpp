// glTF loading, keyframe animation, skinning, and a skinned model streamed through the software renderer.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <nlohmann/json.hpp>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/Gltf/Animation.h"
#include "aura/Core/JobSystem/JobSystem.h"
#include "aura/Renderer/Model.h"
#ifdef AURA_HAS_CPU
#include "aura/Renderer/Software/CPURenderer.h"
#endif

#include "TestUtils.h"
#include "WindowTestUtils.h"

using namespace aura3d;
using json = nlohmann::json;

namespace
{

[[nodiscard]] bool near(glm::vec3 a, glm::vec3 b, f32 tolerance = 1e-4f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
}

[[nodiscard]] std::string base64(const std::vector<std::byte> &bytes)
{
    static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (usize i = 0; i < bytes.size(); i += 3)
    {
        u32 group = 0;
        const usize n = std::min<usize>(3, bytes.size() - i);
        for (usize k = 0; k < 3; ++k)
            group = (group << 8u) | (k < n ? static_cast<u32>(bytes[i + k]) : 0u);
        for (usize k = 0; k < 4; ++k)
            out.push_back(k <= n ? kDigits[(group >> (18u - 6u * k)) & 63u] : '=');
    }
    return out;
}

//! One binary buffer and the views and accessors into it.
struct Builder
{
    std::vector<std::byte> bytes;
    json views = json::array();
    json accessors = json::array();

    template <typename T> i32 add(std::initializer_list<T> values, i32 componentType, const char *type)
    {
        while (bytes.size() % 4 != 0)
            bytes.push_back(std::byte{0});
        const usize offset = bytes.size();
        bytes.resize(offset + values.size() * sizeof(T));
        std::memcpy(bytes.data() + offset, std::data(values), values.size() * sizeof(T));
        views.push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", values.size() * sizeof(T)}});
        const usize components = std::string(type) == "SCALAR" ? 1
                                 : std::string(type) == "VEC2" ? 2
                                 : std::string(type) == "VEC3" ? 3
                                 : std::string(type) == "VEC4" ? 4
                                                               : 16;
        accessors.push_back({{"bufferView", views.size() - 1},
                             {"componentType", componentType},
                             {"count", values.size() / components},
                             {"type", type}});
        return static_cast<i32>(accessors.size() - 1);
    }
};

/// A triangle with two joints: the root holds the base, a child one unit up holds the tip. One animation turns the
/// child 90 degrees about z (linear), another steps the root along x.
[[nodiscard]] json skinnedDocument(Builder &b)
{
    const i32 positions = b.add<f32>({0, 0, 0, 1, 0, 0, 0, 2, 0}, 5126, "VEC3");
    const i32 normals = b.add<f32>({0, 0, 1, 0, 0, 1, 0, 0, 1}, 5126, "VEC3");
    const i32 joints = b.add<u8>({0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}, 5121, "VEC4");
    const i32 weights = b.add<f32>({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, 5126, "VEC4");
    const i32 indices = b.add<u16>({0, 1, 2}, 5123, "SCALAR");
    const i32 inverseBind = b.add<f32>({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,  0, 1, //
                                        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1},
                                       5126, "MAT4");
    const i32 times = b.add<f32>({0, 1}, 5126, "SCALAR");
    const f32 half = std::sqrt(0.5f);
    const i32 turns = b.add<f32>({0, 0, 0, 1, 0, 0, half, half}, 5126, "VEC4");
    const i32 steps = b.add<f32>({0, 0, 0, 5, 0, 0}, 5126, "VEC3");
    return {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", {{{"nodes", {0, 1}}}}},
        {"nodes", {{{"mesh", 0}, {"skin", 0}}, {{"name", "root"}, {"children", {2}}}, {{"translation", {0, 1, 0}}}}},
        {"meshes",
         {{{"primitives",
            {{{"attributes",
               {{"POSITION", positions}, {"NORMAL", normals}, {"JOINTS_0", joints}, {"WEIGHTS_0", weights}}},
              {"indices", indices}}}}}}},
        {"skins", {{{"joints", {1, 2}}, {"inverseBindMatrices", inverseBind}}}},
        {"animations",
         {{{"name", "bend"},
           {"samplers", {{{"input", times}, {"output", turns}}}},
           {"channels", {{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}}}}},
          {{"name", "slide"},
           {"samplers", {{{"input", times}, {"output", steps}, {"interpolation", "STEP"}}}},
           {"channels", {{{"sampler", 0}, {"target", {{"node", 1}, {"path", "translation"}}}}}}}}},
        {"bufferViews", b.views},
        {"accessors", b.accessors},
    };
}

[[nodiscard]] std::optional<gltf::Model> parseText(const json &document)
{
    const std::string text = document.dump();
    return gltf::parse(std::as_bytes(std::span(text)), ".");
}

[[nodiscard]] std::vector<std::byte> glb(const json &document, const std::vector<std::byte> &binary)
{
    std::string text = document.dump();
    while (text.size() % 4 != 0)
        text.push_back(' ');
    std::vector<std::byte> bin = binary;
    while (bin.size() % 4 != 0)
        bin.push_back(std::byte{0});
    std::vector<std::byte> out;
    const auto put = [&](u32 value)
    {
        const auto *raw = reinterpret_cast<const std::byte *>(&value);
        out.insert(out.end(), raw, raw + 4);
    };
    put(0x46546C67u);
    put(2);
    put(static_cast<u32>(12 + 8 + text.size() + 8 + bin.size()));
    put(static_cast<u32>(text.size()));
    put(0x4E4F534Au);
    const auto *chars = reinterpret_cast<const std::byte *>(text.data());
    out.insert(out.end(), chars, chars + text.size());
    put(static_cast<u32>(bin.size()));
    put(0x004E4942u);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}

void test_load_and_animate()
{
    Builder b;
    json document = skinnedDocument(b);
    document["buffers"] = {
        {{"byteLength", b.bytes.size()}, {"uri", "data:application/octet-stream;base64," + base64(b.bytes)}}};
    std::optional<gltf::Model> model = parseText(document);
    AURA_CHECK(model.has_value(), "gltf: a data-URI document loads");
    if (!model)
        return;
    const gltf::Primitive &primitive = model->meshes[0].primitives[0];
    AURA_CHECK(primitive.mesh.vertices.size() == 3 && primitive.mesh.indices == std::vector<u32>({0, 1, 2}) &&
                   near(primitive.mesh.vertices[2].pos, {0, 2, 0}) && primitive.joints[2] == glm::uvec4(1, 0, 0, 0),
               "gltf: positions, indices and joints arrive as written");
    AURA_CHECK(model->roots == std::vector<i32>({0, 1}) && model->nodes[2].parent == 1, "gltf: the hierarchy");
    AURA_CHECK(model->animations.size() == 2 && model->animations[0].duration == 1.0f, "gltf: two animations");

    const auto pose = [&](f32 seconds)
    {
        gltf::animate(model->nodes, model->animations[0], seconds);
        const std::vector<glm::mat4> world = gltf::worldMatrices(*model);
        std::vector<glm::mat4> joints;
        gltf::jointMatrices(model->skins[0], world, joints);
        std::vector<gfx::Vertex3D> out(3);
        gltf::skin(primitive, joints, out);
        return out;
    };
    std::vector<gfx::Vertex3D> rest = pose(0.0f);
    AURA_CHECK(near(rest[2].pos, {0, 2, 0}) && near(rest[0].pos, {0, 0, 0}), "skin: the bind pose is the rest pose");
    std::vector<gfx::Vertex3D> bent = pose(1.0f);
    AURA_CHECK(near(bent[2].pos, {-1, 1, 0}) && near(bent[1].pos, {1, 0, 0}) && near(bent[2].normal, {0, 0, 1}),
               "skin: the tip turns 90 degrees about the child joint; the base stays");
    const f32 h = std::sqrt(0.5f);
    std::vector<gfx::Vertex3D> halfway = pose(0.5f);
    AURA_CHECK(near(halfway[2].pos, {-h, 1 + h, 0}), "animate: linear rotation slerps to 45 degrees halfway");

    gltf::animate(model->nodes, model->animations[1], 0.5f);
    AURA_CHECK(near(model->nodes[1].translation, {0, 0, 0}), "animate: a step holds the earlier key");
    gltf::animate(model->nodes, model->animations[1], 2.0f);
    AURA_CHECK(near(model->nodes[1].translation, {5, 0, 0}), "animate: past the end holds the last key");

    //! The same document as .glb, its buffer moved into the BIN chunk.
    document["buffers"] = {{{"byteLength", b.bytes.size()}}};
    const std::vector<std::byte> binary = glb(document, b.bytes);
    const std::optional<gltf::Model> fromGlb = gltf::parse(binary, ".");
    AURA_CHECK(fromGlb && fromGlb->meshes[0].primitives[0].mesh.vertices.size() == 3, "gltf: the .glb container");
}

void test_flat_normals_and_matrix()
{
    Builder b;
    const i32 positions = b.add<f32>({0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}, 5126, "VEC3");
    const i32 indices = b.add<u16>({0, 1, 2, 0, 3, 1}, 5123, "SCALAR");
    json document = {
        {"asset", {{"version", "2.0"}}},
        {"nodes", {{{"mesh", 0}, {"matrix", {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 3, 4, 5, 1}}}}},
        {"meshes", {{{"primitives", {{{"attributes", {{"POSITION", positions}}}, {"indices", indices}}}}}}},
        {"bufferViews", b.views},
        {"accessors", b.accessors},
        {"buffers", {{{"byteLength", b.bytes.size()}, {"uri", "data:;base64," + base64(b.bytes)}}}},
    };
    const std::optional<gltf::Model> model = parseText(document);
    AURA_CHECK(model.has_value(), "gltf: a mesh without normals loads");
    if (!model)
        return;
    const gfx::Mesh3D &mesh = model->meshes[0].primitives[0].mesh;
    AURA_CHECK(mesh.vertices.size() == 6 && near(mesh.vertices[0].normal, {0, 0, 1}) &&
                   near(mesh.vertices[3].normal, {0, 1, 0}),
               "gltf: no normals means flat ones, a vertex per triangle corner");
    const gltf::Node &node = model->nodes[0];
    AURA_CHECK(near(node.translation, {3, 4, 5}) && near(node.scale, {2, 2, 2}) && model->roots == std::vector<i32>{0},
               "gltf: a node matrix becomes TRS; parentless nodes are the roots without a scene");
}

void test_malformed()
{
    Builder b;
    const i32 positions = b.add<f32>({0, 0, 0, 1, 0, 0, 0, 1, 0}, 5126, "VEC3");
    json document = {
        {"asset", {{"version", "2.0"}}},
        {"meshes", {{{"primitives", {{{"attributes", {{"POSITION", positions}}}}}}}}},
        {"bufferViews", b.views},
        {"accessors", b.accessors},
        {"buffers", {{{"byteLength", b.bytes.size()}, {"uri", "data:;base64," + base64(b.bytes)}}}},
    };
    document["accessors"][0]["count"] = 4000000000u;
    AURA_CHECK(!parseText(document), "gltf: an accessor longer than its view is refused");
    document["accessors"][0]["count"] = 3;
    document["bufferViews"][0]["byteOffset"] = 1u << 30u;
    AURA_CHECK(!parseText(document), "gltf: a view outside its buffer is refused");
    document["bufferViews"][0]["byteOffset"] = 0;
    document["accessors"][0]["count"] = "three";
    AURA_CHECK(!parseText(document), "gltf: a field of the wrong type is a malformed file, not an exception");
    //! A .glb header claiming 4 GiB, with no chunks behind it.
    const std::array<u8, 12> garbage{'g', 'l', 'T', 'F', 2, 0, 0, 0, 0xff, 0xff, 0xff, 0xff};
    AURA_CHECK(!gltf::parse(std::as_bytes(std::span(garbage)), "."), "gltf: a truncated .glb is refused");
    AURA_CHECK(!gltf::load("/nonexistent/model.gltf"), "gltf: a missing file is refused");
}

void test_skinned_model_on_cpu()
{
#ifdef AURA_HAS_CPU
    JobSystem jobs{2};
    cpu::CPURenderer renderer(wma::WindowDetails{.width = 16, .height = 16});
    renderer.setWindowFactory(
        [](auto, const auto &, auto)
        {
            return std::make_unique<test::FakeWindow>();
        });
    renderer.initialize(AuraSettings::get(), &jobs);

    Builder b;
    json document = skinnedDocument(b);
    document["buffers"] = {{{"byteLength", b.bytes.size()}, {"uri", "data:;base64," + base64(b.bytes)}}};
    std::optional<gltf::Model> model = parseText(document);
    if (!model)
        return;
    ModelHandles handles = uploadModel(renderer, *model);
    AURA_CHECK(handles.meshes.size() == 1 && isValidHandle(handles.meshes[0][0].mesh) &&
                   isValidHandle(handles.meshes[0][0].material),
               "uploadModel: a mesh and the default material per primitive");

    gltf::animate(model->nodes, model->animations[0], 1.0f);
    const std::vector<glm::mat4> world = gltf::worldMatrices(*model);
    updateSkins(renderer, *model, handles, world, &jobs);
    const Aabb bounds = renderer.meshBounds(handles.meshes[0][0].mesh);
    AURA_CHECK(near(bounds.min, {-1, 0, 0}) && near(bounds.max, {1, 1, 0}),
               "updateSkins: the deformed mesh and its bounds reach the renderer");

    std::vector<IRenderer::DrawItem> items;
    appendDrawItems(*model, handles, world, glm::mat4{1.0f}, items);
    AURA_CHECK(items.size() == 1 && items[0].model == glm::mat4{1.0f},
               "appendDrawItems: a skinned mesh ignores its node's transform");
    AURA_CHECK(!renderer.updateMesh(handles.meshes[0][0].mesh, std::span(handles.skinned).first(2)),
               "updateMesh: a different vertex count is refused");
    renderer.cleanup();
#endif
}

} // namespace

int main()
{
    try
    {
        test_load_and_animate();
        test_flat_normals_and_matrix();
        test_malformed();
        test_skinned_model_on_cpu();
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "test_gltf: %s\n", error.what());
        return 1;
    }
    return aura3d::test::failureCount() == 0 ? 0 : 1;
}
