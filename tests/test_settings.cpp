/*
 * AuraSettings' three-layer resolution: a JSON document over an AuraConfig set
 * in code over the engine's own built-ins.
 *
 * The layering is what lets an application ship without a settings.json, so
 * the case worth pinning down is the one that does not fail loudly -- a key
 * the file omits must read the application's value, not the engine's, and a
 * reload must not quietly discard the application's values along the way.
 */

#include <cstdio>
#include <fstream>
#include <string>

#include <wma/wma.hpp>

#include "aura/Core/AuraSettings/AuraSettings.h"

#include "TestUtils.h"

using namespace aura3d;

namespace {

//! Writes @p contents to a scratch file and returns its path.
[[nodiscard]] std::string writeConfig(const std::string& name, const std::string& contents)
{
    const std::string path = std::string{"aura_test_"} + name + ".json";
    std::ofstream file(path);
    file << contents;
    return path;
}

//! Back to a pristine singleton: every test below shares one AuraSettings.
void reset()
{
    AuraSettings::get()->setDefaults(AuraConfig{});
    AuraSettings::get()->reload("no-such-file-on-purpose.json");
}

void testBuiltInDefaultsWithNoFile()
{
    reset();

    const AuraSettings* settings = AuraSettings::get();

    AURA_CHECK(settings->getWindowWidth() == 1280, "an unconfigured engine is 1280 wide");
    AURA_CHECK(settings->getWindowHeight() == 720, "an unconfigured engine is 720 tall");
    AURA_CHECK(settings->getRendererBackend() == "vulkan", "the default backend is vulkan");
    AURA_CHECK(settings->getWindowBackend() == wma::WindowBackend::SDL3,
               "the default window backend is SDL3");
}

void testCodeDefaultsReplaceTheBuiltIns()
{
    reset();

    AuraConfig config;
    config.window.width = 1024;
    config.window.height = 768;
    config.window.title = "Viewer";
    config.window.backend = wma::WindowBackend::WAYLAND;
    config.window.vsyncMode = VSyncMode::Mailbox;
    config.renderer.backend = "opengl";
    config.renderer.validationLayers = false;
    config.graphics.msaaSamples = 4;
    config.audio.maxVoices = 8;
    config.paths.textures = "./art/";
    config.logging.level = ink::LogLevel::WARN;
    config.logging.toFile = true;

    AuraSettings::get()->setDefaults(config);

    const AuraSettings* settings = AuraSettings::get();

    AURA_CHECK(settings->getWindowWidth() == 1024, "a code default sets the width");
    AURA_CHECK(settings->getWindowTitle() == "Viewer", "a code default sets the title");
    AURA_CHECK(settings->getWindowBackend() == wma::WindowBackend::WAYLAND,
               "a code default sets the window backend");
    AURA_CHECK(settings->getVSyncMode() == VSyncMode::Mailbox,
               "a code default sets the present mode without a vsync flag to derive it from");
    AURA_CHECK(settings->getRendererBackend() == "opengl", "a code default sets the renderer");
    AURA_CHECK(!settings->getValidationLayers(),
               "a code default turns validation layers off in a debug build");
    AURA_CHECK(settings->getMsaaSamples() == 4, "a code default sets MSAA");
    AURA_CHECK(settings->getAudioMaxVoices() == 8, "a code default sets the voice count");
    AURA_CHECK(settings->getTexturesPath() == "./art/", "a code default sets an asset path");
    AURA_CHECK(settings->getLogLevel() == ink::LogLevel::WARN, "a code default sets the log level");
    AURA_CHECK(settings->getLogToFile(), "a code default turns file logging on");
}

void testTheFileOverridesCodeKeyByKey()
{
    reset();

    AuraConfig config;
    config.window.width = 1024;
    config.window.height = 768;
    config.renderer.backend = "opengl";
    AuraSettings::get()->setDefaults(config);

    //! Sets the width and nothing else: height and backend must survive.
    const std::string path = writeConfig("partial", R"({ "window": { "width": 640 } })");
    AuraSettings::get()->reload(path);

    const AuraSettings* settings = AuraSettings::get();

    AURA_CHECK(settings->getWindowWidth() == 640, "a key the file sets wins");
    AURA_CHECK(settings->getWindowHeight() == 768, "a key the file omits keeps the code default");
    AURA_CHECK(settings->getRendererBackend() == "opengl",
               "a whole group the file omits keeps the code defaults");

    std::remove(path.c_str());
}

void testReloadKeepsTheCodeDefaults()
{
    reset();

    AuraConfig config;
    config.window.width = 1024;
    AuraSettings::get()->setDefaults(config);

    const std::string path = writeConfig("full", R"({ "window": { "width": 640 } })");
    AuraSettings::get()->reload(path);
    AURA_CHECK(AuraSettings::get()->getWindowWidth() == 640, "the file is picked up");

    //! A second reload of a file that is not there must fall back to the
    //! application's value, not to the engine's.
    AuraSettings::get()->reload("no-such-file-on-purpose.json");
    AURA_CHECK(AuraSettings::get()->getWindowWidth() == 1024,
               "a failed reload falls back to the code defaults, not the built-ins");

    std::remove(path.c_str());
}

void testAudioBackendAutoStillMeansThePlatformDefault()
{
    reset();

    AuraConfig config;
    config.audio.backend = wma::AudioBackend::Null;
    AuraSettings::get()->setDefaults(config);

    AURA_CHECK(AuraSettings::get()->getAudioBackend() == wma::AudioBackend::Null,
               "an unset key takes the code default");

    const std::string path = writeConfig("audio", R"({ "audio": { "backend": "auto" } })");
    AuraSettings::get()->reload(path);

    AURA_CHECK(AuraSettings::get()->getAudioBackend() == wma::getDefaultAudioBackend(),
               "an explicit \"auto\" still asks for the platform default");

    std::remove(path.c_str());
}

} // namespace

int main()
{
    testBuiltInDefaultsWithNoFile();
    testCodeDefaultsReplaceTheBuiltIns();
    testTheFileOverridesCodeKeyByKey();
    testReloadKeepsTheCodeDefaults();
    testAudioBackendAutoStillMeansThePlatformDefault();

    AURA_TEST_MAIN_RETURN();
}
