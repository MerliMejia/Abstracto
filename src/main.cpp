#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "passes/GeometryPass.h"
#include "passes/LightPass.h"
#include "renderable/DescriptorBindings.h"
#include "renderable/FrameUniforms.h"
#include "renderable/Mesh.h"
#include "renderable/Sampler.h"
#include "renderable/Texture.h"
#include "renderer/PassRenderer.h"
#include "renderer/PipelineSpec.h"
#include "renderer/RenderPass.h"
#include "vulkan/vulkan.hpp"
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

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

  PassRenderer renderer;
  std::vector<RenderItem> renderItems;

  ObjGeometryMesh mesh;
  FullscreenMesh lightQuad;
  FrameUniforms frameUniforms;
  Texture albedoTexture;
  Sampler sampler;
  DescriptorBindings geometryBindings;
  LightPass *lightPass = nullptr;
  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  float lightAzimuthRadians = glm::radians(225.0f);
  float lightElevationRadians = glm::radians(-35.264f);
  float lightIntensity = 1.0f;

  void initWindow() { window.create(WIDTH, HEIGHT, "Double Pass"); }
  void initVulkan() {
    backend.initialize(window, config);

    mesh.loadModel(ASSET_PATH + "/models/viking_room.obj");
    mesh.createVertexBuffer(commandContext(), deviceContext());
    mesh.createIndexBuffer(commandContext(), deviceContext());

    albedoTexture.create(ASSET_PATH + "/textures/viking_room.png",
                         commandContext(), deviceContext());
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

    renderer.initialize(deviceContext(), swapchainContext());

    frameUniforms.create(deviceContext(), MAX_FRAMES_IN_FLIGHT);
    geometryBindings.create(deviceContext(), renderer.descriptorSetLayout(),
                            frameUniforms, albedoTexture, sampler,
                            MAX_FRAMES_IN_FLIGHT);

    renderItems = {RenderItem{.mesh = &mesh,
                              .descriptorBindings = &geometryBindings,
                              .targetPass = geometryPassPtr},
                   RenderItem{.mesh = &lightQuad,
                              .descriptorBindings = nullptr,
                              .targetPass = lightPass}};
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

    UniformBufferObject ubo{};

    ubo.model = glm::mat4(1.0f);

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
