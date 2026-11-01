#include "aura/Core/Gltf/Animation.h"

#include <algorithm>

#include "aura/Core/JobSystem/JobSystem.h"

namespace aura3d::gltf
{
namespace
{

//! glTF's cubic Hermite spline between keys a and b, @p t in [0, 1] over @p span seconds.
[[nodiscard]] glm::vec4 hermite(const Channel &channel, usize a, usize b, f32 t, f32 span) noexcept
{
    const glm::vec4 &p0 = channel.values[a * 3 + 1];
    const glm::vec4 m0 = channel.values[a * 3 + 2] * span;
    const glm::vec4 &p1 = channel.values[b * 3 + 1];
    const glm::vec4 m1 = channel.values[b * 3] * span;
    const f32 t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * m1;
}

[[nodiscard]] glm::vec4 sample(const Channel &channel, f32 seconds) noexcept
{
    const usize stride = channel.interpolation == Interpolation::CubicSpline ? 3 : 1;
    const usize middle = stride == 3 ? 1 : 0;
    const auto &times = channel.times;

    if (seconds <= times.front())
        return channel.values[middle];
    if (seconds >= times.back())
        return channel.values[(times.size() - 1) * stride + middle];

    const usize b = static_cast<usize>(std::ranges::upper_bound(times, seconds) - times.begin());
    const usize a = b - 1;
    const f32 span = times[b] - times[a];
    const f32 t = span > 0.0f ? (seconds - times[a]) / span : 0.0f;

    switch (channel.interpolation)
    {
    case Interpolation::Step:
        return channel.values[a];
    case Interpolation::CubicSpline:
        return hermite(channel, a, b, t, span);
    case Interpolation::Linear:
        break;
    }

    if (channel.path != Path::Rotation)
        return glm::mix(channel.values[a], channel.values[b], t);

    const glm::quat from{channel.values[a].w, channel.values[a].x, channel.values[a].y, channel.values[a].z};
    const glm::quat to{channel.values[b].w, channel.values[b].x, channel.values[b].y, channel.values[b].z};
    const glm::quat q = glm::slerp(from, to, t);

    return {q.x, q.y, q.z, q.w};
}

} // namespace

void animate(std::span<Node> nodes, const Animation &animation, f32 seconds)
{
    for (const Channel &channel : animation.channels)
    {
        if (channel.node < 0 || static_cast<usize>(channel.node) >= nodes.size() || channel.times.empty())
            continue;
        Node &node = nodes[static_cast<usize>(channel.node)];
        const glm::vec4 value = sample(channel, seconds);
        switch (channel.path)
        {
        case Path::Translation:
            node.translation = glm::vec3(value);
            break;
        case Path::Rotation:
            //! A spline's value is not unit length between keys.
            node.rotation = glm::normalize(glm::quat{value.w, value.x, value.y, value.z});
            break;
        case Path::Scale:
            node.scale = glm::vec3(value);
            break;
        }
    }
}

void jointMatrices(const Skin &skin, std::span<const glm::mat4> world, std::vector<glm::mat4> &out)
{
    out.resize(skin.joints.size());
    for (usize i = 0; i < skin.joints.size(); ++i)
    {
        const auto joint = static_cast<usize>(skin.joints[i]);
        out[i] = joint < world.size() ? world[joint] * skin.inverseBindMatrices[i] : glm::mat4{1.0f};
    }
}

void skin(const Primitive &primitive, std::span<const glm::mat4> joints, std::span<gfx::Vertex3D> out,
          const JobSystem *jobs)
{
    const std::vector<gfx::Vertex3D> &bind = primitive.mesh.vertices;
    if (out.size() != bind.size() || primitive.joints.size() != bind.size())
        return;
    const auto band = [&](i32 begin, i32 end)
    {
        for (auto v = static_cast<usize>(begin); v < static_cast<usize>(end); ++v)
        {
            const glm::uvec4 &index = primitive.joints[v];
            const glm::vec4 &weight = primitive.weights[v];
            glm::mat4 blend{0.0f};
            for (glm::length_t k = 0; k < 4; ++k)
                if (weight[k] > 0.0f && index[k] < joints.size())
                    blend += joints[index[k]] * weight[k];
            out[v] = bind[v];
            out[v].pos = glm::vec3(blend * glm::vec4(bind[v].pos, 1.0f));
            //! The cofactor matrix is the inverse transpose up to scale, which the normalize below removes.
            const glm::vec3 c0{blend[0]}, c1{blend[1]}, c2{blend[2]};
            const glm::vec3 normal =
                glm::mat3(glm::cross(c1, c2), glm::cross(c2, c0), glm::cross(c0, c1)) * bind[v].normal;
            const f32 length = glm::length(normal);
            out[v].normal = length > 0.0f ? normal / length : bind[v].normal;
        }
    };
    const auto count = static_cast<i32>(bind.size());
    if (jobs)
        jobs->dispatch(count, band);
    else
        band(0, count);
}

} // namespace aura3d::gltf
