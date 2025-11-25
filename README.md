# Aura3D

High performance 3D engine.

### Project Overview

This project is the foundation of a future **cross-platform, configurable game engine**.
Currently, it implements an **incomplete rendering backend** that supports **Vulkan**, **OpenGL**, and **CPU-based (software) rendering**. The long-term goal is to unify these rendering paths under a **single backend abstraction layer**, enabling the same rendering logic to run on either the CPU or GPU.

---

### Core Design Goal

Create a **unified rendering interface** where high-level drawing functions (e.g., `drawPolygon()`, `drawMesh()`, `drawTexture()`) can operate independently of the underlying graphics API or hardware.
This means:

* The same code path should work whether the engine is using Vulkan, OpenGL, or a software rasterizer.
* The backend will determine *how* to execute the draw call (GPU command buffers, OpenGL draw calls, or CPU rasterization).

---

### Functional Objectives

1. **Abstraction Layer**

   * Define a unified rendering interface (`IRenderer`, `IDeviceContext`, etc.).
   * Implement backend-specific modules for:

     * Vulkan (GPU)
     * OpenGL (GPU)
     * CPU (software rasterizer)
   * Each backend must implement the same interface methods for initialization, buffer management, and rendering primitives.

2. **Cross-Backend Resource Model**

   * Create unified structures for:

     * Vertex/Index buffers
     * Textures and framebuffers
     * Shaders (or CPU equivalent functions)
   * Handle memory management consistently across backends.

3. **Render Pipeline Architecture**

   * Abstract pipeline states (blend, depth, rasterization, shader stages) to allow shared configuration between APIs.
   * On CPU backend, simulate these states in software.

4. **Scene Management**

   * Design a lightweight render graph or scene graph system that feeds unified draw calls into the backend.
   * Ensure all entities are independent from the rendering API.

5. **Configuration System**

   * Implement a JSON-based or script-based configuration file to define:

     * Rendering backend (`vulkan`, `opengl`, or `cpu`)
     * Mode (`2D` or `3D`)
     * Resolution, VSync, and runtime parameters
   * Make backend and dimensionality fully switchable through configuration.

6. **Platform Independence**

   * Use **SDL2** or **GLFW** for window and input handling (already partially implemented).
   * Ensure the engine builds and runs on Windows, Linux, and Android (macOS optional).

---

### Future Expansion Roadmap

* Implement unified **math library** (vectors, matrices, transforms).
* Introduce **entity/component system** decoupled from the renderer.
* Add **physics** and **scripting** layers (optional for now).
* Create **debug visualization tools** for CPU/GPU backend comparison.
* Develop a **resource compiler** for converting assets into engine-ready formats.

---

### Vision Summary

The ultimate objective is a **modular, configurable game engine** capable of running the same rendering logic on both software and GPU pipelines, adaptable between 2D and 3D with simple configuration changes.
All rendering, resource management, and engine logic will share a common interface, making the system extensible, testable, and maintainable.
