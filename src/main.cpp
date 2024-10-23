#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <memory.h>

#define GLFW_INCLUDE_VULKAN

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>
// #include <plog/Formatters/MessageOnlyFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>

#include "GlfwWindow/GlfwWindow.h"
#include "VkInstanceManager/VkInstanceManager.h"
#include "VkDeviceManager/VkDeviceManager.h"


#define WIDTH 1280
#define HEIGHT 720

int main(int argc, char **argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::debug, &consoleAppender);

    VkInstanceData vkInstanceData = {
        .appName = "Aura3D",
        .engineName =  "Aura3DEngine",
        .appVersion = {1, 0, 0},
        .vkInstanceExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            // VK_KHR_SWAPCHAIN_EXTENSION_NAME
        },
        .vkValidationLayers {
            "VK_LAYER_KHRONOS_validation",
        }
    };

    std::unique_ptr<VkInstanceManager> vkInstance = std::make_unique<VkInstanceManager>(vkInstanceData);
    std::unique_ptr<VkDeviceManager> vkDeviceManager = std::make_unique<VkDeviceManager>();
    vkDeviceManager->setBestDevice(vkInstance->getVkInstance());

    std::unique_ptr<GlfwWindow> glfwWindow = std::make_unique<GlfwWindow>(WIDTH, HEIGHT, "Aura3D");

    glfwWindow->process([&](){

    });

    return 0;
}
