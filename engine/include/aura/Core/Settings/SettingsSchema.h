#ifndef AURA_SETTINGS_SCHEMA_H
#define AURA_SETTINGS_SCHEMA_H

#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <ink/ink_base.hpp>

/**
 * @file SettingsSchema.h
 * @brief Every engine setting, declared once.
 *
 * AURA_SETTINGS is the only place a setting is named. The typed keys in
 * aura3d::setting, the descriptor table the loader validates against, the JSON
 * Schema and the reference page are all generated from it, so none of them can
 * drift from another.
 *
 * Adding a setting is one line:
 * @code
 * INT(windowWidth, "window.width", 1280, 1, 16384, "Initial window width in pixels.")
 * @endcode
 * then read it with @c settings.get(setting::windowWidth).
 */

//! Entry forms, in the order the arguments run: id, path, default, [minimum, maximum], [choices], doc.
//! BUILD forms take an expression evaluated in the library, and the words the docs use for it.
// clang-format off
#define AURA_SETTINGS(BOOL, BOOL_BUILD, INT, FLOAT, TEXT, CHOICE, CHOICE_BUILD)                                        \
    INT(schemaVersion, "schema_version", 1, 1, 1,                                                                      \
        "Layout version of the file; 1 is the only one.")                                                              \
                                                                                                                       \
    CHOICE(windowBackend, "window.backend", "sdl3", ("sdl3", "glfw", "x11", "wayland"),                                \
           "Library the window is created through. SDL3 is the one every renderer supports on every platform.")        \
    INT(windowWidth, "window.width", 1280, 1, 16384, "Initial width in pixels.")                                       \
    INT(windowHeight, "window.height", 720, 1, 16384, "Initial height in pixels.")                                     \
    TEXT(windowTitle, "window.title", "", "Window title. Empty takes the application name.")                           \
    BOOL(windowResizable, "window.resizable", true, "Whether the user can resize the window.")                         \
    BOOL(windowFullscreen, "window.fullscreen", false, "Start fullscreen.")                                            \
    CHOICE(windowDecorations, "window.decorations", "server", ("server", "client"),                                    \
           "Who draws the title bar. `client` makes UIView draw it; a Wayland compositor may override either way.")    \
                                                                                                                       \
    CHOICE(presentMode, "present.mode", "fifo",                                                                        \
           ("fifo", "fifo_relaxed", "mailbox", "immediate", "uncapped"),                                               \
           "How frames reach the display. `fifo` waits for it and never tears; `fifo_relaxed` may tear when late; "    \
           "`mailbox` does not wait and does not tear; `immediate` does not wait; `uncapped` is the best of those "    \
           "the surface has. OpenGL and Metal only take wait or no wait from it.")                                     \
    INT(presentFpsLimit, "present.fps_limit", 0, 0, 1000,                                                              \
        "Frame cap for the modes that do not wait for the display. 0 is uncapped.")                                    \
                                                                                                                       \
    CHOICE(rendererApi, "renderer.api", "auto", ("auto", "vulkan", "opengl", "metal", "software"),                     \
           "Graphics API. `auto` is the platform's best; one that is unavailable falls back with a warning.")          \
    INT(rendererMsaa, "renderer.msaa", 1, 0, 64,                                                                       \
        "MSAA sample count, clamped to what the device supports; 1 is off. Vulkan only.")                              \
    INT(rendererFramesInFlight, "renderer.frames_in_flight", 2, 1, 8,                                                  \
        "Frames the CPU may record ahead of the GPU. Vulkan only.")                                                    \
    BOOL_BUILD(rendererGpuTiming, "renderer.gpu_timing", kProfileFrameBuild, "on in AURA_PROFILE_FRAME builds",        \
               "Measure each frame's GPU time. Ignored without AURA_PROFILE_FRAME.")                                   \
    INT(rendererCpuThreads, "renderer.cpu_threads", 0, 0, 256,                                                         \
        "Worker threads of the job system and the software rasteriser. 0 detects the core count.")                     \
    BOOL_BUILD(vulkanValidation, "renderer.vulkan.validation", kDebugBuild, "on in debug builds",                      \
               "Enable the Khronos validation layer.")                                                                 \
    CHOICE(vulkanGpu, "renderer.vulkan.gpu", "discrete", ("discrete", "integrated", "any"),                            \
           "Which kind of device to prefer. `any` lets capability decide alone.")                                      \
    TEXT(vulkanApiVersion, "renderer.vulkan.api_version", "1.4",                                                       \
         "Vulkan API version the allocator is created for, capped at what the headers support.")                       \
    BOOL(vmaBufferDeviceAddress, "renderer.vulkan.vma.buffer_device_address", true,                                    \
         "Allow buffer device addresses, where the device has them.")                                                  \
    BOOL(vmaPreferDeviceMemory, "renderer.vulkan.vma.prefer_device_memory", true,                                      \
         "Prefer device-local memory for resources.")                                                                  \
    BOOL(vmaMapUploadBuffers, "renderer.vulkan.vma.map_upload_buffers", true,                                          \
         "Keep upload buffers mapped for their whole life.")                                                           \
    INT(vmaHeapBlockMb, "renderer.vulkan.vma.heap_block_mb", 128, 1, 4096,                                             \
        "Preferred size of one large heap block, in MiB.")                                                             \
                                                                                                                       \
    CHOICE(audioBackend, "audio.backend", "auto", ("auto", "alsa", "sdl3", "null"),                                    \
           "API sound is played through, independent of `window.backend`. `auto` is ALSA on desktop Linux, SDL3 "      \
           "elsewhere; one that cannot open a device degrades to `null`.")                                             \
    FLOAT(audioVolume, "audio.volume", 1.0, 0.0, 1.0, "Master output gain.")                                           \
    INT(audioSampleRate, "audio.sample_rate", 48000, 8000, 192000,                                                     \
        "Requested output rate; the device may grant another.")                                                        \
    INT(audioChannels, "audio.channels", 2, 1, 8, "Requested output channels.")                                        \
    INT(audioBufferFrames, "audio.buffer_frames", 1024, 32, 16384,                                                     \
        "Frames per device callback, the latency dial. SDL3 may choose its own.")                                      \
    INT(audioMaxVoices, "audio.max_voices", 32, 0, 1024,                                                               \
        "Voices that can sound at once; further play() calls are dropped. 0 plays nothing.")                                            \
                                                                                                                       \
    TEXT(pathShaders, "paths.shaders", "./resources/shaders/", "Shader directory for the application's own loading.")  \
    TEXT(pathTextures, "paths.textures", "./resources/textures/", "Texture directory.")                                \
    TEXT(pathModels, "paths.models", "./resources/models/", "Model directory.")                                        \
    TEXT(pathAudio, "paths.audio", "./resources/audio/", "Sound directory.")                                           \
    TEXT(pathLogs, "paths.logs", "./logs/", "Where the log file goes when `logging.file` is on.")                      \
                                                                                                                       \
    CHOICE_BUILD(loggingLevel, "logging.level", (kDebugBuild ? "trace" : "info"),                                      \
                 ("off", "fatal", "error", "warn", "info", "debug", "verbose", "trace"),                               \
                 "trace in debug builds, info otherwise", "Lowest level logged.")                                      \
    BOOL(loggingFile, "logging.file", false, "Also write the log to `<paths.logs>/<application>.log`.")                \
                                                                                                                       \
    TEXT(debugReportPath, "debug.report_path", "aura3d-benchmark.json",                                                \
         "Where the benchmark report is written. AURA_ENABLE_DEBUG_MODE builds only.")                                 \
    TEXT(debugLabel, "debug.label", "", "Free-form tag copied into the report, such as a commit or a scene.")          \
    INT(debugWarmupFrames, "debug.warmup_frames", 60, 0, 1000000, "Frames discarded before sampling begins.")          \
    INT(debugSampleCapacity, "debug.sample_capacity", 20000, 1, 10000000,                                              \
        "Ring capacity for raw samples, about 96 bytes each.")                                                         \
    INT(debugTargetFrames, "debug.target_frames", 0, 0, 1000000000,                                                    \
        "Frames to capture before the report is written; 0 runs until the application stops.")                         \
    FLOAT(debugFrameBudgetMs, "debug.frame_budget_ms", 16.6667, 0.001, 10000.0,                                        \
          "Frame time a frame must stay under to be on budget.")                                                       \
    FLOAT(debugMaxOverBudgetRatio, "debug.max_over_budget_ratio", 0.05, 0.0, 1.0,                                      \
          "Fraction of over-budget frames above which the verdict fails.")                                             \
    FLOAT(debugLeakSlopeBytesPerFrame, "debug.leak_slope_bytes_per_frame", 1024.0, 0.0, 1.0e12,                        \
          "Retained growth per frame above which a leak is called.")                                                   \
    INT(debugAutoFlushFrames, "debug.auto_flush_interval_frames", 0, 0, 1000000000,                                    \
        "Write an interim report every N frames; 0 writes only at the end.")                                           \
    BOOL(debugExitOnComplete, "debug.exit_on_complete", false,                                                         \
         "End the process once `debug.target_frames` are captured.")
// clang-format on

namespace aura3d
{

enum class SettingType : u8
{
    Bool,
    Int,
    Float,
    Text,
    Choice,
};

//! Choices are stored as their lower-case spelling, so a comparison never has to fold case.
using SettingValue = std::variant<bool, i64, f64, std::string>;

/**
 * @brief A setting's name and slot. The type parameter is what get() returns.
 *
 * Bool for flags, int for whole numbers, double for fractions, std::string for text and choices.
 */
template <class T> struct Key
{
    std::string_view path;
    u16 index;
};

//! What the loader checks a value against.
struct SettingDesc
{
    std::string_view path;
    SettingType type;
    bool fallbackBool;
    i64 fallbackInt;
    f64 fallbackFloat;
    std::string_view fallbackText;
    f64 minimum;
    f64 maximum;
    std::span<const std::string_view> choices;
    //! Empty unless the default depends on how the library was built; then it says how, and the default above is
    //! the value for this build.
    std::string_view buildNote;
    std::string_view doc;
};

namespace detail
{

enum class SettingIndex : u16
{
#define AURA_SETTING_INDEX(id, ...) id,
    AURA_SETTINGS(AURA_SETTING_INDEX, AURA_SETTING_INDEX, AURA_SETTING_INDEX, AURA_SETTING_INDEX, AURA_SETTING_INDEX,
                  AURA_SETTING_INDEX, AURA_SETTING_INDEX)
#undef AURA_SETTING_INDEX
        Count
};

} // namespace detail

inline constexpr usize kSettingCount = static_cast<usize>(detail::SettingIndex::Count);

//! The typed keys.
namespace setting
{
// One inline constexpr variable per setting; the form decides the type get() returns.
#define AURA_KEY_BOOL(id, path, ...) inline constexpr Key<bool> id{path, static_cast<u16>(detail::SettingIndex::id)};
#define AURA_KEY_INT(id, path, ...) inline constexpr Key<int> id{path, static_cast<u16>(detail::SettingIndex::id)};
#define AURA_KEY_FLOAT(id, path, ...) inline constexpr Key<double> id{path, static_cast<u16>(detail::SettingIndex::id)};
#define AURA_KEY_TEXT(id, path, ...)                                                                                   \
    inline constexpr Key<std::string> id{path, static_cast<u16>(detail::SettingIndex::id)};
AURA_SETTINGS(AURA_KEY_BOOL, AURA_KEY_BOOL, AURA_KEY_INT, AURA_KEY_FLOAT, AURA_KEY_TEXT, AURA_KEY_TEXT, AURA_KEY_TEXT)
#undef AURA_KEY_BOOL
#undef AURA_KEY_INT
#undef AURA_KEY_FLOAT
#undef AURA_KEY_TEXT
} // namespace setting

//! Every setting, in declaration order; a Key's index is its position.
[[nodiscard]] std::span<const SettingDesc> settingsSchema() noexcept;

//! Null for a path no setting has.
[[nodiscard]] const SettingDesc *findSetting(std::string_view path) noexcept;

//! The candidate nearest to @p word, or empty when none is close: the "did you mean" of a typo.
[[nodiscard]] std::string_view closestMatch(std::string_view word, std::span<const std::string_view> candidates);

//! closestMatch() over every declared path.
[[nodiscard]] std::string_view suggestSetting(std::string_view path);

//! The default as a value of the setting's type.
[[nodiscard]] SettingValue settingDefault(const SettingDesc &desc);

//! JSON Schema (draft-07) for the settings file: editors complete from it and flag a misspelt key.
[[nodiscard]] std::string settingsJsonSchema();

//! The reference page for every setting, as Markdown tables.
[[nodiscard]] std::string settingsMarkdown();

} // namespace aura3d

#endif // AURA_SETTINGS_SCHEMA_H
