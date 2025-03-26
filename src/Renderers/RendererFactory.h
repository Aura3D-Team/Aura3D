#ifndef RENDERER_FACTORY_H
#define RENDERER_FACTORY_H

#pragma once

#include <memory>

#include "VkAura/VkAuraDefs.h"
#include "CPURenderer.h"

namespace aura3d {

/**
 * @brief Enumeration of available renderer types
 */
enum class RendererType {
    VULKAN,
    OPENGL,
    CPU
};

/**
 * @brief Factory class for creating renderer instances
 */
class RendererFactory {
public:

    static std::unique_ptr<Renderer> createVulkanRenderer(
        const WindowDetails& windowDetails,
        const VkInstanceData& vkInstanceData,
        const VkDeviceData& vkDeviceData,
        const ImageViewData& vkImageViewData
    );

    static std::unique_ptr<Renderer> createOpenGLRenderer(const WindowDetails& windowDetails);

    static std::unique_ptr<Renderer> createCPURenderer(const WindowDetails& windowDetails);
};

} // namespace aura3d

#endif // RENDERER_FACTORY_H
