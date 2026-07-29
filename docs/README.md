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

See also the [top-level README](../README.md) for the repository layout and
CMake option reference, and `apps/Sandbox/main.cpp` / `apps/OrgLogo/main.cpp`
for two complete, runnable programs these docs draw their examples from.
