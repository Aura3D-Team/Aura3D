#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "TestUtils.h"

#ifdef AURA_HAS_OPENGL
#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"
#endif

#ifdef AURA_HAS_VULKAN
#include "aura/Renderer/Vulkan/VkAura/EmbeddedSpirv.h"
#include "aura/Renderer/Vulkan/VkAura/VkShader/ShaderSpirvExtractor.h"
#endif

using namespace aura3d;

// Covers only the parts of each backend's shader-loading path that don't
// need a live GL context / VkDevice, so this suite runs on any CI runner
// with no GPU: GL's source-file reading, and Vulkan's SPIR-V byte-file
// reading plus the embedded SPIR-V modules baked into EmbeddedSpirv.h.
// Actual shader-module compilation (glCompileShader / vkCreateShaderModule)
// needs a real context/device and is exercised manually via the Sandbox app.

namespace {

std::filesystem::path scratchPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("aura_test_shader_loader_" + name);
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream file(path, std::ios::binary);
    file << content;
}

void writeFile(const std::filesystem::path& path, const std::vector<char>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

#ifdef AURA_HAS_OPENGL
void test_gl_shader_source_reading()
{
    const std::string glsl =
        "#version 330 core\nlayout(location = 0) in vec3 aPos;\nvoid main() { gl_Position = vec4(aPos, 1.0); }\n";

    const auto path = scratchPath("valid.vert");
    writeFile(path, glsl);

    const std::string readBack = gl::GlShaderManager::readShaderSource_GL(path.string());
    AURA_CHECK(readBack == glsl, "GlShaderManager::readShaderSource_GL: round-trips file content exactly");

    std::filesystem::remove(path);

    AURA_CHECK_THROWS(gl::GlShaderManager::readShaderSource_GL(scratchPath("missing.vert").string()),
                      std::runtime_error,
                      "GlShaderManager::readShaderSource_GL: throws runtime_error for a missing file");
}
#endif // AURA_HAS_OPENGL

#ifdef AURA_HAS_VULKAN
void test_vk_spirv_extractor()
{
    const std::vector<char> fakeSpirv = {'\x03', '\x02', '\x23', '\x07', 'x', 'y', 'z', 'w'};

    const auto vertPath = scratchPath("shader_vert.spv");
    const auto fragPath = scratchPath("shader_frag.spv");
    writeFile(vertPath, fakeSpirv);
    writeFile(fragPath, fakeSpirv);

    vk::ShaderSpirvExtractor extractor;

    extractor.readVertFile(vertPath.string());
    AURA_CHECK(extractor.getVertByteCode() == fakeSpirv,
              "ShaderSpirvExtractor::readVertFile: byte content round-trips exactly");

    extractor.readFragFile(fragPath.string());
    AURA_CHECK(extractor.getFragByteCode() == fakeSpirv,
              "ShaderSpirvExtractor::readFragFile: byte content round-trips exactly");

    std::filesystem::remove(vertPath);
    std::filesystem::remove(fragPath);

    // Wrong extension is rejected before the file is even opened.
    AURA_CHECK_THROWS(extractor.readVertFile(scratchPath("shader.txt").string()),
                      std::invalid_argument,
                      "ShaderSpirvExtractor::readVertFile: rejects a non-'vert.spv' filename");
    AURA_CHECK_THROWS(extractor.readFragFile(scratchPath("shader.txt").string()),
                      std::invalid_argument,
                      "ShaderSpirvExtractor::readFragFile: rejects a non-'frag.spv' filename");

    // Right extension, but the file doesn't exist.
    AURA_CHECK_THROWS(extractor.readVertFile(scratchPath("missing_vert.spv").string()),
                      std::runtime_error,
                      "ShaderSpirvExtractor::readVertFile: throws runtime_error for a missing file");
    AURA_CHECK_THROWS(extractor.readFragFile(scratchPath("missing_frag.spv").string()),
                      std::runtime_error,
                      "ShaderSpirvExtractor::readFragFile: throws runtime_error for a missing file");
}

void test_embedded_spirv_well_formed()
{
    // Regression guard for exactly the bug this suite exists because of:
    // EmbeddedSpirv.h silently going stale relative to the GLSL sources
    // (see scripts/gen_embedded_spirv.sh). Every embedded module must at
    // least be structurally valid SPIR-V: correct magic number, and a
    // length that's a whole number of 32-bit words.
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;

    auto checkModule = [](const unsigned char* data, unsigned int len, const std::string& name) {
        AURA_CHECK(len >= 20, name + ": embedded module is at least as long as a SPIR-V header");
        AURA_CHECK(len % 4 == 0, name + ": embedded module length is a whole number of 32-bit words");

        std::uint32_t magic = 0;
        if (len >= 4) {
            magic = static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
                    (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
        }
        AURA_CHECK(magic == kSpirvMagic, name + ": starts with the SPIR-V magic number");
    };

    checkModule(vk::vk_vert_2d, vk::vk_vert_2d_len, "EmbeddedSpirv.h: vk_vert_2d");
    checkModule(vk::vk_frag_2d, vk::vk_frag_2d_len, "EmbeddedSpirv.h: vk_frag_2d");
    checkModule(vk::vk_vert_3d, vk::vk_vert_3d_len, "EmbeddedSpirv.h: vk_vert_3d");
    checkModule(vk::vk_frag_3d, vk::vk_frag_3d_len, "EmbeddedSpirv.h: vk_frag_3d");
}
#endif // AURA_HAS_VULKAN

} // namespace

int main()
{
#ifdef AURA_HAS_OPENGL
    test_gl_shader_source_reading();
#else
    std::cout << "[SKIP] OpenGL backend not enabled; skipping GlShaderManager tests\n";
#endif

#ifdef AURA_HAS_VULKAN
    test_vk_spirv_extractor();
    test_embedded_spirv_well_formed();
#else
    std::cout << "[SKIP] Vulkan backend not enabled; skipping VkShader tests\n";
#endif

    AURA_TEST_MAIN_RETURN();
}
