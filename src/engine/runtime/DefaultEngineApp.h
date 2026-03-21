#pragma once

#include "AppPerformanceStats.h"
#include "AppRendererSetup.h"
#include "DefaultEngineConfig.h"
#include "ShadowSystem.h"
#include "engine/assets/RenderableModel.h"
#include "engine/editor/DebugSessionIO.h"
#include "engine/editor/DefaultDebugUI.h"
#include "engine/scene/AppSceneController.h"
#include "engine/scene/SceneDefinition.h"
#include "renderer/backend/AppWindow.h"
#include "renderer/backend/BackendConfig.h"
#include "renderer/backend/VulkanBackend.h"
#include "renderer/core/PassRenderer.h"
#include "renderer/core/RenderPass.h"
#include "renderer/debug/DebugLightMeshes.h"
#include "renderer/passes/ShadowPass.h"
#include "renderer/resources/FrameGeometryUniforms.h"
#include "renderer/resources/Sampler.h"
#include "vulkan/vulkan.hpp"
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <fstream>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <utility>

class DefaultEngineApp {
public:
  explicit DefaultEngineApp(DefaultEngineConfig config = {},
                            SceneDefinition sceneDefinition = {})
      : engineConfig(std::move(config)),
        sceneDefinition(std::move(sceneDefinition)),
        backendConfig{.appName = engineConfig.windowTitle,
                      .maxFramesInFlight =
                          DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT} {}

  static DefaultEngineApp create(DefaultEngineConfig config = {},
                                 SceneDefinition sceneDefinition = {}) {
    return DefaultEngineApp(std::move(config), std::move(sceneDefinition));
  }

  void run() {
    debugUiSettings = buildBaseDebugUiSettings();
    debugUiVisible = !isDebuggerAttached();
    if (engineConfig.enableDebugSessionPersistence &&
        engineConfig.restoreSessionOnStartup) {
      loadDebugSessionFromDisk();
    }
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  DefaultEngineConfig engineConfig;
  SceneDefinition sceneDefinition;
  AppWindow window;
  VulkanBackend backend;
  BackendConfig backendConfig;
  PassRenderer renderer;
  std::vector<RenderItem> renderItems;

  std::vector<SceneAssetInstance> sceneAssets;
  std::vector<RenderableModel> sceneAssetModels;
  RenderableModel emptyEditorModel;
  FullscreenMesh lightQuad;
  TypedMesh<Vertex> pointLightMarkerMesh;
  TypedMesh<Vertex> spotLightMarkerMesh;
  TypedMesh<Vertex> directionalLightMarkerMesh;
  FrameGeometryUniforms frameGeometryUniforms;
  Sampler sampler;
  ImageBasedLighting imageBasedLighting;
  GeometryPass *geometryPass = nullptr;
  ShadowPass *directionalShadowPass = nullptr;
  std::array<ShadowPass *, DEFAULT_ENGINE_MAX_SPOT_SHADOW_PASSES>
      spotShadowPasses{nullptr, nullptr, nullptr};
  PbrPass *pbrPass = nullptr;
  TonemapPass *tonemapPass = nullptr;
  DebugPresentPass *debugPresentPass = nullptr;
  DebugOverlayPass *debugOverlayPass = nullptr;
  ImGuiPass *imguiPass = nullptr;

  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  DefaultDebugUISettings debugUiSettings;
  float smoothedFrameTimeMs = 0.0f;
  bool debugUiVisible = true;
  bool debugUiToggleHeld = false;

  DeviceContext &deviceContext() { return backend.device(); }
  SwapchainContext &swapchainContext() { return backend.swapchain(); }
  CommandContext &commandContext() { return backend.commands(); }

  static bool isDebuggerAttached() {
#if defined(__APPLE__)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    kinfo_proc info{};
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size != sizeof(info)) {
      return false;
    }
    return (info.kp_proc.p_flag & P_TRACED) != 0;
#elif defined(_WIN32)
    return IsDebuggerPresent() != 0;
#elif defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
      if (line.rfind("TracerPid:", 0) != 0) {
        continue;
      }
      const size_t valueStart = line.find_first_not_of(" \t", 10);
      return valueStart != std::string::npos && line[valueStart] != '0';
    }
    return false;
#else
    return false;
#endif
  }

  void updateDebugUiToggle() {
    const bool togglePressed =
        glfwGetKey(window.handle(), GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
    if (togglePressed && !debugUiToggleHeld) {
      debugUiVisible = !debugUiVisible;
    }
    debugUiToggleHeld = togglePressed;
  }

  vk::raii::DescriptorSetLayout &sceneDescriptorSetLayout() {
    if (geometryPass == nullptr ||
        geometryPass->descriptorSetLayout() == nullptr) {
      throw std::runtime_error(
          "GeometryPass descriptor set layout is not available");
    }
    return *geometryPass->descriptorSetLayout();
  }

  std::filesystem::path debugSessionPath() const {
    return resolvedDebugSessionPath(engineConfig);
  }

  std::vector<SceneAssetInstance> resolvedSceneAssets() const {
    if (!sceneDefinition.assets.empty()) {
      return sceneDefinition.assets;
    }
    if (!sceneDefinition.modelPath.empty()) {
      return {SceneAssetInstance{.assetPath = sceneDefinition.modelPath}};
    }
    return {};
  }

  RenderableModel &currentEditorModel() {
    if (debugUiSettings.selectedLightIndex >= 0 || sceneAssetModels.empty() ||
        debugUiSettings.sceneObjects.empty()) {
      return emptyEditorModel;
    }

    const size_t selectedIndex =
        static_cast<size_t>(debugUiSettings.selectedObjectIndex);
    if (selectedIndex >= sceneAssetModels.size()) {
      return emptyEditorModel;
    }
    return sceneAssetModels[selectedIndex];
  }

  uint32_t totalMaterialCount() const {
    uint32_t total = 0;
    for (const auto &sceneAssetModel : sceneAssetModels) {
      if (sceneAssetModel.modelAsset() == nullptr) {
        continue;
      }
      total += static_cast<uint32_t>(sceneAssetModel.materials().size());
    }
    return total;
  }

  uint32_t totalVertexCount() const {
    uint32_t total = 0;
    for (const auto &sceneAssetModel : sceneAssetModels) {
      const ModelAsset *asset = sceneAssetModel.modelAsset();
      if (asset == nullptr) {
        continue;
      }
      total += static_cast<uint32_t>(asset->mesh().vertexCount());
    }
    return total;
  }

  uint32_t totalTriangleCount() const {
    uint32_t total = 0;
    for (const auto &sceneAssetModel : sceneAssetModels) {
      const ModelAsset *asset = sceneAssetModel.modelAsset();
      if (asset == nullptr) {
        continue;
      }
      total += static_cast<uint32_t>(asset->mesh().getIndices().size() / 3);
    }
    return total;
  }

  DefaultDebugUISettings buildBaseDebugUiSettings() const {
    DefaultDebugUISettings settings;
    const std::vector<SceneAssetInstance> initialSceneAssets =
        resolvedSceneAssets();
    if (initialSceneAssets.empty()) {
      settings.sceneObjects.clear();
    } else {
      settings.sceneObjects.clear();
      settings.sceneObjects.reserve(initialSceneAssets.size());
      for (size_t index = 0; index < initialSceneAssets.size(); ++index) {
        const auto &sceneAsset = initialSceneAssets[index];
        settings.sceneObjects.push_back(SceneObject{
            .name = sceneAsset.name.empty()
                        ? std::filesystem::path(sceneAsset.assetPath)
                              .stem()
                              .string()
                        : sceneAsset.name,
            .transform = sceneAsset.transform,
            .visible = sceneAsset.visible,
        });
      }
    }
    settings.sceneLights = sceneDefinition.sceneLights;
    settings.iblBakeSettings.environmentHdrPath =
        resolvedDefaultEnvironmentHdrPath(engineConfig);
    if (engineConfig.configureSettings) {
      engineConfig.configureSettings(settings);
    }
    if (sceneDefinition.configureSettings) {
      sceneDefinition.configureSettings(settings);
    }
    clampSceneObjectSelection(settings);
    return settings;
  }

  void initWindow() {
    window.create(engineConfig.width, engineConfig.height,
                  engineConfig.windowTitle, true);
  }

  void syncProceduralSkySunWithLight() {
    const glm::vec3 sunDirection = -currentPrimaryDirectionalLightWorld();
    debugUiSettings.iblBakeSettings.sky.sunAzimuthRadians =
        std::atan2(sunDirection.y, sunDirection.x);
    debugUiSettings.iblBakeSettings.sky.sunElevationRadians =
        std::asin(glm::clamp(sunDirection.z, -1.0f, 1.0f));
  }

  void ensureDefaultEnvironmentPath() {
    if (debugUiSettings.iblBakeSettings.environmentHdrPath.empty()) {
      debugUiSettings.iblBakeSettings.environmentHdrPath =
          resolvedDefaultEnvironmentHdrPath(engineConfig);
    }
  }

  void syncSceneObjectsWithAssets() {
    AppSceneController::syncSceneObjectsWithAssets(debugUiSettings,
                                                   sceneAssets);
    AppSceneController::applyObjectOverrides(debugUiSettings,
                                             sceneDefinition.objectOverrides);
  }

  void rebuildSceneRenderItems() {
    AppSceneController::rebuildRenderItems(
        renderItems, sceneAssetModels, debugUiSettings, geometryPass,
        directionalShadowPass, spotShadowPasses, lightQuad, pbrPass,
        tonemapPass, debugPresentPass);
  }

  void reloadSceneAssets() {
    backend.waitIdle();
    sceneAssets = resolvedSceneAssets();
    sceneAssetModels.clear();
    sceneAssetModels.resize(sceneAssets.size());

    for (size_t index = 0; index < sceneAssets.size(); ++index) {
      if (sceneAssets[index].assetPath.empty()) {
        continue;
      }
      sceneAssetModels[index].loadFromFile(
          sceneAssets[index].assetPath, commandContext(), deviceContext(),
          sceneDescriptorSetLayout(), frameGeometryUniforms, sampler,
          DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
    }
    syncSceneObjectsWithAssets();
    rebuildSceneRenderItems();
  }

  void loadDebugSessionFromDisk() {
    if (!engineConfig.enableDebugSessionPersistence) {
      return;
    }

    try {
      DebugSessionIO::loadDebugSession(debugSessionPath(), debugUiSettings);
      ensureDefaultEnvironmentPath();
    } catch (const std::exception &e) {
      std::cerr << "Failed to load debug session: " << e.what() << std::endl;
    }
  }

  void saveDebugSessionToDisk() const {
    if (!engineConfig.enableDebugSessionPersistence) {
      return;
    }

    if (!DebugSessionIO::saveDebugSession(debugSessionPath(),
                                          debugUiSettings)) {
      std::cerr << "Failed to save debug session to "
                << debugSessionPath().string() << std::endl;
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
    reloadSceneAssets();
  }

  void initVulkan() {
    backend.initialize(window, backendConfig);
    ensureDefaultEnvironmentPath();

    sampler.create(deviceContext());
    AppRendererSetup::registerShadowPasses(
        renderer, directionalShadowPass, spotShadowPasses,
        DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT,
        DEFAULT_ENGINE_SHADOW_MAP_RESOLUTION, engineConfig.rendererAssetPath);

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

    if (debugUiSettings.syncSkySunToLight) {
      syncProceduralSkySunWithLight();
    }
    imageBasedLighting.create(deviceContext(), commandContext(),
                              debugUiSettings.iblBakeSettings);
    AppRendererSetup::registerMainPasses(
        renderer, geometryPass, pbrPass, tonemapPass, debugPresentPass,
        debugOverlayPass, imguiPass, window, backend.instance(),
        commandContext(), debugUiSettings, imageBasedLighting,
        directionalShadowPass, spotShadowPasses, pointLightMarkerMesh,
        spotLightMarkerMesh, directionalLightMarkerMesh,
        debugUiSettings.sceneLights,
        AppSceneController::sceneObjectsAnchor(debugUiSettings),
        DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT, DEFAULT_ENGINE_CAMERA_NEAR_PLANE,
        engineConfig.rendererAssetPath);

    renderer.initialize(deviceContext(), swapchainContext());

    frameGeometryUniforms.create(deviceContext(),
                                 DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT);
    reloadSceneAssets();
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

    updateDebugUiToggle();

    if (imguiPass != nullptr) {
      const uint32_t activeShadowPasses = ShadowSystem::activeShadowPassCount(
          debugUiSettings, pbrPass, directionalShadowPass, spotShadowPasses);
      imguiPass->beginFrame(debugUiVisible);
      DefaultDebugUIResult uiResult;
      if (debugUiVisible) {
        RenderableModel &editorModel = currentEditorModel();
        DefaultDebugUI defaultDebugUi = DefaultDebugUI::create(
            editorModel, debugUiSettings,
            DefaultDebugUICallbacks{
                .syncProceduralSkySunWithLight =
                    [this]() { syncProceduralSkySunWithLight(); },
                .currentPrimaryDirectionalLightWorld =
                    [this]() { return currentPrimaryDirectionalLightWorld(); },
            },
            AppPerformanceStats::build(
                smoothedFps, smoothedFrameTimeMs, debugUiSettings,
                totalMaterialCount(), totalVertexCount(), totalTriangleCount(),
                renderItems, geometryPass, pbrPass, tonemapPass,
                debugPresentPass, activeShadowPasses),
            imguiPass->dockspaceId());
        uiResult = defaultDebugUi.build();
        if (uiResult.materialChanged && editorModel.modelAsset() != nullptr) {
          editorModel.syncMaterialParameters();
        }
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
        debugUiSettings = buildBaseDebugUiSettings();
        applyLoadedDebugSettings();
      }
      if (debugPresentPass != nullptr) {
        debugPresentPass->setSelectedOutput(
            static_cast<uint32_t>(debugUiSettings.presentedOutput));
        debugPresentPass->setClipPlanes(DEFAULT_ENGINE_CAMERA_NEAR_PLANE,
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
    rebuildSceneRenderItems();

    GeometryUniformData geometryUniformData{};
    geometryUniformData.model = glm::mat4(1.0f);
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
        DEFAULT_ENGINE_CAMERA_NEAR_PLANE, debugUiSettings.cameraFarPlane);
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
      ShadowSystem::configure(
          debugUiSettings,
          AppSceneController::sceneObjectsAnchor(debugUiSettings), pbrPass,
          directionalShadowPass, spotShadowPasses);
    }
    if (debugOverlayPass != nullptr) {
      debugOverlayPass->setCamera(geometryUniformData.view,
                                  geometryUniformData.proj);
      debugOverlayPass->setSceneLights(debugUiSettings.sceneLights);
      debugOverlayPass->setMarkersVisible(debugUiSettings.lightMarkersVisible);
      debugOverlayPass->setMarkerScale(debugUiSettings.lightMarkerScale);
      debugOverlayPass->setDirectionalAnchor(
          AppSceneController::sceneObjectsAnchor(debugUiSettings));
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

  void mainLoop() {
    while (!window.shouldClose()) {
      window.pollEvents();
      drawFrame();
    }
    backend.waitIdle();
  }

  void cleanup() { window.destroy(); }
};
