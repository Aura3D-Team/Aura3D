#include <ink/ink.hpp>

// #define VK_USE_PLATFORM_WIN32_KHR
// #define GLFW_EXPOSE_NATIVE_WIN32

#ifdef USE_CPU
#include <Runners/CpuRunner.h>
#elif defined(USE_VULKAN_API)
#include <Runners/VkRunner.h>
#else
#include <Runners/GlRunner.h>
#endif

#ifdef NDEBUG
    const ink::LogLevel logSeverity = aura3d::LogLevel::INFO;
#else
    const ink::LogLevel logSeverity = ink::LogLevel::TRACE;
#endif

int main(int argc, char **argv)
{
    INK_CORE_LOGGER;

    aura3d::WindowDetails windowDetails = {
        .width = 1280,
        .height = 720,
        .resizable = true,
        .targetFPS = 10
    };

#ifdef USE_CPU
    aura3d::CpuRunner CpuRunner(windowDetails);
    CpuRunner.run();
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

    aura3d::VkRunner vkRunner(windowDetails,
                              vkInstanceData,
                              vkDeviceData,
                              vkImageViewData);
    vkRunner.run();
#else
    aura3d::GlRunner glRunner(windowDetails);
    glRunner.run();
#endif

    return 0;
}
