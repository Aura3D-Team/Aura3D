#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H

#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Vulkan/VkAura/VkInstanceManager/VkInstanceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkDeviceManager/VkDeviceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSurfaceManager/VkSurfaceManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSwapChainManager/VkSwapChainManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkImageViewsManager/VkImageViewsManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkRenderPassManager/VkRenderPassManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkDescriptorManager/VkDescriptorManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkTextureManager/VkTextureManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkVertexBufferManager/VkVertexBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkIndexBufferManager/VkIndexBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkUniformBufferManager/VkUniformBufferManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkCommandManager/VkCommandManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkRenderSyncManager/VkRenderSyncManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VulkanMemoryManager/VulkanMemoryManager.h"

namespace aura3d {
namespace vk {

class VulkanRenderer : public IRenderer {
public:
    VulkanRenderer(const wma::WindowDetails& windowDetails);
    virtual ~VulkanRenderer();

    void initialize(const AuraSettings* settings) override;
    void handleWindowChanges() override;
    void cleanup() override;

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u16>&& indices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u32>&& indices) override;
    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) override;

    void beginFrame() override;
    void beginRenderPass() override;
    void endRenderPass() override;
    void endFrame() override;

    void setTransform(const gfx::TransformUBO& ubo) override;
    void bindVertexBuffer(VertexBufferHandle handle) override;
    void bindIndexBuffer(IndexBufferHandle handle) override;
    void bindTexture(TextureHandle handle) override;
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    wma::IWindowManager* getWindowManager() override;
    RendererChoice getBackendType() const override;

    VkVertexBufferManager* getVertexBufferManager();
    VkIndexBufferManager* getIndexBufferManager();
    VkUniformBufferManager* getUniformBufferManager();
    VkDescriptorManager* getDescriptorManager();
    VkGraphicsPipelineManager* getGraphicsPipelineManager();
    VkSwapChainManager* getSwapChainManager();
    VkRenderPassManager* getRenderPassManager();
    VkFrameBuffersManager* getFrameBuffersManager();
    VkCommandManager* getCommandManager();
    VkRenderSyncManager* getRenderSyncManager();
    VkTextureManager* getTextureManager();
    VkDeviceManager* getDeviceManager();
    VulkanMemoryManager* getMemoryManager();

    const std::vector<aura3d::vk::QueueData*>& getQueues() const;
    void setupPipeline(const std::string& vertShaderPath, const std::string& fragShaderPath);
    VkFixedArray<VkCommandBuffer>& getCommandBuffers();
    u32 getCurrentFrame() const;
    void advanceFrame();

protected:
    void createWindow(const char* title, const wma::WindowBackend& wBackend) override;

private:
    void setupInput();
    void createCoreObjects(bool enableValidation);
    void createResourceManagers();
    void buildSwapchainResources();
    void destroySwapchainResources();
    void createDepthResources();
    void destroyDepthResources();
    void setupCommandBuffers();
    void createUniformBuffers();
    void createDescriptorSets();
    void updateTextureDescriptorSets(TextureHandle textureHandle);

    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
    std::unique_ptr<VulkanMemoryManager> _memoryManager;
    std::unique_ptr<aura3d::vk::VkInstanceManager> _vkInstance;
    std::unique_ptr<aura3d::vk::VkDeviceManager> _vkDeviceManager;
    std::unique_ptr<aura3d::vk::VkSurfaceManager> _vkSurfaceManager;
    std::unique_ptr<aura3d::vk::VkSwapChainManager> _vkSwapChainManager;
    std::unique_ptr<aura3d::vk::VkImageViewsManager> _vkImageViewsManager;
    std::unique_ptr<aura3d::vk::VkRenderPassManager> _vkRenderPassManager;
    std::unique_ptr<aura3d::vk::VkFrameBuffersManager> _vkFrameBuffersManager;
    std::unique_ptr<aura3d::vk::VkGraphicsPipelineManager> _vkGraphicsPipelineManager;
    std::unique_ptr<aura3d::vk::VkTextureManager> _vkTextureManager;
    std::unique_ptr<aura3d::vk::VkDescriptorManager> _vkDescriptorManager;
    std::unique_ptr<aura3d::vk::VkVertexBufferManager> _vkVertexBufferManager;
    std::unique_ptr<aura3d::vk::VkIndexBufferManager> _vkIndexBufferManager;
    std::unique_ptr<aura3d::vk::VkUniformBufferManager> _vkUniformBufferManager;
    std::unique_ptr<aura3d::vk::VkCommandManager> _vkCommandManager;
    std::unique_ptr<aura3d::vk::VkRenderSyncManager> _vkRenderSyncManager;

    VkFixedArray<VkCommandBuffer> _cmdBuffers;
    u32 _currentFrame = 0;
    u32 _currentImageIndex = 0;
    u32 _imagesCount = 0;
    bool _isInitialized = false;
    bool _frameBegun = false;
    bool _renderPassActive = false;
    bool _pipelineReady = false;

    VulkanMemoryManager::Config _vmaConfig{};

    VkInstanceData _vkInstanceData;
    VkDeviceData _vkDeviceData;
    ImageViewData _vkImageViewData;
    std::vector<aura3d::vk::QueueData*> _queueDataFromExclusiveFlags;
    u32 _graphicsIndexFamily = 0;
    DepthResources _depth;

    VertexBufferHandle _nextVbHandle = 1;
    IndexBufferHandle _nextIbHandle = 1;
    TextureHandle _nextTexHandle = 1;

    std::unordered_map<VertexBufferHandle, std::string> _vbNames;
    std::unordered_map<IndexBufferHandle, std::string> _ibNames;
    std::unordered_map<TextureHandle, std::string> _texNames;

    std::vector<VkDescriptorSet> _descSets;
    std::vector<VkDescriptorSet> _texDescSets;

    VertexBufferHandle _currentVertexBuffer = INVALID_HANDLE;
    IndexBufferHandle _currentIndexBuffer = INVALID_HANDLE;
    TextureHandle _currentTexture = INVALID_HANDLE;
    gfx::TransformUBO _currentTransform;

    f32 _clearR = 0.05f;
    f32 _clearG = 0.05f;
    f32 _clearB = 0.05f;
    f32 _clearA = 1.0f;
};

} // namespace vk
} // namespace aura3d

#endif // VULKAN_RENDERER_H
