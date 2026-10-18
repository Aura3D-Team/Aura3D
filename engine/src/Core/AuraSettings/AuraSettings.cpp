#include "aura/Core/AuraSettings/AuraSettings.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <wma/wma.hpp>

#include "aura/aura.h"

namespace aura3d
{
namespace
{

//! Lends a source the config shares to a store that owns its sources.
class SharedSource final : public ISettingsSource
{
  public:
    explicit SharedSource(std::shared_ptr<ISettingsSource> source) : _source(std::move(source))
    {
    }

    [[nodiscard]] std::string name() const override
    {
        return _source->name();
    }
    void load(SettingsSink &sink) override
    {
        _source->load(sink);
    }

  private:
    std::shared_ptr<ISettingsSource> _source;
};

[[nodiscard]] std::string_view platformName() noexcept
{
#if defined(__ANDROID__)
    return "android";
#elif defined(__EMSCRIPTEN__)
    return "wasm";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return "ios";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

} // namespace

bool VSyncModeFromString(const std::string &s, VSyncMode &out)
{
    const std::string wanted = lowerAscii(s);
    for (const VSyncMode mode :
         {VSyncMode::Fifo, VSyncMode::FifoRelaxed, VSyncMode::Mailbox, VSyncMode::Immediate, VSyncMode::Uncapped})
    {
        if (wanted == VSyncModeToString(mode))
        {
            out = mode;
            return true;
        }
    }
    return false;
}

const char *VSyncModeToString(VSyncMode mode)
{
    switch (mode)
    {
    case VSyncMode::Fifo:
        return "fifo";
    case VSyncMode::FifoRelaxed:
        return "fifo_relaxed";
    case VSyncMode::Mailbox:
        return "mailbox";
    case VSyncMode::Immediate:
        return "immediate";
    case VSyncMode::Uncapped:
        return "uncapped";
    }
    return "fifo";
}

bool VSyncModeSyncsToDisplay(VSyncMode mode) noexcept
{
    return mode == VSyncMode::Fifo || mode == VSyncMode::FifoRelaxed;
}

std::string platformSettingsPath(std::string_view path)
{
    const std::filesystem::path file{path};
    return (file.parent_path() / (file.stem().string() + "." + std::string{platformName()} + file.extension().string()))
        .string();
}

AuraConfig &AuraConfig::set(Key<bool> key, bool value)
{
    _values.emplace_back(std::string{key.path}, value);
    return *this;
}

AuraConfig &AuraConfig::set(Key<int> key, int value)
{
    _values.emplace_back(std::string{key.path}, static_cast<i64>(value));
    return *this;
}

AuraConfig &AuraConfig::set(Key<double> key, double value)
{
    _values.emplace_back(std::string{key.path}, value);
    return *this;
}

AuraConfig &AuraConfig::set(Key<std::string> key, std::string value)
{
    _values.emplace_back(std::string{key.path}, std::move(value));
    return *this;
}

AuraConfig &AuraConfig::setChoice(Key<std::string> key, std::string spelling)
{
    //! An enum names one of a choice's spellings; given to a free-text key it would be written there as text.
    const SettingDesc &desc = settingsSchema()[key.index];
    if (desc.type != SettingType::Choice)
    {
        INK_WARN << "Settings: " << key.path << ": is free text, not a choice; the value is ignored";
        return *this;
    }
    return set(key, std::move(spelling));
}

AuraConfig &AuraConfig::set(Key<std::string> key, wma::WindowBackend value)
{
    return setChoice(key, lowerAscii(WindowBackendToString(value)));
}

AuraConfig &AuraConfig::set(Key<std::string> key, wma::DecorationMode value)
{
    return setChoice(key, value == wma::DecorationMode::ClientSide ? "client" : "server");
}

AuraConfig &AuraConfig::set(Key<std::string> key, wma::AudioBackend value)
{
    return setChoice(key, lowerAscii(AudioBackendToString(value)));
}

AuraConfig &AuraConfig::set(Key<std::string> key, VSyncMode value)
{
    return setChoice(key, VSyncModeToString(value));
}

AuraConfig &AuraConfig::set(Key<std::string> key, ink::LogLevel value)
{
    return setChoice(key, lowerAscii(ink::MAP_COLORS_FOR_LEVEL[std::to_underlying(value)].desc));
}

AuraConfig &AuraConfig::arguments(int argc, const char *const *argv)
{
    _commandLine.assign(argv, argv + std::max(argc, 0));
    return *this;
}

AuraConfig &AuraConfig::addSource(std::shared_ptr<ISettingsSource> source)
{
    _sources.push_back(std::move(source));
    return *this;
}

AuraSettings *AuraSettings::get()
{
    static AuraSettings instance;
    return &instance;
}

void AuraSettings::configure(const AuraConfig &config, std::string_view path)
{
    std::vector<std::unique_ptr<ISettingsSource>> sources;
    sources.push_back(std::make_unique<MemorySource>("code", config.values()));
    if (!path.empty())
    {
        sources.push_back(std::make_unique<JsonFileSource>(std::string{path}, true));
        sources.push_back(std::make_unique<JsonFileSource>(platformSettingsPath(path), false));
    }
    for (const std::shared_ptr<ISettingsSource> &source : config.sources())
        sources.push_back(std::make_unique<SharedSource>(source));
    sources.push_back(std::make_unique<EnvironmentSource>());
    sources.push_back(std::make_unique<CommandLineSource>(config.commandLine()));
    setSources(std::move(sources));
}

std::string AuraSettings::windowTitle() const
{
    std::string title = get(setting::windowTitle);
    return title.empty() ? std::string{APPLICATION_NAME} : title;
}

wma::WindowBackend AuraSettings::windowBackend() const
{
    wma::WindowBackend backend = wma::WindowBackend::SDL3;
    WindowBackendFromString(get(setting::windowBackend), backend);
    return backend;
}

wma::DecorationMode AuraSettings::windowDecorations() const
{
    return get(setting::windowDecorations) == "client" ? wma::DecorationMode::ClientSide
                                                       : wma::DecorationMode::ServerSide;
}

VSyncMode AuraSettings::presentMode() const
{
    VSyncMode mode = VSyncMode::Fifo;
    VSyncModeFromString(get(setting::presentMode), mode);
    return mode;
}

wma::AudioBackend AuraSettings::audioBackend() const
{
    const std::string name = get(setting::audioBackend);
    if (name == "auto")
        return wma::getDefaultAudioBackend();

    wma::AudioBackend backend = wma::getDefaultAudioBackend();
    if (!AudioBackendFromString(name, backend))
        return wma::getDefaultAudioBackend();

    if (!wma::isAudioBackendAvailable(backend))
    {
        INK_WARN << "Settings: audio backend '" << name
                 << "' is not compiled into this build of wma; using the default";
        return wma::getDefaultAudioBackend();
    }
    return backend;
}

ink::LogLevel AuraSettings::logLevel() const
{
    const std::string name = get(setting::loggingLevel);
    for (u32 i = 0; i < std::to_underlying(ink::LogLevel::COUNT); ++i)
    {
        if (name == lowerAscii(ink::MAP_COLORS_FOR_LEVEL[i].desc))
            return static_cast<ink::LogLevel>(i);
    }
    return ink::LogLevel::INFO;
}

} // namespace aura3d
