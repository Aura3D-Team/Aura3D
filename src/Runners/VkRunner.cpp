#include "VkRunner.h"
#include <ink/ink.hpp>

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

    VkHostAllocatorCreateInfo vkHostAllocatorConfig = {
        .threadSafetyMode = HostThreadSafetyMode::NONE,
        .enableMemoryPools = true,
#ifdef NDEBUG
        .trackLeaks = false,  // Disable in release builds
#else
        .trackLeaks = true,
#endif
        .enableBatchProcessing = true,
        .batchDeallocLimit = 256  // Higher batch threshold
    };

    _vkHostAllocator = std::make_unique<aura3d::VkHostAllocator>(vkHostAllocatorConfig);

#ifdef SDL_WINDOW_MANAGER
    _windowManagerApi = std::make_unique<aura3d::SDLAuraWindowManager>(windowDetails);
#else
    _windowManagerApi = std::make_unique<aura3d::GlfwAuraWindowManager>(windowDetails);
#endif
    const std::vector<const char*> windowApiExts = _windowManagerApi->getVulkanExtensions();
    for (const char* ext : windowApiExts) {
        vkInstanceData.vkInstanceExtensions.push_back(ext);
    }

    _vkInstance = std::make_unique<aura3d::VkInstanceManager>(_vkHostAllocator.get(), vkInstanceData, enableValidationLayers);
    _vkDeviceManager = std::make_unique<aura3d::VkDeviceManager>(_vkHostAllocator.get(), _vkInstance->getVkInstance(), vkDeviceData);

    aura3d::VkDeviceAllocatorCreateInfo vkDeviceAllocatorCreateInfo = {
        .physicalDevice = *_vkDeviceManager->getPhysicalDevice(),
        .device = *_vkDeviceManager->getDevice(),
        .blockSize = 128 * 1024 * 1024,  // 128MB for fewer reallocations if you have lots of memory
        .smallBlockSize = 8 * 1024 * 1024, // 8MB for small allocations
        .enableDefragmentation = false, // Disable unless needed
#ifdef NDEBUG
        .trackLeaks = false,  // Disable in release builds
#else
        .trackLeaks = true,
#endif
        .threadSafetyMode = ThreadSafetyMode::NONE,
        .strategy = AllocationStrategy::FIRST_FIT, // Fastest allocation strategy
        .dedicatedAllocationThreshold = 64 * 1024 * 1024, // 64MB threshold for dedicated allocations
        .useBuddyAllocatorForBuffers = true,
        .deferFrees = true,
        .deferredFreeLimit = 256, // Larger batch for complex scenes
    };

    _vkDeviceAllocator = std::make_unique<aura3d::VkDeviceAllocator>(vkDeviceAllocatorCreateInfo);

    _windowManagerApi->createWindow(APPLICATION_NAME);
    _vkSurfaceManager = std::make_unique<aura3d::VkSurfaceManager>(
        _vkHostAllocator.get(),
        _vkInstance->getVkInstance(),
        _windowManagerApi->getWindowInstance()
        );

    _queueDataFromExclusiveFlags = _vkDeviceManager->getQueueManager()->getQueues(vkDeviceData.exclusiveQueueFlags);
    _graphicsIndexFamily = aura3d::VkQueueManager::findQueueFamilyIndex(*_vkDeviceManager->getPhysicalDevice(),
                                                                        vkDeviceData.exclusiveQueueFlags,
                                                                        *_vkSurfaceManager->getSurface());
    _vkSwapChainManager = std::make_unique<aura3d::VkSwapChainManager>(
        _vkHostAllocator.get(),
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceManager->getDevice(),
        *_vkSurfaceManager->getSurface()
        );
    _vkImageViewsManager = std::make_unique<aura3d::VkImageViewsManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
    _vkRenderPassManager = std::make_unique<aura3d::VkRenderPassManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
    _vkFrameBuffersManager = std::make_unique<aura3d::VkFrameBuffersManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());

    _vkGraphicsPipelineManager = std::make_unique<aura3d::VkGraphicsPipelineManager>(_vkHostAllocator.get(),
                                                                                     "./shaders/vert/test_shader2d_vert.spv",
                                                                                     "./shaders/frag/test_shader2d_frag.spv",
                                                                                     _vkDeviceManager->getDevice());

    _vkDescriptorManager = std::make_unique<aura3d::VkDescriptorManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
    _vkVertexBufferManager = std::make_unique<aura3d::VkVertexBufferManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice()
    );
    _vkUniformBufferManager = std::make_unique<aura3d::VkUniformBufferManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice()
    );
    _vkCommandManager = std::make_unique<aura3d::VkCommandManager>(_vkHostAllocator.get(),
                                                                   _vkDeviceManager->getDevice(),
                                                                   _graphicsIndexFamily);
    _vkTextureManager = std::make_unique<VkTextureManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice(),
        _vkDeviceManager->getPhysicalDevice(),
        _vkCommandManager->getThreadCommandPool(),
        _queueDataFromExclusiveFlags.front()->queues.front()
        );

    _vkRenderSyncManager = std::make_unique<aura3d::VkRenderSyncManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
}

VkRunner::~VkRunner()
{
    // Wait for the device to finish operations before destroying resources
    if (_vkDeviceManager && _vkDeviceManager->getDevice()) {
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    }

    cleanup();
}

void VkRunner::run()
{
    _vkSwapChainManager->createSwapChain(_windowManagerApi->getWindowInstance(), *_vkSurfaceManager->getSurface(), _vkDeviceManager.get());

    _vkImageViewsManager->createImageViews(_vkSwapChainManager->getSwapChainImages(),
                                           _vkSwapChainManager->getChoosedSurfaceFormat()->format,
                                           _vkImageViewData);

    _vkRenderPassManager->createRenderPass(_vkSwapChainManager->getChoosedSurfaceFormat()->format);

    _vkFrameBuffersManager->createFrameBuffers(_vkImageViewsManager->getImageViews(),
                                               *_vkRenderPassManager->getRenderPass(),
                                               *_vkSwapChainManager->getExtent2D());

    // Define a proper triangle with non-overlapping vertices
    std::vector<Vertex2d> vertices = {
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},  // Bottom-left, red
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  // Bottom-right, green
        {{ 0.0f,  0.5f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}   // Top-center, blue
    };

    std::vector<VkVertexInputBindingDescription> vertexBindingDescArray = {
        VkVertexBufferManager::getBindingDescription(true),
    };

    auto vertexAttributeArray = VkVertexBufferManager::getAttributeDescriptions(true);
    VkQueue queueToDraw = _queueDataFromExclusiveFlags.front()->queues.front();

    // Create vertex buffer with named buffer for easier management
    _vkVertexBufferManager->createVertexBuffer("mainTriangle",
                                               *_vkDeviceManager->getPhysicalDevice(),
                                               _vkCommandManager->getThreadCommandPool(),
                                               _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
                                               queueToDraw,
                                               vertices,
                                               false);  // No need for persistent mapping for static geometry

    // Get swapchain image count for multiple descriptor sets
    uint32_t swapChainImageCount = _vkSwapChainManager->getSwapChainImages().size();

    // 1. Set up UBO for transformation
    _vkUniformBufferManager->createUniformBuffers(
        *_vkDeviceManager->getPhysicalDevice(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        swapChainImageCount
        );

    // Update uniform buffer with identity matrix
    TransformUBO ubo{};
    ubo.transform = glm::mat4(1.0f);
    for (uint32_t i = 0; i < swapChainImageCount; i++) {
        _vkUniformBufferManager->updateUniformBuffer(i, ubo);
    }

    // Create descriptor set layouts in your pipeline manager
    _vkGraphicsPipelineManager->createDescriptorSetLayouts();

    // Allocate and update descriptor sets using VkDescriptorManager
    std::vector<VkDescriptorSet> descriptorSets(swapChainImageCount);
    for (size_t i = 0; i < swapChainImageCount; i++) {
        // Allocate a descriptor set
        descriptorSets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(0)
        );

        // Update the descriptor set with uniform buffer
        _vkDescriptorManager->updateDescriptorSet(
            descriptorSets[i],             // The descriptor set
            0,                             // Binding point in shader
            _vkUniformBufferManager->getUniformBuffer(i),   // Uniform buffer
            _vkUniformBufferManager->getUniformBufferSize() // Size of the data
        );
    }

    // Create a white texture for our triangle
    auto whiteTexture = _vkTextureManager->createSolidColorTexture("white", 255, 255, 255);

    // Allocate and update descriptor sets for the texture (set 1)
    std::vector<VkDescriptorSet> textureDescriptorSets(swapChainImageCount);
    for (size_t i = 0; i < swapChainImageCount; i++) {
        // Allocate descriptor set for texture
        textureDescriptorSets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(1)
        );

        // Get the texture data
        const auto* texture = _vkTextureManager->getTexture("white");

        // Update descriptor set with texture
        _vkDescriptorManager->updateCombinedImageSamplerDescriptorSet(
            textureDescriptorSets[i],
            0,
            texture->view,
            texture->sampler
        );
    }

    _vkGraphicsPipelineManager->createPipeline(*_vkRenderPassManager->getRenderPass(),
                                               *_vkSwapChainManager->getExtent2D(),
                                               vertexBindingDescArray,
                                               vertexAttributeArray);

    VkFixedArray<VkCommandBuffer> cmdBuffers = _vkCommandManager->createCommandBuffer();
    _vkRenderSyncManager->create();

    auto* windowFlags = _windowManagerApi->getWindowFlags();
    const auto& frameBuffers = _vkFrameBuffersManager->getFrameBuffers();
    VkExtent2D* extent = _vkSwapChainManager->getExtent2D();
    auto& imageAvailableSemaphores = _vkRenderSyncManager->getImageAvailableSemaphores();
    auto& renderFinishedSemaphores = _vkRenderSyncManager->getRenderFinishedSemaphores();
    auto& fences = _vkRenderSyncManager->getInFlightFences();
    uint32_t ImagesCount = _vkSwapChainManager->getSwapChainImages().size();
    uint32_t currentFrame = 0;

    auto vertexBuffer = _vkVertexBufferManager->getVertexBuffer("mainTriangle");
    auto& vertexAllocationInfo = _vkDeviceAllocator->getAllocation(vertexBuffer.allocationId);

    VkClearValue clearColor = {{{0.05f, 0.05f, 0.05f, 1.0f}}};  // Dark blue background

    _windowManagerApi->process([&]() {
        _vkRenderSyncManager->waitForFences(currentFrame);
        const uint32_t imageIndex = _vkSwapChainManager->acquireNextImage(imageAvailableSemaphores[currentFrame], windowFlags);

        if (imageIndex >= ImagesCount)
        {
            INK_WARN << "Swap chain needs recreation!";
            windowFlags->resized = false;
            handleWindowChanges();
            return;
        }

        _vkRenderSyncManager->resetFences(currentFrame);
        _vkCommandManager->resetCommandPool();

        // Begin recording the command buffer.
        aura3d::VkCommandManager::beginCommandBuffer(cmdBuffers[currentFrame]);

        // Begin render pass with clear values
        _vkRenderPassManager->beginRenderPass(cmdBuffers[currentFrame],
                                              frameBuffers[imageIndex],  // Use imageIndex, not currentFrame!
                                              *extent,
                                              &clearColor);

        // Record drawing commands
        _vkGraphicsPipelineManager->cmdBindPipeline(cmdBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS);

        // Bind vertex buffer
        // auto vertexBuffer = _vkVertexBufferManager->getVertexBuffer("mainTriangle");
        vkCmdBindVertexBuffers(cmdBuffers[currentFrame], 0, 1, &vertexBuffer.buffer, &vertexAllocationInfo.offset);

        // Bind the UBO descriptor set (set 0)
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmdBuffers[currentFrame],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            0,  // First set index to bind
            1,  // Number of sets to bind
            &descriptorSets[imageIndex],
            0,
            nullptr
        );

        // Bind the texture descriptor set (set 1)
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmdBuffers[currentFrame],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            1,  // First set index to bind
            1,  // Number of sets to bind
            &textureDescriptorSets[imageIndex],
            0,
            nullptr
        );

        // Draw the triangle
        _vkGraphicsPipelineManager->cmdDraw(cmdBuffers[currentFrame], *extent, static_cast<uint32_t>(vertices.size()));

        // End render pass
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

void VkRunner::cleanup() {
    // Wait for device to be idle before cleanup
    if (_vkDeviceManager && _vkDeviceManager->getDevice()) {
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    }

    // 1. First explicitly destroy all buffers and resources
    if (_vkVertexBufferManager) {
        _vkVertexBufferManager->cleanup();  // Explicitly destroy vertex buffers
    }
    if (_vkUniformBufferManager) {
        _vkUniformBufferManager->cleanup(); // Explicitly destroy uniform buffers
    }
    if (_vkTextureManager) {
        _vkTextureManager->cleanup();       // Explicitly destroy textures
    }

    // 2. Reset higher-level managers
    _vkDescriptorManager.reset();
    _vkFrameBuffersManager.reset();
    _vkGraphicsPipelineManager.reset();
    _vkRenderPassManager.reset();
    _vkImageViewsManager.reset();
    _vkSwapChainManager.reset();
    _vkCommandManager.reset();
    _vkRenderSyncManager.reset();

    // 3. Reset allocators BEFORE device manager
    // deallocate memory before destroying the device
    _vkDeviceAllocator.reset();  // MUST be before device manager

    // 4. Now it's safe to reset device
    _vkDeviceManager.reset();

    // 5. Reset surface and instance after device
    _vkSurfaceManager.reset();
    _vkInstance.reset();

    // 6. Destroying allocator after other managers that use it are destroyed
    _vkHostAllocator.reset();

    // 7. Finally reset window system
    _windowManagerApi.reset();
}

} // namespace aura3d
