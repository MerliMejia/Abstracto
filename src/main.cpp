#include "backend/AppWindow.h"
#include "backend/VulkanBackend.h"
#include "renderable/DescriptorBindings.h"
#include "renderable/FrameUniforms.h"
#include "renderable/Mesh.h"
#include "renderable/Sampler.h"
#include "renderable/Texture.h"
#include "renderer/ForwardRenderer.h"
#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
const std::string ASSET_PATH = "assets";
const std::string MODEL_PATH = ASSET_PATH + "/models/viking_room.obj";
const std::string TEXTURE_PATH = ASSET_PATH + "/textures/viking_room.png";
const std::string SHADER_PATH = ASSET_PATH + "/shaders/slang.spv";
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

class HelloTriangleApplication {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  AppWindow appWindow;
  VulkanBackend backend;
  BackendConfig backendConfig{.appName = "App Window",
                              .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT};

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }
  FrameSync &frameSync() { return backend.sync(); }

  // For now just 1 mesh, at some point should be more.
  Mesh mesh;
  Texture texture;
  Sampler sampler;
  FrameUniforms frameUniforms;
  DescriptorBindings descriptorBindings;
  ForwardRenderer forwardRenderer;

  void initWindow() { appWindow.create(WIDTH, HEIGHT, "App Window"); }

  void initVulkan() {
    backend.initialize(appWindow, backendConfig);
    forwardRenderer.initialize(deviceContext(), swapchainContext(),
                               SHADER_PATH);
    // Per texture init
    texture.create(TEXTURE_PATH, commandContext(), deviceContext());
    sampler.create(deviceContext());
    // Per mesh init
    mesh.loadModel(MODEL_PATH);
    mesh.createVertexBuffer(commandContext(), deviceContext());
    mesh.createIndexBuffer(commandContext(), deviceContext());
    //................
    frameUniforms.create(deviceContext(), MAX_FRAMES_IN_FLIGHT);
    descriptorBindings.create(
        deviceContext(), forwardRenderer.descriptorSetLayout(), frameUniforms,
        texture, sampler, MAX_FRAMES_IN_FLIGHT);
  }

  void mainLoop() {
    while (!appWindow.shouldClose()) {
      appWindow.pollEvents();
      drawFrame();
    }

    backend.waitIdle();
  }

  void cleanup() const { appWindow.destroy(); }

  void recreateSwapChain() {
    backend.recreateSwapchain(appWindow);
    forwardRenderer.recreate(deviceContext(), swapchainContext(), SHADER_PATH);
  }

  void updateUniformBuffer(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.model = rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                      glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        0.1f, 10.0f);
    ubo.proj[1][1] *= -1;

    frameUniforms.write(currentImage, ubo);
  }

  void drawFrame() {
    auto frameState = backend.beginFrame(appWindow);
    if (!frameState) {
      recreateSwapChain();
      return;
    }
    updateUniformBuffer(frameState->frameIndex);
    forwardRenderer.record(
        commandContext().commandBuffer(frameState->frameIndex),
        swapchainContext(), mesh, descriptorBindings, frameState->frameIndex,
        frameState->imageIndex);
    if (backend.endFrame(*frameState, appWindow)) {
      recreateSwapChain();
    }
  }
};

int main() {
  try {
    HelloTriangleApplication app;
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
