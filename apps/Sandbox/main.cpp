/**
 * Sandbox – Aura3D test application.
 *
 * This file contains all application-specific rendering code: geometry data,
 * textures, descriptor sets, shader paths and the per-frame draw loop.
 * The engine library is intentionally kept clean of these concerns.
 */

#include <chrono>
#include <thread>

#include <glad/glad.h>
#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"

#include "aura/Core/Engine.h"
#include "aura/Core/AuraCore.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/Vulkan/VulkanRenderer.h"
#include "aura/Renderer/OpenGL/OpenGLRenderer.h"
#include "aura/Renderer/Software/CPURenderer.h"
#include "aura/Utils/ColorsDefinitions.h"

// ============================================================
// Forward declarations for per-backend entry points
// ============================================================

static void runVulkan (aura3d::vk::VulkanRenderer*  renderer);
static void runOpenGL (aura3d::gl::OpenGLRenderer*  renderer);
static void runCPU    (aura3d::cpu::CPURenderer*    renderer);

// ============================================================
// main
// ============================================================

int main()
{
    Engine engine("settings.json");

    aura3d::IRenderer* renderer = engine.getRenderer();

    if (auto* vk = dynamic_cast<aura3d::vk::VulkanRenderer*>(renderer)) {
        runVulkan(vk);
    } else if (auto* gl = dynamic_cast<aura3d::gl::OpenGLRenderer*>(renderer)) {
        runOpenGL(gl);
    } else if (auto* cpu = dynamic_cast<aura3d::cpu::CPURenderer*>(renderer)) {
        runCPU(cpu);
    }

    return 0;
}

// ============================================================
// Vulkan sandbox
// ============================================================

static void runVulkan(aura3d::vk::VulkanRenderer* renderer)
{
    // ------------------------------------------------------------------
    // 1. Build the graphics pipeline (shader paths are app-specific)
    // ------------------------------------------------------------------
    renderer->setupPipeline(
        "./resources/shaders/vk/vk_shader2d_vert.spv",
        "./resources/shaders/vk/vk_shader2d_frag.spv");

    auto* pipeline        = renderer->getGraphicsPipelineManager();
    auto* swapChain       = renderer->getSwapChainManager();
    auto* vertexMgr       = renderer->getVertexBufferManager();
    auto* indexMgr        = renderer->getIndexBufferManager();
    auto* uniformMgr      = renderer->getUniformBufferManager();
    auto* descriptorMgr   = renderer->getDescriptorManager();
    auto* textureMgr      = renderer->getTextureManager();
    auto* commandMgr      = renderer->getCommandManager();
    auto* renderPassMgr   = renderer->getRenderPassManager();
    auto* frameBufMgr     = renderer->getFrameBuffersManager();
    auto* syncMgr         = renderer->getRenderSyncManager();
    auto* deviceAllocator = renderer->getDeviceAllocator();

    const auto& queues       = renderer->getQueues();
    VkQueue     graphicsQueue = queues.front()->queues.front();

    u32 swapChainImageCount = swapChain->getSwapChainImages().size();

    // ------------------------------------------------------------------
    // 2. Create descriptor-set layouts + pipeline
    // ------------------------------------------------------------------
    pipeline->createDescriptorSetLayouts();

    // ------------------------------------------------------------------
    // 3. Geometry – coloured rectangle
    // ------------------------------------------------------------------
    const std::vector<aura3d::Vertex2d> vertices = {
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}
    };
    const std::vector<u16> indices = { 0, 1, 2, 2, 3, 0 };

    vertexMgr->createVertexBuffer(
        "mainRect",
        *renderer->getDeviceManager()->getPhysicalDevice(),
        commandMgr->getThreadCommandPool(),
        swapChain->getSwapchainCreateInfoKHR()->imageSharingMode,
        graphicsQueue,
        vertices,
        false);

    indexMgr->createIndexBuffer(
        "rectIndices",
        *renderer->getDeviceManager()->getPhysicalDevice(),
        commandMgr->getThreadCommandPool(),
        swapChain->getSwapchainCreateInfoKHR()->imageSharingMode,
        graphicsQueue,
        indices,
        false);

    // ------------------------------------------------------------------
    // 4. Uniform buffers (one per swapchain image)
    // ------------------------------------------------------------------
    uniformMgr->createUniformBuffers(
        *renderer->getDeviceManager()->getPhysicalDevice(),
        swapChain->getSwapchainCreateInfoKHR()->imageSharingMode,
        swapChainImageCount);

    aura3d::TransformUBO ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view  = glm::mat4(1.0f);
    ubo.proj  = glm::mat4(1.0f);
    for (u32 i = 0; i < swapChainImageCount; ++i) {
        uniformMgr->updateUniformBuffer(i, ubo);
    }

    // ------------------------------------------------------------------
    // 5. Texture + descriptor sets
    // ------------------------------------------------------------------
    textureMgr->createSolidColorTexture("white", 255, 255, 255);
    const auto* whiteTexture = textureMgr->getTexture("white");

    std::vector<VkDescriptorSet> descriptorSets(swapChainImageCount);
    std::vector<VkDescriptorSet> textureDescriptorSets(swapChainImageCount);

    for (size_t i = 0; i < swapChainImageCount; ++i) {
        descriptorSets[i] = descriptorMgr->allocateDescriptorSet(
            pipeline->getDescriptorSetLayout(0));
        descriptorMgr->updateDescriptorSet(
            descriptorSets[i], 0,
            uniformMgr->getUniformBuffer(i),
            uniformMgr->getUniformBufferSize());

        textureDescriptorSets[i] = descriptorMgr->allocateDescriptorSet(
            pipeline->getDescriptorSetLayout(1));
        descriptorMgr->updateCombinedImageSamplerDescriptorSet(
            textureDescriptorSets[i], 0,
            whiteTexture->view, whiteTexture->sampler);
    }

    // ------------------------------------------------------------------
    // 6. Create the pipeline (needs render pass, extent, vertex layout)
    // ------------------------------------------------------------------
    std::vector<VkVertexInputBindingDescription> bindingDescs = {
        aura3d::vk::VkVertexBufferManager::getBindingDescription(true)
    };
    auto attrDescs = aura3d::vk::VkVertexBufferManager::getAttributeDescriptions(true);

    pipeline->createPipeline(
        *renderPassMgr->getRenderPass(),
        *swapChain->getExtent2D(),
        bindingDescs,
        attrDescs);

    // ------------------------------------------------------------------
    // 7. Cache frequently accessed objects before the loop
    // ------------------------------------------------------------------
    auto* windowMgr  = renderer->getWindowManager();
    auto* windowFlags = windowMgr->getWindowFlags();

    auto& cmdBuffers              = renderer->getCommandBuffers();
    const auto& frameBuffers      = frameBufMgr->getFrameBuffers();
    auto& imageAvailableSemaphores= syncMgr->getImageAvailableSemaphores();
    auto& renderFinishedSemaphores= syncMgr->getRenderFinishedSemaphores();
    auto& fences                  = syncMgr->getInFlightFences();

    auto  vertexBuffer      = vertexMgr->getVertexBuffer("mainRect");
    auto& vertexAllocInfo   = deviceAllocator->getAllocation(vertexBuffer.allocationId);
    auto  indexBuffer       = indexMgr->getIndexBuffer("rectIndices");

    u32 imagesCount  = swapChainImageCount;
    unsigned long long frameCount = 0;

    // ------------------------------------------------------------------
    // 8. Main render loop
    // ------------------------------------------------------------------
    windowMgr->process([&]()
    {
        syncMgr->waitForFences(renderer->getCurrentFrame());

        const u32 imageIndex = swapChain->acquireNextImage(
            imageAvailableSemaphores[renderer->getCurrentFrame()], windowFlags);

        if (imageIndex >= imagesCount) {
            while (windowFlags->resized) {
                windowFlags->resized = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            renderer->handleWindowChanges();
            return;
        }

        if (frameCount % 1000 == 0) {
            INK_LOG << " | FPS: "     << windowFlags->fps
                    << " | Frame: "   << frameCount
                    << " | Delta: "   << windowFlags->deltaTime << "ms";
        }

        syncMgr->resetFences(renderer->getCurrentFrame());
        commandMgr->resetCommandPool();

        VkCommandBuffer cmd = cmdBuffers[renderer->getCurrentFrame()];
        aura3d::vk::VkCommandManager::beginCommandBuffer(cmd);

        VkClearValue clearColor = {{{0.05f, 0.05f, 0.05f, 1.0f}}};
        renderPassMgr->beginRenderPass(cmd, frameBuffers[imageIndex],
                                       *swapChain->getExtent2D(), &clearColor);

        pipeline->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &vertexAllocInfo.offset);
        vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

        pipeline->cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            0, 1, &descriptorSets[imageIndex], 0, nullptr);
        pipeline->cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            1, 1, &textureDescriptorSets[imageIndex], 0, nullptr);

        pipeline->cmdIndexedDraw(cmd, *swapChain->getExtent2D(),
            indexBuffer.indexCount, 1, 0, vertexAllocInfo.offset, 0);

        aura3d::vk::VkRenderPassManager::endRenderPass(cmd);
        aura3d::vk::VkCommandManager::endCommandBuffer(cmd);

        aura3d::vk::VkQueueManager::submitCmdIntoQueue(
            graphicsQueue, &cmd,
            &imageAvailableSemaphores[renderer->getCurrentFrame()],
            &renderFinishedSemaphores[renderer->getCurrentFrame()],
            fences[renderer->getCurrentFrame()]);

        swapChain->presentBackToSwapChain(
            graphicsQueue,
            &renderFinishedSemaphores[renderer->getCurrentFrame()],
            imageIndex);

        renderer->advanceFrame();
        ++frameCount;
    });

    vkDeviceWaitIdle(*renderer->getDeviceManager()->getDevice());
}

static void runOpenGL(aura3d::gl::OpenGLRenderer* renderer)
{
    auto* windowMgr = renderer->getWindowManager();

    // ------------------------------------------------------------------
    // Shaders
    // ------------------------------------------------------------------
    auto compileShader = [](GLenum type, const std::string& path) -> u32 {
        std::string src = aura3d::gl::GlShaderManager::readShaderSource_GL(path);
        const char* c   = src.c_str();
        u32 s = glCreateShader(type);
        glShaderSource(s, 1, &c, nullptr);
        glCompileShader(s);
        int ok; char log[512];
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(s, 512, nullptr, log); INK_THROW(std::string(log)); }
        return s;
    };

    u32 vert = compileShader(GL_VERTEX_SHADER,   "./resources/shaders/gl/gl_shader2d.vert");
    u32 frag = compileShader(GL_FRAGMENT_SHADER, "./resources/shaders/gl/gl_shader2d.frag");

    u32 shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vert);
    glAttachShader(shaderProgram, frag);
    glLinkProgram(shaderProgram);
    { int ok; char log[512];
      glGetProgramiv(shaderProgram, GL_LINK_STATUS, &ok);
      if (!ok) { glGetProgramInfoLog(shaderProgram, 512, nullptr, log); INK_THROW(std::string(log)); } }
    glDeleteShader(vert);
    glDeleteShader(frag);

    // ------------------------------------------------------------------
    // Geometry
    // ------------------------------------------------------------------
    const std::vector<aura3d::Vertex2d> vertices = {
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}
    };
    const unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    u32 VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(aura3d::Vertex2d),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(aura3d::Vertex2d),
                          (void*)offsetof(aura3d::Vertex2d, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(aura3d::Vertex2d),
                          (void*)offsetof(aura3d::Vertex2d, texCoord));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(aura3d::Vertex2d),
                          (void*)offsetof(aura3d::Vertex2d, color));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // ------------------------------------------------------------------
    // Uniform buffer (model / view / proj matrices)
    // ------------------------------------------------------------------
    u32 UBO;
    u32 uboSize = 3 * sizeof(glm::mat4);
    glGenBuffers(1, &UBO);
    glBindBuffer(GL_UNIFORM_BUFFER, UBO);
    glBufferData(GL_UNIFORM_BUFFER, uboSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    u32 blockIdx = glGetUniformBlockIndex(shaderProgram, "UniformBufferObject");
    if (blockIdx != GL_INVALID_INDEX) {
        glUniformBlockBinding(shaderProgram, blockIdx, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, UBO);
    }

    // ------------------------------------------------------------------
    // Default 1×1 white texture
    // ------------------------------------------------------------------
    u32 whiteTexture;
    glGenTextures(1, &whiteTexture);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    unsigned char white[] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    unsigned long long frameCount = 0;
    wma::WindowFlags* windowFlags = windowMgr->getWindowFlags();

    // ------------------------------------------------------------------
    // Main render loop
    // ------------------------------------------------------------------
    windowMgr->process([&]()
    {
        if (windowFlags->resized) {
            while (windowFlags->resized) {
                windowFlags->resized = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            renderer->handleWindowChanges();
        }

        if (frameCount % 1000 == 0) {
            INK_LOG << " | FPS: "   << windowFlags->fps
                    << " | Frame: " << frameCount
                    << " | Delta: " << windowFlags->deltaTime << "ms";
        }

        // Update matrices every frame
        glm::mat4 model(1.0f), view(1.0f), proj(1.0f);
        glBindBuffer(GL_UNIFORM_BUFFER, UBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0,                sizeof(glm::mat4), glm::value_ptr(model));
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4),sizeof(glm::mat4), glm::value_ptr(view));
        glBufferSubData(GL_UNIFORM_BUFFER, 2*sizeof(glm::mat4),sizeof(glm::mat4),glm::value_ptr(proj));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTexture);
        glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        ++frameCount;
    });

    // Cleanup GL resources
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteBuffers(1, &UBO);
    glDeleteTextures(1, &whiteTexture);
    glDeleteProgram(shaderProgram);
}

// ============================================================
// CPU / Software sandbox
// ============================================================

static void runCPU(aura3d::cpu::CPURenderer* renderer)
{
    auto* windowMgr      = renderer->getWindowManager();
    auto* frameBuf       = renderer->getFrameBufferManager();
    auto* windowFlags    = windowMgr->getWindowFlags();

    // ------------------------------------------------------------------
    // Main render loop
    // ------------------------------------------------------------------
    windowMgr->process([&]()
    {
        auto* wd = windowMgr->getWindowDetails();

        if (windowFlags->resized) {
            while (windowFlags->resized) {
                windowFlags->resized = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            frameBuf->resizeFramebuffer(wd->width, wd->height);
        }

        frameBuf->clear(aura3d::colors::CORNSILK_UINT32);

        int textY = 20;
        int fps   = windowFlags->fps;
        frameBuf->drawText("FPS: " + std::to_string(fps), {10, textY},
                           aura3d::colors::RED_UINT32);

        int textH = frameBuf->getTextHeight("FPS: 999");
        textY += 10 + textH;

        frameBuf->drawText("Aura3D Sandbox", {10, textY}, aura3d::colors::RED_UINT32);

        frameBuf->drawLine({250, 250}, {400, 400}, aura3d::colors::BLACK_UINT32);
        frameBuf->drawLine({400, 400}, {150, 600}, aura3d::colors::BLACK_UINT32);
        frameBuf->drawFilledPolygon({{250, 250}, {150, 600}, {600, 250}},
                                    aura3d::colors::BLUE_UINT32);

        frameBuf->renderFramebuffer();
    });
}
