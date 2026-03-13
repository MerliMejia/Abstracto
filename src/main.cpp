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
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <iostream>
#include <memory>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr bool DEBUG_SHOW_SOLID_TRANSFORM_PASS = false;
const std::string ASSET_PATH = "assets";

enum class PresentedOutput : uint32_t {
  GBufferAlbedo = 0,
  GBufferNormal = 1,
  GBufferMaterial = 2,
  GBufferEmissive = 3,
  GBufferDepth = 4,
  GeometryPass = 5,
  PbrPass = 6,
  TonemapPass = 7,
};

std::string normalizedMaterialName(std::string_view name) {
  std::string normalized;
  normalized.reserve(name.size());

  for (char c : name) {
    normalized.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  return normalized;
}

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
  FrameGeometryUniforms frameGeometryUniforms;
  Sampler sampler;
  ImageBasedLightingResources iblResources;
  GeometryPass *geometryPass = nullptr;
  PbrPass *pbrPass = nullptr;
  TonemapPass *tonemapPass = nullptr;
  DebugPass *debugPass = nullptr;
  ImGuiPass *imguiPass = nullptr;
  std::chrono::steady_clock::time_point lastFrameTime =
      std::chrono::steady_clock::now();
  float lightAzimuthRadians = glm::radians(-129.316f);
  float lightElevationRadians = glm::radians(-39.011f);
  float lightIntensity = 13.263f;
  glm::vec3 lightColor = {1.0f, 242.0f / 255.0f, 230.0f / 255.0f};
  float exposure = 1.0f;
  float autoExposureKey = 2.5f;
  float whitePoint = 2.716f;
  float gamma = 1.684f;
  bool autoExposureEnabled = true;
  TonemapOperator tonemapOperator = TonemapOperator::ACES;
  PresentedOutput presentedOutput = PresentedOutput::TonemapPass;
  PbrDebugView pbrDebugView = PbrDebugView::Final;
  int selectedMaterialIndex = 0;
  float environmentIntensity = 1.1f;
  float environmentBackgroundWeight = 1.0f;
  float environmentDiffuseWeight = 0.85f;
  float environmentSpecularWeight = 1.35f;
  float dielectricSpecularScale = 2.4f;
  float environmentRotationRadians = 0.0f;
  bool iblEnabled = true;
  bool skyboxVisible = true;
  bool syncSkySunToLight = true;
  ImageBasedLightingBakeSettings iblBakeSettings{};
  glm::vec3 modelPosition = {0.0f, 0.0f, 0.0f};
  glm::vec3 modelRotationDegrees = {90.0f, 180.0f, 0.0f};
  glm::vec3 modelScale = {0.5f, 0.5f, 0.5f};
  bool smoothGltfNormalsEnabled = false;
  glm::vec3 cameraPosition = {2.7f, 2.7f, 1.1f};
  float cameraYawRadians = glm::radians(-135.0f);
  float cameraPitchRadians = glm::radians(-11.1f);
  float cameraMoveSpeed = 2.5f;
  float cameraLookSensitivity = 0.0035f;
  bool cameraLookActive = false;
  double lastCursorX = 0.0;
  double lastCursorY = 0.0;

  void initWindow() { window.create(WIDTH, HEIGHT, "Double Pass"); }

  void resetCamera() {
    cameraPosition = {2.7f, 2.7f, 1.1f};
    cameraYawRadians = glm::radians(-135.0f);
    cameraPitchRadians = glm::radians(-11.1f);
    cameraLookActive = false;
  }

  glm::vec3 currentCameraForward() const {
    const float cosPitch = std::cos(cameraPitchRadians);
    return glm::normalize(glm::vec3(std::cos(cameraYawRadians) * cosPitch,
                                    std::sin(cameraYawRadians) * cosPitch,
                                    std::sin(cameraPitchRadians)));
  }

  void syncProceduralSkySunWithLight() {
    const glm::vec3 sunDirection = -currentLightDirectionWorld();
    iblBakeSettings.sky.sunAzimuthRadians =
        std::atan2(sunDirection.y, sunDirection.x);
    iblBakeSettings.sky.sunElevationRadians =
        std::asin(glm::clamp(sunDirection.z, -1.0f, 1.0f));
  }

  std::string sceneModelPath() const {
    return ASSET_PATH + "/models/material_test.glb";
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
    sceneModel.setSmoothGltfNormalsEnabled(smoothGltfNormalsEnabled);
    sceneModel.loadFromFile(sceneModelPath(), commandContext(), deviceContext(),
                            renderer.descriptorSetLayout(),
                            frameGeometryUniforms, sampler,
                            MAX_FRAMES_IN_FLIGHT);
    rebuildSceneRenderItems();
  }

  void initVulkan() {
    backend.initialize(window, config);
    iblBakeSettings.environmentHdrPath = ASSET_PATH + "/textures/skybox.hdr";

    sampler.create(deviceContext());

    lightQuad = buildFullscreenQuadMesh();
    lightQuad.createVertexBuffer(commandContext(), deviceContext());
    lightQuad.createIndexBuffer(commandContext(), deviceContext());

    auto geometryPassLocal = std::make_unique<GeometryPass>(
        PipelineSpec{.shaderPath = ASSET_PATH + "/shaders/geometry_pass.spv",
                     .cullMode = vk::CullModeFlagBits::eBack,
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
    if (syncSkySunToLight) {
      syncProceduralSkySunWithLight();
    }
    iblResources.create(deviceContext(), commandContext(), iblBakeSettings);
    pbrPass->setImageBasedLighting(iblResources);
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
    debugPass->setSelectedOutput(static_cast<uint32_t>(presentedOutput));
    renderer.addPass(std::move(debugPassLocal));

    auto imguiPassLocal = std::make_unique<ImGuiPass>(
        window, backend.instance(), commandContext());
    imguiPass = imguiPassLocal.get();
    renderer.addPass(std::move(imguiPassLocal));

    renderer.initialize(deviceContext(), swapchainContext());

    frameGeometryUniforms.create(deviceContext(), MAX_FRAMES_IN_FLIGHT);
    sceneModel.setSmoothGltfNormalsEnabled(smoothGltfNormalsEnabled);
    sceneModel.loadFromFile(sceneModelPath(), commandContext(),
                            deviceContext(), renderer.descriptorSetLayout(),
                            frameGeometryUniforms, sampler,
                            MAX_FRAMES_IN_FLIGHT);
    rebuildSceneRenderItems();
  }

  bool buildMaterialEditorUi() {
    bool materialChanged = false;
    auto &materials = sceneModel.mutableMaterials();
    if (materials.empty()) {
      return false;
    }

    selectedMaterialIndex = std::clamp(selectedMaterialIndex, 0,
                                       static_cast<int>(materials.size()) - 1);

    ImGui::Begin("Materials");
    for (int index = 0; index < static_cast<int>(materials.size()); ++index) {
      const bool selected = selectedMaterialIndex == index;
      const char *label = materials[index].name.empty()
                              ? "<unnamed>"
                              : materials[index].name.c_str();
      if (ImGui::Selectable(label, selected)) {
        selectedMaterialIndex = index;
      }
    }
    ImGui::End();

    auto &material = materials[static_cast<size_t>(selectedMaterialIndex)];
    ImGui::Begin("Material Properties");
    ImGui::Text("Selected: %s",
                material.name.empty() ? "<unnamed>" : material.name.c_str());
    materialChanged |=
        ImGui::ColorEdit4("Base Color", &material.baseColorFactor.x);
    materialChanged |=
        ImGui::ColorEdit3("Emissive", &material.emissiveFactor.x);
    materialChanged |=
        ImGui::SliderFloat("Metallic", &material.metallicFactor, 0.0f, 1.0f);
    materialChanged |=
        ImGui::SliderFloat("Roughness", &material.roughnessFactor, 0.0f, 1.0f);
    materialChanged |=
        ImGui::SliderFloat("Normal Scale", &material.normalScale, 0.0f, 2.0f);
    materialChanged |= ImGui::SliderFloat(
        "Occlusion Strength", &material.occlusionStrength, 0.0f, 1.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Textures");
    ImGui::BulletText("Base Color: %s",
                      material.baseColorTexture.hasPath() ||
                              material.baseColorTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText(
        "Metallic/Roughness: %s",
        material.metallicRoughnessTexture.hasPath() ||
                material.metallicRoughnessTexture.hasEmbeddedRgba()
            ? "yes"
            : "no");
    ImGui::BulletText("Normal: %s",
                      material.normalTexture.hasPath() ||
                              material.normalTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText("Emissive: %s",
                      material.emissiveTexture.hasPath() ||
                              material.emissiveTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::BulletText("Occlusion: %s",
                      material.occlusionTexture.hasPath() ||
                              material.occlusionTexture.hasEmbeddedRgba()
                          ? "yes"
                          : "no");
    ImGui::End();

    return materialChanged;
  }

  void buildLightUi() {
    ImGui::Begin("Light");
    float azimuthDegrees = glm::degrees(lightAzimuthRadians);
    float elevationDegrees = glm::degrees(lightElevationRadians);
    if (ImGui::SliderFloat("Azimuth", &azimuthDegrees, -180.0f, 180.0f)) {
      lightAzimuthRadians = glm::radians(azimuthDegrees);
    }
    if (ImGui::SliderFloat("Elevation", &elevationDegrees, -89.0f, 89.0f)) {
      lightElevationRadians = glm::radians(elevationDegrees);
    }
    ImGui::SliderFloat("Intensity", &lightIntensity, 0.0f, 20.0f);
    ImGui::ColorEdit3("Color", &lightColor.x);
    ImGui::SeparatorText("Tonemap");

    int tonemapOperatorIndex = static_cast<int>(tonemapOperator);
    ImGui::Combo("Operator", &tonemapOperatorIndex,
                 "None\0Reinhard\0ACES\0Filmic\0");
    tonemapOperator = static_cast<TonemapOperator>(tonemapOperatorIndex);

    ImGui::Checkbox("Auto Exposure", &autoExposureEnabled);
    if (autoExposureEnabled) {
      ImGui::SliderFloat("Auto Key", &autoExposureKey, 0.1f, 2.5f);
    } else {
      ImGui::SliderFloat("Exposure", &exposure, 0.1f, 4.0f);
    }
    ImGui::SliderFloat("White Point", &whitePoint, 0.5f, 16.0f);
    ImGui::SliderFloat("Gamma", &gamma, 1.0f, 3.0f);
    ImGui::Text("Direction: %.2f %.2f %.2f", currentLightDirectionWorld().x,
                currentLightDirectionWorld().y, currentLightDirectionWorld().z);
    ImGui::End();
  }

  void buildViewUi() {
    ImGui::Begin("View");
    int output = static_cast<int>(presentedOutput);
    ImGui::SeparatorText("GBuffers (Geometry Pass)");
    ImGui::RadioButton("Albedo", &output,
                       static_cast<int>(PresentedOutput::GBufferAlbedo));
    ImGui::RadioButton("Normal", &output,
                       static_cast<int>(PresentedOutput::GBufferNormal));
    ImGui::RadioButton("Material", &output,
                       static_cast<int>(PresentedOutput::GBufferMaterial));
    ImGui::RadioButton("Emissive", &output,
                       static_cast<int>(PresentedOutput::GBufferEmissive));
    ImGui::RadioButton("Depth", &output,
                       static_cast<int>(PresentedOutput::GBufferDepth));

    ImGui::SeparatorText("Pass Outputs");
    ImGui::RadioButton("Geometry Pass", &output,
                       static_cast<int>(PresentedOutput::GeometryPass));
    ImGui::RadioButton("PBR Pass", &output,
                       static_cast<int>(PresentedOutput::PbrPass));
    ImGui::RadioButton("Tone Mapping Pass", &output,
                       static_cast<int>(PresentedOutput::TonemapPass));

    presentedOutput = static_cast<PresentedOutput>(output);
    if (debugPass != nullptr) {
      debugPass->setSelectedOutput(static_cast<uint32_t>(presentedOutput));
    }
    ImGui::End();
  }

  void buildTransformUi() {
    ImGui::Begin("Transform");
    ImGui::DragFloat3("Position", &modelPosition.x, 0.01f);
    ImGui::SliderFloat3("Rotation", &modelRotationDegrees.x, -180.0f, 180.0f);
    ImGui::DragFloat3("Scale", &modelScale.x, 0.1f, 0.01f, 200.0f);
    ImGui::Separator();
    ImGui::Checkbox("Smooth glTF Normals", &smoothGltfNormalsEnabled);
    if (ImGui::Button("Reload Model")) {
      reloadSceneModel();
    }
    ImGui::End();
  }

  void buildCameraUi() {
    ImGui::Begin("Camera");
    ImGui::TextUnformatted("Move: WASD + Q/E");
    ImGui::TextUnformatted("Look: Hold RMB and drag");
    ImGui::SliderFloat("Move Speed", &cameraMoveSpeed, 0.5f, 10.0f);
    ImGui::SliderFloat("Look Sensitivity", &cameraLookSensitivity, 0.001f,
                       0.01f);
    if (ImGui::Button("Reset Camera")) {
      resetCamera();
    }
    ImGui::Text("Position: %.2f %.2f %.2f", cameraPosition.x, cameraPosition.y,
                cameraPosition.z);
    ImGui::End();
  }

  void buildPbrDebugUi() {
    ImGui::Begin("PBR Debug");
    int pbrDebugMode = static_cast<int>(pbrDebugView);
    ImGui::RadioButton("Final", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::Final));
    ImGui::RadioButton("Direct Lighting", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::DirectLighting));
    ImGui::RadioButton("IBL Diffuse", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::IblDiffuse));
    ImGui::RadioButton("IBL Specular", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::IblSpecular));
    ImGui::RadioButton("Ambient Total", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::AmbientTotal));
    ImGui::RadioButton("Reflections", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::Reflections));
    ImGui::RadioButton("Background", &pbrDebugMode,
                       static_cast<int>(PbrDebugView::Background));
    pbrDebugView = static_cast<PbrDebugView>(pbrDebugMode);
    ImGui::End();

    if (pbrPass != nullptr) {
      pbrPass->setDebugView(pbrDebugView);
    }
  }

  bool buildEnvironmentUi() {
    ImGui::Begin("Environment");
    ImGui::Checkbox("Enable IBL", &iblEnabled);
    ImGui::Checkbox("Show Skybox", &skyboxVisible);
    ImGui::SliderFloat("Env Intensity", &environmentIntensity, 0.0f, 4.0f);
    ImGui::SliderFloat("Skybox Weight", &environmentBackgroundWeight, 0.0f,
                       4.0f);
    ImGui::SliderFloat("Diffuse IBL", &environmentDiffuseWeight, 0.0f, 4.0f);
    ImGui::SliderFloat("Specular IBL", &environmentSpecularWeight, 0.0f, 4.0f);
    ImGui::SliderFloat("Dielectric Specular", &dielectricSpecularScale, 0.5f,
                       3.0f);
    ImGui::SliderAngle("Env Rotation", &environmentRotationRadians, -180.0f,
                       180.0f);
    ImGui::End();

    ImGui::Begin("Procedural Sky");
    ImGui::TextUnformatted("Changes here do not rebuild automatically.");
    ImGui::TextUnformatted("Use the button below to regenerate the IBL.");
    ImGui::Separator();
    if (!iblBakeSettings.environmentHdrPath.empty()) {
      ImGui::TextWrapped("Using HDRI environment: %s",
                         iblBakeSettings.environmentHdrPath.c_str());
      ImGui::TextUnformatted(
          "Procedural sky controls are ignored while an HDRI is active.");
    } else {
      ImGui::Checkbox("Sync Sun To Light", &syncSkySunToLight);

      if (syncSkySunToLight) {
        syncProceduralSkySunWithLight();
        ImGui::Text("Sun Azimuth: %.1f deg",
                    glm::degrees(iblBakeSettings.sky.sunAzimuthRadians));
        ImGui::Text("Sun Elevation: %.1f deg",
                    glm::degrees(iblBakeSettings.sky.sunElevationRadians));
      } else {
        float sunAzimuthDegrees =
            glm::degrees(iblBakeSettings.sky.sunAzimuthRadians);
        float sunElevationDegrees =
            glm::degrees(iblBakeSettings.sky.sunElevationRadians);
        if (ImGui::SliderFloat("Sun Azimuth", &sunAzimuthDegrees, -180.0f,
                               180.0f)) {
          iblBakeSettings.sky.sunAzimuthRadians =
              glm::radians(sunAzimuthDegrees);
        }
        if (ImGui::SliderFloat("Sun Elevation", &sunElevationDegrees, -89.0f,
                               89.0f)) {
          iblBakeSettings.sky.sunElevationRadians =
              glm::radians(sunElevationDegrees);
        }
      }

      ImGui::ColorEdit3("Zenith", &iblBakeSettings.sky.zenithColor.x);
      ImGui::ColorEdit3("Horizon", &iblBakeSettings.sky.horizonColor.x);
      ImGui::ColorEdit3("Ground", &iblBakeSettings.sky.groundColor.x);
      ImGui::ColorEdit3("Sun Color", &iblBakeSettings.sky.sunColor.x);
      ImGui::SliderFloat("Sun Intensity", &iblBakeSettings.sky.sunIntensity,
                         0.0f, 80.0f);
      ImGui::SliderFloat("Sun Radius", &iblBakeSettings.sky.sunAngularRadius,
                         0.005f, 0.15f);
      ImGui::SliderFloat("Sun Glow", &iblBakeSettings.sky.sunGlow, 0.0f, 8.0f);
      ImGui::SliderFloat("Horizon Glow", &iblBakeSettings.sky.horizonGlow,
                         0.0f, 1.0f);
    }

    const bool rebuildRequested = ImGui::Button("Rebuild IBL");
    ImGui::End();
    return rebuildRequested;
  }

  glm::vec3 currentLightDirectionWorld() const {
    const float cosElevation = std::cos(lightElevationRadians);
    return glm::normalize(
        glm::vec3(cosElevation * std::cos(lightAzimuthRadians),
                  cosElevation * std::sin(lightAzimuthRadians),
                  std::sin(lightElevationRadians)));
  }

  void updateFreeCamera(float deltaSeconds) {
    ImGuiIO &io = ImGui::GetIO();
    GLFWwindow *glfwWindow = window.handle();

    if (glfwGetMouseButton(glfwWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      double cursorX = 0.0;
      double cursorY = 0.0;
      glfwGetCursorPos(glfwWindow, &cursorX, &cursorY);

      if (!cameraLookActive && !io.WantCaptureMouse) {
        cameraLookActive = true;
        lastCursorX = cursorX;
        lastCursorY = cursorY;
      } else if (cameraLookActive) {
        const double deltaX = cursorX - lastCursorX;
        const double deltaY = cursorY - lastCursorY;
        lastCursorX = cursorX;
        lastCursorY = cursorY;

        cameraYawRadians -= static_cast<float>(deltaX) * cameraLookSensitivity;
        cameraPitchRadians -=
            static_cast<float>(deltaY) * cameraLookSensitivity;
        cameraPitchRadians = glm::clamp(
            cameraPitchRadians, glm::radians(-89.0f), glm::radians(89.0f));
      }
    } else {
      cameraLookActive = false;
    }

    if (io.WantCaptureKeyboard) {
      return;
    }

    const glm::vec3 forward = currentCameraForward();
    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float moveStep = cameraMoveSpeed * deltaSeconds;

    if (glfwGetKey(glfwWindow, GLFW_KEY_W) == GLFW_PRESS) {
      cameraPosition += forward * moveStep;
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_S) == GLFW_PRESS) {
      cameraPosition -= forward * moveStep;
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_D) == GLFW_PRESS) {
      cameraPosition += right * moveStep;
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_A) == GLFW_PRESS) {
      cameraPosition -= right * moveStep;
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_E) == GLFW_PRESS) {
      cameraPosition += up * moveStep;
    }
    if (glfwGetKey(glfwWindow, GLFW_KEY_Q) == GLFW_PRESS) {
      cameraPosition -= up * moveStep;
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

    if (imguiPass != nullptr) {
      imguiPass->beginFrame();
      const bool materialChanged = buildMaterialEditorUi();
      buildCameraUi();
      buildTransformUi();
      buildLightUi();
      buildViewUi();
      buildPbrDebugUi();
      const bool iblBakeChanged = buildEnvironmentUi();
      if (materialChanged) {
        sceneModel.syncMaterialParameters();
      }
      imguiPass->endFrame();
      if (iblBakeChanged) {
        backend.waitIdle();
        if (syncSkySunToLight) {
          syncProceduralSkySunWithLight();
        }
        iblResources.rebuild(deviceContext(), commandContext(),
                             iblBakeSettings);
        renderer.recreate(deviceContext(), swapchainContext());
      }
    }

    updateFreeCamera(deltaSeconds);

    GeometryUniformData geometryUniformData{};

    geometryUniformData.model = glm::mat4(1.0f);
    geometryUniformData.model =
        glm::translate(geometryUniformData.model, modelPosition);
    geometryUniformData.model = glm::rotate(
        geometryUniformData.model, glm::radians(modelRotationDegrees.x),
        glm::vec3(1.0f, 0.0f, 0.0f));
    geometryUniformData.model = glm::rotate(
        geometryUniformData.model, glm::radians(modelRotationDegrees.y),
        glm::vec3(0.0f, 1.0f, 0.0f));
    geometryUniformData.model = glm::rotate(
        geometryUniformData.model, glm::radians(modelRotationDegrees.z),
        glm::vec3(0.0f, 0.0f, 1.0f));
    geometryUniformData.model =
        glm::scale(geometryUniformData.model, modelScale);
    geometryUniformData.modelNormal =
        glm::transpose(glm::inverse(geometryUniformData.model));

    geometryUniformData.view =
        glm::lookAt(cameraPosition, cameraPosition + currentCameraForward(),
                    glm::vec3(0.0f, 0.0f, 1.0f));

    geometryUniformData.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapchainContext().extent2D().width) /
            static_cast<float>(swapchainContext().extent2D().height),
        0.1f, 10.0f);

    // Vulkan inverts Y.
    geometryUniformData.proj[1][1] *= -1.0f;

    frameGeometryUniforms.write(frameState->frameIndex, geometryUniformData);

    if (pbrPass != nullptr) {
      glm::vec3 lightDirectionWorld = currentLightDirectionWorld();
      glm::vec3 lightDirectionView =
          glm::normalize(glm::mat3(geometryUniformData.view) *
                         lightDirectionWorld);

      pbrPass->setCamera(geometryUniformData.proj,
                         geometryUniformData.view);
      pbrPass->setDirectionalLight(lightDirectionView,
                                   lightColor * lightIntensity);
      pbrPass->setEnvironmentControls(
          environmentRotationRadians,
          environmentIntensity * environmentBackgroundWeight,
          environmentIntensity * environmentDiffuseWeight,
          environmentIntensity * environmentSpecularWeight, iblEnabled,
          skyboxVisible);
      pbrPass->setDielectricSpecularScale(dielectricSpecularScale);
      pbrPass->setDebugView(pbrDebugView);
    }
    if (tonemapPass != nullptr) {
      const glm::vec3 lightRadiance = lightColor * lightIntensity;
      const float lightLuminance =
          glm::dot(lightRadiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
      const float resolvedExposure =
          autoExposureEnabled
              ? glm::clamp(autoExposureKey / std::max(lightLuminance, 0.001f),
                           0.05f, 8.0f)
              : exposure;
      tonemapPass->setExposure(resolvedExposure);
      tonemapPass->setWhitePoint(whitePoint);
      tonemapPass->setGamma(gamma);
      tonemapPass->setOperator(tonemapOperator);
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
