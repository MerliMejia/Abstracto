<div align="center" style="margin-bottom:10px">
    <img src="assets/gallery/logo2.jpg" alt="Abstracto logo" height="200" />
</div>

<p align="center">
  <img src="https://img.shields.io/badge/status-experimental-E67E22?style=for-the-badge" alt="Status experimental" />
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++20" />
  <img src="https://img.shields.io/badge/CMake-3.20%2B-064F8C?style=for-the-badge&logo=cmake&logoColor=white" alt="CMake 3.20+" />
  <img src="https://img.shields.io/badge/Vulkan-top--down-learning-A41E22?style=for-the-badge&logo=vulkan&logoColor=white" alt="Vulkan top down learning" />
</p>

Abstracto is a Vulkan learning project built around one idea:

Start at the highest level of abstraction, get something on screen quickly, and
then peel away one layer at a time until you are looking at raw Vulkan concepts
and calls.

This README follows that path using the app that is currently wired in
`src/main.cpp`: a small animated triangle sample. The goal is not to hide
Vulkan forever. The goal is to delay the overwhelm long enough for each layer
to make sense before you go deeper.

This README is written for the current checkout. Other branches or older docs
may show a larger scene sample, but the teaching path here starts from the
triangle app in `src/main.cpp`.

## What This Repo Is Teaching

Instead of starting with `VkInstance`, queues, swapchains, synchronization, and
pipeline creation all at once, this repo lets you learn Vulkan in descending
layers:

1. Make the triangle move and change color.
2. See how the app packages that data per frame.
3. See how a concrete pass uploads that data and issues a draw.
4. See how the renderer creates pipelines and records rendering commands.
5. See how the backend acquires swapchain images and submits work.
6. See how the backend contexts map almost one-to-one to Vulkan objects.
7. Arrive at the point where the remaining concepts are just Vulkan.

As you read through the repo, the idea is:

- Start where the sample already works.
- Change one thing and see the result immediately.
- Ask what machinery made that change visible on screen.
- Go down exactly one layer to answer that question.

If you follow the layers in order, the lower-level Vulkan code stops feeling
like a wall of unrelated setup. Each layer answers the question created by the
layer above it.

## Quick Start

### Requirements

- CMake 3.20+
- A C++20 compiler
- A Vulkan SDK or Vulkan loader/runtime available to CMake
- `slangc` only if you want CMake to regenerate `.spv` files from `.slang`
- Git if you want CMake to auto-fetch missing dependencies

### Build

```bash
cmake -S . -B build -DABSTRACTO_FETCH_DEPS=ON
cmake --build build -j4
./build/Abstracto
```

If `slangc` is not installed, the checked-in `.spv` files are enough to build
the current sample.

## How To Use This README

Treat the repo as a staircase.

At each level:

- Read only the files listed for that level.
- Make one visible change.
- Rebuild and run.
- Explain to yourself what the layer does and what it is hiding.
- Then move down one level.

Do not jump to the bottom too early. Vulkan becomes easier once every lower
layer answers a question you already have.

The important habit is this:

- At the current layer, figure out what data you control.
- Then figure out where that data goes next.
- Then figure out which layer turns that data into Vulkan work.

If you can explain that handoff clearly, you are ready for the next level.

## The Learning Ladder

### Level 0: `src/main.cpp`

This is the highest level in the current sample.

This file is the "I just want something on screen" entry point. It gives you a
simple mental model:

- there is an app
- the app gives you a frame context
- you change values in that frame context
- the triangle on screen responds

What you see in practice:

- A `TriangleApp` object
- An `onUpdate` callback
- Per-frame edits to position, rotation, scale, and per-vertex colors
- A call to `triangle.run()`

What is actually happening at this level:

- `main()` does not know anything about swapchains, command buffers, descriptor
  sets, or pipelines.
- It only knows that each frame it receives a `TriangleFrameContext`.
- That context contains the values that will eventually become GPU-visible
  uniform data.
- The callback is your "authoring surface" for animation and per-frame behavior.

What this level lets you do:

- Animate the triangle
- Change the shape
- Change the colors
- Treat rendering as "I update a frame context every frame"

What this level hides:

- Window creation
- Vulkan instance and device setup
- Swapchain creation
- Command buffers
- Descriptor sets and uniform buffers
- Graphics pipeline creation
- Queue submission and presentation

Files to read:

- `src/main.cpp`

Exact code to follow in this file:

- `int main()`
- `triangle.onUpdate(...)`
- the lambda `[](TriangleFrameContext &context) { ... }`
- `triangle.run()`

Data flow at this level:

1. `main()` registers an update callback.
2. Every frame, the app calls that callback with a mutable frame context.
3. You modify transform, positions, and colors.
4. Some lower layer takes those values and turns them into GPU data and draw
   commands.

What `onUpdate()` means at this level:

At Level 0, the call `triangle.onUpdate(...)` is just a registration point. You
are not rendering inside that callback. You are only describing what the next
frame should look like.

When the lambda passed to `triangle.onUpdate(...)` runs:

- `context.timeSeconds` already contains the total time since the app started
- `context.deltaSeconds` already contains the time since the previous frame
- `context.positions`, `context.colors`, and `context.transform` already exist
  as editable CPU-side values

When that lambda returns:

- the frame context now contains your latest triangle state
- the app still has to hand that state to a render pass
- the render pass still has to turn it into GPU-visible data
- the renderer still has to record commands
- the backend still has to submit and present those commands

So the right mental model is:

- `onUpdate()` does not draw
- `onUpdate()` prepares the data that later layers will draw

<details>
<summary>Questions At This Layer</summary>

- **Q: Why is there almost no Vulkan code in `src/main.cpp`?**  
  **A:** Because this layer is intentionally hiding Vulkan. `main()` only calls `triangle.onUpdate(...)` and `triangle.run()`. The Vulkan work starts lower down, first in `TriangleApp::drawFrame()`.
- **Q: What does "per frame" mean here?**  
  **A:** It means "once for every image the app wants to show." The lambda passed to `triangle.onUpdate(...)` runs once during each call to `TriangleApp::drawFrame()`, so you are describing the next frame's triangle state.
- **Q: What Vulkan concept is closest to this frame context idea?**  
  **A:** The closest idea is "CPU-side frame state." At this layer Vulkan still does not see that data directly. Lower layers will copy it into GPU-readable data and use it when preparing the frame.

</details>

Try this first:

- Change the rotation speed
- Move the triangle around
- Make the triangle pulse by changing `scale`
- Change the `wave` frequencies to produce different color animation

Before you move on, make sure you understand:

- The triangle is not loaded from a mesh file here.
- The app is exposing a high-level per-frame interface.
- The visible result comes from writing to a frame context, not from touching
  Vulkan directly.
- This file is intentionally small because it is not the rendering system. It is
  just the user-facing entry point into the rendering system.

### Level 1: The App Layer

Now open `src/apps/TriangleApp.h`.

This is the first place where the sample stops being "just animation logic" and
starts becoming "an application that owns a renderer."

This layer introduces three important ideas:

- `TriangleFrameContext`: the high-level data the app wants to change every
  frame
- `TriangleUniformData`: the packed form of that data that the GPU-facing code
  will use
- `TriangleApp`: the object that owns the window, backend, renderer, and main
  loop

What this layer does:

- Creates the window
- Initializes the Vulkan backend
- Creates and registers the triangle render pass
- Measures time per frame
- Invokes the update callback
- Starts and ends command buffer recording
- Handles swapchain recreation

Files to read:

- `src/apps/TriangleApp.h`

Read it in this order:

- `TriangleFrameContext`
- `TriangleUniformData`
- `TriangleApp`

Exact code to follow in this file:

- `TriangleApp::onUpdate(UpdateCallback callback)`
- `TriangleApp::run()`
- `TriangleApp::initWindow()`
- `TriangleApp::initVulkan()`
- `TriangleApp::mainLoop()`
- `TriangleApp::drawFrame()`
- `TriangleFrameContext`
- `TriangleUniformData`

How this layer works:

`TriangleFrameContext` is the friendly version of the data. Look at the
`TriangleFrameContext` struct definition in this file. It stores positions,
colors, time, and transform using the types that are most convenient for your
app code.

`TriangleUniformData` is the GPU-facing version of the same information. Look
at the `TriangleUniformData` struct definition in the same file. It is laid out
as raw values that can be copied into a uniform buffer and read by the shader.

`TriangleApp` is the coordinator:

- `TriangleApp::initWindow()` creates the GLFW-backed window.
- `TriangleApp::initVulkan()` initializes the backend, creates the triangle pass, and gives
  that pass to the renderer.
- `TriangleApp::mainLoop()` runs until the window closes.
- `TriangleApp::drawFrame()` is the real center of the sample. It advances time, runs your
  callback, updates the pass, records commands, and presents the frame.

This is the key abstraction at this layer:

- you think in terms of a frame lifecycle
- the app thinks in terms of backend + renderer + pass
- you still do not have to think in terms of raw Vulkan handles

How Level 0 works once you understand Level 1:

This is the layer that explains what `TriangleApp::onUpdate(...)` actually
does.

Inside `TriangleApp::drawFrame()`, the flow is:

1. Ask the backend for a frame slot and swapchain image with
   `backend.beginFrame(window)`
2. Compute `deltaSeconds` and `timeSeconds`
3. Call your `updateCallback(frameContext)`
4. Copy that updated `frameContext` into the concrete render pass with
   `trianglePass->setFrameContext(frameContext)`
5. Begin the command buffer
6. Ask the renderer to record the passes with `renderer.record(context, {})`
7. End the command buffer
8. Ask the backend to submit and present with
   `backend.endFrame(*frameState, window)`

So from this layer's point of view, `onUpdate()` is just the moment where you
fill in the CPU-side frame description before recording starts.

How the context becomes something visible at this layer:

- `TriangleFrameContext` is the app-owned state for the current frame
- `TriangleApp::drawFrame()` passes that state into
  `TrianglePass::setFrameContext(...)`
- `TrianglePass::setFrameContext(...)` keeps a GPU-friendly copy of that state
  for later binding
- `PassRenderer::record(const RenderPassContext &, const std::vector<RenderItem> &)`
  records the pass
- `VulkanBackend::endFrame(...)` submits the recorded commands
- the presented swapchain image is what you finally see on screen

This layer still does not show the actual draw mechanics, but it does show the
full control flow.

What this layer is hiding:

- How the backend creates and owns Vulkan objects
- How the renderer creates pipelines and rendering attachments
- How per-frame data gets uploaded into buffers
- How synchronization and presentation are handled

<details>
<summary>Questions At This Layer</summary>

- **Q: What is a swapchain?**  
  **A:** A swapchain is the set of images Vulkan rotates through to show frames in a window. This app asks the backend for one of those images at the start of each frame.
- **Q: What is a swapchain image?**  
  **A:** It is one image inside the swapchain. In `TriangleApp::drawFrame()`, `backend.beginFrame(window)` returns a `FrameState` containing `imageIndex`, which identifies which swapchain image this frame will render and present.
- **Q: What is a frame slot?**  
  **A:** A frame slot is the reusable per-frame bundle of CPU-side resources used for one in-flight frame. In this app, `MAX_FRAMES_IN_FLIGHT` controls how many of those reusable frame slots exist.
- **Q: What does "frames in flight" mean?**  
  **A:** It means the CPU is allowed to prepare a new frame while the GPU may still be finishing an older one. In this app, `MAX_FRAMES_IN_FLIGHT` sets how many frame slots can overlap this way.
- **Q: What does "present" mean?**  
  **A:** It means handing the rendered swapchain image to the window system so it can be displayed. At this layer you only see the call flow in `TriangleApp::drawFrame()`, but the actual present happens later in `VulkanBackend::endFrame(...)`.
- **Q: Why is there both `TriangleFrameContext` and `TriangleUniformData`?**  
  **A:** `TriangleFrameContext` is convenient app-side state. `TriangleUniformData` is a tightly packed representation meant to be copied into a Vulkan uniform buffer and read by the shader.

</details>

Try this:

- Change the window size
- Change `MAX_FRAMES_IN_FLIGHT`
- Log `deltaSeconds` and `timeSeconds`
- Freeze the animation by skipping the callback

Before you move on, make sure you understand:

- The callback edits CPU-side state.
- `TriangleApp` turns that state into a rendered frame.
- The app loop already exposes the classic Vulkan frame lifecycle at a safer
  level: acquire, record, submit, present.
- This layer is where the sample's control flow becomes visible:
  input data -> frame context -> pass update -> command recording -> present.

### Level 2: The Concrete Render Pass

Still in `src/apps/TriangleApp.h`, look at `TrianglePass`.

This is the first layer where rendering becomes explicit again.

What `TrianglePass` does:

- Inherits from `RasterRenderPass`
- Declares one descriptor binding for a uniform buffer
- Converts `TriangleFrameContext` into `TriangleUniformData`
- Uploads uniform data every frame
- Binds the descriptor set
- Issues `draw(3, 1, 0, 0)`

How it works:

`TrianglePass` sits exactly between your app code and the reusable renderer
core.

Its job is to say:

- what resources this pass needs
- what shader this pass uses
- what attachments this pass renders to
- how CPU-side frame data becomes GPU-side uniform data
- what draw commands should be recorded

The important method here is
`TrianglePass::setFrameContext(const TriangleFrameContext &context)`.

That method takes the friendly app-facing `TriangleFrameContext` and rewrites it
into `TriangleUniformData`. This is the point where app data becomes
shader-readable data.

Then, during rendering, follow these exact methods:

- `TrianglePass::initializePassResources(...)` creates the pass-owned uniform
  resources
- `TrianglePass::bindPassResources(...)` copies the current uniform data into
  the current frame's buffer and binds the descriptor set
- `TrianglePass::recordDrawCommands(...)` emits the actual draw call

How Level 1 works once you understand Level 2:

Level 1 showed you that `TriangleApp::drawFrame()` calls
`trianglePass->setFrameContext(frameContext)`
after `onUpdate()`. This layer explains what that actually means.

The handoff looks like this:

1. Your callback modifies `TriangleFrameContext`
2. `TriangleApp::drawFrame()` passes that context into
   `TrianglePass::setFrameContext(...)`
3. `TrianglePass::setFrameContext(...)` copies positions, colors, translation,
   rotation, and scale into `TriangleUniformData`
4. Later, during command recording, `TrianglePass::bindPassResources(...)`
   writes that uniform data into the mapped uniform buffer for the current frame
5. The descriptor set for that uniform buffer is bound
6. `TrianglePass::recordDrawCommands(...)` records `draw(3, 1, 0, 0)`
7. The shader reads the bound uniform data and produces the triangle you see

So this is the first layer where the answer to "how did my `onUpdate()` changes
reach the screen?" becomes concrete:

- your callback changed CPU-side frame state
- `TrianglePass` repacked that state into shader data
- that shader data was written into a uniform buffer
- a draw call was recorded using that buffer

What appears on screen at this layer:

- `vertMain(...)` reads `triangleData.positions`, `triangleData.colors`,
  `triangleData.translation`, `triangleData.rotation`, and `triangleData.scale`
- `vertMain(...)` computes the final per-vertex position
- `fragMain(...)` returns the chosen color
- the pass writes the result into the current render target

Why this layer matters:

- It is still comfortable to read
- It already feels like real rendering work
- It is close enough to Vulkan that every concept here has a clear Vulkan
  counterpart

Files to read:

- `src/apps/TriangleApp.h`
- `assets/shaders/triangle_pass.slang`

Exact code to follow in these files:

- `class TrianglePass : public RasterRenderPass`
- `TrianglePass::setFrameContext(const TriangleFrameContext &context)`
- `TrianglePass::descriptorBindings() const`
- `TrianglePass::initializePassResources(DeviceContext &, SwapchainContext &)`
- `TrianglePass::bindPassResources(const RenderPassContext &context)`
- `TrianglePass::recordDrawCommands(const RenderPassContext &, const std::vector<RenderItem> &)`
- `vertMain(uint vertexId : SV_VertexID)` in `assets/shaders/triangle_pass.slang`
- `fragMain(VSOutput input)` in `assets/shaders/triangle_pass.slang`

What this layer is abstracting:

- Descriptor set layout creation
- Descriptor pool allocation
- Graphics pipeline creation
- Attachment setup
- Dynamic rendering setup
- Image layout transitions

What is intentionally explicit:

- The pass owns its frame data
- The shader path is explicit
- The draw call is explicit
- The binding layout is explicit

What is not happening here:

- There is no vertex buffer in the current sample.
- There is no model loader involved.
- The triangle geometry is effectively generated from the uniform data and
  `SV_VertexID` in the shader.

That makes this a good layer to study because you can focus on:

- uniform data
- descriptor binding
- the draw call
- the shader

without also having to explain vertex input and asset loading yet.

<details>
<summary>Questions At This Layer</summary>

- **Q: What is a buffer?**  
  **A:** A buffer is a block of GPU-usable storage for structured data. In this example, the data stored in that buffer is the packed `TriangleUniformData` for the current frame.
- **Q: What is a uniform buffer?**  
  **A:** It is a Vulkan buffer that stores small pieces of read-only data for shaders, such as transform and color data. Here, `TrianglePass::bindPassResources(...)` writes `TriangleUniformData` into the current frame's uniform buffer before the draw is recorded.
- **Q: What is a descriptor?**  
  **A:** A descriptor is Vulkan's way of saying "this shader resource lives in this slot and points at this actual object." In this pass, the described resource is the uniform buffer holding `TriangleUniformData`.
- **Q: What is a binding?**  
  **A:** A binding is a numbered shader-visible slot used to look up a resource. Here, `TrianglePass::descriptorBindings() const` declares binding `0`, and the shader reads from `[[vk::binding(0, 0)]]`.
- **Q: What is a descriptor set layout?**  
  **A:** It is the Vulkan declaration of which resources a shader expects and at which bindings. Here, `TrianglePass::descriptorBindings() const` declares one binding at `binding = 0` with descriptor type `vk::DescriptorType::eUniformBuffer`.
- **Q: What is a descriptor set?**  
  **A:** It is a concrete bound group of descriptors that matches a descriptor set layout. Here, `TrianglePass::bindPassResources(...)` binds the descriptor set for the current frame before `TrianglePass::recordDrawCommands(...)` records the draw.
- **Q: What is a vertex shader?**  
  **A:** A vertex shader runs once per vertex and computes each vertex's final position. In this example, `vertMain(...)` reads positions from `triangleData` and computes the rotated and translated output position.
- **Q: What is a fragment shader?**  
  **A:** A fragment shader computes the final output color for the rasterized fragments produced by the triangle. In this example, `fragMain(...)` simply returns the color passed from the vertex shader.
- **Q: What is `SV_VertexID`?**  
  **A:** It is the index of the current vertex-shader invocation. In `vertMain(uint vertexId : SV_VertexID)`, the values `0`, `1`, and `2` are used to read the three triangle vertices from `triangleData`.
- **Q: Why does `draw(3, 1, 0, 0)` draw a triangle?**  
  **A:** Because the draw call asks Vulkan to run the vertex shader three times. `vertMain(uint vertexId : SV_VertexID)` uses vertex IDs `0`, `1`, and `2` to produce the triangle's three corners, and rasterization then fills the area between them.

</details>

Try this:

- Add another value to `TriangleUniformData`
- Use that value in the shader
- Change the draw call to prove that this pass is what actually submits the
  triangle draw

Before you move on, make sure you understand:

- The triangle is generated procedurally in the shader using `SV_VertexID`.
- The pass is responsible for passing uniform data to the GPU.
- The render pass abstraction is thin enough that you can still see the draw.
- This is the first layer where "rendering" is no longer abstract. You can point
  to the exact place where resources are bound and where the draw happens.

### Level 3: The Render Core

Now move into the reusable renderer layer.

Files to read:

- `src/renderer/RenderPass.h`
- `src/renderer/PassRenderer.h`
- `src/renderer/RasterRenderPass.h`
- `src/renderer/PassUniformSet.h`
- `src/renderer/ShaderProgram.h`

Exact code to follow in these files:

- `RenderPass::initialize(...)`
- `RenderPass::recreate(...)`
- `RenderPass::record(...)`
- `PassRenderer::addPass(...)`
- `PassRenderer::initialize(...)`
- `PassRenderer::record(const RenderPassContext &, const std::vector<RenderItem> &)`
- `RasterRenderPass::initialize(...)`
- `RasterRenderPass::record(...)`
- `PassUniformSet::initialize(...)`
- `PassUniformSet::write(...)`
- `PassUniformSet::bind(...)`
- `ShaderProgram::load(...)`

This is where the project turns repeated Vulkan setup into named reusable
pieces.

What these classes do:

- `RenderPass`: defines the lifecycle of a pass
- `PassRenderer`: stores passes and records them in sequence
- `RasterRenderPass`: builds the graphics pipeline and records raster work
- `PassUniformSet`: allocates, maps, writes, and binds pass-owned uniform
  buffers and descriptor sets
- `ShaderProgram`: loads SPIR-V into a Vulkan shader module

Read this layer as a set of answers to the question:
"What code would I have to repeat for every pass if these helpers did not
exist?"

How the pieces fit together:

- `RenderPass` is the common interface. It says every pass must be able to
  initialize itself, recreate itself, and record commands.
- `PassRenderer` owns a list of passes and records them in order. It is very
  small on purpose. Its main job is orchestration, not magic.
- `RasterRenderPass` is the heavy lifter. It turns a pass description into a
  real graphics pipeline and a real rendering sequence.
- `PassUniformSet` handles the repetitive buffer, memory, descriptor pool, and
  descriptor set work needed for per-pass uniforms.
- `ShaderProgram` turns a `.spv` file into a Vulkan shader module.

What this layer is abstracting:

- `VkShaderModule` creation
- Descriptor set layout creation
- Pipeline layout creation
- Graphics pipeline creation
- Attachment configuration
- Viewport and scissor setup
- `vkCmdBeginRendering` and `vkCmdEndRendering`
- Uniform buffer allocation and descriptor writes

What `RasterRenderPass` is really doing for you:

- It interprets the `PipelineSpec`.
- It creates layouts and pipeline objects.
- It prepares the render attachments.
- It begins dynamic rendering with the right attachment info.
- It binds the pipeline.
- It sets viewport and scissor.
- It gives the concrete pass one clean place to bind resources and one clean
  place to record draw commands.

That means `TrianglePass` gets to stay small because this layer absorbed the
boilerplate.

How Level 2 works once you understand Level 3:

Level 2 showed methods like `TrianglePass::bindPassResources(...)` and
`TrianglePass::recordDrawCommands(...)`,
but it did not show who calls them or what surrounds them. This layer explains
that missing machinery.

The flow here is:

1. `PassRenderer::record(const RenderPassContext &, const std::vector<RenderItem> &)`
   loops through its passes
2. For each pass, it calls `renderPass->record(...)`
3. For `TrianglePass`, that inherited implementation is
   `RasterRenderPass::record(...)`
4. `RasterRenderPass::record(...)` handles the Vulkan-heavy setup around the
   pass:
   - transition attachments into the right layouts
   - build the attachment info
   - begin dynamic rendering
   - bind the graphics pipeline
   - set viewport and scissor
5. Only then does it call the hooks supplied by `TrianglePass`:
   - `TrianglePass::bindPassResources(...)`
   - `TrianglePass::recordDrawCommands(...)`
6. Then it ends rendering and transitions attachments to their final layouts

That means the concrete pass is only responsible for the pass-specific parts,
while this layer owns the repeated rendering structure.

How the previous layer becomes pixels at this layer:

- `TriangleApp` gave the renderer a `TrianglePass`
- `TrianglePass` supplied the pass-specific data and hooks
- this layer wrapped those hooks in the full render sequence needed by Vulkan
- the result was a complete recorded command buffer section for that pass

<details>
<summary>Questions At This Layer</summary>

- **Q: What is a graphics pipeline?**  
  **A:** It is the Vulkan object that bundles shader stages and the main rendering state needed for a draw. At this layer, `RasterRenderPass::initialize(...)` creates that pipeline so the concrete pass only has to provide its specific settings.
- **Q: What is a pipeline layout?**  
  **A:** It is the Vulkan object that describes which descriptor set layouts a pipeline will use. This matters here because the uniform-buffer binding declared by `TrianglePass::descriptorBindings() const` must match the pipeline layout used by the pass.
- **Q: What is SPIR-V?**  
  **A:** SPIR-V is the binary shader format Vulkan consumes. `ShaderProgram::load(...)` reads the `.spv` file and creates the Vulkan shader module from that binary data.
- **Q: What is a shader module?**  
  **A:** It is the Vulkan object created from SPIR-V shader bytecode. At this layer, `ShaderProgram::load(...)` reads the `.spv` file and creates the shader module used by the graphics pipeline.
- **Q: What is a color attachment?**  
  **A:** A color attachment is an image used as a render output target. For this triangle pass, the final color result is written into the current color target for the frame.
- **Q: What is a depth attachment?**  
  **A:** A depth attachment is an image used for depth testing during rendering. This triangle example disables depth usage for the pass, but the render-core layer supports it.
- **Q: What is a render target?**  
  **A:** A render target is the image you are currently rendering into. In this renderer, `RasterRenderPass::record(...)` prepares the attachments that act as the render target for the pass.
- **Q: What is dynamic rendering?**  
  **A:** It is the Vulkan API used here to begin rendering by specifying attachments directly at record time. In this project, `RasterRenderPass::record(...)` calls `beginRendering(...)` and supplies the needed attachment information there.
- **Q: What is an image layout?**  
  **A:** An image layout is Vulkan's declared current usage state for an image, such as being ready for rendering, sampling, or presentation. Vulkan tracks that state explicitly.
- **Q: What is an image layout transition?**  
  **A:** It is the Vulkan operation that changes an image from one layout to another so it can be used correctly for the next step. `RasterRenderPass::record(...)` handles those transitions around rendering so the concrete pass does not have to.

</details>

Why this layer is a good next step:

- The code is still structured around rendering concepts instead of raw Vulkan
  structs everywhere
- Every abstraction here is small and inspectable
- You can now see exactly which parts of Vulkan are repetitive enough
  to wrap and which parts are still left explicit

Try this:

- Read how `RasterRenderPass::record` turns pass state into rendering commands
- Trace how `PassUniformSet` maps CPU memory to GPU-visible uniform buffers
- Add a second pass once the first one is understood

Before you move on, make sure you understand:

- The renderer layer is not a giant engine.
- It is mostly packaging the same Vulkan steps you would otherwise repeat.
- At this point, most of the remaining complexity is backend and API setup.
- If you removed these helpers, you would not change the rendering model. You
  would just have to write the same Vulkan setup manually in more places.

### Level 4: The Frame Backend

Now read the frame orchestration layer.

Files to read:

- `src/backend/VulkanBackend.h`

Exact code to follow in this file:

- `VulkanBackend::initialize(AppWindow &, const BackendConfig &)`
- `VulkanBackend::beginFrame(AppWindow &)`
- `VulkanBackend::endFrame(const FrameState &, AppWindow &)`
- `VulkanBackend::recreateSwapchain(AppWindow &)`
- `VulkanBackend::waitIdle()`

This class coordinates the per-frame Vulkan flow.

What it does:

- Initializes the backend contexts
- Waits on the current frame fence
- Acquires the next swapchain image
- Resets the command buffer
- Submits recorded commands to the graphics queue
- Presents the rendered image
- Recreates the swapchain when needed

This is where the classic Vulkan frame loop becomes clear:

1. Wait for GPU work from the previous use of this frame slot
2. Acquire a swapchain image
3. Record commands
4. Submit commands
5. Present
6. Advance the frame index

How this layer works:

This class is the bridge between "the renderer has recorded some commands" and
"the GPU actually executes them and the window shows the result."

`VulkanBackend::initialize(...)` builds the full backend stack in dependency
order:

1. instance
2. surface
3. device
4. swapchain
5. command context
6. frame synchronization

`VulkanBackend::beginFrame(...)` does the preparation work required before
recording:

- wait for the current frame fence
- acquire the next swapchain image
- reset the fence
- reset the command buffer

`VulkanBackend::endFrame(...)` does the finishing work:

- submit the command buffer to the queue
- signal and wait on the correct semaphores
- present the swapchain image
- detect whether the swapchain must be recreated

How Level 3 works once you understand Level 4:

Level 3 explained how the renderer records commands, but recorded commands do
not appear on screen by themselves. This layer explains how recorded work
becomes executed work.

From this layer's point of view:

- the renderer writes commands into a command buffer
- `VulkanBackend` owns the frame lifecycle around that command buffer
- `VulkanBackend::beginFrame(...)` makes sure it is safe to record into the
  current frame slot
- `VulkanBackend::endFrame(...)` hands the finished command buffer to the GPU
  queue
- presentation shows the rendered swapchain image in the window

How the previous layer becomes pixels at this layer:

1. A command buffer is recorded by the renderer
2. `VulkanBackend::endFrame(...)` submits that command buffer to the graphics
   queue
3. The GPU executes the recorded draw and rendering commands
4. The rendered swapchain image is presented
5. The OS window system displays that presented image in the window

This is the layer where "recorded rendering commands" become "an actually
displayed frame."

What this layer is abstracting:

- It hides the individual context objects behind one frame-facing API
- It still does not hide the important synchronization concepts

<details>
<summary>Questions At This Layer</summary>

- **Q: What is a command buffer?**  
  **A:** A command buffer is the recorded list of GPU commands for a frame or task. In this example, `TriangleApp::drawFrame()` begins the command buffer, the renderer records commands into it, and `VulkanBackend::endFrame(...)` submits it.
- **Q: What is command recording?**  
  **A:** Command recording means filling a command buffer with commands the GPU will execute later. In this app, that happens between `commandBuffer.begin({})` and `commandBuffer.end()` inside `TriangleApp::drawFrame()`.
- **Q: What is command submission?**  
  **A:** Command submission means sending a recorded command buffer to a queue for execution. Here, `VulkanBackend::endFrame(...)` submits the finished command buffer to the graphics queue.
- **Q: What is a queue?**  
  **A:** A Vulkan queue is where recorded command buffers are submitted for GPU execution. At this layer, `VulkanBackend::endFrame(...)` submits the recorded command buffer to the queue owned by `DeviceContext`.
- **Q: What is a fence?**  
  **A:** A fence is a synchronization object mainly used so the CPU can wait until the GPU finishes work. `VulkanBackend::beginFrame(...)` waits on the current frame fence before reusing that frame slot.
- **Q: What is a semaphore?**  
  **A:** A semaphore is a synchronization object used to order GPU operations such as image acquisition, rendering, and presentation. `VulkanBackend::endFrame(...)` uses semaphores so rendering waits for the acquired image and presentation waits for rendering to finish.
- **Q: What does "acquire next image" mean?**  
  **A:** It means asking the swapchain which presentable image is available for the next frame. `VulkanBackend::beginFrame(...)` gets that image index before command recording starts.
- **Q: Why are acquire and present separate?**  
  **A:** Because Vulkan treats "get an image to render into" and "show the finished image" as separate steps. `VulkanBackend::beginFrame(...)` handles acquisition, while `VulkanBackend::endFrame(...)` handles presentation.

</details>

Try this:

- Trace `beginFrame`
- Trace `endFrame`
- Explain why a swapchain recreation can happen on acquire or present
- Explain why fence wait and command buffer reset happen before recording

Before you move on, make sure you understand:

- This is nearly Vulkan already.
- The main remaining abstractions are the context classes that own the actual
  Vulkan objects.
- This class is not inventing new rendering ideas. It is packaging the normal
  Vulkan frame loop into one place so the app layer can stay readable.

### Level 5: The Backend Contexts

This is the lowest repo-owned layer before you are mostly just reading Vulkan
code.

Files to read:

- `src/backend/AppWindow.h`
- `src/backend/InstanceContext.h`
- `src/backend/SurfaceContext.h`
- `src/backend/DeviceContext.h`
- `src/backend/SwapchainContext.h`
- `src/backend/CommandContext.h`
- `src/backend/FrameSync.h`

Exact code to follow in these files:

- `AppWindow::create(...)`
- `InstanceContext::create(const BackendConfig &)`
- `SurfaceContext::create(AppWindow &, InstanceContext &)`
- `DeviceContext::create(InstanceContext &, SurfaceContext &)`
- `DeviceContext::pickPhysicalDevice(...)`
- `DeviceContext::createLogicalDevice(...)`
- `SwapchainContext::create(AppWindow &, SurfaceContext &, DeviceContext &)`
- `CommandContext::create(DeviceContext &, uint32_t)`
- `FrameSync::create(DeviceContext &, size_t, uint32_t)`

These classes map very closely to Vulkan objects and responsibilities.

#### `AppWindow`

- Wraps GLFW window creation and resize handling
- Gives Vulkan something to present into
- Keeps windowing concerns separate from Vulkan object ownership

#### `InstanceContext`

- Creates the Vulkan instance
- Enables validation layers when available
- Enables required instance extensions
- Creates the debug messenger
- Answers the question: "How do I bootstrap Vulkan itself?"

#### `SurfaceContext`

- Creates `VkSurfaceKHR` from the window
- Connects the platform windowing system to Vulkan presentation

#### `DeviceContext`

- Selects a physical device
- Checks queue support, extension support, and required features
- Creates the logical device and graphics/present queue
- Answers the question: "Which GPU can run this app, and how do I talk to it?"

#### `SwapchainContext`

- Chooses the swapchain format, present mode, image count, and extent
- Creates the swapchain
- Creates image views for swapchain images
- Owns the images that will eventually be presented to the screen

#### `CommandContext`

- Creates the command pool
- Allocates per-frame command buffers
- Owns the objects that store recorded GPU commands

#### `FrameSync`

- Creates semaphores and fences
- Tracks the current frame slot
- Owns the synchronization primitives that stop CPU and GPU work from stepping
  on each other

What this layer is abstracting:

- Mostly ownership and naming
- Not much conceptual complexity is being hidden anymore

How to read this layer:

Do not read these classes as clever abstractions. Read them as named boxes for
Vulkan responsibilities.

Each one exists because Vulkan itself separates the world this way:

- instance-level setup
- presentation surface
- physical and logical device
- swapchain images
- command recording
- synchronization

That is why this layer feels much closer to the metal. It is very close to the
shape of Vulkan itself.

How Level 4 works once you understand Level 5:

Level 4 gave you a single `VulkanBackend` object. This layer opens that box.

What looked like one backend is actually several object owners working together:

- `InstanceContext` owns Vulkan instance creation
- `SurfaceContext` owns the presentation surface created from the window
- `DeviceContext` owns GPU selection and logical device creation
- `SwapchainContext` owns presentable images and their image views
- `CommandContext` owns the command pool and command buffers
- `FrameSync` owns semaphores, fences, and frame-slot progression

So when `VulkanBackend::beginFrame(...)` or `VulkanBackend::endFrame(...)` does
something, it is really coordinating these lower-level pieces:

- fence waiting comes from `FrameSync`
- image acquisition comes from `SwapchainContext`
- queue submission uses `DeviceContext`
- command buffer reset uses `CommandContext`
- swapchain recreation uses `SwapchainContext` again

How the previous layer becomes pixels at this layer:

- the frame backend coordinates the frame
- these context classes own the actual Vulkan objects used in that frame
- those Vulkan objects are the real handles the driver and GPU work with
- this is the last repo layer before you are effectively just reading Vulkan
  setup and ownership code

<details>
<summary>Questions At This Layer</summary>

- **Q: What is a Vulkan instance?**  
  **A:** It is the root Vulkan object used to initialize the API, enable layers and extensions, and query physical devices. In this repo, `InstanceContext::create(const BackendConfig &)` builds it.
- **Q: What are validation layers?**  
  **A:** They are optional debugging layers that check Vulkan API usage and report mistakes. In this repo, `InstanceContext::create(const BackendConfig &)` enables them when available in non-release builds.
- **Q: What are instance extensions?**  
  **A:** They are optional Vulkan features enabled when creating the instance. In this repo, `InstanceContext::create(const BackendConfig &)` gathers the required extensions before creating the instance.
- **Q: What is a debug messenger?**  
  **A:** It is the callback-based mechanism used to receive validation and debug messages. In this repo, `InstanceContext::setupDebugMessenger()` creates it after the instance is created.
- **Q: What is a surface?**  
  **A:** A surface is Vulkan's connection to the platform window system for presentation. Here, `SurfaceContext::create(AppWindow &, InstanceContext &)` creates the `VkSurfaceKHR` from the GLFW window.
- **Q: What is a physical device versus a logical device?**  
  **A:** The physical device is the actual GPU being queried and selected. The logical device is the Vulkan device you create after choosing that GPU, and it is the object used to create resources and access queues. In this repo, `DeviceContext::pickPhysicalDevice(...)` selects the GPU and `DeviceContext::createLogicalDevice(...)` creates the logical device.
- **Q: What is a queue family?**  
  **A:** It is a group of queues on a physical device with specific capabilities, such as graphics or presentation. `DeviceContext::pickPhysicalDevice(...)` and `DeviceContext::createLogicalDevice(...)` check queue-family support before creating the device.
- **Q: What is presentation support?**  
  **A:** It means a queue family is capable of presenting swapchain images to the window surface. `DeviceContext::pickPhysicalDevice(...)` checks that before accepting a device.
- **Q: What is device memory?**  
  **A:** It is Vulkan-managed memory allocated on or for the GPU and bound to resources such as buffers and images. The renderer layer uses it behind helpers like `PassUniformSet`, but this backend layer is where the device and memory-selection logic live.
- **Q: What is an image view?**  
  **A:** An image view is the Vulkan object that describes how an image will be accessed. In this repo, `SwapchainContext::create(...)` gets the swapchain images, and `createImageViews(...)` builds views so those images can be used as render targets.
- **Q: Why do swapchain images need image views?**  
  **A:** Because Vulkan usually uses images through image views rather than directly. In this repo, `SwapchainContext::create(...)` gets the swapchain images, then `createImageViews(...)` builds the views needed before those images can be used as render targets.

</details>

Why this is the right place to say "now we are doing Vulkan":

- The objects here correspond directly to core Vulkan concepts
- The code is mostly Vulkan-Hpp syntax over Vulkan API structure
- If you remove the class wrappers, you are left with the same Vulkan ideas

Try this:

- Change the preferred present mode
- Change validation-layer behavior
- Trace how the physical device is selected
- Explain why swapchain images need image views before rendering into them

Before you move on, make sure you understand:

- Each context exists because Vulkan separates responsibilities very sharply
- The repo is no longer inventing many new ideas here
- It is mostly organizing Vulkan objects by ownership and lifecycle

## Level 6: Pure Vulkan Mapping

At this point, the remaining step is not "learn a new Abstracto abstraction."
It is "map each Abstracto abstraction to the Vulkan object or call it wraps."

Here is the direct correspondence:

| Abstracto concept                    | Vulkan concept                                                        |
| ------------------------------------ | --------------------------------------------------------------------- |
| `AppWindow`                          | GLFW window plus native platform window                               |
| `InstanceContext`                    | `VkInstance`, validation layers, instance extensions, debug messenger |
| `SurfaceContext`                     | `VkSurfaceKHR`                                                        |
| `DeviceContext`                      | `VkPhysicalDevice`, `VkDevice`, `VkQueue`                             |
| `SwapchainContext`                   | `VkSwapchainKHR`, swapchain images, image views                       |
| `CommandContext`                     | `VkCommandPool`, `VkCommandBuffer`                                    |
| `FrameSync`                          | `VkSemaphore`, `VkFence`                                              |
| `ShaderProgram`                      | `VkShaderModule`                                                      |
| `PassUniformSet`                     | `VkBuffer`, `VkDeviceMemory`, descriptor pool, descriptor sets        |
| `RasterRenderPass`                   | pipeline layout, graphics pipeline, dynamic rendering commands        |
| `VulkanBackend::beginFrame/endFrame` | acquire, submit, present, swapchain recreation                        |

For the current triangle sample, the underlying Vulkan story is:

1. Create a window and Vulkan instance
2. Create a surface from the window
3. Pick a physical device and create a logical device
4. Create a swapchain and image views
5. Create a command pool and command buffers
6. Create semaphores and fences for frames in flight
7. Create a shader module from the triangle SPIR-V
8. Create descriptor set layout and pipeline layout
9. Create the graphics pipeline
10. Allocate a uniform buffer and descriptor set for each frame
11. Each frame: update uniform data, acquire image, record commands, draw,
    submit, present

Once you can explain that list comfortably, you are no longer just using the
abstraction. You understand what the abstraction is doing.

<details>
<summary>Questions At This Layer</summary>

- **Q: Which Vulkan objects are actually mandatory for this triangle?**  
  **A:** At minimum you still need the instance, surface, physical device, logical device, queue, swapchain, image views, command pool, command buffers, synchronization objects, shader module, pipeline layout, graphics pipeline, uniform buffer, descriptor set layout, descriptor pool, and descriptor set. The helper classes only organize these requirements.
- **Q: If the abstractions disappeared, what would still remain?**  
  **A:** The Vulkan object graph and frame flow would remain the same: initialize Vulkan, create presentation objects, create pipeline/resources, record commands, submit them, and present the swapchain image.
- **Q: What is the minimum frame flow I should remember?**  
  **A:** Acquire a swapchain image, update/bind resources, record commands into a command buffer, submit the command buffer to a queue, then present the image.
- **Q: What is the minimum resource-binding flow I should remember?**  
  **A:** Create a uniform buffer, describe it in a descriptor, match that descriptor with a descriptor set layout and pipeline layout, bind the descriptor set, then issue the draw so the shader can read the data.

</details>

## The Shader Fits Into The Story Too

The triangle sample is intentionally small enough that the shader can be read
early.

Read:

- `assets/shaders/triangle_pass.slang`

Important detail:

- The triangle is not coming from a vertex buffer in the current sample.
- The vertex shader uses `SV_VertexID` to index into positions and colors stored
  in the uniform buffer.

That makes the progression easier:

- First learn per-frame state
- Then learn uniform upload
- Then learn draw submission
- Then learn full geometry inputs later

## Suggested Reading Order

If you want to follow the intended descent through the code, this order works
well:

1. Run the app and only touch `src/main.cpp`
2. Open `TriangleFrameContext` and `TriangleApp`
3. Open `TrianglePass`
4. Open the shader
5. Open `PassUniformSet`
6. Open `RasterRenderPass`
7. Open `VulkanBackend`
8. Open the backend contexts
9. Rewrite one piece mentally in raw Vulkan terms

The moment you can say "oh, this class is basically managing this Vulkan
object," the abstraction has done its job.

## Why This Structure Exists

Abstracto is not trying to replace Vulkan with a black box. It is trying to
turn Vulkan into a sequence of understandable layers:

- app logic
- pass logic
- render-core reuse
- backend frame control
- backend Vulkan objects
- raw Vulkan mental model

That is the teaching strategy.

You begin with something that works.
You make one change.
You go one level deeper.
You repeat until the bottom is no longer scary.

## Further Reading

- Wiki home: [Abstracto Wiki](https://github.com/MerliMejia/Abstracto/wiki)
- Current project structure: [Current abstractions in the project](https://github.com/MerliMejia/Abstracto/wiki/Current-abstractions-in-the-project)
- Triangle tutorial: [Triangle to the Swapchain Tutorial](https://github.com/MerliMejia/Abstracto/wiki/Triangle-to-the-Swapchain-Tutorial)
