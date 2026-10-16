# 2. Project Configuration

Every setting is declared once, in `AURA_SETTINGS`
([`SettingsSchema.h`](../engine/include/aura/Core/Settings/SettingsSchema.h)).
The typed keys you read with, the validation, the [reference](settings-reference.md)
and the JSON Schema ([`aura.schema.json`](aura.schema.json)) are all generated from it.

```cpp
Engine engine("settings.json");         // file only
Engine engine(config);                  // code only — no settings.json anywhere
Engine engine(config, "settings.json"); // code, overridden by the file
```

## Layers

Later layers win, key by key. A value a layer cannot accept is reported and
leaves the layer beneath it standing.

| Layer | From | Example |
|---|---|---|
| built-in | the schema | — |
| code | `AuraConfig` | `config.set(setting::windowWidth, 1024)` |
| file | `settings.json` | `{ "window": { "width": 800 } }` |
| platform file | `settings.<linux\|windows\|macos\|ios\|android\|wasm>.json` beside it, only the keys that differ | `{ "renderer": { "api": "opengl" } }` |
| environment | `AURA_` + the path in capitals, dots as `__` | `AURA_WINDOW__WIDTH=800` |
| command line | `--set path=value`, after `config.arguments(argc, argv)` | `--set window.width=800` |
| runtime | `settings->set(key, value)` | — |

A missing `settings.json` is a warning, not an error; a missing platform file is silent.

## The file

```json
{
    "$schema": "./aura.schema.json",
    "schema_version": 1,
    "window":  { "backend": "sdl3", "width": 1280, "height": 720, "title": "My Game" },
    "present": { "mode": "fifo" },
    "renderer": {
        "api": "vulkan",
        "msaa": 4,
        "vulkan": { "validation": true, "gpu": "discrete", "vma": { "heap_block_mb": 128 } }
    },
    "audio":   { "backend": "auto", "volume": 1.0 },
    "paths":   { "textures": "./resources/textures/" },
    "logging": { "level": "info", "file": false }
}
```

Nested objects spell a path: `{ "window": { "width": 800 } }` is `window.width`.
Every key is optional. With `$schema` set, an editor completes keys, shows their
documentation and flags a misspelt one. Choices are lower case; the loader accepts any case.

The loader is strict about what it cannot use: an unknown key, a wrong type, a number
out of range or a choice that does not exist is logged with its path and, where one is
close, a suggestion, then ignored.

```text
'settings.json': window.wdth: no such setting; did you mean 'window.width'?
'settings.json': present.mode: 'Fifoo' is not one of fifo, fifo_relaxed, ...; did you mean 'fifo'?
```

`AuraSettings::diagnostics()` holds the same lines for a test or a tool.

Every key, its type, range and default: **[settings-reference.md](settings-reference.md)**.

## Reading

```cpp
const aura3d::AuraSettings *settings = engine.getSettings();

settings->get(aura3d::setting::windowWidth);   // int
settings->get(aura3d::setting::audioVolume);   // double
settings->get(aura3d::setting::pathTextures);  // std::string
settings->windowBackend();                     // wma::WindowBackend
settings->presentMode();                       // aura3d::VSyncMode
settings->audioBackend();                      // `auto` resolved
```

`get()` takes a typed `Key<T>`, so the wrong type is a compile error and a renamed
setting breaks every reader at build time. The helpers cover values that are not a plain
scalar or that resolve against the platform.

## Writing, in code

```cpp
aura3d::AuraConfig config;
config.set(aura3d::setting::windowTitle, "Viewer")
      .set(aura3d::setting::windowWidth, 1024)
      .set(aura3d::setting::windowBackend, wma::WindowBackend::SDL3)
      .set(aura3d::setting::rendererApi, "opengl");

Engine engine(config);   // ships as one binary; no settings.json to lose
```

Values are checked when loaded, exactly like a file's.

## Changing at run time

```cpp
aura3d::AuraSettings *settings = aura3d::AuraSettings::get();

settings->set(aura3d::setting::audioVolume, 0.5);   // false if refused

const u32 id = settings->subscribe([](std::string_view path) { /* "audio.volume" */ });
settings->unsubscribe(id);

settings->reload();       // sources again; runtime changes stand
settings->clearRuntime(); // drop them
```

Subsystems read their settings once, at startup. Subscribe to react to one afterwards.
Call `set()` and `reload()` from the thread that owns the engine.

## Adding a setting

One line in `AURA_SETTINGS`:

```cpp
INT(windowWidth, "window.width", 1280, 1, 16384, "Initial width in pixels.")
```

then regenerate the reference and schema:

```sh
cmake --build build/linux/debug --target update-settings-docs
```

`test_settings` fails when the committed copies are stale, and a default outside its own
range or choices does not compile.

## Custom sources

A layer is an `ISettingsSource`; the store runs its sources in order and each writes
through a validating `SettingsSink`.

```cpp
class RemoteConfig final : public aura3d::ISettingsSource {
public:
    std::string name() const override { return "remote config"; }
    void load(aura3d::SettingsSink &sink) override { sink.set("present.mode", std::string{"mailbox"}); }
};

aura3d::AuraConfig config;
config.addSource(std::make_shared<RemoteConfig>());   // after the platform file, before the environment
```

Next: **[03-engine-and-renderer.md](03-engine-and-renderer.md)**.
