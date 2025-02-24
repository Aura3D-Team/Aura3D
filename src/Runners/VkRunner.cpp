#include "VkRunner.h"

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

VkRunner::VkRunner() {}

void VkRunner::run()
{
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

    std::unique_ptr<aura3d::GlfwWindowManager> glfwWindowManager = std::make_unique<aura3d::GlfwWindowManager>(1280, 720);


    aura3d::VkInstanceData vkInstanceData = {
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

    std::unique_ptr<aura3d::VkInstanceManager> vkInstance = std::make_unique<aura3d::VkInstanceManager>(vkInstanceData, enableValidationLayers);

    aura3d::VkDeviceData vkDeviceData = {
        .vkDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        },
        .vkEnabledLayers = {},
        .concurrentQueueFlags = {},
        .exclusiveQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT
    };

    std::unique_ptr<aura3d::VkDeviceManager> vkDeviceManager = std::make_unique<aura3d::VkDeviceManager>(vkInstance->getVkInstance(), vkDeviceData);


    glfwWindowManager->createGlfwWindowManager(APPLICATION_NAME);
    std::unique_ptr<aura3d::VkSurfaceManager> vkSurfaceManager = std::make_unique<aura3d::VkSurfaceManager>(
        vkInstance->getVkInstance(),
        glfwWindowManager->getWindowInstance()
        );


    std::vector<aura3d::QueueData*> arrayQueueData = vkDeviceManager->getQueueManager()->getQueues(vkDeviceData.exclusiveQueueFlags);
    uint32_t graphicsIndexFamily = aura3d::VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(),
                                                                                vkDeviceData.exclusiveQueueFlags, *vkSurfaceManager->getSurface());


    std::unique_ptr<aura3d::VkSwapChainManager> vkSwapChainManager = std::make_unique<aura3d::VkSwapChainManager>(
        *vkDeviceManager->getPhysicalDevice(),
        vkDeviceManager->getDevice(),
        *vkSurfaceManager->getSurface()
        );

    vkSwapChainManager->createSwapChain(glfwWindowManager->getWindowInstance(), *vkSurfaceManager->getSurface(), vkDeviceManager.get());
    // uint32_t imageCount = vkSwapChainManager->getSwapchainCreateInfoKHR()->minImageCount;


    std::unique_ptr<aura3d::VkImageViewsManager> vkImageViewsManager = std::make_unique<aura3d::VkImageViewsManager>(vkDeviceManager->getDevice());
    vkImageViewsManager->createImageViews(vkSwapChainManager->getSwapChainImages(), vkSwapChainManager->getChoosedSurfaceFormat()->format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, vkSwapChainManager->getSwapchainCreateInfoKHR()->minImageCount);


    std::unique_ptr<aura3d::VkRenderPassManager> vkRenderPassManager = std::make_unique<aura3d::VkRenderPassManager>(vkDeviceManager->getDevice());
    vkRenderPassManager->createRenderPass(vkSwapChainManager->getChoosedSurfaceFormat()->format);

    std::unique_ptr<aura3d::VkFrameBuffersManager> vkFrameBuffersManager = std::make_unique<aura3d::VkFrameBuffersManager>(vkDeviceManager->getDevice());
    vkFrameBuffersManager->createFrameBuffers(vkImageViewsManager->getImageViews(),
                                              *vkRenderPassManager->getRenderPass(),
                                              *vkSwapChainManager->getExtent2D());


    std::unique_ptr<aura3d::VkGraphicsPipelineManager> vkGraphicsPipelineManager = std::make_unique<aura3d::VkGraphicsPipelineManager>("./shaders/vert/test_shader2d_vert.spv",
                                                                                                                                       "./shaders/frag/test_shader2d_frag.spv",
                                                                                                                                       vkDeviceManager->getDevice());
    vkGraphicsPipelineManager->createPipeline(*vkRenderPassManager->getRenderPass(), *vkSwapChainManager->getExtent2D());


    std::unique_ptr<aura3d::VkCommandManager> vkCommandManager = std::make_unique<aura3d::VkCommandManager>(vkDeviceManager->getDevice(), graphicsIndexFamily);
    VkFixedArray<VkCommandBuffer> cmdBuffers = vkCommandManager->createCommandBuffer();


    std::unique_ptr<aura3d::VkRenderSyncManager> vkRenderSyncManager = std::make_unique<aura3d::VkRenderSyncManager>(vkDeviceManager->getDevice());


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
        aura3d::VkCommandManager::beginCommandBuffer(cmdBuffers[currentFrame]);

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

        aura3d::VkRenderPassManager::endRenderPass(cmdBuffers[currentFrame]);

        // Transition image layout for presentation.
        // vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
        //                                           imageIndex,
        //                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        //                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        //                                           {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT},
        //                                           {VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_NONE});

        // ✅ END the command buffer before submission
        aura3d::VkCommandManager::endCommandBuffer(cmdBuffers[currentFrame]);

        // ✅ Now submit the finished command buffer to the queue.
        aura3d::VkQueueManager::submitCmdIntoQueue(queueToDraw,
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
}
