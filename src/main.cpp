#include <ink/ink.hpp>

// #define VK_USE_PLATFORM_WIN32_KHR
// #define GLFW_EXPOSE_NATIVE_WIN32

#ifdef USE_CPU
#include <Renderers/CPURenderer.h>
#elif defined(USE_VULKAN_API)
#include <Renderers/VulkanRenderer.h>
#else
#include <Renderers/OpenGLRenderer.h>
#endif

#ifdef NDEBUG
    const ink::LogLevel logSeverity = aura3d::LogLevel::INFO;
#else
    const ink::LogLevel logSeverity = ink::LogLevel::TRACE;
#endif

int main(int argc, char **argv)
{
    INK_CORE_LOGGER;
    ink::LogManager::getInstance().setGlobalLevel(logSeverity);

    aura3d::WindowDetails windowDetails = {
        .width = 1280,
        .height = 720,
        .resizable = true,
        .targetFPS = 60
    };

#ifdef USE_CPU
    aura3d::CPURenderer cpuRenderer(windowDetails);
    cpuRenderer.run();
#elif defined(USE_VULKAN_API)
    aura3d::VkInstanceData vkInstanceData = {
        .appName = "Aura3D",
        .engineName = "Aura3DEngine",
        .appVersion = {1, 0, 0},
        .vkInstanceExtensions = {},
        .vkValidationLayers = {
            "VK_LAYER_KHRONOS_validation",
        }
    };

    aura3d::VkDeviceData vkDeviceData = {
        .vkDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        },
        .vkEnabledLayers = {},
        .concurrentQueueFlags = {},
        .exclusiveQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT
    };

    aura3d::ImageViewData vkImageViewData = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    aura3d::VulkanRenderer vkRenderer(windowDetails,
                              vkInstanceData,
                              vkDeviceData,
                              vkImageViewData);
    vkRenderer.run();
#else
    aura3d::OpenGLRenderer glRenderer(windowDetails);
    glRenderer.run();
#endif

    return 0;
}
