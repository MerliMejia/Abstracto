<div align="center" style="margin-bottom:10px">
    <img src="assets/gallery/logo2.jpg" alt="Abstracto logo" height="200" />
</div>

<p align="center">
  <a href="https://github.com/MerliMejia/Abstracto/wiki"><img src="https://img.shields.io/badge/wiki-open-181717?style=for-the-badge&logo=github" alt="Wiki" /></a>
  <img src="https://img.shields.io/badge/status-experimental-E67E22?style=for-the-badge" alt="Status experimental" />
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++20" />
  <img src="https://img.shields.io/badge/CMake-3.20%2B-064F8C?style=for-the-badge&logo=cmake&logoColor=white" alt="CMake 3.20+" />
  <img src="https://img.shields.io/badge/Vulkan-pass--oriented-A41E22?style=for-the-badge&logo=vulkan&logoColor=white" alt="Vulkan pass oriented" />
  <img src="https://img.shields.io/badge/Shaders-Slang-3A6EA5?style=for-the-badge" alt="Slang shaders" />
</p>

<p align="center">
  <img src="https://img.shields.io/github/stars/MerliMejia/Abstracto?style=flat-square&logo=github" alt="GitHub stars" />
  <img src="https://img.shields.io/github/last-commit/MerliMejia/Abstracto?style=flat-square&logo=github" alt="Last commit" />
</p>

Abstracto is a Vulkan renderer that turns the API's moving parts into named, reusable layers. It is built as a collection of abstractions, not a rigid framework: you can stay close to the metal with the backend contexts, or move upward into a pass-oriented renderer with scene passes, pass-owned uniforms, fullscreen post-process passes, model loading, PBR, tonemapping, debug views, and an ImGui control layer.

The name comes from the Spanish word for "abstract." That is the point of the project: learn Vulkan by breaking it into smaller systems that are easier to understand, reuse, and compose.

![Current Abstracto render](assets/gallery/current_render.jpg)

## Why Abstracto?

- Thin bootstrap path: get a window, Vulkan device, swapchain, command buffers, and frame sync without committing to a full engine.
- Layered renderer API: move from `VulkanBackend` into `RenderPass`, `PassRenderer`, `SceneRenderPass`, `UniformSceneRenderPass`, and `FullscreenRenderPass`.
- Real sample app on `main`: the current `src/main.cpp` assembles `GeometryPass -> PbrPass -> TonemapPass -> DebugPass -> ImGuiPass`.
- Asset pipeline included: OBJ and glTF loading, textures, samplers, material bindings, HDR environment maps, and image-based lighting utilities.
- Built to be studied: the wiki walks from a triangle on the swapchain to pass-owned uniforms and fullscreen source-pass sampling.

### Abstraction ladder

| Layer                   | What it gives you                                                                                       |
| ----------------------- | ------------------------------------------------------------------------------------------------------- |
| Bootstrap               | `AppWindow`, `BackendConfig`, `VulkanBackend`, `FrameState`                                             |
| Backend contexts        | `InstanceContext`, `SurfaceContext`, `DeviceContext`, `SwapchainContext`, `CommandContext`, `FrameSync` |
| Geometry and assets     | `Mesh`, `FullscreenMesh`, `RenderableModel`, `ObjModelAsset`, `GltfModelAsset`                          |
| Materials and resources | `Texture`, `Sampler`, `DescriptorBindings`, `FrameGeometryUniforms`, `ImageBasedLighting`               |
| Rendering core          | `RenderPass`, `RenderItem`, `PassRenderer`, `PipelineSpec`, `ShaderProgram`                             |
| High-level pass types   | `SceneRenderPass`, `UniformSceneRenderPass`, `FullscreenRenderPass`                                     |
| Concrete features       | `GeometryPass`, `PbrPass`, `TonemapPass`, `DebugPass`, `ImGuiPass`                                      |

For the full breakdown, see the wiki page: [Current abstractions in the project](https://github.com/MerliMejia/Abstracto/wiki/Current-abstractions-in-the-project).

## What The Main Branch Shows Right Now

- A Vulkan backend with swapchain lifecycle, command submission, and frame synchronization.
- A pass chain that builds a geometry buffer, runs fullscreen PBR lighting, tonemaps the result, exposes debug outputs, and overlays an ImGui UI.
- Runtime model loading from `assets/models`, including glTF and OBJ examples.
- Material editing, camera movement, output switching, tonemap controls, light tuning, and IBL bake controls in the debug UI.
- Slang shaders compiled to SPIR-V, with generated `.spv` files already checked into `assets/shaders`.

## Quick Start

### Requirements

- CMake 3.20+
- A C++20 compiler
- A Vulkan SDK or Vulkan loader/runtime available to CMake
- `slangc` if you want CMake to regenerate SPIR-V from `.slang` files
- Git if you want CMake to auto-fetch missing dependencies

### Build

```bash
cmake -S . -B build -DABSTRACTO_FETCH_DEPS=ON
cmake --build build -j4
./build/Abstracto
```

`ABSTRACTO_FETCH_DEPS` is enabled by default and can fetch these libraries when they are missing:

- GLFW
- GLM
- stb
- tinyobjloader
- tinygltf
- Dear ImGui

If `slangc` is not installed, the current checked-in `.spv` files still let the project build. You only need `slangc` when you add or modify Slang shaders and want CMake to regenerate them automatically.

## Pick Your Level

- Want only a clean Vulkan bootstrap? Start with `AppWindow`, `BackendConfig`, and `VulkanBackend`.
- Want direct control over the low-level setup? Work from the backend contexts in `src/backend`.
- Want to route meshes through reusable render stages? Build on `RenderPass`, `RenderItem`, and `PassRenderer`.
- Want pass-owned uniform buffers and optional push constants? Extend `UniformSceneRenderPass<TUniform, TPush>`.
- Want a post-process that samples another pass? Extend `FullscreenRenderPass`.
- Want the most complete reference in this repo? Read `src/main.cpp` and follow the passes downward.

## Learning Path

The wiki is one of the best parts of this repo. It does not just explain what classes exist; it shows how the abstractions are meant to be used.

| Step | Focus                                             | Wiki                                                                                                                                                                              | Branch                                    |
| ---- | ------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------- |
| 0    | Bootstrap the smallest window + backend setup     | [How To](<https://github.com/MerliMejia/Abstracto/wiki/How-To-(Work-In-Progress)>)                                                                                                | `example-minimal-code`                    |
| 1    | Learn the abstraction map                         | [Current abstractions in the project](https://github.com/MerliMejia/Abstracto/wiki/Current-abstractions-in-the-project)                                                           | `main`                                    |
| 2    | Render a triangle straight to the swapchain       | [Triangle to the Swapchain Tutorial](https://github.com/MerliMejia/Abstracto/wiki/Triangle-to-the-Swapchain-Tutorial)                                                             | `tutorial-triangle-to-swapchain`          |
| 3    | Add a pass-owned uniform buffer and push constant | [Animated Triangle with a Pass-Owned Uniform or Push Constant](https://github.com/MerliMejia/Abstracto/wiki/Animated-Triangle-with-a-Pass%E2%80%90Owned-Uniform-or-Push-Constant) | `tutorial-animated-triangle-to-swapchain` |
| 4    | Feed one pass into a fullscreen post-process      | [Fullscreen Post-Process over a Source Pass](https://github.com/MerliMejia/Abstracto/wiki/Fullscreen-Post%E2%80%90Process-over-a-Source-Pass)                                     | `tutorial-fullscreen-post-process`        |

Wiki home: [Abstracto Wiki](https://github.com/MerliMejia/Abstracto/wiki)

## Cool things about this project

- It treats "abstraction" as something you can inspect instead of something hidden behind a giant engine wall.
- It keeps the low-level Vulkan pieces visible enough to learn from, while still giving you higher-level rendering building blocks.
- It uses the same pass model for simple tutorial triangles and for the more complete PBR/debug pipeline on `main`.

If you want a renderer that is small enough to read, but structured enough to grow, this is a repo to study.
