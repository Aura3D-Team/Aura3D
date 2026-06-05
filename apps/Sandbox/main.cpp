#include <chrono>
#include <thread>

#include "aura/Core/Engine.h"
#include "aura/Core/AuraCore.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Utils/ColorsDefinitions.h"

int main()
{
    Engine engine("settings.json");
    aura3d::IRenderer* r = engine.getRenderer();

    const bool is2d = r->is2D();

    std::vector<u16> indices = { 0, 1, 2, 2, 3, 0 };
    const u32 indexCount = static_cast<u32>(indices.size());
    auto ib = r->createIndexBuffer(std::move(indices));

    aura3d::VertexBufferHandle vb;
    if (is2d) {
        vb = r->createVertexBuffer(std::vector<aura3d::gfx::Vertex2D>{
            {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {{ 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{ 0.5f,  0.5f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
            {{-0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        });
    } else {
        vb = r->createVertexBuffer(std::vector<aura3d::gfx::Vertex3D>{
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{ 0.5f,  0.5f, 0.0f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        });
    }

    auto tex = r->createSolidColorTexture(255, 255, 255, 255);

    r->setClearColor(0.2f, 0.3f, 0.3f, 1.0f);

    aura3d::gfx::TransformUBO ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view  = glm::mat4(1.0f);

    if (is2d) {
        ubo.proj = glm::mat4(1.0f);
    } else {
        auto* wd = r->getWindowManager()->getWindowDetails();
        float aspect = static_cast<float>(wd->width) / static_cast<float>(wd->height);
        ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        ubo.view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 2.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
    }

    r->setTransform(ubo);

    r->run([&]() {
        r->beginRenderPass();
        r->bindVertexBuffer(vb);
        r->bindIndexBuffer(ib);
        r->bindTexture(tex);
        r->drawIndexed(indexCount);
        r->endRenderPass();
    });

    return 0;
}
