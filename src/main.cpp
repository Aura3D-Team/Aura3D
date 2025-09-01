#include <ink/ink.hpp>

// #define VK_USE_PLATFORM_WIN32_KHR
// #define GLFW_EXPOSE_NATIVE_WIN32

#include "aura.hpp"

#ifdef USE_CPU
#include "Renderers/CPURenderer.h"
#elif defined(USE_VULKAN_API)
#include "Renderers/VulkanRenderer.h"
#else
#include "Renderers/OpenGLRenderer.h"
#endif

#ifdef NDEBUG
    const ink::LogLevel logSeverity = aura3d::LogLevel::INFO;
#else
    const ink::LogLevel logSeverity = ink::LogLevel::TRACE;
#endif

int main(int argc, char **argv)
{
    INK_CORE_LOGGER;
    INK_CORE_LOGGER->setName(APPLICATION_NAME);
    ink::LogManager::getInstance().setGlobalLevel(logSeverity);

    wma::WindowDetails windowDetails = {};
    windowDetails.width = 1280;
    windowDetails.height = 720;
    windowDetails.resizable = true;
    windowDetails.targetFPS = 60;

#ifdef USE_CPU
    aura3d::CPURenderer cpuRenderer(windowDetails);
    cpuRenderer.run();
#elif defined(USE_VULKAN_API)
    aura3d::VkInstanceData vkInstanceData = {};
    vkInstanceData.appName = "Aura3D";
    vkInstanceData.engineName = "Aura3DEngine";
    vkInstanceData.appVersion = {1, 0, 0};
    vkInstanceData.vkInstanceExtensions = {};
    vkInstanceData.vkValidationLayers = {
        "VK_LAYER_KHRONOS_validation",
    };

    aura3d::VkDeviceData vkDeviceData = {};
    vkDeviceData.vkDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    vkDeviceData.vkEnabledLayers = {};
        vkDeviceData.concurrentQueueFlags = {};
    vkDeviceData.exclusiveQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

    aura3d::ImageViewData vkImageViewData = {};
    vkImageViewData.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkImageViewData.baseMipLevel = 0;
    vkImageViewData.levelCount = 1;
    vkImageViewData.baseArrayLayer = 0;
    vkImageViewData.layerCount = 1;

    aura3d::VulkanRenderer vkRenderer(
        windowDetails,
        vkInstanceData,
        vkDeviceData,
        vkImageViewData
    );
    vkRenderer.run();
#else
    aura3d::OpenGLRenderer glRenderer(windowDetails);
    glRenderer.run();
#endif

    return 0;
}
