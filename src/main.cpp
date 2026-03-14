#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "renderable/Mesh.h"
#include "renderer/FullscreenRenderPass.h"
#include "renderer/PassRenderer.h"
#include "renderer/PipelineSpec.h"
#include "renderer/UniformSceneRenderPass.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
const std::string ASSET_PATH = "assets";

struct TriangleUniformData {
  alignas(16) glm::mat4 transform{1.0f};
};

struct TrianglePushConstant {
  glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

class TrianglePass
    : public UniformSceneRenderPass<TriangleUniformData, TrianglePushConstant> {
public:
  TrianglePass(PipelineSpec spec, uint32_t framesInFlight)
      : UniformSceneRenderPass(
            std::move(spec), framesInFlight,
            RasterPassAttachmentConfig{
                .useColorAttachment = true,
                .useDepthAttachment = false,
                .useMsaaColorAttachment = false,
                .resolveToSwapchain = false,
                .useSwapchainColorAttachment = false,
                .colorAttachments = {{
                    .name = "triangleColor",
                    .format = RasterAttachmentFormat::RGBA8,
                    .sampled = true,
                    .clearColor = {0.08f, 0.08f, 0.10f, 1.0f},
                }},
            }) {}

  void setElapsedTime(float seconds) { elapsedTimeSeconds = seconds; }

protected:
  TriangleUniformData buildUniformData(uint32_t) const override {
    const float angle = elapsedTimeSeconds;
    const float scale = 0.85f + 0.15f * std::sin(elapsedTimeSeconds * 2.0f);

    const glm::mat4 rotation =
        glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::mat4 transform =
        glm::scale(rotation, glm::vec3(scale, scale, 1.0f));

    return TriangleUniformData{
        .transform = transform,
    };
  }

  TrianglePushConstant buildPushConstantData(uint32_t) const override {
    const float pulse =
        0.6f + 0.4f * (0.5f + 0.5f * std::sin(elapsedTimeSeconds * 3.0f));

    return TrianglePushConstant{
        .tint = glm::vec4(1.0f, pulse, pulse, 1.0f),
    };
  }

private:
  float elapsedTimeSeconds = 0.0f;
};

class PostProcessPass : public FullscreenRenderPass {
public:
  PostProcessPass(PipelineSpec spec, uint32_t framesInFlight,
                  const RasterRenderPass *sourcePass = nullptr)
      : FullscreenRenderPass(std::move(spec), framesInFlight,
                             RasterPassAttachmentConfig{
                                 .useColorAttachment = true,
                                 .useDepthAttachment = false,
                                 .useMsaaColorAttachment = false,
                                 .resolveToSwapchain = false,
                                 .useSwapchainColorAttachment = true,
                                 .clearColor = {0.02f, 0.02f, 0.02f, 1.0f},
                             }),
        sourcePassRef(sourcePass) {}

  void setSourcePass(const RasterRenderPass &sourcePass) {
    sourcePassRef = &sourcePass;
  }

protected:
  std::vector<FullscreenImageInputBinding> imageInputBindings() const override {
    return {{.binding = 0}};
  }

  VertexInputLayoutSpec vertexInputLayout() const override {
    auto attrs = FullscreenVertex::getAttributeDescriptions();
    return VertexInputLayoutSpec{
        .bindings = {FullscreenVertex::getBindingDescription()},
        .attributes = {attrs.begin(), attrs.end()},
    };
  }

  std::vector<PassImageBinding>
  resolveImageBindings(const vk::raii::Sampler &sampler) const override {
    validateSourcePass();

    return {
        {.binding = 0, .resource = sourcePassRef->sampledColorOutput(sampler)},
    };
  }

private:
  const RasterRenderPass *sourcePassRef = nullptr;

  void validateSourcePass() const {
    if (sourcePassRef == nullptr) {
      throw std::runtime_error("PostProcessPass requires a source pass");
    }
  }
};

class TriangleApp {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  AppWindow window;
  VulkanBackend backend;
  BackendConfig config{
      .appName = "Fullscreen Post Process",
      .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT,
  };

  PassRenderer renderer;
  VertexMesh triangleMesh;
  FullscreenMesh fullscreenQuadMesh;
  std::vector<RenderItem> renderItems;
  TrianglePass *trianglePass = nullptr;
  PostProcessPass *postProcessPass = nullptr;
  const std::chrono::steady_clock::time_point startTime =
      std::chrono::steady_clock::now();

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

  void initWindow() { window.create(WIDTH, HEIGHT, "Fullscreen Post Process"); }

  void initVulkan() {
    backend.initialize(window, config);

    triangleMesh.setGeometry(
        {
            {{0.0f, -0.6f, 0.0f}, {1.0f, 0.2f, 0.2f}, {0.5f, 1.0f}},
            {{0.6f, 0.6f, 0.0f}, {0.2f, 1.0f, 0.2f}, {1.0f, 0.0f}},
            {{-0.6f, 0.6f, 0.0f}, {0.2f, 0.4f, 1.0f}, {0.0f, 0.0f}},
        },
        {0, 1, 2});

    triangleMesh.createVertexBuffer(commandContext(), deviceContext());
    triangleMesh.createIndexBuffer(commandContext(), deviceContext());

    fullscreenQuadMesh = buildFullscreenQuadMesh();
    fullscreenQuadMesh.createVertexBuffer(commandContext(), deviceContext());
    fullscreenQuadMesh.createIndexBuffer(commandContext(), deviceContext());

    auto trianglePassLocal = std::make_unique<TrianglePass>(
        PipelineSpec{
            .shaderPath = ASSET_PATH + "/shaders/triangle_pass.spv",
            .cullMode = vk::CullModeFlagBits::eNone,
            .enableDepthTest = false,
            .enableDepthWrite = false,
        },
        MAX_FRAMES_IN_FLIGHT);

    trianglePass = trianglePassLocal.get();

    auto postProcessPassLocal = std::make_unique<PostProcessPass>(
        PipelineSpec{
            .shaderPath = ASSET_PATH + "/shaders/post_process_pass.spv",
            .cullMode = vk::CullModeFlagBits::eNone,
            .enableDepthTest = false,
            .enableDepthWrite = false,
        },
        MAX_FRAMES_IN_FLIGHT, trianglePass);

    postProcessPass = postProcessPassLocal.get();

    renderer.addPass(std::move(trianglePassLocal));
    renderer.addPass(std::move(postProcessPassLocal));
    renderer.initialize(deviceContext(), swapchainContext());

    renderItems = {
        RenderItem{
            .mesh = &triangleMesh,
            .descriptorBindings = nullptr,
            .targetPass = trianglePass,
        },
        RenderItem{
            .mesh = &fullscreenQuadMesh,
            .descriptorBindings = nullptr,
            .targetPass = postProcessPass,
        },
    };
  }

  void drawFrame() {
    auto frameState = backend.beginFrame(window);

    if (!frameState.has_value()) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds =
        std::chrono::duration<float>(now - startTime).count();

    trianglePass->setElapsedTime(elapsedSeconds);

    renderer.record(commandContext().commandBuffer(frameState->frameIndex),
                    swapchainContext(), renderItems, frameState->frameIndex,
                    frameState->imageIndex);

    const bool shouldRecreate = backend.endFrame(*frameState, window);
    if (shouldRecreate) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
    }
  }

  void mainLoop() {
    while (!window.shouldClose()) {
      window.pollEvents();
      drawFrame();
    }

    backend.waitIdle();
  }

  void cleanup() { window.destroy(); }
};

int main() {
  try {
    TriangleApp app;
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
