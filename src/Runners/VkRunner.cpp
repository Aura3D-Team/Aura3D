#include "VkRunner.h"
#include "VkAura/VkVertexBufferManager/VkVertexBufferManager.h"

#include <set>
#include <plog/Log.h>

namespace aura3d {

VkRunner::VkRunner(WindowDetails windowDetails,
                   VkInstanceData vkInstanceData,
                   VkDeviceData vkDeviceData,
                   ImageViewData vkImageViewData) :
    _vkImageViewData(vkImageViewData)
{
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

#ifdef SDL_WINDOW_MANAGER
    _windowManagerApi = std::make_unique<aura3d::SDLAuraWindowManager>(windowDetails);
#else
    _windowManagerApi = std::make_unique<aura3d::GlfwAuraWindowManager>(windowDetails);
#endif

    const std::vector<const char*> glfwExtensions = _windowManagerApi->getVulkanExtensions();
    for (const char* ext : glfwExtensions) {
        vkInstanceData.vkInstanceExtensions.push_back(ext);
    }


    _vkInstance = std::make_unique<aura3d::VkInstanceManager>(vkInstanceData, enableValidationLayers);


    _vkDeviceManager = std::make_unique<aura3d::VkDeviceManager>(_vkInstance->getVkInstance(), vkDeviceData);


    _windowManagerApi->createWindow(APPLICATION_NAME);
    _vkSurfaceManager = std::make_unique<aura3d::VkSurfaceManager>(
        _vkInstance->getVkInstance(),
        _windowManagerApi->getWindowInstance()
    );


    _queueDataFromExclusiveFlags = _vkDeviceManager->getQueueManager()->getQueues(vkDeviceData.exclusiveQueueFlags);
    _graphicsIndexFamily = aura3d::VkQueueManager::findQueueFamilyIndex(*_vkDeviceManager->getPhysicalDevice(),
                                                                                vkDeviceData.exclusiveQueueFlags,
                                                                                *_vkSurfaceManager->getSurface());


    _vkSwapChainManager = std::make_unique<aura3d::VkSwapChainManager>(
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceManager->getDevice(),
        *_vkSurfaceManager->getSurface()
    );


    _vkImageViewsManager = std::make_unique<aura3d::VkImageViewsManager>(_vkDeviceManager->getDevice());


    _vkRenderPassManager = std::make_unique<aura3d::VkRenderPassManager>(_vkDeviceManager->getDevice());


    _vkFrameBuffersManager = std::make_unique<aura3d::VkFrameBuffersManager>(_vkDeviceManager->getDevice());


    _vkGraphicsPipelineManager = std::make_unique<aura3d::VkGraphicsPipelineManager>("./shaders/vert/test_shader2d_vert.spv",
                                                                                     "./shaders/frag/test_shader2d_frag.spv",
                                                                                     _vkDeviceManager->getDevice());


    _vkCommandManager = std::make_unique<aura3d::VkCommandManager>(_vkDeviceManager->getDevice(),
                                                                   _graphicsIndexFamily);


    _vkRenderSyncManager = std::make_unique<aura3d::VkRenderSyncManager>(_vkDeviceManager->getDevice());
}

VkRunner::~VkRunner()
{

}

void VkRunner::run()
{

    _vkSwapChainManager->createSwapChain(_windowManagerApi->getWindowInstance(), *_vkSurfaceManager->getSurface(), _vkDeviceManager.get());
    // uint32_t imageCount = vkSwapChainManager->getSwapchainCreateInfoKHR()->minImageCount;


    _vkImageViewsManager->createImageViews(_vkSwapChainManager->getSwapChainImages(),
                                           _vkSwapChainManager->getChoosedSurfaceFormat()->format,
                                           _vkImageViewData);


    _vkRenderPassManager->createRenderPass(_vkSwapChainManager->getChoosedSurfaceFormat()->format);


    _vkFrameBuffersManager->createFrameBuffers(_vkImageViewsManager->getImageViews(),
                                               *_vkRenderPassManager->getRenderPass(),
                                               *_vkSwapChainManager->getExtent2D());

    // TODO
    // Things are hardcoded like bool is2d, think of a interesting way of solving this.
    std::vector<Vertex2d> vertices = {
        {{0.0f, -0.5f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{0.0f, -0.5f}, {1.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}
    };

    std::vector<VkVertexInputBindingDescription> vertexBindingDescArray = {
        VkVertexBufferManager::getBindingDescription(true),
    };

    auto vertexAttributeArray = VkVertexBufferManager::getAttributeDescriptions(true);

    _vkGraphicsPipelineManager->createPipeline(*_vkRenderPassManager->getRenderPass(),
                                               *_vkSwapChainManager->getExtent2D(),
                                               vertexBindingDescArray,
                                               vertexAttributeArray);


    VkFixedArray<VkCommandBuffer> cmdBuffers = _vkCommandManager->createCommandBuffer();


    _vkRenderSyncManager->create();

    auto* windowFlags = _windowManagerApi->getWindowFlags();
    const auto& frameBuffers = _vkFrameBuffersManager->getFrameBuffers();
    VkExtent2D* extent = _vkSwapChainManager->getExtent2D();
    VkQueue queueToDraw = _queueDataFromExclusiveFlags.front()->queues.front();

    auto& imageAvailableSemaphores = _vkRenderSyncManager->getImageAvailableSemaphores();
    auto& renderFinishedSemaphores = _vkRenderSyncManager->getRenderFinishedSemaphores();
    auto& fences = _vkRenderSyncManager->getInFlightFences();

    uint32_t ImagesCount = _vkSwapChainManager->getSwapChainImages().size();

    uint32_t currentFrame = 0;

    std::set<uint32_t> imageIndexes = {};

    _windowManagerApi->process([&]() {
        _vkRenderSyncManager->waitForFences(currentFrame);

        const uint32_t imageIndex = _vkSwapChainManager->acquireNextImage(imageAvailableSemaphores[currentFrame], windowFlags);

        if (imageIndex >= ImagesCount)
        {
            PLOG_WARNING << "Swap chain needs recreation!";
            windowFlags->resized = false; // TODO: SDL HAndle
            handleWindowChanges();
            imageIndexes.clear();
            return;
        }

        _vkRenderSyncManager->resetFences(currentFrame);

        _vkCommandManager->resetCommandPool();

        // Begin recording the command buffer.
        aura3d::VkCommandManager::beginCommandBuffer(cmdBuffers[currentFrame]);

        // Transition image layout if it is the first time we encounter this imageIndex
        if (imageIndexes.find(imageIndex) == imageIndexes.end())
        {
            // PLOG_DEBUG << "Transitioning image layout for first use of imageIndex " << imageIndex;
            _vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
                                                       imageIndex,
                                                       VK_IMAGE_LAYOUT_UNDEFINED,
                                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                       {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
                                                       {VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});
            imageIndexes.insert(imageIndex);
        }
        else
        {
            // PLOG_DEBUG << "Transitioning image layout for already used imageIndex " << imageIndex;
            _vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
                                                       imageIndex,
                                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                       {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
                                                       {VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});
        }

        // Transition image layout for final presentation
        _vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
                                                   imageIndex,
                                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                   {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT},
                                                   {VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_NONE});

        // Begin render pass
        _vkRenderPassManager->beginRenderPass(cmdBuffers[currentFrame], frameBuffers[currentFrame], *extent);

        // Record drawing commands
        // _vkGraphicsPipelineManager->cmdBindPipeline(cmdBuffers[currentFrame]);
        // _vkGraphicsPipelineManager->cmdDraw(cmdBuffers[currentFrame], extent);

        aura3d::VkRenderPassManager::endRenderPass(cmdBuffers[currentFrame]);

        // End command buffer recording
        aura3d::VkCommandManager::endCommandBuffer(cmdBuffers[currentFrame]);

        // Submit command buffer to the queue
        aura3d::VkQueueManager::submitCmdIntoQueue(queueToDraw,
                                                   &cmdBuffers[currentFrame],
                                                   &imageAvailableSemaphores[currentFrame],
                                                   &renderFinishedSemaphores[currentFrame],
                                                   fences[currentFrame]);

        // Present the image to the swap chain
        _vkSwapChainManager->presentBackToSwapChain(queueToDraw,
                                                    &renderFinishedSemaphores[currentFrame],
                                                    imageIndex);

        // Move to the next frame
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    });

    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
}

void VkRunner::handleWindowChanges()
{
    auto device = *_vkDeviceManager->getDevice();

#ifdef SDL_WINDOW_MANAGER
    SDL_Window* window = _windowManagerApi->getWindowInstance();

    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);

    // Wait for window to be restored
    while (width == 0 || height == 0) {
        SDL_Event event;
        while (SDL_WaitEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 event.window.event == SDL_WINDOWEVENT_RESTORED)) {
                SDL_GetWindowSize(window, &width, &height);
                if (width > 0 && height > 0)
                    break;
            }
        }
    }
#else
    GLFWwindow* window = _windowManagerApi->getWindowInstance();

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    // Wait for window to be restored
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }
#endif

    vkDeviceWaitIdle(device);

    _vkFrameBuffersManager->cleanup();
    _vkImageViewsManager->cleanup();
    _vkSwapChainManager->cleanup();
    _vkRenderPassManager->cleanup();
    _vkRenderSyncManager->cleanup();
    _vkImageViewsManager->cleanup();

    _vkSwapChainManager->initSwapChainSupportDetails(*_vkDeviceManager->getPhysicalDevice(), *_vkSurfaceManager->getSurface());

    _vkSwapChainManager->createSwapChain(window, *_vkSurfaceManager->getSurface(), _vkDeviceManager.get());

    _vkImageViewsManager->createImageViews(_vkSwapChainManager->getSwapChainImages(),
                                           _vkSwapChainManager->getChoosedSurfaceFormat()->format,
                                           _vkImageViewData);

    _vkRenderPassManager->createRenderPass(_vkSwapChainManager->getChoosedSurfaceFormat()->format);

    _vkFrameBuffersManager->createFrameBuffers(_vkImageViewsManager->getImageViews(),
                                               *_vkRenderPassManager->getRenderPass(),
                                               *_vkSwapChainManager->getExtent2D());

    _vkRenderSyncManager->create();
}

}
