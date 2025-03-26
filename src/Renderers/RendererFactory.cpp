#include "RendererFactory.h"

#include "VulkanRenderer.h"
#include "OpenGLRenderer.h"

namespace aura3d {

std::unique_ptr<Renderer> RendererFactory::createVulkanRenderer(
    const WindowDetails& windowDetails,
    const VkInstanceData& vkInstanceData,
    const VkDeviceData& vkDeviceData,
    const ImageViewData& vkImageViewData)
{
    return std::make_unique<VulkanRenderer>(
        windowDetails,
        vkInstanceData,
        vkDeviceData,
        vkImageViewData
    );
}

std::unique_ptr<Renderer> RendererFactory::createOpenGLRenderer(const WindowDetails& windowDetails)
{
    return std::make_unique<OpenGLRenderer>(windowDetails);
}

std::unique_ptr<Renderer> RendererFactory::createCPURenderer(const WindowDetails& windowDetails)
{
    return std::make_unique<CPURenderer>(windowDetails);
}

} // namespace aura3d
