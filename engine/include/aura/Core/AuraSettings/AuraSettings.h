#ifndef AURASETTINGS_H
#define AURASETTINGS_H

#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <ink/Inkogger.h>

#include <wma/core/Types.hpp>

#include "aura/Core/Settings/SettingsStore.h"

/// Windowing backends wma can create a window through. Mirrors
/// wma::WindowBackend; keep in sync by hand, there's no cross-library way to
/// generate one from the other.
#define WINDOW_BACKEND_LIST                                                                                            \
    X(GLFW)                                                                                                            \
    X(SDL3)                                                                                                            \
    X(X11)                                                                                                             \
    X(WAYLAND)

/**
 * @brief Audio backends wma can play sound through. Mirrors wma::AudioBackend
 *        (separate axis from WINDOW_BACKEND_LIST -- only SDL3 appears in both).
 *
 * Two columns, unlike the lists above: AudioBackend's enumerators are
 * PascalCase (Alsa, Sdl3, Null), so the second column carries the uppercase
 * form a case-folded config value is compared against.
 */
#define AUDIO_BACKEND_LIST                                                                                             \
    X(Alsa, "ALSA")                                                                                                    \
    X(Sdl3, "SDL3")                                                                                                    \
    X(Null, "NULL")

namespace aura3d
{

/**
 * @brief Converts a string representation to a wma::WindowBackend. Case-insensitive.
 * @return true if the string matches a known backend, false otherwise.
 */
inline bool WindowBackendFromString(const std::string &s, wma::WindowBackend &out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

#define X(name)                                                                                                        \
    if (up == #name)                                                                                                   \
    {                                                                                                                  \
        out = wma::WindowBackend::name;                                                                                \
        return true;                                                                                                   \
    }
    WINDOW_BACKEND_LIST
#undef X

    return false;
}

/**
 * @brief Converts a wma::WindowBackend to its exact string representation.
 */
inline const char *WindowBackendToString(wma::WindowBackend backend)
{
    switch (backend)
    {
#define X(name)                                                                                                        \
    case wma::WindowBackend::name:                                                                                     \
        return #name;
        WINDOW_BACKEND_LIST
#undef X
    }
    return "UNKNOWN";
}

/**
 * @brief Converts a string representation to a wma::AudioBackend. Case-insensitive.
 * @return true if the string matches a known backend, false otherwise.
 */
inline bool AudioBackendFromString(const std::string &s, wma::AudioBackend &out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

#define X(name, upper)                                                                                                 \
    if (up == (upper))                                                                                                 \
    {                                                                                                                  \
        out = wma::AudioBackend::name;                                                                                 \
        return true;                                                                                                   \
    }
    AUDIO_BACKEND_LIST
#undef X

    //! Spellings a config file is likely to use that the enumerators do not
    //! cover. "SDL" without the version number is the common one.
    if (up == "SDL")
    {
        out = wma::AudioBackend::Sdl3;
        return true;
    }
    if (up == "NONE" || up == "SILENT")
    {
        out = wma::AudioBackend::Null;
        return true;
    }

    return false;
}

/**
 * @brief Converts a wma::AudioBackend to its exact string representation.
 */
inline const char *AudioBackendToString(wma::AudioBackend backend)
{
    switch (backend)
    {
#define X(name, upper)                                                                                                 \
    case wma::AudioBackend::name:                                                                                      \
        return #name;
        AUDIO_BACKEND_LIST
#undef X
    }
    return "UNKNOWN";
}

/**
 * @brief How frames reach the display; `present.mode` in the settings.
 *
 * Uncapped is the best non-waiting mode the surface has: Immediate, else Mailbox, else Fifo.
 */
enum class VSyncMode
{
    Fifo,
    FifoRelaxed,
    Mailbox,
    Immediate,
    Uncapped,
};

/// Case-insensitive; false for a spelling that is not a mode.
bool VSyncModeFromString(const std::string &s, VSyncMode &out);

/// The settings spelling: `fifo`, `fifo_relaxed`, `mailbox`, `immediate`, `uncapped`.
const char *VSyncModeToString(VSyncMode mode);

/// Whether a frame waits for the display, so the loop needs no cap of its own. OpenGL and Metal take only this.
[[nodiscard]] bool VSyncModeSyncsToDisplay(VSyncMode mode) noexcept;

/// `settings.json` becomes `settings.linux.json` (`android`, `wasm`, `windows`, `macos`, `ios`).
[[nodiscard]] std::string platformSettingsPath(std::string_view path);

/**
 * @class AuraConfig
 * @brief What code sets, as the layer under the file.
 *
 * Takes the same typed keys the engine reads with. Values are checked when loaded, like a file's.
 *
 * @code
 * aura3d::AuraConfig config;
 * config.set(aura3d::setting::windowTitle, "Viewer")
 *     .set(aura3d::setting::windowWidth, 1024)
 *     .set(aura3d::setting::windowBackend, wma::WindowBackend::SDL3);
 *
 * Engine engine(config);                     // nothing else to ship
 * Engine tweakable(config, "settings.json"); // or a file that may override
 * @endcode
 */
class AuraConfig
{
  public:
    AuraConfig &set(Key<bool> key, bool value);
    AuraConfig &set(Key<int> key, int value);
    AuraConfig &set(Key<double> key, double value);
    AuraConfig &set(Key<std::string> key, std::string value);
    AuraConfig &set(Key<std::string> key, wma::WindowBackend value);
    AuraConfig &set(Key<std::string> key, wma::DecorationMode value);
    AuraConfig &set(Key<std::string> key, wma::AudioBackend value);
    AuraConfig &set(Key<std::string> key, VSyncMode value);
    AuraConfig &set(Key<std::string> key, ink::LogLevel value);

    /// The program's arguments, for `--set path=value`.
    AuraConfig &arguments(int argc, const char *const *argv);

    /// A layer of your own, between the platform file and the environment.
    AuraConfig &addSource(std::shared_ptr<ISettingsSource> source);

    [[nodiscard]] const std::vector<std::pair<std::string, SettingValue>> &values() const noexcept
    {
        return _values;
    }
    [[nodiscard]] const std::vector<std::string> &commandLine() const noexcept
    {
        return _commandLine;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<ISettingsSource>> &sources() const noexcept
    {
        return _sources;
    }

  private:
    std::vector<std::pair<std::string, SettingValue>> _values;
    std::vector<std::string> _commandLine;
    std::vector<std::shared_ptr<ISettingsSource>> _sources;
};

/**
 * @class AuraSettings
 * @brief The engine's settings: one store, layered, read through typed keys.
 *
 * Later layers win, key by key:
 *
 * | Layer | From |
 * |---|---|
 * | built-in | the schema's defaults |
 * | code | the AuraConfig |
 * | file | `settings.json` |
 * | platform file | `settings.<platform>.json`, only the keys that differ |
 * | environment | `AURA_WINDOW__WIDTH=800` |
 * | command line | `--set window.width=800` |
 * | runtime | set() |
 *
 * @code
 * const auto *settings = engine.getSettings();
 * settings->get(aura3d::setting::windowWidth); // int
 * settings->windowBackend();                   // wma::WindowBackend
 * @endcode
 */
class AuraSettings final : public SettingsStore
{
  public:
    using SettingsStore::get;

    static AuraSettings *get();

    /// Rebuilds the layers above and loads them. @p path may be empty, or name a file that is not there.
    void configure(const AuraConfig &config, std::string_view path = {});

    /// Window title, the application name when the setting is empty.
    [[nodiscard]] std::string windowTitle() const;
    [[nodiscard]] wma::WindowBackend windowBackend() const;
    [[nodiscard]] wma::DecorationMode windowDecorations() const;
    [[nodiscard]] VSyncMode presentMode() const;
    /// `auto`, and a backend this build of wma lacks, resolve to the platform's.
    [[nodiscard]] wma::AudioBackend audioBackend() const;
    [[nodiscard]] ink::LogLevel logLevel() const;

  private:
    AuraSettings() = default;
};

} // namespace aura3d

#endif // AURASETTINGS_H
