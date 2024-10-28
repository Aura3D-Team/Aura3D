#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <memory.h>

#define GLFW_INCLUDE_VULKAN
// #define VK_USE_PLATFORM_WIN32_KHR
// #define GLFW_EXPOSE_NATIVE_WIN32

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>
// #include <plog/Formatters/MessageOnlyFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>

#include "GlfwWindowManager/GlfwWindowManager.h"
#include "VkInstanceManager/VkInstanceManager.h"
#include "VkDeviceManager/VkDeviceManager.h"
#include "VkSurfaceManager/VkSurfaceManager.h"

int main(int argc, char **argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::debug, &consoleAppender);

    std::unique_ptr<GlfwWindowManager> glfwWindowManager = std::make_unique<GlfwWindowManager>(1280, 720);

    VkInstanceData vkInstanceData = {
        .appName = "Aura3D",
        .engineName = "Aura3DEngine",
        .appVersion = {1, 0, 0},
        .vkInstanceExtensions = {},
        .vkValidationLayers = {
            "VK_LAYER_KHRONOS_validation",
        }
    };

    const std::vector<const char*> glfwExtensions = glfwWindowManager->getGlfwVulkanExtensions();
    for (const char* ext : glfwExtensions) {
        vkInstanceData.vkInstanceExtensions.push_back(ext);
    }

    std::unique_ptr<VkInstanceManager> vkInstance = std::make_unique<VkInstanceManager>(vkInstanceData);

    VkDeviceData vkDeviceData = {
        .vkDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        },
        .vkEnabledLayers = {}
    };

    std::unique_ptr<VkDeviceManager> vkDeviceManager = std::make_unique<VkDeviceManager>(vkInstance->getVkInstance(), vkDeviceData);

    glfwWindowManager->createGlfwWindowManager("Aura3D");
    std::unique_ptr<VkSurfaceManager> vkSurfaceManager = std::make_unique<VkSurfaceManager>(
        vkInstance->getVkInstance(),
        glfwWindowManager->getWindowInstance()
    );

    glfwWindowManager->process([&](){

    });

    return 0;
}
