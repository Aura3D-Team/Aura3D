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

#include <GlfwAura/GlfwWindowManager/GlfwWindowManager.h>
#include <VkAura/VkInstanceManager/VkInstanceManager.h>
#include <VkAura/VkDeviceManager/VkDeviceManager.h>
#include <VkAura/VkSurfaceManager/VkSurfaceManager.h>
#include <VkAura/VkSwapChainManager/VkSwapChainManager.h>
#include <VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h>
#include <VkAura/VkImageViewsManager/VkImageViewsManager.h>
#include <VkAura/VkRenderPassManager/VkRenderPassManager.h>
#include <VkAura/VkCommandManager/VkCommandManager.h>
#include <VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h>
#include <VkAura/VkRenderSyncManager/VkRenderSyncManager.h>


// #include <ImguiAura/ImguiAura.h>
// #include <imgui/imgui.h>
// #include <imgui/backends/imgui_impl_vulkan.h>
// #include <imgui/backends/imgui_impl_glfw.h>

// #include "Utils/LastWish.h"

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
        .exclusiveQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT
    };

    std::unique_ptr<VkDeviceManager> vkDeviceManager = std::make_unique<VkDeviceManager>(vkInstance->getVkInstance(), vkDeviceData);


    glfwWindowManager->createGlfwWindowManager(APPLICATION_NAME);
    std::unique_ptr<VkSurfaceManager> vkSurfaceManager = std::make_unique<VkSurfaceManager>(
        vkInstance->getVkInstance(),
        glfwWindowManager->getWindowInstance()
    );


    std::vector<QueueData*> arrayQueueData = vkDeviceManager->getQueueManager()->getQueues(vkDeviceData.exclusiveQueueFlags);
    uint32_t graphicsIndexFamily = VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(),
                                                                        vkDeviceData.exclusiveQueueFlags, *vkSurfaceManager->getSurface());


    std::unique_ptr<VkSwapChainManager> vkSwapChainManager = std::make_unique<VkSwapChainManager>(
        *vkDeviceManager->getPhysicalDevice(),
        vkDeviceManager->getDevice(),
        *vkSurfaceManager->getSurface()
    );

    vkSwapChainManager->createSwapChain(glfwWindowManager->getWindowInstance(), *vkSurfaceManager->getSurface(), vkDeviceManager.get());
    // uint32_t imageCount = vkSwapChainManager->getSwapchainCreateInfoKHR()->minImageCount;


    std::unique_ptr<VkImageViewsManager> vkImageViewsManager = std::make_unique<VkImageViewsManager>(vkDeviceManager->getDevice());
    vkImageViewsManager->createImageViews(vkSwapChainManager->getSwapChainImages(), vkSwapChainManager->getChoosedSurfaceFormat()->format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, vkSwapChainManager->getSwapchainCreateInfoKHR()->minImageCount);


    std::unique_ptr<VkRenderPassManager> vkRenderPassManager = std::make_unique<VkRenderPassManager>(vkDeviceManager->getDevice());
    vkRenderPassManager->createRenderPass(vkSwapChainManager->getChoosedSurfaceFormat()->format);

    std::unique_ptr<VkFrameBuffersManager> vkFrameBuffersManager = std::make_unique<VkFrameBuffersManager>(vkDeviceManager->getDevice());
    vkFrameBuffersManager->createFrameBuffers(vkImageViewsManager->getImageViews(),
                                              *vkRenderPassManager->getRenderPass(),
                                              *vkSwapChainManager->getExtent2D());


    std::unique_ptr<VkGraphicsPipelineManager> vkGraphicsPipelineManager = std::make_unique<VkGraphicsPipelineManager>("./shaders/vert/test_shader2d_vert.spv",
                                                                                                                       "./shaders/frag/test_shader2d_frag.spv",
                                                                                                                       vkDeviceManager->getDevice());
    vkGraphicsPipelineManager->createPipeline(*vkRenderPassManager->getRenderPass(), *vkSwapChainManager->getExtent2D());


    std::unique_ptr<VkCommandManager> vkCommandManager = std::make_unique<VkCommandManager>(vkDeviceManager->getDevice(), graphicsIndexFamily);
    VkFixedArray<VkCommandBuffer> cmdBuffers = vkCommandManager->createCommandBuffer();


    std::unique_ptr<VkRenderSyncManager> vkRenderSyncManager = std::make_unique<VkRenderSyncManager>(vkDeviceManager->getDevice());


    const auto& frameBuffers = vkFrameBuffersManager->getFrameBuffers();
    VkExtent2D extent = *vkSwapChainManager->getExtent2D();
    VkQueue queueToDraw = arrayQueueData.front()->queues.front();

    auto& imageAvailableSemaphores = vkRenderSyncManager->getImageAvailableSemaphores();
    auto& renderFinishedSemaphores = vkRenderSyncManager->getRenderFinishedSemaphores();

    uint32_t currentFrame = 0;

    glfwWindowManager->process([&]() {
        vkRenderSyncManager->waitForFences(currentFrame);
        vkRenderSyncManager->resetFences(currentFrame);

        const uint32_t imageIndex = vkSwapChainManager->acquireNextImage(imageAvailableSemaphores[currentFrame]);

        if (imageIndex == UINT32_MAX)
        {
            vkSwapChainManager->recreateSwapChain(vkImageViewsManager.get(),
                                                  vkFrameBuffersManager.get(),
                                                  glfwWindowManager->getWindowInstance(),
                                                  *vkSurfaceManager->getSurface(),
                                                  vkDeviceManager.get(),
                                                  *vkRenderPassManager->getRenderPass());
            return; // continue
        }

        vkSwapChainManager->debug(imageIndex);

        vkCommandManager->resetCommandPool();

        // Begin recording the command buffer.
        VkCommandManager::beginCommandBuffer(cmdBuffers[currentFrame]);

        // vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
        //                                           imageIndex,
        //                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        //                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        //                                           {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        //                                           {VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});


        // vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
        //                                           imageIndex,
        //                                           VK_IMAGE_LAYOUT_UNDEFINED,
        //                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        //                                           {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        //                                           {VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});

        vkRenderPassManager->beginRenderPass(cmdBuffers[currentFrame], frameBuffers[currentFrame], extent);

        // Record drawing commands.
        vkGraphicsPipelineManager->cmdBindPipeline(cmdBuffers[currentFrame]);
        vkGraphicsPipelineManager->cmdDraw(cmdBuffers[currentFrame], extent);

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

        VkRenderPassManager::endRenderPass(cmdBuffers[currentFrame]);

        // Transition image layout for presentation.
        // vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
        //                                           imageIndex,
        //                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        //                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        //                                           {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT},
        //                                           {VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_NONE});

        // ✅ END the command buffer before submission
        VkCommandManager::endCommandBuffer(cmdBuffers[currentFrame]);

        // ✅ Now submit the finished command buffer to the queue.
        VkQueueManager::submitCmdIntoQueue(queueToDraw,
                                           &cmdBuffers[currentFrame],
                                           waitSemaphores,
                                           signalSemaphores);

        // Present the image.
        vkSwapChainManager->presentBackToSwapChain(queueToDraw,
                                                   signalSemaphores,
                                                   imageIndex);

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    });

    vkDeviceWaitIdle(*vkDeviceManager->getDevice());

    // vkCommandManager->endCommandBuffer(commandBuffer, vkDeviceManager->getQueueManager()->getQueueData(vkDeviceData.exclusiveQueueFlags)->queues);


    // std::unique_ptr<ImguiAura> imguiAura = std::make_unique<ImguiAura>(
    //     *vkInstance->getVkInstance(),
    //     *vkDeviceManager->getDevice(),
    //     *vkDeviceManager->getPhysicalDevice(),
    //     vkGraphicsQueues.front(),
    //     *vkSwapChainManager->getRenderPass(),
    //     glfwWindowManager->getWindowInstance(),
    //     imageCount
    // );

    // std::thread t1([&vkCommandManager, graphicsIndexFamily, &glfwWindowManager, vkGraphicsQueues]() {
    //     VkCommandPool vkCommandPool = vkCommandManager->getThreadCommandPool();

    //     // ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    //     // Use process method of glfwWindowManager to execute GUI commands
    //     glfwWindowManager->process([&]() {
    //         // // Begin a new ImGui frame
    //         // ImGui_ImplVulkan_NewFrame();
    //         // ImGui_ImplGlfw_NewFrame();
    //         // ImGui::NewFrame();

    //         // // Render ImGui UI components
    //         // ImGui::Begin("Hello, ImGui!");
    //         // ImGui::Text("This is some useful text.");
    //         // ImGui::ColorEdit3("clear color", (float*)&clearColor);
    //         // ImGui::End();

    //         // ImGui::Render();
    //         // ImDrawData* draw_data = ImGui::GetDrawData();

    //         // VkCommandBuffer commandBuffer = vkCommandManager->beginCommandBuffer();
    //         // ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
    //         // vkCommandManager->endCommandBuffer(commandBuffer, vkGraphicsQueues.front());
    //     });
    // });

    // t1.join();

    return 0;
}
