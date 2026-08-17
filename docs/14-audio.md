# 14. Audio

Aura3D plays sound through `aura3d::AudioEngine`, reached from
`Engine::audio()`. It loads WAV and Ogg Vorbis files, mixes any number of
sounds at once, and places them in 3D so a source pans and fades as the
listener moves.

Like the renderer, it degrades rather than failing: on a machine with no
sound hardware you get a silent device, every call still works, and nothing
needs a null check.

## The two halves

Audio is split across the two repositories, the same way windowing is:

| Layer | Lives in | Job |
|---|---|---|
| `wma::IAudioDevice` | **libwma** | Opens the platform device and runs the audio thread. ALSA, SDL3, or null. |
| `aura3d::AudioEngine` | **Aura3D** | Decodes clips, mixes voices, computes 3D pan and attenuation. |

Everything above the device is platform-independent arithmetic, so it lives
in the engine; talking to ALSA or SDL3 is a platform concern, so it lives in
libwma next to `IWindowManager`. You only ever touch `AudioEngine`.

**Audio and windowing backends are independent.** GLFW, X11 and Wayland are
display protocols with no audio API — there is no "X11 audio". An X11 window
alongside an ALSA device is a perfectly normal configuration, and
`window.backend` never constrains `audio.backend`.

## Playing a sound

```cpp
Engine engine("settings.json");
AudioEngine* audio = engine.audio();
ResourceManager* resources = engine.resources();

// Cached by path, exactly like loadTexture/loadMesh.
const AudioClipHandle blip = resources->loadSound("./resources/audio/blip.wav");

// One-shot at 80% volume.
(void)audio->play(blip, 0.8f);
```

`play()` returns an `AudioSourceHandle` — one *voice*, i.e. one playing
instance. The same clip can be playing many times at once, which is why
clips and voices are separate handles.

Call `update()` once per frame:

```cpp
r->run([&]() {
    const float dt = windowManager->getWindowFlags()->deltaTime / 1000.0f;
    audio->update(dt);
    // ... render
});
```

The mixing itself runs on the audio thread, driven by the device. `update()`
is bookkeeping — it reclaims voices that finished. Skip it and playback still
works, but finished voices stop being recycled and the pool eventually fills.

## Looping music

Music has no position in the scene, so leave `spatial` off and it plays at an
even level in both ears no matter where the camera goes:

```cpp
const AudioClipHandle music =
    resources->loadSound("./resources/audio/music.wav", AudioClipMode::Streaming);

AudioSourceDesc desc;
desc.clip = music;
desc.loop = true;
desc.gain = 0.35f;
const AudioSourceHandle musicVoice = audio->play(desc);
```

`AudioClipMode::Streaming` marks a clip as long-form. **It currently still
decodes the whole file at load time** — it differs in the playback path, not
in the load, and exists as the seam incremental decoding slots into. Marking
your music `Streaming` today means it picks that up for free later.

## 3D positional audio

Set `spatial` and give the source a position. Then keep the listener in sync
with the camera each frame — *both* position and orientation matter, since
panning is computed against the direction the listener faces:

```cpp
AudioSourceDesc desc;
desc.clip        = hum;
desc.loop        = true;
desc.spatial     = true;
desc.position    = orbPosition;
desc.minDistance = 1.5f;   // full volume inside this radius
desc.maxDistance = 14.0f;  // silent past it
const AudioSourceHandle voice = audio->play(desc);

// every frame, after the camera moves:
audio->setListener({.position = camera.position(),
                    .forward  = camera.forward(),
                    .up       = glm::vec3(0.0f, 1.0f, 0.0f)});

// and whenever the source moves:
audio->setSourcePosition(voice, object.position);
```

Two things happen to a spatial voice:

- **Distance attenuation** — full volume within `minDistance`, falling
  linearly to silence at `maxDistance`. Linear rather than the physically
  correct inverse-square, because it reaches actual zero at a defined
  distance, which lets far-away voices be skipped entirely.
- **Equal-power panning** — the gains follow a quarter circle so total power
  stays constant as a source sweeps across the stereo field. A linear
  crossfade would dip audibly in the middle.

**Spatialize mono clips.** A point in the world has one signal; direction is
expressed through per-channel gains, not baked into the file. A stereo clip
marked `spatial` still plays, but its existing channel content fights the
panning. `scripts/gen_sandbox_audio.py` writes mono for this reason.

## Controlling voices

```cpp
audio->pause(voice);                    // keeps its slot, stops contributing
audio->resume(voice);
audio->stop(voice);                     // frees the slot; handle dies
audio->setSourceGain(voice, 0.5f);
audio->setSourceLooping(voice, false);  // let a looping voice end naturally

audio->setMasterVolume(0.5f);           // clamped to [0, 1], applied last
audio->stopAll();

if (audio->isPlaying(voice)) { /* ... */ }
```

Voice handles are **recycled**. When a sound finishes, its slot is reused by
a later `play()`, and a handle from before that point is retired — a stale
handle reads as "not playing" and cannot stop or modify whatever took its
place. Check with `isPlaying()` rather than assuming a handle held across
frames is still live.

When every voice is busy, `play()` returns an invalid handle and the sound is
dropped. That is deliberate: cutting off something already audible to make
room is more noticeable than one missing sound. Raise `audio.max_voices` if
you hit it.

## Missing files

A missing or undecodable file yields a short **silent** clip and a warning,
so a broken path costs silence rather than a crash — the audio counterpart of
the magenta checkerboard a missing texture gets. It is deliberately not an
attention-grabbing noise: a failed sound effect substituting a buzz would be
worse than the silence it replaced.

```cpp
// Always returns a usable handle.
const AudioClipHandle clip = resources->loadSound("typo-in-this-path.wav");
```

## Supported formats

| Format | Notes |
|---|---|
| **WAV** | 8/16/24/32-bit PCM and 32/64-bit IEEE float, any channel count. Parsed in-tree. |
| **Ogg Vorbis** | Via the vendored `stb_vorbis`. Use it for music — far smaller than WAV. |

The format is chosen by sniffing the file's magic bytes, not its extension.
Compressed WAV variants (ADPCM, mu-law) are not supported; use Ogg instead.

Clips are resampled to the device's rate once at load, so a 44.1 kHz file on
a 48 kHz device plays at the right pitch with no per-sample cost at playback.

## Generated audio

`AudioClipLoader` can synthesize clips, which is handy for tests and for
placeholder audio before real assets exist:

```cpp
const AudioClipData tone = AudioClipLoader::makeSineTone(440.0f, 1.0f, 0.25f);
const AudioClipHandle handle = audio->createClip(tone);
```

`makeSilence()` is the fallback used for missing files, and is also useful as
a deliberate no-op clip.

## Configuration

The `audio` section of `settings.json` (see
[Project Configuration](02-project-configuration.md)):

```json
"audio": {
    "backend": "auto",
    "master_volume": 1.0,
    "sample_rate": 48000,
    "channels": 2,
    "buffer_frames": 1024,
    "max_voices": 32
}
```

| Key | Default | Meaning |
|---|---|---|
| `backend` | `"auto"` | `auto`, `alsa`, `sdl3`, or `null`. `auto` picks ALSA on desktop Linux, SDL3 elsewhere. |
| `master_volume` | `1.0` | Clamped to `[0, 1]`. |
| `sample_rate` | `48000` | The native rate of nearly all modern hardware. |
| `channels` | `2` | Stereo. Panning needs two. |
| `buffer_frames` | `1024` | Latency dial: ~21 ms at 48 kHz. Lower is tighter but risks dropouts. |
| `max_voices` | `32` | Simultaneous sounds before `play()` starts dropping. |

Plus `paths.audio` (default `./resources/audio/`), read via
`settings->getAudioPath()`.

The device may not grant exactly what you ask for. Read back what it actually
did:

```cpp
INK_INFO << audio->sampleRate() << " Hz, " << audio->channelCount() << " ch, "
         << wma::audioBackendName(audio->backend());
```

## Backends per platform

| Platform | Default | Notes |
|---|---|---|
| **Linux** | ALSA | Lowest latency; talks to the kernel PCM directly. Works on a PulseAudio/PipeWire desktop, which both expose an ALSA-compatible `default`. |
| **Windows** | SDL3 | WASAPI underneath. |
| **Android** | SDL3 | AAudio/OpenSL ES underneath. |
| **WASM** | SDL3 | Web Audio underneath. See the browser note below. |
| **macOS/iOS** | SDL3 | CoreAudio underneath. |
| *any* | Null | Always available. Silent, never fails — CI and headless. |

If the preferred backend cannot open a device, libwma degrades automatically
(ALSA → SDL3 → Null) and logs which one it settled on. You never get a
failure you have to handle.

### Browsers need a user gesture

Browsers refuse to start audio until the page has seen a click, key or touch.
The device opens and reports success, but stays suspended. Call `resumeDevice()`
from an input handler:

```cpp
keyboard.addKeyAction(wma::KEY_SPACE, wma::KeyAction{
    [&]() { audio->resumeDevice(); },
    []() {}
});
```

It is a no-op once running, and on every other platform, so it is safe to
wire unconditionally.

## Try it

`apps/Sandbox` is a working example: looping music, a 3D-positional hum on
the centre orb, and a one-shot on **E** (**M** toggles mute). Walk around the
orb with WASD and you can hear it pan and fade.

Generate the demo audio first — the WAVs are produced, not checked in:

```bash
python3 scripts/gen_sandbox_audio.py
```

## Threading

`AudioEngine`'s public API is safe to call from the game thread while the
audio thread mixes; the shared state sits behind short locks. Drive it from
the thread that owns the `Engine` — two threads calling the API concurrently
is not supported.

Audio outlives a renderer backend switch. `Engine::switchBackend()` leaves it
alone, so music keeps playing and clip handles stay valid — which graphics API
is drawing has nothing to do with which device is playing sound.
