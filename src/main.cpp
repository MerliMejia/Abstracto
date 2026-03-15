#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "passes/DebugPass.h"
#include "passes/GeometryPass.h"
#include "passes/ImGuiPass.h"
#include "passes/PbrPass.h"
#include "passes/TonemapPass.h"
#include "renderable/FrameGeometryUniforms.h"
#include "renderable/RenderableModel.h"
#include "renderable/Sampler.h"
#include "renderer/PassRenderer.h"
#include "renderer/PipelineSpec.h"
#include "renderer/RenderPass.h"
#include "utils/DefaultDebugUI.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr bool DEBUG_SHOW_SOLID_TRANSFORM_PASS = false;
constexpr float CAMERA_NEAR_PLANE = 0.1f;
const std::string ASSET_PATH = "assets";

class DefaultExampleApp {
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
  BackendConfig config{.appName = "Default Example",
                       .maxFramesInFlight = MAX_FRAMES_IN_FLIGHT};

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

  PassRenderer renderer;
  std::vector<RenderItem> renderItems;

  RenderableModel sceneModel;
  FullscreenMesh lightQuad;
  FrameGeometryUniforms frameGeometryUniforms;
  Sampler sampler;
  ImageBasedLighting imageBasedLighting;
  GeometryPass *geometryPass = nullptr;
  PbrPass *pbrPass = nullptr;
  TonemapPass *tonemapPass = nullptr;
  DebugPass *debugPass = nullptr;
  ImGuiPass *imguiPass = nullptr;

  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  DefaultDebugUISettings debugUiSettings;
  float smoothedFrameTimeMs = 0.0f;

  void initWindow() { window.create(WIDTH, HEIGHT, "Default Example", true); }

  void syncProceduralSkySunWithLight() {
    const glm::vec3 sunDirection = -currentLightDirectionWorld();
    debugUiSettings.iblBakeSettings.sky.sunAzimuthRadians =
        std::atan2(sunDirection.y, sunDirection.x);
    debugUiSettings.iblBakeSettings.sky.sunElevationRadians =
        std::asin(glm::clamp(sunDirection.z, -1.0f, 1.0f));
  }

  std::string sceneModelPath() const {
    return ASSET_PATH + "/models/night.glb";
  }

  void rebuildSceneRenderItems() {
    renderItems = sceneModel.buildRenderItems(geometryPass);
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = pbrPass});
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = tonemapPass});
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = debugPass});
  }

  void reloadSceneModel() {
    backend.waitIdle();
    sceneModel.setSmoothGltfNormalsEnabled(
        debugUiSettings.smoothGltfNormalsEnabled);
    sceneModel.loadFromFile(sceneModelPath(), commandContext(), deviceContext(),
                            renderer.descriptorSetLayout(),
                            frameGeometryUniforms, sampler,
                            MAX_FRAMES_IN_FLIGHT);
    rebuildSceneRenderItems();
  }

  void initVulkan() {
    backend.initialize(window, config);
    debugUiSettings.iblBakeSettings.environmentHdrPath =
        ASSET_PATH + "/textures/dikhololo_night_4k.hdr";

    sampler.create(deviceContext());

    lightQuad = buildFullscreenQuadMesh();
    lightQuad.createVertexBuffer(commandContext(), deviceContext());
    lightQuad.createIndexBuffer(commandContext(), deviceContext());

    auto geometryPassLocal = std::make_unique<GeometryPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/geometry_pass.spv",
                     .cullMode = vk::CullModeFlagBits::eNone,
                     .frontFace = vk::FrontFace::eCounterClockwise});
    auto *geometryPassPtr = geometryPassLocal.get();
    geometryPass = geometryPassPtr;
    renderer.addPass(std::move(geometryPassLocal));

    auto pbrPassLocal = std::make_unique<PbrPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/pbr_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, geometryPassPtr);
    pbrPass = pbrPassLocal.get();
    if (debugUiSettings.syncSkySunToLight) {
      syncProceduralSkySunWithLight();
    }
    imageBasedLighting.create(deviceContext(), commandContext(),
                              debugUiSettings.iblBakeSettings);
    pbrPass->setImageBasedLighting(imageBasedLighting);
    renderer.addPass(std::move(pbrPassLocal));

    auto tonemapPassLocal = std::make_unique<TonemapPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/tonemap_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, pbrPass);
    tonemapPass = tonemapPassLocal.get();
    renderer.addPass(std::move(tonemapPassLocal));

    auto debugPassLocal = std::make_unique<DebugPass>(
        PipelineSpec{.shaderPath =
                         ASSET_PATH + "/shaders/debug_gbuffer_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, geometryPassPtr, pbrPass, tonemapPass);
    debugPass = debugPassLocal.get();
    debugPass->setSelectedOutput(
        static_cast<uint32_t>(debugUiSettings.presentedOutput));
    debugPass->setClipPlanes(CAMERA_NEAR_PLANE, debugUiSettings.cameraFarPlane);
    renderer.addPass(std::move(debugPassLocal));

    auto imguiPassLocal = std::make_unique<ImGuiPass>(
        window, backend.instance(), commandContext());
    imguiPass = imguiPassLocal.get();
    renderer.addPass(std::move(imguiPassLocal));

    renderer.initialize(deviceContext(), swapchainContext());

    frameGeometryUniforms.create(deviceContext(), MAX_FRAMES_IN_FLIGHT);
    sceneModel.setSmoothGltfNormalsEnabled(
        debugUiSettings.smoothGltfNormalsEnabled);
    sceneModel.loadFromFile(sceneModelPath(), commandContext(), deviceContext(),
                            renderer.descriptorSetLayout(),
                            frameGeometryUniforms, sampler,
                            MAX_FRAMES_IN_FLIGHT);
    rebuildSceneRenderItems();
  }

  glm::vec3 currentLightDirectionWorld() const {
    const float cosElevation = std::cos(debugUiSettings.lightElevationRadians);
    return glm::normalize(
        glm::vec3(cosElevation * std::cos(debugUiSettings.lightAzimuthRadians),
                  cosElevation * std::sin(debugUiSettings.lightAzimuthRadians),
                  std::sin(debugUiSettings.lightElevationRadians)));
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
    const float frameTimeMs = deltaSeconds * 1000.0f;
    if (smoothedFrameTimeMs == 0.0f) {
      smoothedFrameTimeMs = frameTimeMs;
    } else {
      smoothedFrameTimeMs =
          smoothedFrameTimeMs + (frameTimeMs - smoothedFrameTimeMs) * 0.1f;
    }
    const float smoothedFps =
        smoothedFrameTimeMs > 0.0f ? 1000.0f / smoothedFrameTimeMs : 0.0f;

    if (imguiPass != nullptr) {
      imguiPass->beginFrame();
      DefaultDebugUI defaultDebugUi = DefaultDebugUI::create(
          sceneModel, debugUiSettings,
          DefaultDebugUICallbacks{
              .reloadSceneModel = [this]() { reloadSceneModel(); },
              .syncProceduralSkySunWithLight =
                  [this]() { syncProceduralSkySunWithLight(); },
              .currentLightDirectionWorld =
                  [this]() { return currentLightDirectionWorld(); },
          },
          DefaultDebugUIPerformanceStats{.fps = smoothedFps,
                                         .frameTimeMs = smoothedFrameTimeMs});
      const DefaultDebugUIResult uiResult = defaultDebugUi.build();
      if (uiResult.materialChanged) {
        sceneModel.syncMaterialParameters();
      }
      imguiPass->endFrame();
      if (debugPass != nullptr) {
        debugPass->setSelectedOutput(
            static_cast<uint32_t>(debugUiSettings.presentedOutput));
        debugPass->setClipPlanes(CAMERA_NEAR_PLANE,
                                 debugUiSettings.cameraFarPlane);
      }
      if (uiResult.iblBakeRequested) {
        backend.waitIdle();
        if (debugUiSettings.syncSkySunToLight) {
          syncProceduralSkySunWithLight();
        }
        imageBasedLighting.rebuild(deviceContext(), commandContext(),
                                   debugUiSettings.iblBakeSettings);
        renderer.recreate(deviceContext(), swapchainContext());
      }
    }

    DefaultDebugCameraController cameraController =
        DefaultDebugCameraController::create(debugUiSettings);
    cameraController.update(deltaSeconds, window.handle());

    GeometryUniformData geometryUniformData{};

    geometryUniformData.model = glm::mat4(1.0f);
    geometryUniformData.model = glm::translate(geometryUniformData.model,
                                               debugUiSettings.modelPosition);
    geometryUniformData.model =
        glm::rotate(geometryUniformData.model,
                    glm::radians(debugUiSettings.modelRotationDegrees.x),
                    glm::vec3(1.0f, 0.0f, 0.0f));
    geometryUniformData.model =
        glm::rotate(geometryUniformData.model,
                    glm::radians(debugUiSettings.modelRotationDegrees.y),
                    glm::vec3(0.0f, 1.0f, 0.0f));
    geometryUniformData.model =
        glm::rotate(geometryUniformData.model,
                    glm::radians(debugUiSettings.modelRotationDegrees.z),
                    glm::vec3(0.0f, 0.0f, 1.0f));
    geometryUniformData.model =
        glm::scale(geometryUniformData.model, debugUiSettings.modelScale);
    geometryUniformData.modelNormal =
        glm::transpose(glm::inverse(geometryUniformData.model));

    geometryUniformData.view = glm::lookAt(
        debugUiSettings.cameraPosition,
        debugUiSettings.cameraPosition +
            DefaultDebugCameraController::forwardFromSettings(debugUiSettings),
        glm::vec3(0.0f, 0.0f, 1.0f));

    geometryUniformData.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        CAMERA_NEAR_PLANE, debugUiSettings.cameraFarPlane);

    // Vulkan inverts Y.
    geometryUniformData.proj[1][1] *= -1.0f;

    frameGeometryUniforms.write(frameState->frameIndex, geometryUniformData);

    if (pbrPass != nullptr) {
      glm::vec3 lightDirectionWorld = currentLightDirectionWorld();
      glm::vec3 lightDirectionView = glm::normalize(
          glm::mat3(geometryUniformData.view) * lightDirectionWorld);

      pbrPass->setCamera(geometryUniformData.proj, geometryUniformData.view);
      pbrPass->setDirectionalLight(lightDirectionView,
                                   debugUiSettings.lightColor *
                                       debugUiSettings.lightIntensity);
      pbrPass->setEnvironmentControls(
          debugUiSettings.environmentRotationRadians,
          debugUiSettings.environmentIntensity *
              debugUiSettings.environmentBackgroundWeight,
          debugUiSettings.environmentIntensity *
              debugUiSettings.environmentDiffuseWeight,
          debugUiSettings.environmentIntensity *
              debugUiSettings.environmentSpecularWeight,
          debugUiSettings.iblEnabled, debugUiSettings.skyboxVisible);
      pbrPass->setDielectricSpecularScale(
          debugUiSettings.dielectricSpecularScale);
      pbrPass->setDebugView(debugUiSettings.pbrDebugView);
    }
    if (tonemapPass != nullptr) {
      const glm::vec3 lightRadiance =
          debugUiSettings.lightColor * debugUiSettings.lightIntensity;
      const float lightLuminance =
          glm::dot(lightRadiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
      const float resolvedExposure =
          debugUiSettings.autoExposureEnabled
              ? glm::clamp(debugUiSettings.autoExposureKey /
                               std::max(lightLuminance, 0.001f),
                           0.05f, 8.0f)
              : debugUiSettings.exposure;
      tonemapPass->setExposure(resolvedExposure);
      tonemapPass->setWhitePoint(debugUiSettings.whitePoint);
      tonemapPass->setGamma(debugUiSettings.gamma);
      tonemapPass->setOperator(debugUiSettings.tonemapOperator);
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
    DefaultExampleApp app;
    app.run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
