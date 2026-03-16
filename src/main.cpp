#include "backend/AppWindow.h"
#include "backend/BackendConfig.h"
#include "backend/VulkanBackend.h"
#include "passes/DebugOverlayPass.h"
#include "passes/DebugPresentPass.h"
#include "passes/GeometryPass.h"
#include "passes/ImGuiPass.h"
#include "passes/PbrPass.h"
#include "passes/ShadowPass.h"
#include "passes/TonemapPass.h"
#include "renderable/DebugLightMeshes.h"
#include "renderable/FrameGeometryUniforms.h"
#include "renderable/RenderableModel.h"
#include "renderable/Sampler.h"
#include "renderer/PassRenderer.h"
#include "renderer/PipelineSpec.h"
#include "renderer/RenderPass.h"
#include "utils/DebugSessionIO.h"
#include "utils/DefaultDebugUI.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr bool DEBUG_SHOW_SOLID_TRANSFORM_PASS = false;
constexpr float CAMERA_NEAR_PLANE = 0.1f;
constexpr uint32_t SHADOW_MAP_RESOLUTION = 1024;
constexpr uint32_t MAX_SPOT_SHADOW_PASSES = 3;
const std::string ASSET_PATH = "assets";
const std::filesystem::path DEBUG_SESSION_PATH =
    std::filesystem::path(ASSET_PATH) / "debug" / "last_session.json";

class DefaultExampleApp {
public:
  void run() {
    loadDebugSessionFromDisk();
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
  vk::raii::DescriptorSetLayout &sceneDescriptorSetLayout() {
    if (geometryPass == nullptr || geometryPass->descriptorSetLayout() == nullptr) {
      throw std::runtime_error(
          "GeometryPass descriptor set layout is not available");
    }
    return *geometryPass->descriptorSetLayout();
  }

  PassRenderer renderer;
  std::vector<RenderItem> renderItems;

  RenderableModel sceneModel;
  FullscreenMesh lightQuad;
  TypedMesh<Vertex> pointLightMarkerMesh;
  TypedMesh<Vertex> spotLightMarkerMesh;
  TypedMesh<Vertex> directionalLightMarkerMesh;
  FrameGeometryUniforms frameGeometryUniforms;
  Sampler sampler;
  ImageBasedLighting imageBasedLighting;
  GeometryPass *geometryPass = nullptr;
  ShadowPass *directionalShadowPass = nullptr;
  std::array<ShadowPass *, MAX_SPOT_SHADOW_PASSES> spotShadowPasses{
      nullptr, nullptr, nullptr};
  PbrPass *pbrPass = nullptr;
  TonemapPass *tonemapPass = nullptr;
  DebugPresentPass *debugPresentPass = nullptr;
  DebugOverlayPass *debugOverlayPass = nullptr;
  ImGuiPass *imguiPass = nullptr;

  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  DefaultDebugUISettings debugUiSettings;
  float smoothedFrameTimeMs = 0.0f;

  void initWindow() { window.create(WIDTH, HEIGHT, "Default Example", true); }

  void syncProceduralSkySunWithLight() {
    const glm::vec3 sunDirection = -currentPrimaryDirectionalLightWorld();
    debugUiSettings.iblBakeSettings.sky.sunAzimuthRadians =
        std::atan2(sunDirection.y, sunDirection.x);
    debugUiSettings.iblBakeSettings.sky.sunElevationRadians =
        std::asin(glm::clamp(sunDirection.z, -1.0f, 1.0f));
  }

  std::string sceneModelPath() const {
    return ASSET_PATH + "/models/night.glb";
  }

  void ensureDefaultEnvironmentPath() {
    if (debugUiSettings.iblBakeSettings.environmentHdrPath.empty()) {
      debugUiSettings.iblBakeSettings.environmentHdrPath =
          ASSET_PATH + "/textures/dikhololo_night_4k.hdr";
    }
  }

  void rebuildSceneRenderItems() {
    renderItems = sceneModel.buildRenderItems(geometryPass);
    if (directionalShadowPass != nullptr) {
      auto shadowItems = sceneModel.buildRenderItems(directionalShadowPass);
      renderItems.insert(renderItems.end(), shadowItems.begin(),
                         shadowItems.end());
    }
    for (ShadowPass *spotShadowPass : spotShadowPasses) {
      if (spotShadowPass == nullptr) {
        continue;
      }
      auto shadowItems = sceneModel.buildRenderItems(spotShadowPass);
      renderItems.insert(renderItems.end(), shadowItems.begin(),
                         shadowItems.end());
    }
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = pbrPass});
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = tonemapPass});
    renderItems.push_back(RenderItem{.mesh = &lightQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = debugPresentPass});
  }

  void reloadSceneModel() {
    backend.waitIdle();
    sceneModel.loadFromFile(sceneModelPath(), commandContext(), deviceContext(),
                            sceneDescriptorSetLayout(),
                            frameGeometryUniforms, sampler,
                            MAX_FRAMES_IN_FLIGHT);
    rebuildSceneRenderItems();
  }

  void loadDebugSessionFromDisk() {
    try {
      DebugSessionIO::loadDebugSession(DEBUG_SESSION_PATH, debugUiSettings);
      ensureDefaultEnvironmentPath();
    } catch (const std::exception &e) {
      std::cerr << "Failed to load debug session: " << e.what() << std::endl;
    }
  }

  void saveDebugSessionToDisk() const {
    if (!DebugSessionIO::saveDebugSession(DEBUG_SESSION_PATH,
                                          debugUiSettings)) {
      std::cerr << "Failed to save debug session to "
                << DEBUG_SESSION_PATH.string() << std::endl;
    }
  }

  void applyLoadedDebugSettings() {
    ensureDefaultEnvironmentPath();
    backend.waitIdle();
    if (debugUiSettings.syncSkySunToLight) {
      syncProceduralSkySunWithLight();
    }
    imageBasedLighting.rebuild(deviceContext(), commandContext(),
                               debugUiSettings.iblBakeSettings);
    renderer.recreate(deviceContext(), swapchainContext());
    reloadSceneModel();
  }

  void registerShadowPasses() {
    auto directionalShadowPassLocal = std::make_unique<ShadowPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/shadow_pass.spv",
                     .cullMode = vk::CullModeFlagBits::eBack,
                     .frontFace = vk::FrontFace::eCounterClockwise,
                     .enableDepthBias = true},
        MAX_FRAMES_IN_FLIGHT, SHADOW_MAP_RESOLUTION);
    directionalShadowPass = directionalShadowPassLocal.get();
    renderer.addPass(std::move(directionalShadowPassLocal));

    for (uint32_t index = 0; index < MAX_SPOT_SHADOW_PASSES; ++index) {
      auto spotShadowPassLocal = std::make_unique<ShadowPass>(
          PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/shadow_pass.spv",
                       .cullMode = vk::CullModeFlagBits::eBack,
                       .frontFace = vk::FrontFace::eCounterClockwise,
                       .enableDepthBias = true},
          MAX_FRAMES_IN_FLIGHT, SHADOW_MAP_RESOLUTION);
      spotShadowPasses[index] = spotShadowPassLocal.get();
      renderer.addPass(std::move(spotShadowPassLocal));
    }
  }

  void initVulkan() {
    backend.initialize(window, config);
    ensureDefaultEnvironmentPath();

    sampler.create(deviceContext());
    registerShadowPasses();

    lightQuad = buildFullscreenQuadMesh();
    lightQuad.createVertexBuffer(commandContext(), deviceContext());
    lightQuad.createIndexBuffer(commandContext(), deviceContext());

    pointLightMarkerMesh = buildPointLightMarkerMesh();
    pointLightMarkerMesh.createVertexBuffer(commandContext(), deviceContext());
    pointLightMarkerMesh.createIndexBuffer(commandContext(), deviceContext());

    spotLightMarkerMesh = buildSpotLightMarkerMesh();
    spotLightMarkerMesh.createVertexBuffer(commandContext(), deviceContext());
    spotLightMarkerMesh.createIndexBuffer(commandContext(), deviceContext());

    directionalLightMarkerMesh = buildDirectionalLightMarkerMesh();
    directionalLightMarkerMesh.createVertexBuffer(commandContext(),
                                                  deviceContext());
    directionalLightMarkerMesh.createIndexBuffer(commandContext(),
                                                 deviceContext());

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
    pbrPass->setShadowPass(0, *directionalShadowPass);
    for (uint32_t index = 0; index < MAX_SPOT_SHADOW_PASSES; ++index) {
      pbrPass->setShadowPass(index + 1, *spotShadowPasses[index]);
    }
    renderer.addPass(std::move(pbrPassLocal));

    auto tonemapPassLocal = std::make_unique<TonemapPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/tonemap_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, pbrPass);
    tonemapPass = tonemapPassLocal.get();
    renderer.addPass(std::move(tonemapPassLocal));

    auto debugPresentPassLocal = std::make_unique<DebugPresentPass>(
        PipelineSpec{.shaderPath =
                         ASSET_PATH + "/shaders/debug_gbuffer_pass.spv",
                     .enableDepthTest = false,
                     .enableDepthWrite = false},
        MAX_FRAMES_IN_FLIGHT, geometryPassPtr, pbrPass, tonemapPass,
        directionalShadowPass, spotShadowPasses);
    debugPresentPass = debugPresentPassLocal.get();
    debugPresentPass->setSelectedOutput(
        static_cast<uint32_t>(debugUiSettings.presentedOutput));
    debugPresentPass->setClipPlanes(CAMERA_NEAR_PLANE,
                                    debugUiSettings.cameraFarPlane);
    renderer.addPass(std::move(debugPresentPassLocal));

    auto debugOverlayPassLocal = std::make_unique<DebugOverlayPass>(
        PipelineSpec{.shaderPath =
                         ASSET_PATH + "/shaders/debug_overlay_pass.spv",
                     .topology = vk::PrimitiveTopology::eLineList,
                     .cullMode = vk::CullModeFlagBits::eNone,
                     .enableDepthTest = false,
                     .enableDepthWrite = false,
                     .enableBlending = true},
        MAX_FRAMES_IN_FLIGHT, &debugUiSettings.sceneLights);
    debugOverlayPass = debugOverlayPassLocal.get();
    debugOverlayPass->setPointMarkerMesh(pointLightMarkerMesh);
    debugOverlayPass->setSpotMarkerMesh(spotLightMarkerMesh);
    debugOverlayPass->setDirectionalMarkerMesh(directionalLightMarkerMesh);
    debugOverlayPass->setMarkersVisible(debugUiSettings.lightMarkersVisible);
    debugOverlayPass->setMarkerScale(debugUiSettings.lightMarkerScale);
    debugOverlayPass->setDirectionalAnchor(debugUiSettings.modelPosition);
    renderer.addPass(std::move(debugOverlayPassLocal));

    auto imguiPassLocal = std::make_unique<ImGuiPass>(
        window, backend.instance(), commandContext());
    imguiPass = imguiPassLocal.get();
    renderer.addPass(std::move(imguiPassLocal));

    renderer.initialize(deviceContext(), swapchainContext());

    frameGeometryUniforms.create(deviceContext(), MAX_FRAMES_IN_FLIGHT);
    sceneModel.loadFromFile(sceneModelPath(), commandContext(), deviceContext(),
                            sceneDescriptorSetLayout(),
                            frameGeometryUniforms, sampler,
                            MAX_FRAMES_IN_FLIGHT);
    rebuildSceneRenderItems();
  }

  glm::vec3 currentPrimaryDirectionalLightWorld() const {
    const int directionalIndex =
        debugUiSettings.sceneLights.firstDirectionalLightIndex();
    if (directionalIndex < 0) {
      return glm::normalize(glm::vec3(-0.55f, -0.25f, -1.0f));
    }
    return debugUiSettings.sceneLights
        .lights()[static_cast<size_t>(directionalIndex)]
        .direction;
  }

  glm::vec3 estimatedSceneLightRadiance() const {
    glm::vec3 radiance(0.0f);
    for (const auto &light : debugUiSettings.sceneLights.lights()) {
      if (!light.enabled) {
        continue;
      }
      radiance += light.color * light.radianceScale();
    }
    return radiance;
  }

  static glm::vec3 shadowUpVector(const glm::vec3 &direction) {
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(direction, worldUp)) > 0.98f) {
      return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return worldUp;
  }

  glm::mat4 buildDirectionalShadowMatrix(const SceneLight &light) const {
    const glm::vec3 direction = glm::normalize(light.direction);
    const float shadowExtent =
        std::max(debugUiSettings.directionalShadowExtent, 0.5f);
    const float nearPlane =
        std::max(debugUiSettings.directionalShadowNearPlane, 0.01f);
    const float farPlane =
        std::max(debugUiSettings.directionalShadowFarPlane, nearPlane + 0.5f);
    const glm::vec3 target = debugUiSettings.modelPosition;
    const glm::vec3 eye = target - direction * (farPlane * 0.5f);
    const glm::mat4 view = glm::lookAt(eye, target, shadowUpVector(direction));
    glm::mat4 proj = glm::ortho(-shadowExtent, shadowExtent, -shadowExtent,
                                shadowExtent, nearPlane, farPlane);
    proj[1][1] *= -1.0f;
    return proj * view;
  }

  glm::mat4 buildSpotShadowMatrix(const SceneLight &light) const {
    const glm::vec3 direction = glm::normalize(light.direction);
    const float nearPlane = 0.05f;
    const float farPlane = std::max(light.range, nearPlane + 0.05f);
    const float fovRadians =
        std::clamp(light.outerConeAngleRadians * 2.0f, glm::radians(1.0f),
                   glm::radians(179.0f));
    const glm::mat4 view = glm::lookAt(
        light.position, light.position + direction, shadowUpVector(direction));
    glm::mat4 proj = glm::perspective(fovRadians, 1.0f, nearPlane, farPlane);
    proj[1][1] *= -1.0f;
    return proj * view;
  }

  void configureShadowPasses(const GeometryUniformData &geometryUniformData) {
    if (pbrPass == nullptr) {
      return;
    }

    pbrPass->clearLightShadows();

    if (directionalShadowPass != nullptr) {
      directionalShadowPass->setEnabled(false);
      directionalShadowPass->setModelMatrix(geometryUniformData.model);
      directionalShadowPass->setLightViewProj(glm::mat4(1.0f));
    }
    for (ShadowPass *spotShadowPass : spotShadowPasses) {
      if (spotShadowPass == nullptr) {
        continue;
      }
      spotShadowPass->setEnabled(false);
      spotShadowPass->setModelMatrix(geometryUniformData.model);
      spotShadowPass->setLightViewProj(glm::mat4(1.0f));
    }

    if (!debugUiSettings.shadowsEnabled) {
      return;
    }

    bool directionalAssigned = false;
    uint32_t spotShadowSlot = 0;
    const auto &lights = debugUiSettings.sceneLights.lights();
    for (size_t sceneLightIndex = 0; sceneLightIndex < lights.size();
         ++sceneLightIndex) {
      const auto &light = lights[sceneLightIndex];
      if (!light.enabled || !light.castsShadow) {
        continue;
      }

      const auto uniformLightIndex =
          pbrPass->uniformLightIndexForSceneLight(sceneLightIndex);
      if (!uniformLightIndex.has_value()) {
        continue;
      }

      if (light.type == SceneLightType::Directional && !directionalAssigned &&
          directionalShadowPass != nullptr) {
        const glm::mat4 shadowMatrix = buildDirectionalShadowMatrix(light);
        directionalShadowPass->setEnabled(true);
        directionalShadowPass->setLightViewProj(shadowMatrix);
        pbrPass->setLightShadow(
            *uniformLightIndex, 0, shadowMatrix, light.shadowBias,
            light.shadowNormalBias,
            static_cast<float>(directionalShadowPass->resolution()));
        directionalAssigned = true;
        continue;
      }

      if (light.type == SceneLightType::Spot &&
          spotShadowSlot < MAX_SPOT_SHADOW_PASSES &&
          spotShadowPasses[spotShadowSlot] != nullptr) {
        const glm::mat4 shadowMatrix = buildSpotShadowMatrix(light);
        spotShadowPasses[spotShadowSlot]->setEnabled(true);
        spotShadowPasses[spotShadowSlot]->setLightViewProj(shadowMatrix);
        pbrPass->setLightShadow(
            *uniformLightIndex, spotShadowSlot + 1, shadowMatrix,
            light.shadowBias, light.shadowNormalBias,
            static_cast<float>(spotShadowPasses[spotShadowSlot]->resolution()));
        ++spotShadowSlot;
      }
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
              .currentPrimaryDirectionalLightWorld =
                  [this]() { return currentPrimaryDirectionalLightWorld(); },
          },
          DefaultDebugUIPerformanceStats{.fps = smoothedFps,
                                         .frameTimeMs = smoothedFrameTimeMs});
      const DefaultDebugUIResult uiResult = defaultDebugUi.build();
      if (uiResult.materialChanged) {
        sceneModel.syncMaterialParameters();
      }
      imguiPass->endFrame();
      if (uiResult.saveSessionRequested) {
        saveDebugSessionToDisk();
      }
      if (uiResult.reloadSessionRequested) {
        loadDebugSessionFromDisk();
        applyLoadedDebugSettings();
      }
      if (uiResult.resetSessionRequested) {
        debugUiSettings = DefaultDebugUISettings{};
        applyLoadedDebugSettings();
      }
      if (debugPresentPass != nullptr) {
        debugPresentPass->setSelectedOutput(
            static_cast<uint32_t>(debugUiSettings.presentedOutput));
        debugPresentPass->setClipPlanes(CAMERA_NEAR_PLANE,
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
        glm::vec3(0.0f, 1.0f, 0.0f));

    geometryUniformData.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        CAMERA_NEAR_PLANE, debugUiSettings.cameraFarPlane);

    // Vulkan inverts Y.
    geometryUniformData.proj[1][1] *= -1.0f;

    frameGeometryUniforms.write(frameState->frameIndex, geometryUniformData);

    if (pbrPass != nullptr) {
      pbrPass->setCamera(geometryUniformData.proj, geometryUniformData.view);
      pbrPass->setSceneLights(debugUiSettings.sceneLights);
      pbrPass->clearLightShadows();
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
      configureShadowPasses(geometryUniformData);
    }
    if (debugOverlayPass != nullptr) {
      debugOverlayPass->setCamera(geometryUniformData.view,
                                  geometryUniformData.proj);
      debugOverlayPass->setSceneLights(debugUiSettings.sceneLights);
      debugOverlayPass->setMarkersVisible(debugUiSettings.lightMarkersVisible);
      debugOverlayPass->setMarkerScale(debugUiSettings.lightMarkerScale);
      debugOverlayPass->setDirectionalAnchor(debugUiSettings.modelPosition);
    }
    if (tonemapPass != nullptr) {
      const glm::vec3 lightRadiance = estimatedSceneLightRadiance();
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

    auto &commandBuffer =
        backend.commands().commandBuffer(frameState->frameIndex);
    commandBuffer.begin({});
    RenderPassContext context{.commandBuffer = commandBuffer,
                              .swapchainContext = swapchainContext(),
                              .frameIndex = frameState->frameIndex,
                              .imageIndex = frameState->imageIndex};
    renderer.record(context, renderItems);
    commandBuffer.end();

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
