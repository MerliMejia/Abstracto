#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "passes/DebugPresentPass.h"
#include "passes/SolidTransformPass.h"
#include "passes/TexturePass.h"
#include "renderer/ForwardRenderer.h"
#include "renderer/PipelineSpec.h"
#include <cstdlib>
#include <exception>
#include <iostream>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr bool DEBUG_SHOW_SOLID_TRANSFORM_PASS = false;
const std::string ASSET_PATH = "assets";

class DoublePassApp {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLopp();
    cleanup();
  }

private:
  AppWindow window;
  VulkanBackend backend;
  BackendConfig config{.appName = "Double Pass",
                       .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT};

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

  TypedMesh<PositionUvVertex> modelMesh;
  TypedMesh<PositionUvVertex> fullscreenQuadMesh;

  std::vector<PositionUvVertex> quadVertices = {
      {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
      {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
      {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
      {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
  };

  std::vector<uint32_t> quadIndices = {
      0, 1, 2, 2, 3, 0,
  };

  Texture albedoTexture;
  ForwardRenderer renderer;
  std::unique_ptr<SolidTransformPass> solidTransformPass;
  std::unique_ptr<TexturePass> texturePass;
  std::unique_ptr<DebugPresentPass> debugPresentPass;
  std::vector<RenderItem> renderItems;

  PipelineSpec solidSpec{
      .shaderPath = "assets/shaders/solid_transform_pass.spv",
      .vertexEntry = "vertMain",
      .fragmentEntry = "fragMain",
      .topology = vk::PrimitiveTopology::eTriangleList,
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eNone,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .enableDepthTest = true,
      .enableDepthWrite = true,
      .enableBlending = false,
  };

  PipelineSpec textureSpec{
      .shaderPath = "assets/shaders/texture_pass.spv",
      .vertexEntry = "vertMain",
      .fragmentEntry = "fragMain",
      .topology = vk::PrimitiveTopology::eTriangleList,
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eNone,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .enableDepthTest = false,
      .enableDepthWrite = false,
      .enableBlending = false,
  };

  PipelineSpec debugPresentSpec{
      .shaderPath = "assets/shaders/debug_present_pass.spv",
      .vertexEntry = "vertMain",
      .fragmentEntry = "fragMain",
      .topology = vk::PrimitiveTopology::eTriangleList,
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eNone,
      .frontFace = vk::FrontFace::eCounterClockwise,
      .enableDepthTest = false,
      .enableDepthWrite = false,
      .enableBlending = false,
  };

  void initWindow() { window.create(WIDTH, HEIGHT, "Double Pass"); }
  void initVulkan() {
    backend.initialize(window, config);
    auto objData = loadObjData(ASSET_PATH + "/models/viking_room.obj");
    modelMesh = buildMeshFromObj<PositionUvVertex>(
        objData, [](const ObjVertexRef &vertex) {
          return PositionUvVertex{
              .pos = vertex.position(),
              .uv = vertex.texCoord(),
          };
        });
    modelMesh.createVertexBuffer(commandContext(), deviceContext());
    modelMesh.createIndexBuffer(commandContext(), deviceContext());

    fullscreenQuadMesh.setGeometry(quadVertices, quadIndices);
    fullscreenQuadMesh.createVertexBuffer(backend.commands(), backend.device());
    fullscreenQuadMesh.createIndexBuffer(backend.commands(), backend.device());

    solidTransformPass =
        std::make_unique<SolidTransformPass>(solidSpec, MAX_FRAMES_IN_FLIGHT);

    auto *solidPassPtr = solidTransformPass.get();
    renderer.addPass(std::move(solidTransformPass));

    RenderPass *presentPassPtr = nullptr;
    if (DEBUG_SHOW_SOLID_TRANSFORM_PASS) {
      debugPresentPass = std::make_unique<DebugPresentPass>(
          debugPresentSpec, MAX_FRAMES_IN_FLIGHT, solidPassPtr);
      presentPassPtr = debugPresentPass.get();
      renderer.addPass(std::move(debugPresentPass));
    } else {
      albedoTexture.create("assets/textures/viking_room.png", commandContext(),
                           deviceContext());
      texturePass = std::make_unique<TexturePass>(
          textureSpec, MAX_FRAMES_IN_FLIGHT, *solidPassPtr, albedoTexture);
      presentPassPtr = texturePass.get();
      renderer.addPass(std::move(texturePass));
    }

    renderer.initialize(deviceContext(), swapchainContext());

    renderItems = {
        RenderItem{
            .mesh = &modelMesh,
            .descriptorBindings = nullptr,
            .targetPass = solidPassPtr,
        },
        RenderItem{
            .mesh = &fullscreenQuadMesh,
            .descriptorBindings = nullptr,
            .targetPass = presentPassPtr,
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

    renderer.record(backend.commands().commandBuffer(frameState->frameIndex),
                    swapchainContext(), renderItems, frameState->frameIndex,
                    frameState->imageIndex);

    bool shouldRecreate = backend.endFrame(*frameState, window);
    if (shouldRecreate) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
    }
  }
  void mainLopp() {
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
    DoublePassApp app;
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
