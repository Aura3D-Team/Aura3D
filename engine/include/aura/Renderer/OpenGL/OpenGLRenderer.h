#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#pragma once

#include "aura/Renderer/IRenderer.h"

namespace aura3d {
namespace gl {

class OpenGLRenderer : public IRenderer {
public:
    OpenGLRenderer(const wma::WindowDetails& windowDetails, RendererMode mode);
    virtual ~OpenGLRenderer();

    void initialize() override;
    void handleWindowChanges() override;
    void cleanup() override;

    wma::IWindowManager* getWindowManager() override { return _windowManagerApi.get(); }
    RendererChoice       getBackendType() const override { return RendererChoice::OPENGL; }

protected:
    void createWindow(const char* title) override;

private:
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
    bool _isInitialized = false;
};

}
} // namespace aura3d

#endif // OPENGL_RENDERER_H
