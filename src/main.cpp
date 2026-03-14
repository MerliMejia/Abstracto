#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "renderable/Mesh.h"
#include "renderer/PassRenderer.h"
#include "renderer/PipelineSpec.h"
#include "renderer/SceneRenderPass.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
const std::string ASSET_PATH = "assets";

class TrianglePass : public SceneRenderPass {
public:
  explicit TrianglePass(PipelineSpec spec)
      : SceneRenderPass(std::move(spec),
                        RasterPassAttachmentConfig{
                            .useColorAttachment = true,
                            .useDepthAttachment = false,
                            .useMsaaColorAttachment = false,
                            .resolveToSwapchain = false,
                            .useSwapchainColorAttachment = true,
                            .clearColor = {0.08f, 0.08f, 0.10f, 1.0f},
                        }) {}
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
      .appName = "Triangle To Swapchain",
      .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT,
  };

  PassRenderer renderer;
  VertexMesh triangleMesh;
  std::vector<RenderItem> renderItems;
  TrianglePass *trianglePass = nullptr;

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

  void initWindow() { window.create(WIDTH, HEIGHT, "Triangle To Swapchain"); }

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

    auto trianglePassLocal = std::make_unique<TrianglePass>(PipelineSpec{
        .shaderPath = ASSET_PATH + "/shaders/triangle_pass.spv",
        .cullMode = vk::CullModeFlagBits::eNone,
        .enableDepthTest = false,
        .enableDepthWrite = false,
    });

    trianglePass = trianglePassLocal.get();
    renderer.addPass(std::move(trianglePassLocal));
    renderer.initialize(deviceContext(), swapchainContext());

    renderItems = {
        RenderItem{
            .mesh = &triangleMesh,
            .descriptorBindings = nullptr,
            .targetPass = trianglePass,
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
