#include "aura/Core/Camera/Camera.h"

namespace aura3d
{

Ray Camera::rayFromScreen(glm::vec2 pixel, glm::vec2 viewport) const noexcept
{
    const glm::vec2 ndc{2.0f * pixel.x / viewport.x - 1.0f, 1.0f - 2.0f * pixel.y / viewport.y};
    const glm::mat4 toWorld = glm::inverse(_projection * viewMatrix());
    const auto unproject = [&](f32 depth)
    {
        const glm::vec4 world = toWorld * glm::vec4(ndc, depth, 1.0f);
        return glm::vec3(world) / world.w;
    };
    const glm::vec3 nearPoint = unproject(_clipSpace == ClipSpace::Vulkan ? 0.0f : -1.0f);
    const glm::vec3 farPoint = unproject(1.0f);
    return {nearPoint, glm::normalize(farPoint - nearPoint)};
}

} // namespace aura3d
