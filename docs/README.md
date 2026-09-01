# Aura3D Documentation

A tutorial series covering every feature Aura3D has, in the order you'd
reach for them building a game. Start at 1 and work through, or jump
straight to whatever you need — each chapter links to the ones it builds on.

1. **[Getting Started](01-getting-started.md)** — build, run, the smallest
   possible Aura3D program.
2. **[Project Configuration](02-project-configuration.md)** — the complete
   `settings.json` reference: every key, its type, its default, and whether
   it's actually wired to behavior.
3. **[Engine & Renderer](03-engine-and-renderer.md)** — `Engine`,
   `IRenderer`, backend resolution/fallback, the run loop, present modes,
   switching backends at runtime.
4. **[Camera](04-camera.md)** — perspective and orthographic projections,
   target mode vs. free-look mode.
5. **[Meshes, Materials & Textures](05-meshes-materials-textures.md)** —
   loading OBJ files and images, built-in primitives, `ResourceManager`
   caching, the `Material` model, handles.
6. **[Lighting](06-lighting.md)** — the single-directional-light model and
   the ambient/intensity knobs.
7. **[2D Rendering & Text](07-2d-rendering-and-text.md)** — the batched 2D
   overlay pipeline, `TextOverlay`, how the glyph atlas stays cheap.
8. **[Input](08-input.md)** — keyboard/mouse contexts, held-key patterns,
   per-frame timing.
9. **[Building a Game](09-building-a-game.md)** — capstone: a small
   playable "collect the orbs" scene built from every chapter above.
10. **[Platform Builds](10-platform-builds.md)** — Linux, Android, and
    WebAssembly: presets, scripts, per-platform backend restrictions.
11. **[Using a Release](11-using-releases.md)** — consuming the prebuilt
    release archives as a dependency, per platform, plus how to build the
    `vulkan-dev` dev container from `qt_dev`.
12. **[Immediate-Mode UI](12-immediate-mode-ui.md)** — `aura3d::ui`: panels,
    buttons, checkboxes and sliders for debug overlays, editors and standalone
    tools, per-component theming, and a complete worked tool application.
    Builds on chapter 7's 2D pipeline, and reads well straight after it.
13. **[How AuraUI Works](13-auraui-internals.md)** — the inside of chapter 12:
    widget identity, the hot/active state machine, how a Part's style reaches a
    widget, how the whole UI stays one draw call, and how to add a widget.
14. **[Audio](14-audio.md)** — `AudioEngine`: loading WAV/Ogg clips, one-shots
    and looping music, 3D positional sound, voice management, and the
    per-platform backends (ALSA, SDL3, null).
15. **[Debug & Benchmark Mode](15-debug-benchmark-mode.md)** — the
    `AURA_ENABLE_DEBUG_MODE` build: CPU and GPU allocation tracking, per-phase
    frame timing across all three backends, and one JSON report with a
    pass/fail verdict for CI. Read it when a change might have regressed
    performance or leaked memory.

See also the [top-level README](../README.md) for the repository layout and
CMake option reference, and `apps/Sandbox/main.cpp` / `apps/OrgLogo/main.cpp`
for two complete, runnable programs these docs draw their examples from —
Sandbox's own `aura3d::ui` panels are this chapter's worked example running
live, alongside its camera, lighting and audio demos.
