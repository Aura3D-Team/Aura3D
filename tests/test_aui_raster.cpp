// Rasterizes DrawListRenderer output the way the GPU does -- pixel-centre
// coverage, bilinear texture sampling, src-over blending -- so a UI frame can be
// checked for seams and lost glyph detail without a window or a driver.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "TestUtils.h"
#include "WindowTestUtils.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/UI/UI.hpp"

using namespace aura3d;
using namespace aura3d::ui;

namespace
{

class CaptureRenderer final : public IRenderer
{
  public:
    struct Batch
    {
        usize first = 0;
        usize count = 0;
        TextureHandle texture;
    };
    struct Texture
    {
        u32 width = 0;
        u32 height = 0;
        std::vector<u8> rgba;
        bool coverageOnly = false;
    };

    CaptureRenderer() : IRenderer(wma::WindowDetails{})
    {
    }

    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32>, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override
    {
        if (space != gfx::BatchSpace::Screen)
            ++sceneBatches;
        batches.push_back({quads.size(), vertices.size(), texture});
        quads.insert(quads.end(), vertices.begin(), vertices.end());
    }
    usize sceneBatches = 0;

    TextureHandle createDynamicTexture(u32 width, u32 height) override
    {
        const auto handle = ++_next;
        textures.emplace(handle, Texture{width, height, std::vector<u8>(usize(width) * height * 4)});
        return handle;
    }

    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgba) override
    {
        auto &texture = textures.at(handle);
        for (u32 row = 0; row < height; ++row)
            std::copy_n(rgba + usize(row) * width * 4, usize(width) * 4,
                        texture.rgba.data() + (usize(y + row) * texture.width + x) * 4);
    }

    TextureHandle createCoverageTexture(u32 width, u32 height) override
    {
        const auto handle = ++_next;
        textures.emplace(handle, Texture{width, height, std::vector<u8>(usize(width) * height), true});
        return handle;
    }

    void updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                     const u8 *coverage) override
    {
        auto &texture = textures.at(handle);
        for (u32 row = 0; row < height; ++row)
            std::copy_n(coverage + usize(row) * width, width, texture.rgba.data() + usize(y + row) * texture.width + x);
    }

    [[nodiscard]] glm::vec4 sample(const Texture &texture, glm::vec2 uv) const
    {
        const auto texel = [&texture](i32 x, i32 y)
        {
            x = std::clamp(x, 0, i32(texture.width) - 1);
            y = std::clamp(y, 0, i32(texture.height) - 1);
            if (texture.coverageOnly)
                return glm::vec4{1, 1, 1, static_cast<f32>(texture.rgba[usize(y) * texture.width + usize(x)]) / 255.0f};
            const u8 *p = texture.rgba.data() + (usize(y) * texture.width + usize(x)) * 4;
            return glm::vec4{p[0], p[1], p[2], p[3]} / 255.0f;
        };
        const glm::vec2 t = uv * glm::vec2{texture.width, texture.height} - 0.5f;
        const glm::vec2 base = glm::floor(t);
        const glm::vec2 f = t - base;
        const i32 x = i32(base.x);
        const i32 y = i32(base.y);
        return glm::mix(glm::mix(texel(x, y), texel(x + 1, y), f.x),
                        glm::mix(texel(x, y + 1), texel(x + 1, y + 1), f.x), f.y);
    }

    /// Linear RGB, background @p clear.
    [[nodiscard]] std::vector<glm::vec3> rasterize(u32 width, u32 height, glm::vec3 clear) const
    {
        std::vector<glm::vec3> image(usize(width) * height, clear);
        for (const Batch &batch : batches)
        {
            const Texture &texture = textures.at(batch.texture);
            for (usize base = batch.first; base + 4 <= batch.first + batch.count; base += 4)
            {
                const gfx::BatchVertex &a = quads[base];
                const gfx::BatchVertex &c = quads[base + 2];
                const i32 x0 = std::max(0, i32(std::ceil(a.pos.x - 0.5f)));
                const i32 y0 = std::max(0, i32(std::ceil(a.pos.y - 0.5f)));
                const i32 x1 = std::min(i32(width), i32(std::ceil(c.pos.x - 0.5f)));
                const i32 y1 = std::min(i32(height), i32(std::ceil(c.pos.y - 0.5f)));
                for (i32 y = y0; y < y1; ++y)
                    for (i32 x = x0; x < x1; ++x)
                    {
                        const glm::vec2 p{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};
                        const glm::vec2 uv = a.texCoord + (c.texCoord - a.texCoord) *
                                                              ((p - glm::vec2(a.pos)) / glm::vec2(c.pos - a.pos));
                        const glm::vec4 src = sample(texture, uv) * a.color;
                        glm::vec3 &dst = image[usize(y) * width + usize(x)];
                        dst = glm::vec3(src) * src.a + dst * (1.0f - src.a);
                    }
            }
        }
        return image;
    }

    std::vector<Batch> batches;
    std::vector<gfx::BatchVertex> quads;
    std::unordered_map<TextureHandle, Texture> textures;

    // Everything below is inert: the UI never calls it.
    void beginFrame() override
    {
    }
    void beginRenderPass() override
    {
    }
    void endRenderPass() override
    {
    }
    void endFrame() override
    {
    }
    void setTransform(const gfx::TransformUBO &) override
    {
    }
    void bindVertexBuffer(VertexBufferHandle) override
    {
    }
    void bindIndexBuffer(IndexBufferHandle) override
    {
    }
    void bindTexture(TextureHandle) override
    {
    }
    void drawIndexed(u32, u32) override
    {
    }
    void draw(u32, u32) override
    {
    }
    void setClearColor(f32, f32, f32, f32) override
    {
    }
    wma::IWindowManager *getWindowManager() override
    {
        return nullptr;
    }
    TextureHandle createSolidColorTexture(u8, u8, u8, u8) override
    {
        return {};
    }
    TextureHandle createTextureFromPixels(const u8 *, u32, u32) override
    {
        return {};
    }
    void initialize(AuraSettings *, const JobSystem *) override
    {
    }
    void handleWindowChanges() override
    {
    }
    void cleanup() override
    {
    }
    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D> &&) override
    {
        return {};
    }
    IndexBufferHandle createIndexBuffer(std::vector<u16> &&) override
    {
        return {};
    }
    IndexBufferHandle createIndexBuffer(std::vector<u32> &&) override
    {
        return {};
    }
    RendererChoice getBackendType() const override
    {
        return RendererChoice::SOFTWARE;
    }
    void createWindow(const char *, const wma::WindowBackend &) override
    {
    }

  private:
    TextureHandle _next{0};
};

/// sRGB-encodes like the swapchain does, so the file shows what the user sees.
void writePpm(const std::string &path, const std::vector<glm::vec3> &image, u32 width, u32 height)
{
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
        return;
    std::fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (const glm::vec3 &linear : image)
        for (int i = 0; i < 3; ++i)
        {
            const float c = std::clamp(linear[i], 0.0f, 1.0f);
            const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            std::fputc(int(std::lround(s * 255.0f)), file);
        }
    std::fclose(file);
}

/// Signed distance from @p p to a rounded rectangle, negative inside.
[[nodiscard]] f32 roundedDistance(glm::vec2 p, const Rect &box, f32 radius)
{
    const glm::vec2 q = glm::abs(p - box.center()) - (box.size() * 0.5f - radius);
    return glm::length(glm::max(q, 0.0f)) + std::min(std::max(q.x, q.y), 0.0f) - radius;
}

void roundedFillsHaveNoSeams()
{
    bool solid = true;
    bool outlined = true;
    for (const f32 scale : {1.0f, 1.25f, 1.5f, 2.0f})
        for (const f32 offset : {0.0f, 0.35f, 0.5f, 0.7f})
        {
            AtlasTextShaper shaper{TextShaperDesc{}};
            shaper.setScale(scale);
            const Rect box = Rect::fromSize({20.0f + offset, 10.0f + offset}, {120.3f, 40.6f});
            DrawList list;
            list.begin(Rect::fromSize({}, {200.0f, 100.0f}));
            list.fillRect(box, {1, 1, 1, 1}, Corners::all(6.0f));
            list.drawRect(Rect::fromSize({20.3f, 60.4f}, {80.0f, 30.0f}), {1, 1, 1, 1}, {0.5f, 0.5f, 0.5f, 1}, 1.0f,
                          Corners::all(5.0f));
            CaptureRenderer renderer;
            DrawListRenderer backend(renderer, shaper);
            backend.build(list, scale);
            backend.submit();

            const auto width = u32(std::lround(200.0f * scale));
            const auto height = u32(std::lround(100.0f * scale));
            const auto image = renderer.rasterize(width, height, glm::vec3{0.0f});
            for (u32 y = 0; y < height; ++y)
                for (u32 x = 0; x < width; ++x)
                {
                    const glm::vec2 p = (glm::vec2{x, y} + 0.5f) / scale;
                    const f32 value = image[usize(y) * width + x].r;
                    //! Two device pixels in: past the arc's antialiasing, and past
                    //! the half-pixel that snapping a radius or an edge may move it.
                    if (roundedDistance(p, box, 6.0f) < -2.0f / scale)
                        solid &= value > 0.999f;
                    const Rect inner = Rect::fromSize({21.3f, 61.4f}, {78.0f, 28.0f});
                    if (roundedDistance(p, inner, 4.0f) < -2.0f / scale)
                        outlined &= value > 0.999f;
                }
        }
    AURA_CHECK(solid, "a rounded fill covers its inside completely: no seam where a corner meets the body");
    AURA_CHECK(outlined, "an outlined rounded fill covers its inside completely");
}

void textStaysCrisp()
{
    bool crisp = true;
    bool inked = true;
    for (const f32 scale : {1.0f, 1.25f, 1.5f, 2.0f})
    {
        AtlasTextShaper shaper{TextShaperDesc{}};
        shaper.setScale(scale);
        ShapedText text;
        shaper.shape("Aura3D quick 0123456789", TextStyle{.pixelSize = 12.6f}, kUnbounded, text);
        DrawList list;
        list.begin(Rect::fromSize({}, {300.0f, 40.0f}));
        list.drawText(text, {10.35f, 6.7f}, {1, 1, 1, 1});
        CaptureRenderer renderer;
        DrawListRenderer backend(renderer, shaper);
        backend.build(list, scale);
        backend.submit();

        const auto width = u32(std::lround(300.0f * scale));
        const auto height = u32(std::lround(40.0f * scale));
        f32 total = 0.0f;
        for (const glm::vec3 &pixel : renderer.rasterize(width, height, glm::vec3{0.0f}))
        {
            crisp &= pixel.r < 0.001f || pixel.r > 0.999f;
            total += pixel.r;
        }
        inked &= total > 0.0f;
    }
    AURA_CHECK(crisp && inked, "bitmap text lands texel for pixel: every pixel is fully on or off at any scale");
}

void fontSizeCoverageBudget()
{
    AtlasTextShaper shaper{TextShaperDesc{.maxPages = 11}};
    std::array<ShapedText, 11> text;
    DrawList list;
    list.begin(Rect::fromSize({}, {320, 500}));
    for (usize i = 0; i < text.size(); ++i)
    {
        shaper.shape("Aura3D 0123456789", TextStyle{.pixelSize = 10.0f + static_cast<f32>(i)}, kUnbounded, text[i]);
        list.drawText(text[i], {0, static_cast<f32>(i) * 30}, glm::vec4{1});
    }
    CaptureRenderer renderer;
    DrawListRenderer backend(renderer, shaper);
    backend.build(list);
    backend.submit();
    usize textureBytes = 0;
    for (const auto &[handle, texture] : renderer.textures)
        textureBytes += texture.rgba.size();
    AURA_CHECK(shaper.pageCount() == 11 && renderer.batches.size() == 1 && renderer.sceneBatches == 0,
               "eleven independently rasterized font sizes submit one overlay draw call");
    AURA_CHECK(textureBytes == usize{1024} * 1024, "eleven font sizes use a single 1 MiB coverage texture");
}

/// AURA_UI_CAPTURE_DIR set: writes the gallery at three scales as PPM, to look at.
void gallery(f32 scale, const char *dumpDir)
{
    aura3d::test::FakeWindow window;
    AtlasTextShaper shaper{TextShaperDesc{}};
    UIRoot root{shaper};
    const glm::vec2 logical{520.0f, 640.0f};
    root.setScale(scale);
    root.resize(logical);

    auto decoration = std::make_unique<DefaultWindowDecoration>();
    auto *bar = decoration.get();
    root.setDecoration(std::move(decoration));

    auto &column = root.setContent<Column>();
    column.layout().padding = Thickness::all(13.3f);
    column.setSpacing(7.7f);
    for (const f32 size : {10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 18.0f, 20.0f, 24.0f})
        column.add<Label>("The quick brown fox jumps 0123456789 " + std::to_string(int(size))).setFontSize(size);
    auto &row = column.add<Row>();
    row.setSpacing(9.4f);
    row.add<Button>("Primary");
    row.add<Button>("Cancel");
    row.add<CheckBox>("Check", true);
    column.add<Slider>(0.0f, 1.0f);
    column.add<ProgressBar>(0.6f);
    column.add<TextField>("Text field");
    column.add<Dropdown>(std::vector<std::string>{"One", "Two"});
    column.add<Separator>();
    auto &tabs = column.add<TabView>();
    tabs.addTab("General").add<CheckBox>("Autosave");
    tabs.addTab("Graphics");

    for (int i = 0; i < 3; ++i)
    {
        bar->update(window, "AuraUI Gallery");
        root.update(0.016f);
    }

    DrawList list;
    root.paint(list);
    CaptureRenderer renderer;
    DrawListRenderer backend(renderer, shaper);
    backend.build(list, root.scale());
    backend.submit();

    const auto width = u32(std::lround(logical.x * scale));
    const auto height = u32(std::lround(logical.y * scale));
    const glm::vec3 clear = glm::vec3(root.theme().palette().window) * 0.35f;
    const auto image = renderer.rasterize(width, height, clear);

    writePpm(std::string(dumpDir) + "/gallery_" + std::to_string(int(scale * 100)) + ".ppm", image, width, height);
}

} // namespace

int main()
{
    fontSizeCoverageBudget();
    roundedFillsHaveNoSeams();
    textStaysCrisp();
    if (const char *dump = std::getenv("AURA_UI_CAPTURE_DIR"))
        for (const f32 scale : {1.0f, 1.25f, 2.0f})
            gallery(scale, dump);
    AURA_TEST_MAIN_RETURN();
}
