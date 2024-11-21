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
#include <thread>

#include "GlfwAura/GlfwWindowManager/GlfwWindowManager.h"
#include "VkAura/VkInstanceManager/VkInstanceManager.h"
#include "VkAura/VkDeviceManager/VkDeviceManager.h"
#include "VkAura/VkSurfaceManager/VkSurfaceManager.h"
#include "VkAura/VkSwapChainManager/VkSwapChainManager.h"
#include <VkAura/VkCommandManager/VkCommandManager.h>
#include "ImguiAura/ImguiAura.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_glfw.h>

#include "Utils/LastWish.h"

#ifdef NDEBUG
    const bool enableValidationLayers = false;
    const plog::Severity plogSeverity = plog::info;
#else
    const bool enableValidationLayers = true;
    const plog::Severity plogSeverity = plog::debug;
#endif

int main(int argc, char **argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plogSeverity, &consoleAppender);


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

    std::unique_ptr<VkInstanceManager> vkInstance = std::make_unique<VkInstanceManager>(vkInstanceData, enableValidationLayers);


    VkDeviceData vkDeviceData = {
        .vkDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        },
        .vkEnabledLayers = {},
        .concurrentQueueFlags = {},
        .exclusiveQueueFlags = {VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_TRANSFER_BIT}
    };

    std::unique_ptr<VkDeviceManager> vkDeviceManager = std::make_unique<VkDeviceManager>(vkInstance->getVkInstance(), vkDeviceData);

    VkQueueFlags exclusiveMergedFlag = 0;
    for (const uint32_t& f : vkDeviceData.exclusiveQueueFlags) {
        exclusiveMergedFlag |= f;
    }

    glfwWindowManager->createGlfwWindowManager("Aura3D");
    std::unique_ptr<VkSurfaceManager> vkSurfaceManager = std::make_unique<VkSurfaceManager>(
        vkInstance->getVkInstance(),
        glfwWindowManager->getWindowInstance()
    );

    std::vector<VkQueue> vkGraphicsQueues = vkDeviceManager->getQueueManager()->getQueueData(exclusiveMergedFlag)->queues;
    uint32_t graphicsIndexFamily = VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(),
                                                                        exclusiveMergedFlag, *vkSurfaceManager->getSurface());

    std::unique_ptr<VkSwapChainManager> vkSwapChainManager = std::make_unique<VkSwapChainManager>(
        *vkDeviceManager->getPhysicalDevice(),
        vkDeviceManager->getDevice(),
        *vkSurfaceManager->getSurface()
    );
    vkSwapChainManager->createSwapChain(glfwWindowManager->getWindowInstance(), *vkSurfaceManager->getSurface(), vkDeviceManager.get());

    uint32_t imageCount = vkSwapChainManager->getSwapchainCreateInfoKHR()->minImageCount;

    std::unique_ptr<ImguiAura> imguiAura = std::make_unique<ImguiAura>(
        *vkInstance->getVkInstance(),
        *vkDeviceManager->getDevice(),
        *vkDeviceManager->getPhysicalDevice(),
        vkGraphicsQueues.front(),
        *vkSwapChainManager->getRenderPass(),
        glfwWindowManager->getWindowInstance(),
        imageCount
    );

    std::unique_ptr<VkCommandManager> vkCommandManager = std::make_unique<VkCommandManager>(vkDeviceManager->getDevice(), graphicsIndexFamily);

    std::thread t1([&vkCommandManager, graphicsIndexFamily, &glfwWindowManager, vkGraphicsQueues]() {
        VkCommandPool vkCommandPool = vkCommandManager->getThreadCommandPool();

        ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

        // Use process method of glfwWindowManager to execute GUI commands
        glfwWindowManager->process([&]() {
            // Begin a new ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Render ImGui UI components
            ImGui::Begin("Hello, ImGui!");
            ImGui::Text("This is some useful text.");
            ImGui::ColorEdit3("clear color", (float*)&clearColor);
            ImGui::End();

            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();

            VkCommandBuffer commandBuffer = vkCommandManager->beginCommandBuffer();
            ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
            vkCommandManager->endCommandBuffer(commandBuffer, vkGraphicsQueues.front());
        });
    });

    t1.join();

    return 0;
}
