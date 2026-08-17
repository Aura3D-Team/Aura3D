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
    OpenGLRenderer(const wma::WindowDetails& windowDetails);
    virtual ~OpenGLRenderer();

    void initialize(AuraSettings* settings, const JobSystem* jobs) override;
    void handleWindowChanges() override;
    void cleanup() override;

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u16>&& indices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u32>&& indices) override;
    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) override;
    TextureHandle createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height) override;
    TextureHandle createDynamicTexture(u32 width, u32 height) override;
    void updateTextureRegion(TextureHandle handle, u32 x, u32 y,
                             u32 width, u32 height, const u8* rgbaPixels) override;

    void beginFrame() override;
    void beginRenderPass() override;
    void endRenderPass() override;
    void endFrame() override;

    void setTransform(const gfx::TransformUBO& ubo) override;
    void setLight(const gfx::LightUBO& light) override;
    void bindVertexBuffer(VertexBufferHandle handle) override;
    void bindIndexBuffer(IndexBufferHandle handle) override;
    void bindTexture(TextureHandle handle) override;
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    void drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                     std::span<const u32> indices,
                     TextureHandle texture) override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;

    wma::IWindowManager* getWindowManager() override { return _windowManagerApi.get(); }
    RendererChoice       getBackendType() const override { return RendererChoice::OPENGL; }

protected:
    void createWindow(const char* title, const wma::WindowBackend& wBackend) override;

private:
    void loadOpenGLEntryPoints();
    void compileBuiltInShaders();

    /**
     * @brief Creates the overlay pipeline's persistent VAO and dynamic buffers.
     *
     * One VAO/VBO/EBO triple serves every drawBatch2D() call for the renderer's
     * whole life. The buffers only ever grow, so a steady-state overlay stops
     * reallocating after the first few frames.
     */
    void createOverlay2DBuffers();

    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
    std::unique_ptr<GlVertexBufferManager> _vertexMgr;
    std::unique_ptr<GlIndexBufferManager> _indexMgr;
    std::unique_ptr<GlUniformBufferManager> _uniformMgr;
    std::unique_ptr<GlTextureManager> _textureMgr;

    GLuint _shaderProgram = 0;
    bool _isInitialized = false;

    //! Dedicated unlit 2D overlay pipeline, independent of the 3D program above.
    GLuint _overlay2DProgram = 0;
    GLint _overlay2DProjLoc = -1;    //! Cached uniform location of uProj.
    GLint _overlay2DSamplerLoc = -1; //! Cached uniform location of textureSampler.

    /*
     * Location of the 3D program's textureSampler, resolved once at link time.
     * Queried per bindTexture() before -- glGetUniformLocation hashes the name
     * string inside the driver on every call, which is a real cost when it
     * happens once per textured object per frame. The value it sets (unit 0)
     * is program state and never changes, so nothing re-writes it per draw.
     */
    GLint _sampler3DLoc = -1;
    GLuint _overlay2DVao = 0;
    GLuint _overlay2DVbo = 0;
    GLuint _overlay2DEbo = 0;
    size_t _overlay2DVboBytes = 0; //! Current VBO allocation, in bytes.
    size_t _overlay2DEboBytes = 0; //! Current EBO allocation, in bytes.
    //! 1x1 opaque white, substituted when a batch asks for no texture.
    TextureHandle _white2DTexture;

    VertexBufferHandle _currentVertexBuffer;
    IndexBufferHandle _currentIndexBuffer;
    TextureHandle _currentTexture;
    f32 _clearR = 0.05f, _clearG = 0.05f, _clearB = 0.05f, _clearA = 1.0f;
};

} // namespace gl
} // namespace aura3d

#endif // OPENGL_RENDERER_H
