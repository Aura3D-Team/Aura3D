// #define VK_USE_PLATFORM_WIN32_KHR
// #define GLFW_EXPOSE_NATIVE_WIN32

#include <ink/EnhancedJson.h>

#include "aura.hpp"

#include "Renderers/CPURenderer.h"
#include "Renderers/VulkanRenderer.h"
#include "Renderers/OpenGLRenderer.h"

#ifdef NDEBUG
    const ink::LogLevel logSeverity = ink::LogLevel::INFO;
#else
    const ink::LogLevel logSeverity = ink::LogLevel::TRACE;
#endif

int main(int argc, char **argv)
{
    INK_CORE_LOGGER;
    INK_CORE_LOGGER->setName(APPLICATION_NAME);
    ink::LogManager::getInstance().setGlobalLevel(logSeverity);

    ink::EnhancedJson appConfig = ink::EnhancedJson::loadFromFile("config.json");
    if (appConfig.empty()) {
        INK_ERROR << "Failed to load config.json";
        std::exit(EXIT_FAILURE);
    }

    wma::WindowDetails windowDetails = {};
    windowDetails.width = appConfig.getPath<uint>("window.width", 1280);
    windowDetails.height = appConfig.getPath<uint>("window.height", 720);
    windowDetails.resizable = appConfig.getPath<uint>("window.resizable", true);
    windowDetails.targetFPS = appConfig.getPath<uint>("window.fps", 60);

    aura3d::RendererChoice renderChoice;
    INK_ASSERT_MSG(aura3d::RendererChoiceFromString(appConfig.get<std::string>("renderer_backend"), renderChoice), "Unsupported renderer backend.");

    switch (renderChoice)
    {
        case aura3d::RendererChoice::SOFTWARE:
        {
            aura3d::cpu::CPURenderer cpuRenderer(windowDetails);
            cpuRenderer.run();
            break;
        }

        case aura3d::RendererChoice::OPENGL:
        {
            aura3d::gl::OpenGLRenderer glRenderer(windowDetails);
            glRenderer.run();
            break;
        }

        case aura3d::RendererChoice::VULKAN:
        {
            aura3d::vk::VkInstanceData vkInstanceData = {};
            vkInstanceData.appName = "Aura3D";
            vkInstanceData.engineName = "Aura3DEngine";
            vkInstanceData.appVersion = {1, 0, 0};
            vkInstanceData.vkInstanceExtensions = {};
            vkInstanceData.vkValidationLayers = {
                "VK_LAYER_KHRONOS_validation",
            };

            aura3d::vk::VkDeviceData vkDeviceData = {};
            vkDeviceData.vkDeviceExtensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            };
            vkDeviceData.vkEnabledLayers = {};
            vkDeviceData.concurrentQueueFlags = {};
            vkDeviceData.exclusiveQueueFlags =
                VK_QUEUE_GRAPHICS_BIT |
                VK_QUEUE_COMPUTE_BIT |
                VK_QUEUE_TRANSFER_BIT;

            aura3d::vk::ImageViewData vkImageViewData = {};
            vkImageViewData.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vkImageViewData.baseMipLevel = 0;
            vkImageViewData.levelCount = 1;
            vkImageViewData.baseArrayLayer = 0;
            vkImageViewData.layerCount = 1;

            aura3d::vk::VulkanRenderer vkRenderer(
                windowDetails,
                vkInstanceData,
                vkDeviceData,
                vkImageViewData
                );
            vkRenderer.run();
            break;
        }
    }

    return 0;
}
