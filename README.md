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

Abstracto is a Vulkan renderer that turns the API's moving parts into named, reusable layers. It is built as a collection of abstractions, not a rigid framework. The point of the project is to keep Vulkan explicit while organizing the renderer into named abstraction layers that can be studied one by one.

## What this repo is for

- learning Vulkan through explicit layers instead of one large engine
- studying a pass-based renderer architecture
- building renderer features on top of reusable pass abstractions
- using small examples and tutorials to climb from simple rendering to multi-pass post-process workflows

## Implemented so far

- Vulkan window, device, swapchain, command-buffer, and frame-sync bootstrap
- Separated backend contexts for instance, surface, device, swapchain, commands, and synchronization
- Pass-based renderer architecture
- Reusable render-pass abstractions
- `RenderPass`
- `RasterRenderPass`
- `SceneRenderPass`
- `UniformSceneRenderPass`
- `FullscreenRenderPass`
- Pass chaining with `PassRenderer`
- Offscreen render targets that can be sampled by later passes
- Pass-owned uniform buffers
- Pass-owned sampled image bindings
- Optional push constants in passes
- Mesh GPU upload and draw submission through `RenderItem`
- Fullscreen quad mesh helper
- Texture loading and sampler abstractions
- Material descriptor bindings
- Image-based lighting resources and baking support
- Geometry pass
- Shadow pass
- Fullscreen lighting pass
- PBR pass
- Tonemap pass
- Debug present pass
- Debug overlay pass
- ImGui pass
- Renderer-side debug visualization for light markers
- Renderer-side skin palette bindings and skinning-related draw data
- Slang shader pipeline with checked-in SPIR-V outputs

## Current abstraction ladder

| Layer | Main types |
| --- | --- |
| Bootstrap | `AppWindow`, `BackendConfig`, `VulkanBackend`, `FrameState` |
| Backend contexts | `InstanceContext`, `SurfaceContext`, `DeviceContext`, `SwapchainContext`, `CommandContext`, `FrameSync` |
| Geometry and resources | `Mesh`, `Texture`, `Sampler`, `DescriptorBindings`, `FrameGeometryUniforms`, `ModelMaterialSet` |
| Lighting resources | `ImageBasedLighting`, `ImageBasedLightingBaker` |
| Renderer core | `RenderPass`, `PassRenderer`, `PipelineSpec`, `ShaderProgram`, `PassUniformSet`, `PassImageSet` |
| High-level pass types | `SceneRenderPass`, `UniformSceneRenderPass`, `FullscreenRenderPass`, `MeshRenderPass`, `UniformMeshRenderPass` |
| Concrete passes | `GeometryPass`, `ShadowPass`, `LightPass`, `PbrPass`, `TonemapPass`, `DebugPresentPass`, `DebugOverlayPass`, `ImGuiPass` |

## Repo layout

- `src/renderer/backend`
  low-level Vulkan bootstrap and frame lifecycle
- `src/renderer/core`
  reusable renderer and pass abstractions
- `src/renderer/resources`
  meshes, textures, samplers, descriptor-backed resources
- `src/renderer/lighting`
  image-based lighting support and bake types
- `src/renderer/debug`
  renderer-side debug helper geometry
- `src/renderer/passes`
  concrete renderer features built on the core abstractions
- `resources/shaders`
  Slang shaders and checked-in SPIR-V outputs
- `apps/module_examples/renderer/main.cpp`
  current renderer example entry point

## Study path

- Start with the backend layer if you want the smallest Vulkan bootstrap.
- Move to `RenderPass`, `RasterRenderPass`, and `PassRenderer` if you want to understand the renderer core.
- Use `tutorial.md` for a step-by-step example of `UniformSceneRenderPass`, pass-owned uniforms, and optional push constants.
- Use `turorial2.md` for a step-by-step example of `FullscreenRenderPass` and sampled pass-to-pass post-processing.
- Use `ist.md` as a compact list of the renderer features implemented so far.

## Build

### Requirements

- CMake 3.20+
- a C++20 compiler
- a Vulkan SDK or Vulkan loader/runtime available to CMake
- `slangc` only if you want CMake to regenerate `.spv` files from `.slang`
- Git only if you want CMake to auto-fetch missing dependencies

### Build commands

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

If `slangc` is not installed, the checked-in `.spv` files still allow the project to build. You only need `slangc` when you add or modify Slang shaders and want CMake to regenerate them automatically.

## How to use the example target

The `Abstracto` executable currently builds from `apps/module_examples/renderer/main.cpp`.

That file is intentionally small and is meant to be replaced by following tutorials and experiments. The tutorials in this repo are written around that workflow: change the example entry point, build, run, study the next abstraction.

## Scope note

This repo is only the renderer.

Higher-level systems such as scene management, animation, editor tooling, and runtime composition are intended to live outside this repo and be integrated elsewhere.

If you want a renderer that stays close enough to Vulkan to study, but still gives you reusable pass-oriented building blocks, that is what `Abstracto` is for.
