#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#pragma once

#include <memory>
#include <unordered_map>

#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/EmbeddedShaders.h"
#include "aura/Renderer/OpenGL/GlAura/GlVertexBufferManager/GlVertexBufferManager.h"
#include "aura/Renderer/OpenGL/GlAura/GlIndexBufferManager/GlIndexBufferManager.h"
#include "aura/Renderer/OpenGL/GlAura/GlUniformBufferManager/GlUniformBufferManager.h"
#include "aura/Renderer/OpenGL/GlAura/GlTextureManager/GlTextureManager.h"

namespace aura3d {
namespace gl {

class OpenGLRenderer : public IRenderer {
public:
    OpenGLRenderer(const wma::WindowDetails& windowDetails, RendererMode mode);
    virtual ~OpenGLRenderer();

    void initialize(const AuraSettings* settings) override;
    void handleWindowChanges() override;
    void cleanup() override;

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex2D>&& vertices) override;
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

    wma::IWindowManager* getWindowManager() override { return _windowManagerApi.get(); }
    RendererChoice       getBackendType() const override { return RendererChoice::OPENGL; }

protected:
    void createWindow(const char* title, const wma::WindowBackend& wBackend) override;

private:
    void loadOpenGLEntryPoints();
    void compileBuiltInShaders();

    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
    std::unique_ptr<GlVertexBufferManager> _vertexMgr;
    std::unique_ptr<GlIndexBufferManager> _indexMgr;
    std::unique_ptr<GlUniformBufferManager> _uniformMgr;
    std::unique_ptr<GlTextureManager> _textureMgr;

    GLuint _shaderProgram = 0;
    bool _isInitialized = false;

    VertexBufferHandle _currentVertexBuffer = INVALID_HANDLE;
    IndexBufferHandle _currentIndexBuffer  = INVALID_HANDLE;
    TextureHandle _currentTexture = INVALID_HANDLE;
    gfx::TransformUBO _currentTransform;
    f32 _clearR = 0.05f, _clearG = 0.05f, _clearB = 0.05f, _clearA = 1.0f;
};

} // namespace gl
} // namespace aura3d

#endif // OPENGL_RENDERER_H
