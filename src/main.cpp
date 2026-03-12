#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "passes/DebugPass.h"
#include "passes/GeometryPass.h"
#include "passes/LightPass.h"
#include "renderable/FrameUniforms.h"
#include "renderable/RenderableModel.h"
#include "renderable/Sampler.h"
#include "renderer/PassRenderer.h"
#include "renderer/PipelineSpec.h"
#include "renderer/RenderPass.h"
#include "vulkan/vulkan.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr bool DEBUG_SHOW_SOLID_TRANSFORM_PASS = false;
const std::string ASSET_PATH = "assets";

enum class PresentedOutput : uint32_t {
  Albedo = 0,
  Normal = 1,
  Material = 2,
  Depth = 3,
  Light = 4,
};

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

  PassRenderer renderer;
  std::vector<RenderItem> renderItems;

  RenderableModel sceneModel;
  FullscreenMesh lightQuad;
  FrameUniforms frameUniforms;
  Sampler sampler;
  LightPass *lightPass = nullptr;
  DebugPass *debugPass = nullptr;
  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  float lightAzimuthRadians = glm::radians(225.0f);
  float lightElevationRadians = glm::radians(-35.264f);
  float lightIntensity = 1.0f;
  PresentedOutput presentedOutput = PresentedOutput::Light;

  void initWindow() { window.create(WIDTH, HEIGHT, "Double Pass"); }

  void initVulkan() {
    backend.initialize(window, config);

    sampler.create(deviceContext());

    lightQuad = buildFullscreenQuadMesh();
    lightQuad.createVertexBuffer(commandContext(), deviceContext());
    lightQuad.createIndexBuffer(commandContext(), deviceContext());

    auto geometryPass = std::make_unique<GeometryPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/geometry_pass.spv",
                     .cullMode = vk::CullModeFlagBits::eBack,
                     .frontFace = vk::FrontFace::eCounterClockwise});
    auto *geometryPassPtr = geometryPass.get();
    renderer.addPass(std::move(geometryPass));

    auto lightPassLocal = std::make_unique<LightPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/light_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, geometryPassPtr);
    lightPass = lightPassLocal.get();
    renderer.addPass(std::move(lightPassLocal));

    auto debugPassLocal = std::make_unique<DebugPass>(
        PipelineSpec{.shaderPath =
                         ASSET_PATH + "/shaders/debug_gbuffer_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, geometryPassPtr, lightPass);
    debugPass = debugPassLocal.get();
    debugPass->setSelectedOutput(static_cast<uint32_t>(presentedOutput));
    renderer.addPass(std::move(debugPassLocal));

    renderer.initialize(deviceContext(), swapchainContext());

    frameUniforms.create(deviceContext(), MAX_FRAMES_IN_FLIGHT);
    sceneModel.loadFromFile(ASSET_PATH + "/models/toy_car.glb",
                            commandContext(), deviceContext(),
                            renderer.descriptorSetLayout(), frameUniforms,
                            sampler, MAX_FRAMES_IN_FLIGHT);

    renderItems = sceneModel.buildRenderItems(geometryPassPtr);
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = lightPass});
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = debugPass});
  }

  glm::vec3 currentLightDirectionWorld() const {
    const float cosElevation = std::cos(lightElevationRadians);
    return glm::normalize(
        glm::vec3(cosElevation * std::cos(lightAzimuthRadians),
                  cosElevation * std::sin(lightAzimuthRadians),
                  std::sin(lightElevationRadians)));
  }

  void processLightControls(float deltaSeconds) {
    const float orbitSpeed = glm::radians(90.0f);
    const float intensitySpeed = 1.5f;

    if (glfwGetKey(window.handle(), GLFW_KEY_LEFT) == GLFW_PRESS) {
      lightAzimuthRadians -= orbitSpeed * deltaSeconds;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_RIGHT) == GLFW_PRESS) {
      lightAzimuthRadians += orbitSpeed * deltaSeconds;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_UP) == GLFW_PRESS) {
      lightElevationRadians += orbitSpeed * deltaSeconds;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_DOWN) == GLFW_PRESS) {
      lightElevationRadians -= orbitSpeed * deltaSeconds;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_Q) == GLFW_PRESS) {
      lightIntensity -= intensitySpeed * deltaSeconds;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_E) == GLFW_PRESS) {
      lightIntensity += intensitySpeed * deltaSeconds;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_R) == GLFW_PRESS) {
      lightAzimuthRadians = glm::radians(225.0f);
      lightElevationRadians = glm::radians(-35.264f);
      lightIntensity = 1.0f;
    }

    lightElevationRadians = glm::clamp(
        lightElevationRadians, glm::radians(-89.0f), glm::radians(89.0f));
    lightIntensity = glm::clamp(lightIntensity, 0.0f, 5.0f);
  }

  void processDebugControls() {
    if (glfwGetKey(window.handle(), GLFW_KEY_1) == GLFW_PRESS) {
      presentedOutput = PresentedOutput::Material;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_2) == GLFW_PRESS) {
      presentedOutput = PresentedOutput::Albedo;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_3) == GLFW_PRESS) {
      presentedOutput = PresentedOutput::Depth;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_4) == GLFW_PRESS) {
      presentedOutput = PresentedOutput::Normal;
    }
    if (glfwGetKey(window.handle(), GLFW_KEY_5) == GLFW_PRESS) {
      presentedOutput = PresentedOutput::Light;
    }

    if (debugPass != nullptr) {
      debugPass->setSelectedOutput(static_cast<uint32_t>(presentedOutput));
    }
  }

  void drawFrame() {
    auto frameState = backend.beginFrame(window);

    if (!frameState.has_value()) {
      backend.recreateSwapchain(window);
      renderer.recreate(deviceContext(), swapchainContext());
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = std::min(
        std::chrono::duration<float>(now - lastFrameTime).count(), 0.1f);
    lastFrameTime = now;

    processLightControls(deltaSeconds);
    processDebugControls();

    UniformBufferObject ubo{};

    ubo.model = glm::scale(glm::mat4(1.0f), glm::vec3(40.0f));
    ubo.model = glm::rotate(ubo.model, glm::radians(-90.0f),
                            glm::vec3(1.0f, 0.0f, 0.0f));
    ubo.model = glm::rotate(ubo.model, glm::radians(180.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.view =
        glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        0.1f, 10.0f);

    // Vulkan inverts Y.
    ubo.proj[1][1] *= -1.0f;

    frameUniforms.write(frameState->frameIndex, ubo);

    if (lightPass != nullptr) {
      glm::vec3 lightDirectionWorld = currentLightDirectionWorld();
      glm::vec3 lightDirectionView =
          glm::normalize(glm::mat3(ubo.view) * lightDirectionWorld);

      lightPass->setProjection(ubo.proj);
      lightPass->setDirectionalLight(
          lightDirectionView, glm::vec3(1.0f, 0.95f, 0.9f) * lightIntensity);
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
