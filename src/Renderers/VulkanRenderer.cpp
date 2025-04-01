#include "Renderers/VulkanRenderer.h"
#include <ink/ink.hpp>

namespace aura3d {

VulkanRenderer::VulkanRenderer(
    const WindowDetails& windowDetails,
    const VkInstanceData& vkInstanceData,
    const VkDeviceData& vkDeviceData,
    const ImageViewData& vkImageViewData
    ) :
    Renderer(windowDetails),
    _vkInstanceData(vkInstanceData),
    _vkDeviceData(vkDeviceData),
    _vkImageViewData(vkImageViewData)
{
    // Constructor only stores parameters - initialization happens in initialize()
}

VulkanRenderer::~VulkanRenderer()
{
    // Wait for the device to finish operations before destroying resources
    if (_vkDeviceManager && _vkDeviceManager->getDevice()) {
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    }
    cleanup();
}

void VulkanRenderer::initialize()
{
    if (_isInitialized) {
        return;
    }

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

    // Create host allocator
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

// Create window manager
#ifdef SDL_WINDOW_MANAGER
    _windowManagerApi = std::make_unique<aura3d::SDLAuraWindowManager>(_windowDetails);
#else
    _windowManagerApi = std::make_unique<aura3d::GlfwAuraWindowManager>(_windowDetails);
#endif

    // Add window-specific Vulkan extensions
    const std::vector<const char*> windowApiExts = _windowManagerApi->getVulkanExtensions();
    for (const char* ext : windowApiExts) {
        _vkInstanceData.vkInstanceExtensions.push_back(ext);
    }

    // Create Vulkan instance
    _vkInstance = std::make_unique<aura3d::VkInstanceManager>(_vkHostAllocator.get(), _vkInstanceData, enableValidationLayers);

    // Create device manager
    _vkDeviceManager = std::make_unique<aura3d::VkDeviceManager>(_vkHostAllocator.get(), _vkInstance->getVkInstance(), _vkDeviceData);

    // Create device allocator
    aura3d::VkDeviceAllocatorCreateInfo vkDeviceAllocatorCreateInfo = {
        .physicalDevice = *_vkDeviceManager->getPhysicalDevice(),
        .device = *_vkDeviceManager->getDevice(),
        .blockSize = 128 * 1024 * 1024,  // 128MB for fewer reallocations
        .smallBlockSize = 8 * 1024 * 1024, // 8MB for small allocations
        .enableDefragmentation = false, // Disable unless needed
#ifdef NDEBUG
        .trackLeaks = false,  // Disable in release builds
#else
        .trackLeaks = true,
#endif
        .threadSafetyMode = ThreadSafetyMode::NONE,
        .strategy = AllocationStrategy::FIRST_FIT, // Fastest allocation strategy
        .dedicatedAllocationThreshold = 64 * 1024 * 1024, // 64MB threshold
        .useBuddyAllocatorForBuffers = true,
        .deferFrees = true,
        .deferredFreeLimit = 256, // Larger batch for complex scenes
    };
    _vkDeviceAllocator = std::make_unique<aura3d::VkDeviceAllocator>(_vkHostAllocator.get(), vkDeviceAllocatorCreateInfo);

    // Create the window
    createWindow("Aura3D Engine");

    // Create surface
    _vkSurfaceManager = std::make_unique<aura3d::VkSurfaceManager>(
        _vkHostAllocator.get(),
        _vkInstance->getVkInstance(),
        _windowManagerApi->getWindowInstance()
        );

    // Set up queue management
    _queueDataFromExclusiveFlags = _vkDeviceManager->getQueueManager()->getQueues(_vkDeviceData.exclusiveQueueFlags);
    _graphicsIndexFamily = aura3d::VkQueueManager::findQueueFamilyIndex(
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceData.exclusiveQueueFlags,
        *_vkSurfaceManager->getSurface()
        );

    // Create swap chain manager
    _vkSwapChainManager = std::make_unique<aura3d::VkSwapChainManager>(
        _vkHostAllocator.get(),
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceManager->getDevice(),
        *_vkSurfaceManager->getSurface()
        );

    // Create other managers
    _vkImageViewsManager = std::make_unique<aura3d::VkImageViewsManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
    _vkRenderPassManager = std::make_unique<aura3d::VkRenderPassManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
    _vkFrameBuffersManager = std::make_unique<aura3d::VkFrameBuffersManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
    _vkGraphicsPipelineManager = std::make_unique<aura3d::VkGraphicsPipelineManager>(
        _vkHostAllocator.get(),
        "./shaders/vert/test_shader2d_vert.spv",
        "./shaders/frag/test_shader2d_frag.spv",
        _vkDeviceManager->getDevice()
    );
    _vkDescriptorManager = std::make_unique<aura3d::VkDescriptorManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());
    _vkVertexBufferManager = std::make_unique<aura3d::VkVertexBufferManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice()
    );
    _vkIndexBufferManager = std::make_unique<aura3d::VkIndexBufferManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice()
    );
    _vkUniformBufferManager = std::make_unique<aura3d::VkUniformBufferManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice()
    );
    _vkCommandManager = std::make_unique<aura3d::VkCommandManager>(
        _vkHostAllocator.get(),
        _vkDeviceManager->getDevice(),
        _graphicsIndexFamily
    );
    _vkTextureManager = std::make_unique<VkTextureManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice(),
        _vkDeviceManager->getPhysicalDevice(),
        _vkCommandManager->getThreadCommandPool(),
        _queueDataFromExclusiveFlags.front()->queues.front()
    );
    _vkRenderSyncManager = std::make_unique<aura3d::VkRenderSyncManager>(_vkHostAllocator.get(), _vkDeviceManager->getDevice());

    _isInitialized = true;
}

void VulkanRenderer::createWindow(const char* title)
{
    _windowManagerApi->createWindow(title);
}

void VulkanRenderer::run()
{
    if (!_isInitialized) {
        initialize();
    }

    // Set up the rendering pipeline and resources
    setupGraphicsPipeline();
    createVertexBuffers();
    createUniformBuffers();
    createDescriptorSets();
    setupCommandBuffers();

    auto* windowFlags = _windowManagerApi->getWindowFlags();
    const auto& frameBuffers = _vkFrameBuffersManager->getFrameBuffers();
    VkExtent2D* extent = _vkSwapChainManager->getExtent2D();
    auto& imageAvailableSemaphores = _vkRenderSyncManager->getImageAvailableSemaphores();
    auto& renderFinishedSemaphores = _vkRenderSyncManager->getRenderFinishedSemaphores();
    auto& fences = _vkRenderSyncManager->getInFlightFences();
    u32 imagesCount = _vkSwapChainManager->getSwapChainImages().size();

    auto vertexBuffer = _vkVertexBufferManager->getVertexBuffer("mainRect");
    auto& vertexAllocationInfo = _vkDeviceAllocator->getAllocation(vertexBuffer.allocationId);
    auto indexBuffer = _vkIndexBufferManager->getIndexBuffer("rectIndices");
    auto& indexAllocationInfo = _vkDeviceAllocator->getAllocation(indexBuffer.allocationId);

    // Main render loop
    _windowManagerApi->process([&]() {
        // Wait for the previous frame to finish
        _vkRenderSyncManager->waitForFences(_currentFrame);

        // Acquire the next image from the swap chain
        const u32 imageIndex = _vkSwapChainManager->acquireNextImage(
            imageAvailableSemaphores[_currentFrame],
            windowFlags
            );

        // Check if swap chain needs recreation
        if (imageIndex >= imagesCount) {
            INK_WARN << "Swap chain needs recreation!";
            windowFlags->resized = false;
            handleWindowChanges();
            return;
        }

        // Reset the fence for the current frame
        _vkRenderSyncManager->resetFences(_currentFrame);

        // Reset command pool
        _vkCommandManager->resetCommandPool();

        // Begin recording the command buffer
        VkCommandBuffer cmdBuffer = _cmdBuffers[_currentFrame];
        aura3d::VkCommandManager::beginCommandBuffer(cmdBuffer);

        // Begin render pass with clear values
        VkClearValue clearColor = {{{0.05f, 0.05f, 0.05f, 1.0f}}};  // Dark background
        _vkRenderPassManager->beginRenderPass(
            cmdBuffer,
            frameBuffers[imageIndex],  // Use imageIndex, not currentFrame!
            *extent,
            &clearColor
        );

        // Record drawing commands
        _vkGraphicsPipelineManager->cmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS);

        // Bind vertex buffer
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, &vertexAllocationInfo.offset);
        vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

        // Bind the UBO descriptor set (set 0)
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            0,  // First set index to bind
            1,  // Number of sets to bind
            &_descriptorSets[imageIndex],
            0,
            nullptr
        );

        // Bind the texture descriptor set (set 1)
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmdBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            1,  // First set index to bind
            1,  // Number of sets to bind
            &_textureDescriptorSets[imageIndex],
            0,
            nullptr
        );

        // Draw the triangle
        _vkGraphicsPipelineManager->cmdIndexedDraw(cmdBuffer, *extent, indexBuffer.indexCount, 1, 0, vertexAllocationInfo.offset, 0);

        // End render pass
        aura3d::VkRenderPassManager::endRenderPass(cmdBuffer);

        // End command buffer recording
        aura3d::VkCommandManager::endCommandBuffer(cmdBuffer);

        // Submit command buffer to the queue
        VkQueue queueToDraw = _queueDataFromExclusiveFlags.front()->queues.front();
        aura3d::VkQueueManager::submitCmdIntoQueue(
            queueToDraw,
            &cmdBuffer,
            &imageAvailableSemaphores[_currentFrame],
            &renderFinishedSemaphores[_currentFrame],
            fences[_currentFrame]
            );

        // Present the image to the swap chain
        _vkSwapChainManager->presentBackToSwapChain(
            queueToDraw,
            &renderFinishedSemaphores[_currentFrame],
            imageIndex
            );

        // Move to the next frame
        _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    });

    // Wait for device to finish before exiting
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
}

void VulkanRenderer::setupGraphicsPipeline()
{
    // Create swap chain
    _vkSwapChainManager->createSwapChain(
        _windowManagerApi->getWindowInstance(),
        *_vkSurfaceManager->getSurface(),
        _vkDeviceManager.get()
    );

    // Create image views
    _vkImageViewsManager->createImageViews(
        _vkSwapChainManager->getSwapChainImages(),
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        _vkImageViewData
    );

    // Create render pass
    _vkRenderPassManager->createRenderPass(_vkSwapChainManager->getChoosedSurfaceFormat()->format);

    // Create framebuffers
    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(),
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D()
    );
}

void VulkanRenderer::createVertexBuffers()
{
    // Define a rect with non-overlapping vertices
    const std::vector<Vertex2d> vertices = {
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}
    };

    // // Define a triangle with non-overlapping vertices
    // std::vector<Vertex2d> vertices = {
    //     {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},  // Bottom-left, red
    //     {{ 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  // Bottom-right, green
    //     {{ 0.0f,  0.5f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}   // Top-center, blue
    // };

    VkQueue queueToDraw = _queueDataFromExclusiveFlags.front()->queues.front();

    // Create vertex buffer with named buffer for easier management
    _vkVertexBufferManager->createVertexBuffer(
        "mainRect",
        *_vkDeviceManager->getPhysicalDevice(),
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        queueToDraw,
        vertices,
        false  // No need for persistent mapping for static geometry
    );

    // DEfine index buffer data for rendering optimizations
    const std::vector<u16> indices = {
        0, 1, 2, 2, 3, 0
    };

    _vkIndexBufferManager->createIndexBuffer(
        "rectIndices",
        *_vkDeviceManager->getPhysicalDevice(),
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        queueToDraw,
        indices,
        false
    );
}

void VulkanRenderer::createUniformBuffers()
{
    // Get swapchain image count for multiple descriptor sets
    u32 swapChainImageCount = _vkSwapChainManager->getSwapChainImages().size();

    // Set up UBO for transformation
    _vkUniformBufferManager->createUniformBuffers(
        *_vkDeviceManager->getPhysicalDevice(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        swapChainImageCount
    );

    // Update uniform buffer with identity matrix
    TransformUBO ubo{};
    ubo.transform = glm::mat4(1.0f);
    for (u32 i = 0; i < swapChainImageCount; i++) {
        _vkUniformBufferManager->updateUniformBuffer(i, ubo);
    }
}

void VulkanRenderer::createDescriptorSets()
{
    u32 swapChainImageCount = _vkSwapChainManager->getSwapChainImages().size();

    // Create descriptor set layouts in the pipeline manager
    _vkGraphicsPipelineManager->createDescriptorSetLayouts();

    // Allocate and update descriptor sets using VkDescriptorManager
    _descriptorSets.resize(swapChainImageCount);
    for (size_t i = 0; i < swapChainImageCount; i++) {
        // Allocate a descriptor set
        _descriptorSets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(0)
            );

        // Update the descriptor set with uniform buffer
        _vkDescriptorManager->updateDescriptorSet(
            _descriptorSets[i],             // The descriptor set
            0,                             // Binding point in shader
            _vkUniformBufferManager->getUniformBuffer(i),   // Uniform buffer
            _vkUniformBufferManager->getUniformBufferSize() // Size of the data
            );
    }

    // Create a white texture for our triangle
    auto whiteTexture = _vkTextureManager->createSolidColorTexture("white", 255, 255, 255);

    // Allocate and update descriptor sets for the texture (set 1)
    _textureDescriptorSets.resize(swapChainImageCount);
    for (size_t i = 0; i < swapChainImageCount; i++) {
        // Allocate descriptor set for texture
        _textureDescriptorSets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(1)
            );

        // Get the texture data
        const auto* texture = _vkTextureManager->getTexture("white");

        // Update descriptor set with texture
        _vkDescriptorManager->updateCombinedImageSamplerDescriptorSet(
            _textureDescriptorSets[i],
            0,
            texture->view,
            texture->sampler
            );
    }

    // Create the pipeline
    std::vector<VkVertexInputBindingDescription> vertexBindingDescArray = {
        VkVertexBufferManager::getBindingDescription(true),
    };
    auto vertexAttributeArray = VkVertexBufferManager::getAttributeDescriptions(true);

    _vkGraphicsPipelineManager->createPipeline(
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(),
        vertexBindingDescArray,
        vertexAttributeArray
    );
}

void VulkanRenderer::setupCommandBuffers()
{
    // Create command buffers
    _cmdBuffers = _vkCommandManager->createCommandBuffer();

    // Create synchronization objects
    _vkRenderSyncManager->create();
}

void VulkanRenderer::handleWindowChanges()
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

    // Wait for device to be idle
    vkDeviceWaitIdle(device);

    // Clean up resources that depend on the window size
    _vkFrameBuffersManager->cleanup();
    _vkImageViewsManager->cleanup();
    _vkSwapChainManager->cleanup();
    _vkRenderPassManager->cleanup();
    _vkRenderSyncManager->cleanup();

    // Recreate swap chain and dependent resources
    _vkSwapChainManager->initSwapChainSupportDetails(
        *_vkDeviceManager->getPhysicalDevice(),
        *_vkSurfaceManager->getSurface()
        );

    _vkSwapChainManager->createSwapChain(
        window,
        *_vkSurfaceManager->getSurface(),
        _vkDeviceManager.get()
        );

    _vkImageViewsManager->createImageViews(
        _vkSwapChainManager->getSwapChainImages(),
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        _vkImageViewData
        );

    _vkRenderPassManager->createRenderPass(
        _vkSwapChainManager->getChoosedSurfaceFormat()->format
        );

    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(),
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D()
        );

    // Recreate synchronization objects
    _vkRenderSyncManager->create();
}

void VulkanRenderer::cleanup()
{
    // Wait for device to be idle before cleanup
    if (_vkDeviceManager && _vkDeviceManager->getDevice()) {
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    }

    // 1. First explicitly destroy all buffers and resources
    if (_vkVertexBufferManager) {
        _vkVertexBufferManager->cleanup();  // Explicitly destroy vertex buffers
    }

    if (_vkIndexBufferManager) {
        _vkIndexBufferManager->cleanup();  // Explicitly destroy index buffers
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

    _isInitialized = false;
}

} // namespace aura3d
