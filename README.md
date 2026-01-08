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

### Folder Structure

Aura3D/
├── apps/                   # The actual executable(s) / The Game
│   └── Sandbox/            # Your "Start doing the game" code
│       ├── main.cpp
│       └── settings.json
│
├── resources/              # Runtime resources
│   ├── shaders/
│   │   ├── gl/
│   │   └── vk/
│   └── textures/
│
├── engine/                 # The Engine Library
│   ├── include/            # PUBLIC HEADERS (The Game includes these)
│   │   └── aura/           # Namespace folder
│   │       ├── Core/       # Logger, Assertions, Config, Math
│   │       ├── Window/     # WindowManager abstract classes
│   │       ├── Renderer/   # Renderer abstract base class, Vertex structs
│   │       ├── Utils/      # Generic utilities
│   │       └── aura.h      # The main include file
│   │
│   └── src/                # PRIVATE SOURCE (Hidden logic)
│       ├── Core/           # Config loader implementation
│       ├── Window/         # SDL2 Window implementation
│       │
│       ├── Renderer/       # The Render Logic
│       │   ├── Common/     # Shared internal helpers
│       │   ├── CPU/        # Was "CpuAura" (CpuFrameBuffer, etc.)
│       │   ├── OpenGL/     # Was "GlAura" (GLShader, GLBuffers)
│       │   └── Vulkan/     # Was "VkAura" (VkDevice, VkSwapchain)
│       │
│       └── Utils/          # Implementation of utils
│
├── vendor/                 # Third-party libraries (Don't touch these)
│   ├── glad/
│   ├── glm/
│   ├── microui/            # Moved from "portables"
│   └── json/
│
└── CMakeLists.txt          # Root CMake

- **Public vs. Private** (include vs src): When you build your game, you will tell CMake to target_include_directories(MyGame PRIVATE engine/include). This prevents your game code from accidentally including internal headers like VkDeviceManager.h. Your game should rely on the Engine, not the Vulkan API.
- **Scalability**: If you decide later to add Audio, you just add an Audio/ folder in src and include. The structure grows horizontally without becoming a mess.
- **Backend Isolation**: If you are working on the OpenGL renderer, you only look inside src/Renderer/OpenGL. You don't have to see Vulkan files or CPU files.
- **Vendor Isolation**: It makes compiling easier. You can set specific compiler warnings for engine/src (strict) and different ones for vendor/ (permissive), so third-party warnings don't clutter your build log.

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
