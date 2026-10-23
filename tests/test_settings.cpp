/*
 * The settings store: one schema, layered sources, and a loader that says what is wrong instead of ignoring it.
 *
 * The cases worth pinning are the ones that fail quietly: a later layer must win key by key, a refused value must
 * leave the earlier layer's standing, and a misspelt key has to be reported -- an unread `vsync_mode` is how this
 * test came to exist.
 */

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <ink/EnhancedJson.h>

#include <wma/wma.hpp>

#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/aura.h"

#include "TestUtils.h"

using namespace aura3d;

namespace
{

void setEnv(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char *name)
{
#ifdef _WIN32
    _putenv_s(name, ""); // an empty value removes the variable
#else
    unsetenv(name);
#endif
}

//! Writes @p contents to a scratch file and returns its path.
[[nodiscard]] std::string writeFile(const std::string &name, const std::string &contents)
{
    const std::string path = std::string{"aura_test_"} + name + ".json";
    std::ofstream file(path);
    file << contents;
    return path;
}

[[nodiscard]] std::string readFile(const std::string &path)
{
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

[[nodiscard]] bool mentions(const AuraSettings &settings, const std::string &needle)
{
    for (const std::string &line : settings.diagnostics())
    {
        if (line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

//! Back to a pristine singleton: every test below shares one AuraSettings.
AuraSettings &fresh()
{
    AuraSettings &settings = *AuraSettings::get();
    settings.clearRuntime();
    settings.configure(AuraConfig{});
    return settings;
}

void testBuiltInDefaults()
{
    AuraSettings &settings = fresh();

    AURA_CHECK(settings.get(setting::windowWidth) == 1280, "the width defaults to 1280");
    AURA_CHECK(settings.get(setting::windowResizable), "a window is resizable by default");
    AURA_CHECK(settings.presentMode() == VSyncMode::Fifo, "frames wait for the display by default");
    AURA_CHECK(settings.get(setting::rendererApi) == "auto", "the renderer is the platform's by default");
    AURA_CHECK(settings.windowBackend() == wma::WindowBackend::SDL3, "SDL3 is the default window backend");
    AURA_CHECK(settings.windowTitle() == APPLICATION_NAME, "an empty title takes the application name");
    AURA_CHECK(settings.get(setting::audioVolume) == 1.0, "full volume by default");
    AURA_CHECK(settings.diagnostics().empty(), "defaults alone report nothing");

    for (const SettingDesc &desc : settingsSchema())
        AURA_CHECK(settings.value(desc.path).has_value(),
                   std::string{"every declared setting has a value: "} + std::string{desc.path});
}

void testLayersOverrideKeyByKey()
{
    AuraSettings &settings = fresh();

    AuraConfig config;
    config.set(setting::windowWidth, 1024).set(setting::windowHeight, 768).set(setting::windowTitle, "Viewer");

    const std::string file = writeFile("layers", R"({ "window": { "width": 800 }, "present": { "mode": "mailbox" } })");
    settings.configure(config, file);

    AURA_CHECK(settings.get(setting::windowWidth) == 800, "the file overrides the code");
    AURA_CHECK(settings.get(setting::windowHeight) == 768, "a key the file omits reads the code's");
    AURA_CHECK(settings.windowTitle() == "Viewer", "and so does the title");
    AURA_CHECK(settings.presentMode() == VSyncMode::Mailbox, "a nested key is its dotted path");
    AURA_CHECK(settings.get(setting::windowResizable), "a key nothing sets reads the built-in default");

    //! The file is gone: the code layer must still stand.
    std::remove(file.c_str());
    settings.reload();
    AURA_CHECK(settings.get(setting::windowWidth) == 1024, "a reload without the file falls back to the code");
    AURA_CHECK(mentions(settings, "no readable file"), "and says the file was missing");
}

void testPlatformFileOverridesTheFile()
{
    AuraSettings &settings = fresh();

    const std::string base = writeFile("platform", R"({ "window": { "width": 800, "height": 600 } })");
    const std::string overlay = platformSettingsPath(base);
    {
        std::ofstream(overlay) << R"({ "window": { "width": 640 } })";
    }

    settings.configure(AuraConfig{}, base);
    AURA_CHECK(settings.get(setting::windowWidth) == 640, "the platform file overrides settings.json");
    AURA_CHECK(settings.get(setting::windowHeight) == 600, "and only the keys it names");
    AURA_CHECK(settings.diagnostics().empty(), "a platform file is no problem");

    std::remove(overlay.c_str());
    settings.configure(AuraConfig{}, base);
    AURA_CHECK(settings.get(setting::windowWidth) == 800, "an absent platform file is silent");
    AURA_CHECK(settings.diagnostics().empty(), "and reports nothing");
    std::remove(base.c_str());

    AURA_CHECK(platformSettingsPath("assets/settings.json").starts_with("assets/settings."),
               "the platform name goes before the extension");
}

void testEnvironmentAndCommandLine()
{
    AuraSettings &settings = fresh();

    setEnv("AURA_WINDOW__WIDTH", "900");
    setEnv("AURA_PRESENT__MODE", "IMMEDIATE");

    std::array<const char *, 4> arguments{"app", "--set", "window.width=700", "--set=audio.volume=0.25"};
    AuraConfig config;
    config.arguments(static_cast<int>(arguments.size()), arguments.data());

    const std::string file = writeFile("env", R"({ "window": { "width": 800 } })");
    settings.configure(config, file);
    unsetEnv("AURA_WINDOW__WIDTH");
    unsetEnv("AURA_PRESENT__MODE");
    std::remove(file.c_str());

    AURA_CHECK(settings.get(setting::windowWidth) == 700, "the command line is the last word");
    AURA_CHECK(settings.get(setting::audioVolume) == 0.25, "a float comes from text");
    AURA_CHECK(settings.presentMode() == VSyncMode::Immediate, "the environment overrides the file, in any case");
    AURA_CHECK(settings.diagnostics().empty(), "all of it was valid");
}

void testBadValuesAreReportedAndRefused()
{
    AuraSettings &settings = fresh();

    const std::string file = writeFile("bad", R"({
        "$schema": "../docs/aura.schema.json",
        "window": { "wdth": 800, "height": 99999, "resizable": "yes", "backend": "wayand" },
        "present": { "mode": "Fifoo" },
        "audio": { "volume": 2 },
        "bogus": 1
    })");
    AuraConfig config;
    config.set(setting::windowHeight, 500);
    settings.configure(config, file);
    std::remove(file.c_str());

    AURA_CHECK(mentions(settings, "window.wdth: no such setting; did you mean 'window.width'?"),
               "a misspelt key is reported with the key it nearly is");
    AURA_CHECK(mentions(settings, "window.height") && settings.get(setting::windowHeight) == 500,
               "a value out of range is refused and the layer beneath stands");
    AURA_CHECK(mentions(settings, "window.resizable: expects true or false"), "a wrong type says what is expected");
    AURA_CHECK(mentions(settings, "did you mean 'wayland'?"), "a wrong choice offers the nearest one");
    AURA_CHECK(mentions(settings, "present.mode") && settings.presentMode() == VSyncMode::Fifo,
               "a bad present mode leaves the default");
    AURA_CHECK(mentions(settings, "audio.volume") && settings.get(setting::audioVolume) == 1.0,
               "a volume above 1 is refused, not clamped");
    AURA_CHECK(mentions(settings, "bogus: no such setting") && !mentions(settings, "$schema"),
               "an unknown section is reported, the editor's $schema is not");
}

void testVulkanTuningIsConfigurable()
{
    AuraSettings &settings = fresh();
    AURA_CHECK(settings.get(setting::vulkanTransformSlots) == 1024 &&
                   settings.get(setting::vulkanMinDrawsToThread) == 512 &&
                   settings.get(setting::vulkanMinDrawsPerChunk) == 128 &&
                   settings.get(setting::vulkanDescriptorHeadroom) == 16,
               "the recording and ring sizes default to what they were when fixed");

    AuraConfig config;
    config.set(setting::vulkanMinDrawsToThread, 64).set(setting::vulkanMinDrawsPerChunk, 0);
    settings.configure(config);
    AURA_CHECK(settings.get(setting::vulkanMinDrawsToThread) == 64, "an application sets them in code");
    AURA_CHECK(settings.get(setting::vulkanMinDrawsPerChunk) == 128 && mentions(settings, "min_draws_per_chunk"),
               "a chunk of no draws would divide by zero, so it is refused");
}

void testInputThatIsMisplacedIsReported()
{
    AuraSettings &settings = fresh();

    std::array<const char *, 3> arguments{"app", "--set=window.width=700", "--set"};
    AuraConfig config;
    config.arguments(static_cast<int>(arguments.size()), arguments.data());
    config.set(setting::windowTitle, VSyncMode::Mailbox);

    const std::string file = writeFile("huge", R"({ "window": { "height": 18446744073709551615 } })");
    settings.configure(config, file);
    std::remove(file.c_str());

    AURA_CHECK(mentions(settings, "--set: expected path=value"), "a --set with nothing after it is reported");
    AURA_CHECK(settings.get(setting::windowWidth) == 700, "and the others before it still apply");
    AURA_CHECK(mentions(settings, "window.height: too large"), "an integer past what a setting can hold is said so");
    AURA_CHECK(settings.get(setting::windowHeight) == 720, "and leaves the default");
    AURA_CHECK(settings.get(setting::windowTitle).empty(), "an enum given to a free-text key writes nothing there");
}

void testNumbersAreCoerced()
{
    AuraSettings &settings = fresh();

    const std::string file = writeFile("numbers", R"({ "audio": { "volume": 1, "sample_rate": 44100.0 } })");
    settings.configure(AuraConfig{}, file);
    std::remove(file.c_str());

    AURA_CHECK(settings.get(setting::audioVolume) == 1.0, "a whole number is a valid float");
    AURA_CHECK(settings.get(setting::audioSampleRate) == 44100, "a float with no fraction is a valid integer");
    AURA_CHECK(settings.diagnostics().empty(), "neither is a problem");
}

void testTypedHelpers()
{
    AuraSettings &settings = fresh();

    AuraConfig config;
    config.set(setting::windowBackend, wma::WindowBackend::WAYLAND)
        .set(setting::windowDecorations, wma::DecorationMode::ClientSide)
        .set(setting::audioBackend, wma::AudioBackend::Null)
        .set(setting::presentMode, VSyncMode::Uncapped)
        .set(setting::loggingLevel, ink::LogLevel::WARN);
    settings.configure(config);

    AURA_CHECK(settings.windowBackend() == wma::WindowBackend::WAYLAND, "a window backend round-trips");
    AURA_CHECK(settings.windowDecorations() == wma::DecorationMode::ClientSide, "decorations round-trip");
    AURA_CHECK(settings.audioBackend() == wma::AudioBackend::Null, "an audio backend round-trips");
    AURA_CHECK(settings.presentMode() == VSyncMode::Uncapped, "a present mode round-trips");
    AURA_CHECK(settings.logLevel() == ink::LogLevel::WARN, "a log level round-trips");
    AURA_CHECK(settings.diagnostics().empty(), "every spelling the helpers write is one the schema accepts");

    settings.configure(AuraConfig{});
    AURA_CHECK(settings.audioBackend() == wma::getDefaultAudioBackend(), "audio `auto` is the platform's backend");
}

void testVSyncModeDecidesWhetherTheDisplayPaces()
{
    AURA_CHECK(VSyncModeSyncsToDisplay(VSyncMode::Fifo) && VSyncModeSyncsToDisplay(VSyncMode::FifoRelaxed),
               "the modes that wait for the display are vsync");
    AURA_CHECK(!VSyncModeSyncsToDisplay(VSyncMode::Immediate) && !VSyncModeSyncsToDisplay(VSyncMode::Mailbox) &&
                   !VSyncModeSyncsToDisplay(VSyncMode::Uncapped),
               "the modes that do not wait are not, Mailbox included");
}

void testRuntimeChangesAndSubscribers()
{
    AuraSettings &settings = fresh();

    std::vector<std::string> changed;
    const u32 id = settings.subscribe(
        [&changed](std::string_view path)
        {
            changed.emplace_back(path);
        });

    AURA_CHECK(settings.set(setting::audioVolume, 0.5), "a valid runtime change is taken");
    AURA_CHECK(settings.get(setting::audioVolume) == 0.5, "and read back");
    AURA_CHECK(changed == std::vector<std::string>{"audio.volume"}, "subscribers hear the path that changed");

    AURA_CHECK(!settings.set(setting::audioVolume, 3.0), "an invalid one is refused");
    AURA_CHECK(settings.get(setting::audioVolume) == 0.5, "and leaves the value alone");

    const std::string file = writeFile("runtime", R"({ "audio": { "volume": 0.9, "channels": 1 } })");
    changed.clear();
    settings.configure(AuraConfig{}, file);
    std::remove(file.c_str());
    AURA_CHECK(settings.get(setting::audioVolume) == 0.5, "a runtime change outlasts a reload");
    AURA_CHECK(changed == std::vector<std::string>{"audio.channels"},
               "a reload tells subscribers about what changed, and only that");

    settings.unsubscribe(id);
    changed.clear();
    AURA_CHECK(settings.set(setting::audioVolume, 0.7) && changed.empty(), "an unsubscribed handler is not called");

    settings.clearRuntime();
    AURA_CHECK(settings.get(setting::audioVolume) == 1.0, "clearing drops runtime changes");
}

void testSchemaAndReferenceMatchTheCommittedFiles()
{
    const std::string schema = settingsJsonSchema();
    AURA_CHECK(readFile(std::string{AURA_DOCS_DIR} + "/aura.schema.json") == schema,
               "docs/aura.schema.json is current (cmake --build . --target update-settings-docs)");
    AURA_CHECK(readFile(std::string{AURA_DOCS_DIR} + "/settings-reference.md") == settingsMarkdown(),
               "docs/settings-reference.md is current (cmake --build . --target update-settings-docs)");

    try
    {
        const nlohmann::json parsed = nlohmann::json::parse(schema);
        const nlohmann::json &window = parsed.at("properties").at("window").at("properties");
        AURA_CHECK(parsed.at("additionalProperties") == false, "the schema rejects unknown top-level keys");
        AURA_CHECK(window.at("width").at("minimum") == 1 && window.at("width").at("default") == 1280,
                   "a nested setting carries its range and default");
        AURA_CHECK(parsed.at("properties").at("present").at("properties").at("mode").at("enum").size() == 5,
                   "a choice lists its spellings");
    }
    catch (const nlohmann::json::exception &error)
    {
        AURA_CHECK(false, std::string{"the schema is valid and complete: "} + error.what());
    }
}

} // namespace

int main()
{
    testBuiltInDefaults();
    testLayersOverrideKeyByKey();
    testPlatformFileOverridesTheFile();
    testEnvironmentAndCommandLine();
    testBadValuesAreReportedAndRefused();
    testVulkanTuningIsConfigurable();
    testInputThatIsMisplacedIsReported();
    testNumbersAreCoerced();
    testTypedHelpers();
    testVSyncModeDecidesWhetherTheDisplayPaces();
    testRuntimeChangesAndSubscribers();
    testSchemaAndReferenceMatchTheCommittedFiles();

    AURA_TEST_MAIN_RETURN();
}
