#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "../backend/AppWindow.h"
#include "../backend/BackendConfig.h"
#include "../backend/VulkanBackend.h"
#include "../renderer/PassRenderer.h"
#include "../renderer/PassUniformSet.h"
#include "../renderer/RasterRenderPass.h"

struct TriangleTransform {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct TriangleFrameContext {
  float deltaSeconds = 0.0f;
  float timeSeconds = 0.0f;
  std::array<glm::vec3, 3> positions{
      glm::vec3(0.0f, 0.5f, 0.0f),
      glm::vec3(-0.5f, -0.5f, 0.0f),
      glm::vec3(0.5f, -0.5f, 0.0f),
  };
  std::array<glm::vec4, 3> colors{
      glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
      glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
      glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
  };
  TriangleTransform transform;
};

struct TriangleUniformData {
  glm::vec4 positions[3]{
      glm::vec4(0.0f, 0.5f, 0.0f, 0.0f),
      glm::vec4(-0.5f, -0.5f, 0.0f, 0.0f),
      glm::vec4(0.5f, -0.5f, 0.0f, 0.0f),
  };
  glm::vec4 colors[3]{
      glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
      glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
      glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
  };
  glm::vec4 translation{0.0f, 0.0f, 0.0f, 0.0f};
  glm::vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 scale{1.0f, 1.0f, 1.0f, 0.0f};
};

class TrianglePass : public RasterRenderPass {
public:
  TrianglePass(PipelineSpec spec, uint32_t framesInFlight)
      : RasterRenderPass(std::move(spec),
                         RasterPassAttachmentConfig{
                             .useColorAttachment = true,
                             .useDepthAttachment = false,
                             .useMsaaColorAttachment = false,
                             .resolveToSwapchain = false,
                             .useSwapchainColorAttachment = true,
                         }),
        framesInFlightCount(framesInFlight) {}

  void setFrameContext(const TriangleFrameContext &context) {
    for (size_t i = 0; i < 3; ++i) {
      uniformData.positions[i] = glm::vec4(context.positions[i], 0.0f);
      uniformData.colors[i] = context.colors[i];
    }
    uniformData.translation = glm::vec4(context.transform.position, 0.0f);
    uniformData.rotation =
        glm::vec4(context.transform.rotation.x, context.transform.rotation.y,
                  context.transform.rotation.z, context.transform.rotation.w);
    uniformData.scale = glm::vec4(context.transform.scale, 0.0f);
  }

protected:
  std::vector<DescriptorBindingSpec> descriptorBindings() const override {
    return {{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    }};
  }

  VertexInputLayoutSpec vertexInputLayout() const override { return {}; }

  void initializePassResources(DeviceContext &deviceContext,
                               SwapchainContext &) override {
    uniformSet.initialize(deviceContext, passDescriptorSetLayout(),
                          framesInFlightCount);
  }

  void bindPassResources(const RenderPassContext &context) override {
    uniformSet.write(context.frameIndex, uniformData);
    uniformSet.bind(context.commandBuffer, pipelineLayoutHandle(),
                    context.frameIndex);
  }

  void recordDrawCommands(const RenderPassContext &context,
                          const std::vector<RenderItem> &) override {
    context.commandBuffer.draw(3, 1, 0, 0);
  }

private:
  uint32_t framesInFlightCount = 0;
  PassUniformSet<TriangleUniformData> uniformSet;
  TriangleUniformData uniformData{};
};

class TriangleApp {
public:
  using UpdateCallback = std::function<void(TriangleFrameContext &)>;

  void onUpdate(UpdateCallback callback) {
    updateCallback = std::move(callback);
  }

  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  static constexpr uint32_t WIDTH = 1280;
  static constexpr uint32_t HEIGHT = 720;
  static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

  AppWindow window;
  VulkanBackend backend;
  BackendConfig config{.appName = "Triangle App",
                       .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT};

  PassRenderer renderer;
  TrianglePass *trianglePass = nullptr;

  UpdateCallback updateCallback;
  TriangleFrameContext frameContext{};
  std::chrono::steady_clock::time_point startTime =
      std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point lastFrameTime = startTime;

  void initWindow() { window.create(WIDTH, HEIGHT, "Triangle App", true); }

  void initVulkan() {
    backend.initialize(window, config);
    auto trianglePassLocal = std::make_unique<TrianglePass>(
        PipelineSpec{
            .shaderPath = "assets/shaders/triangle_pass.spv",
            .enableDepthTest = false,
            .enableDepthWrite = false,
        },
        MAX_FRAMES_IN_FLIGHT);
    trianglePass = trianglePassLocal.get();
    renderer.addPass(std::move(trianglePassLocal));
    renderer.initialize(backend.device(), backend.swapchain());
  }

  void mainLoop() {
    while (!window.shouldClose()) {
      window.pollEvents();
      drawFrame();
    }
    backend.waitIdle();
  }

  void drawFrame() {
    auto frameState = backend.beginFrame(window);
    if (!frameState.has_value()) {
      backend.recreateSwapchain(window);
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    frameContext.deltaSeconds =
        std::chrono::duration<float>(now - lastFrameTime).count();
    frameContext.timeSeconds =
        std::chrono::duration<float>(now - startTime).count();
    lastFrameTime = now;

    if (updateCallback) {
      updateCallback(frameContext);
    }

    if (trianglePass != nullptr) {
      trianglePass->setFrameContext(frameContext);
    }

    auto &commandBuffer =
        backend.commands().commandBuffer(frameState->frameIndex);
    commandBuffer.begin({});

    RenderPassContext context{.commandBuffer = commandBuffer,
                              .swapchainContext = backend.swapchain(),
                              .frameIndex = frameState->frameIndex,
                              .imageIndex = frameState->imageIndex};
    renderer.record(context, {});

    commandBuffer.end();

    const bool shouldRecreate = backend.endFrame(*frameState, window);
    if (shouldRecreate) {
      backend.recreateSwapchain(window);
      renderer.recreate(backend.device(), backend.swapchain());
    }
  }

  void cleanup() { window.destroy(); }
};
