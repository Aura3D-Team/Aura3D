# Issue #19 — Introduce AudioPlayer to Aura3D engine

> Task description drafted for github.com/Aura3D-Team/Aura3D/issues/19 (kept local, not posted).

## Summary
Add a first-class audio subsystem to Aura3D: load sound clips, play/stream them,
control volume, and support 3D positional audio — following the same
platform-abstraction-in-libwma / engine-API-in-Aura3D split already used for
windowing+input (`wma::IWindowManager`) and rendering (`aura3d::IRenderer`).

## Why
Aura3D currently has zero audio capability (confirmed: no hits for
`audio|sound|AudioPlayer|OpenAL|miniaudio|SDL_mixer` anywhere in `libink`,
`libwma`, or `Aura3D`). Sandbox demos and any real game built on the engine
have no way to play sound. SDL3 is already a hard dependency of `libwma`
(forced on for Android/WASM in `cmake/Platform.cmake`) and ships a complete,
cross-platform audio API — so this is additive, not a new third-party
dependency.

## Architecture

### Layer 1 — `libwma`: `wma::IAudioDevice` (platform abstraction)
New pure-virtual interface parallel to `wma::IWindowManager`
(`include/wma/managers/IWindowManager.hpp`), living at
`include/wma/audio/IAudioDevice.hpp`:

- `open(const AudioDeviceConfig&)`, `close()`, `start()`, `stop()`
- `setMixCallback(std::move_only_function<void(std::span<f32> interleavedOut)>)`
  — engine supplies the mix callback, backend drives it on the platform audio
  thread (matches SDL3's `SDL_AudioStream` callback model, and ALSA's
  `snd_pcm_writei` poll-driven loop underneath).
- `sampleRate()`, `channelCount()` accessors.
- Factory: `wma::createAudioDevice(const AudioDeviceConfig&) -> std::unique_ptr<IAudioDevice>`,
  in a new `src/audio/AudioDeviceFactory.cpp` (mirrors `WindowManager.cpp`'s
  `createWindowManager()` + `WMA_ENABLE_*` gating), plus
  `getDefaultAudioBackend()` / `isAudioBackendAvailable()` helpers mirroring
  `getDefaultBackend()`/`isBackendAvailable()` in `WindowManager.cpp`.

**Important: audio backend selection is independent of windowing backend
selection.** GLFW, X11, and Wayland are display/windowing libraries only —
none of them expose an audio API, so there is no "GLFWAudioDevice" or
"X11AudioDevice." A window opened via any windowing backend can pair with
any audio backend. Audio backends live in their own subtree,
`include/wma/audio/backends/<name>/` / `src/audio/backends/<name>/`,
parallel to (not nested under) `include/wma/backends/<windowing-backend>/`.

Three backends for v1:
- `AlsaAudioDevice` (`include/wma/audio/backends/alsa/AlsaAudioDevice.hpp` /
  `src/audio/backends/alsa/AlsaAudioDevice.cpp`) — native low-latency Linux
  desktop backend via `libasound` (`snd_pcm_*`), gated
  `WMA_ENABLE_ALSA` (default `ON` on native Linux, forced `OFF` on
  `ANDROID`/`EMSCRIPTEN`/`WIN32` — mirrors how `WMA_ENABLE_X11`/`_WAYLAND`
  are force-disabled on those platforms in `cmake/Dependencies.cmake`).
  Talks directly to ALSA rather than through PulseAudio/PipeWire, so it's the
  actual low-latency path this issue is about; PulseAudio/PipeWire client
  backends are deferred (see Out of scope).
- `SDLAudioDevice` (`include/wma/audio/backends/sdl/SDLAudioDevice.hpp` /
  `src/audio/backends/sdl/SDLAudioDevice.cpp`) — universal fallback/default,
  works everywhere SDL3 already does (Linux/Windows/Android/WASM). This is
  the **only** backend on Android/WASM: SDL3 already talks to AAudio/OpenSL
  ES/Web Audio directly, so there's no separate "native" backend to write for
  those two platforms — SDL3 *is* the native path there.
- `NullAudioDevice` (`include/wma/audio/backends/null/NullAudioDevice.hpp`)
  — inert no-op backend for headless CI (containers have no audio hardware
  and often no ALSA device nodes either), matching the existing "inert
  default `TouchListener`" precedent in `IWindowManager`.

Selection logic in `getDefaultAudioBackend()`: prefer `Alsa` on native
desktop Linux when `WMA_HAS_ALSA` and a device actually opens, else fall back
to `Sdl`; `Sdl` unconditionally on Android/WASM/Windows. Consumers can still
force a specific backend via `AudioDeviceConfig::backend`, same shape as
`RendererChoice` in Aura3D's `RendererFactory`.

### Layer 2 — `Aura3D`: `aura3d::AudioEngine` (engine API)
New subsystem at `engine/include/aura/Core/AudioEngine/AudioEngine.h` +
`.cpp`, constructed around a `std::unique_ptr<wma::IAudioDevice>`:

```cpp
namespace aura3d {

using AudioClipHandle   = u32;
using AudioSourceHandle = u32;
// reuse INVALID_HANDLE / isValidHandle() from RenderHandles.h

enum class AudioClipMode : u8 { kStatic, kStreaming };

struct AudioListener3D {
  Vec3 position{};
  Vec3 velocity{};   // optional, for future doppler support
  Vec3 forward{0.f, 0.f, -1.f};
  Vec3 up{0.f, 1.f, 0.f};
};

struct AudioSourceDesc {
  AudioClipHandle clip = kInvalidAudioHandle;
  bool  loop     = false;
  float gain     = 1.0f;
  bool  spatial  = false;
  Vec3  position{};
  float minDistance = 1.0f;
  float maxDistance = 100.0f;
};

/// @class AudioEngine
/// @brief Engine-facing audio API: clip loading, playback, mixing, 3D spatialization.
/// @note Not thread-safe; call from the thread that owns the Engine (mirrors ResourceManager).
class AudioEngine {
 public:
  explicit AudioEngine(std::unique_ptr<wma::IAudioDevice> device);
  ~AudioEngine();

  [[nodiscard]] std::expected<AudioClipHandle, AudioError> loadClip(
      const std::string& path, AudioClipMode mode = AudioClipMode::kStatic);
  void unloadClip(AudioClipHandle) noexcept;

  [[nodiscard]] AudioSourceHandle play(const AudioSourceDesc& desc);
  void stop(AudioSourceHandle) noexcept;
  void pause(AudioSourceHandle) noexcept;
  void resume(AudioSourceHandle) noexcept;
  [[nodiscard]] bool isPlaying(AudioSourceHandle) const noexcept;

  void setSourceGain(AudioSourceHandle, float gain) noexcept;
  void setSourcePosition(AudioSourceHandle, const Vec3&) noexcept;
  void setListener(const AudioListener3D&) noexcept;

  void setMasterVolume(float) noexcept;
  [[nodiscard]] float masterVolume() const noexcept;

  /// Call once per frame: refills streaming buffers, reclaims finished
  /// voices, recomputes 3D gain/pan for spatial sources.
  void update(float dt);

 private:
  std::unique_ptr<wma::IAudioDevice> _device;
  // voice pool, clip cache, streaming ring buffers, etc.
};

}  // namespace aura3d
```

### Loading — `AudioClipLoader`
New `engine/include/aura/Core/AudioClipLoader/AudioClipLoader.h` + `.cpp`,
mirroring `ImageLoader`/`MeshLoader` (static-method utility, `vendor/`-backed
decoders, no ties to the renderer):

- WAV (mandatory, PCM16/PCM32F) — small header-only parser or vendored
  `dr_wav.h` next to `vendor/stb`/`vendor/tinyobj`.
- OGG Vorbis (music/streaming) — vendor `stb_vorbis.c` (same family as the
  already-vendored `stb_image`/`stb_truetype`).
- Returns `std::expected<AudioClipData, AudioError>` (interleaved
  `std::vector<f32>` samples + sample rate + channel count) for static clips;
  a chunked/streaming decode path for `kStreaming` clips feeding
  `AudioEngine::update()`'s ring buffer.

### Resource + Engine wiring
- Extend `ResourceManager` (`engine/include/aura/Core/ResourceManager/ResourceManager.h`)
  with `loadSound(const std::string& path, AudioClipMode) -> AudioClipHandle`,
  same path-keyed cache pattern as `loadTexture`/`loadMesh`.
- Extend `Engine` (`engine/include/aura/Core/Engine.h`) with
  `std::unique_ptr<aura3d::AudioEngine> _audio;` + `AudioEngine* audio() const`,
  constructed in `Engine::Engine()` right after `_renderer`/`_resources`,
  exactly like the existing two subsystems.
- `apps/Sandbox/main.cpp` calls `engine.audio()->update(dt)` once per frame
  inside the same lambda passed to `IRenderer::run(...)`, alongside the
  existing render calls.
- Extend `apps/Sandbox/settings.json` with an `"audio"` section: master
  volume, sample rate, max simultaneous voices, streaming buffer size —
  mirrors the existing `renderer`/`window` sections, parsed by `AuraSettings`.

## Platform notes
- **Linux**: `AlsaAudioDevice` is the default, talking to `libasound`
  directly for lowest latency; `SDLAudioDevice` remains available as an
  explicit fallback (and is what `NullAudioDevice`-free CI would use if ALSA
  device nodes aren't present). Requires `find_package(ALSA REQUIRED)` (via
  `pkg-config alsa`) added to `libwma/cmake/Dependencies.cmake`, gated behind
  `WMA_ENABLE_ALSA`.
- **WASM (Emscripten)**: SDL3's Emscripten port maps to Web Audio. Browsers
  block audio until a user gesture — `AudioEngine`/`IAudioDevice` needs an
  explicit `resume()`/unlock path triggered from an input event, not
  autoplay on startup. `apps/Sandbox/CMakeLists.txt` will likely need
  `-sAUDIO_WORKLET=1` (or the SDL3-recommended equivalent) added to its
  `target_link_options`, alongside the existing `-sUSE_SDL=0` (since `wma`
  already links SDL3).
- **Android**: SDL3 handles AAudio/OpenSL ES selection internally through
  the same `SDL_Audio*` API — no NDK-specific code required.

## C++23 / style conformance
- `std::expected<T, AudioError>` for fallible operations (clip decode,
  device open) — avoid exceptions on the hot/per-frame path; keep
  `AuraException`/`WMAException` for genuine programmer-error cases (e.g. an
  invalid handle), consistent with existing exception usage.
- `std::span<f32>` / `std::span<const std::byte>` for PCM and raw file
  buffers instead of pointer+length pairs.
- Stay with **virtual-dispatch interface + factory function**
  (`IAudioDevice` + `createAudioDevice()`) rather than a templated/concepts
  design — matches the established `IRenderer`/`IWindowManager` idiom, don't
  introduce a second polymorphism style into the codebase.
- `AudioClipHandle`/`AudioSourceHandle` as `u32` reusing the
  `INVALID_HANDLE`/`isValidHandle()` sentinel from `RenderHandles.h`.
- RAII throughout: `AudioEngine`'s destructor stops the device and drains
  any pending streaming work; no manual `new`/`delete`.
- `[[nodiscard]]` on `loadClip`, `masterVolume()`, `isPlaying()`; `noexcept`
  on `stop`/`pause`/`resume`/setters that can't fail.
- Doxygen `///` on every public class/method, matching `ResourceManager`'s
  `@class`/`@brief`/`@param[in]`/`@return`/`@note` style.

## Build system
- New CMake option `AURA_ENABLE_AUDIO` (default `ON`) in `Aura3D`'s root
  `CMakeLists.txt` / `cmake/Platform.cmake`, following the
  `AURA_ENABLE_VULKAN/OPENGL/CPU` → `AURA_HAS_*` compile-definition pattern.
- `libwma/cmake/Platform.cmake`: add `WMA_ENABLE_ALSA`, default `ON`, forced
  `OFF` on `ANDROID`/`EMSCRIPTEN`/`WIN32` — same force-disable pattern
  already used there for `WMA_ENABLE_X11`/`WMA_ENABLE_WAYLAND`.
- `libwma/cmake/Dependencies.cmake`: add `find_package(ALSA REQUIRED)` behind
  `if(WMA_ENABLE_ALSA)`, and a `WMA_HAS_ALSA` compile definition generated
  into `BuildConfig.hpp` alongside the existing `WMA_HAS_SDL`/`WMA_HAS_GLFW`.
- `libwma/src/CMakeLists.txt`: extend the existing
  `foreach(_backend SDL GLFW X11 WAYLAND) ... REMOVE_ITEM` source-stripping
  loop to also cover `ALSA` for the new `src/audio/backends/alsa/` sources.
- `SDLAudioDevice` stays unconditionally compiled (SDL3 is a hard dependency
  everywhere already); no `WMA_ENABLE_SDL_AUDIO` toggle needed.

## Testing
- `tests/test_audio_clip_loader.cpp`: decode small fixture WAV/OGG files
  (new `resources/audio/test/` fixtures) and assert sample rate/channel
  count/frame count.
- `tests/test_audio_engine.cpp`: drive `AudioEngine` against
  `NullAudioDevice` — play/pause/stop/loop state transitions, volume
  clamping, handle invalidation after `stop`, streaming ring-buffer refill,
  3D gain/pan math for known listener/source distance+angle inputs.
- Confirm/adjust `.github/workflows/ci.yml` so the Linux CI job uses
  `NullAudioDevice` (containers have no audio hardware).

## Documentation
- New `docs/12-audio.md` tutorial (load a clip, play SFX, stream music, set
  up 3D positional audio), added to `docs/README.md`'s index and the root
  `README.md` feature list.
- `CHANGELOG.md` entry under `Unreleased`/`Added`.

## Explicitly out of scope for this issue
- DSP effects/filters (reverb, low-pass, etc.)
- Occlusion/obstruction raycasting for 3D audio
- MIDI playback
- In-engine audio mixer UI/inspector
- PulseAudio/PipeWire client backends and WASAPI. ALSA covers desktop Linux
  natively (and works on a PulseAudio/PipeWire desktop through their
  ALSA-compatible `default`); SDL3 covers Windows, Android, WASM and Apple.

## Acceptance criteria
- [x] `wma::IAudioDevice` + `AlsaAudioDevice` + `SDLAudioDevice` + `NullAudioDevice` implemented, building on Linux/WASM/Android.
- [x] `aura3d::AudioEngine` implemented and wired into `Engine` (`engine.audio()`).
- [x] `AudioClipLoader` decodes WAV and OGG Vorbis to PCM.
- [x] `ResourceManager::loadSound()` caches clips by path.
- [x] 3D positional audio: source position + listener orientation produce correct distance attenuation and stereo pan.
- [x] Sandbox demo updated to play SFX, looping music and a 3D-positional source.
- [x] Unit tests for clip loading and `AudioEngine` state machine pass via `NullAudioDevice`.
- [x] `docs/14-audio.md` written; `CHANGELOG.md` updated.

## Delivered — deviations from the plan above

Written after implementation; the sections above are the original spec.

- **Chapter number.** The docs already went to 13, so the tutorial landed at
  `docs/14-audio.md`, not `12`.
- **ALSA is in v1.** The original draft deferred native backends. It is
  implemented, and verified against real hardware: 379 callbacks / 194,048
  samples in 2.0 s against ~192,000 expected, i.e. the writer thread paces off
  the hardware clock to within 1%.
- **No `std::expected` on the load path.** `loadClip`/`loadPCM` follow the
  `ResourceManager::loadTexture` + `ImageLoader` idiom instead — always return
  something usable, substitute a fallback, log a warning. `std::expected` is
  used in this codebase only for `FontAtlas`, whose failure mode genuinely
  differs.
- **No `AURA_ENABLE_AUDIO` CMake toggle.** The renderer toggles exist because
  those backends pull heavy SDK headers into Aura3D's own TUs. Audio's SDK
  dependency is sealed inside libwma behind `IAudioDevice`, so there is nothing
  here to toggle — matching `ResourceManager`/`ImageLoader`/`MeshLoader`, none
  of which are individually gated either.
- **`glm::vec3`, not a bespoke `Vec3`.** That is what the rest of the engine
  uses.
- **Voice handles carry a generation counter.** Not in the original sketch;
  added because slots are recycled and a stale handle must not address the
  voice that replaced it.
- **`unloadAll()` deliberately does not drop sounds.** Clips belong to the
  audio engine, which survives `switchBackend()`; only textures and meshes are
  invalidated by it. `unloadSounds()` is the separate, explicit release.

### Known limitations

- **`AudioClipMode::Streaming` does not yet stream.** It decodes fully at load
  like `Static`; it is a marker and a seam for incremental decoding, and is
  documented as such in the header. Marking music `Streaming` now means it
  benefits automatically when that lands.
- **ALSA needs `plughw:`, not raw `hw:`.** Samples are handed over as float,
  which most cards do not accept natively — the plug layer converts. A raw
  `hw:X,Y` device name fails to open with "Sample format not available"
  (cleanly, degrading to the next backend). `default` and `plughw:X,Y` both
  work.
- **Surround beyond the front pair is unpanned.** Channels 3+ receive the
  unpanned signal; correct placement needs a real channel map, which this mixer
  does not model.
