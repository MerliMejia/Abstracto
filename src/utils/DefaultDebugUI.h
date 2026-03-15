#pragma once

#include "../passes/PbrPass.h"
#include "../passes/TonemapPass.h"
#include "../renderable/ImageBasedLightingTypes.h"
#include "../renderable/RenderableModel.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>

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

struct DefaultDebugUISettings {
  PresentedOutput presentedOutput = PresentedOutput::TonemapPass;
  PbrDebugView pbrDebugView = PbrDebugView::Final;
  int selectedMaterialIndex = 0;

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
  float cameraFarPlane = 100.0f;
  bool cameraLookActive = false;
  double cameraLastCursorX = 0.0;
  double cameraLastCursorY = 0.0;
};

struct DefaultDebugUICallbacks {
  std::function<void()> reloadSceneModel;
  std::function<void()> syncProceduralSkySunWithLight;
  std::function<glm::vec3()> currentLightDirectionWorld;
};

class DefaultDebugCameraController {
public:
  explicit DefaultDebugCameraController(DefaultDebugUISettings &settings)
      : settings(settings) {}

  static DefaultDebugCameraController create(DefaultDebugUISettings &settings) {
    return DefaultDebugCameraController(settings);
  }

  void reset() {
    settings.cameraPosition = {2.7f, 2.7f, 1.1f};
    settings.cameraYawRadians = glm::radians(-135.0f);
    settings.cameraPitchRadians = glm::radians(-11.1f);
    settings.cameraFarPlane = 100.0f;
    settings.cameraLookActive = false;
  }

  static glm::vec3 forwardFromAngles(float yawRadians, float pitchRadians) {
    const float cosPitch = std::cos(pitchRadians);
    return glm::normalize(glm::vec3(std::cos(yawRadians) * cosPitch,
                                    std::sin(yawRadians) * cosPitch,
                                    std::sin(pitchRadians)));
  }

  static glm::vec3 forwardFromSettings(const DefaultDebugUISettings &settings) {
    return forwardFromAngles(settings.cameraYawRadians,
                             settings.cameraPitchRadians);
  }

  glm::vec3 currentForward() const { return forwardFromSettings(settings); }

  void update(float deltaSeconds, GLFWwindow *windowHandle) {
    ImGuiIO &io = ImGui::GetIO();

    if (glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_RIGHT) ==
        GLFW_PRESS) {
      double cursorX = 0.0;
      double cursorY = 0.0;
      glfwGetCursorPos(windowHandle, &cursorX, &cursorY);

      if (!settings.cameraLookActive && !io.WantCaptureMouse) {
        settings.cameraLookActive = true;
        settings.cameraLastCursorX = cursorX;
        settings.cameraLastCursorY = cursorY;
      } else if (settings.cameraLookActive) {
        const double deltaX = cursorX - settings.cameraLastCursorX;
        const double deltaY = cursorY - settings.cameraLastCursorY;
        settings.cameraLastCursorX = cursorX;
        settings.cameraLastCursorY = cursorY;

        settings.cameraYawRadians -=
            static_cast<float>(deltaX) * settings.cameraLookSensitivity;
        settings.cameraPitchRadians -=
            static_cast<float>(deltaY) * settings.cameraLookSensitivity;
        settings.cameraPitchRadians =
            glm::clamp(settings.cameraPitchRadians, glm::radians(-89.0f),
                       glm::radians(89.0f));
      }
    } else {
      settings.cameraLookActive = false;
    }

    if (io.WantCaptureKeyboard) {
      return;
    }

    const glm::vec3 forward = currentForward();
    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float moveStep = settings.cameraMoveSpeed * deltaSeconds;

    if (glfwGetKey(windowHandle, GLFW_KEY_W) == GLFW_PRESS) {
      settings.cameraPosition += forward * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_S) == GLFW_PRESS) {
      settings.cameraPosition -= forward * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_D) == GLFW_PRESS) {
      settings.cameraPosition += right * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_A) == GLFW_PRESS) {
      settings.cameraPosition -= right * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_E) == GLFW_PRESS) {
      settings.cameraPosition += up * moveStep;
    }
    if (glfwGetKey(windowHandle, GLFW_KEY_Q) == GLFW_PRESS) {
      settings.cameraPosition -= up * moveStep;
    }
  }

private:
  DefaultDebugUISettings &settings;
};

struct DefaultDebugUIResult {
  bool materialChanged = false;
  bool iblBakeRequested = false;
};

struct DefaultDebugUIPerformanceStats {
  float fps = 0.0f;
  float frameTimeMs = 0.0f;
};

struct DefaultDebugUIBindings {
  RenderableModel &sceneModel;
  DefaultDebugUISettings &settings;
  DefaultDebugUICallbacks callbacks;
  DefaultDebugUIPerformanceStats performanceStats;
};

class DefaultDebugUI {
public:
  explicit DefaultDebugUI(DefaultDebugUIBindings bindings)
      : bindings(std::move(bindings)) {}

  static DefaultDebugUI
  create(RenderableModel &sceneModel, DefaultDebugUISettings &settings,
         DefaultDebugUICallbacks callbacks,
         DefaultDebugUIPerformanceStats performanceStats = {}) {
    return DefaultDebugUI(DefaultDebugUIBindings{
        .sceneModel = sceneModel,
        .settings = settings,
        .callbacks = std::move(callbacks),
        .performanceStats = performanceStats,
    });
  }

  DefaultDebugUIResult build() {
    DefaultDebugUIResult result;
    result.materialChanged = buildMaterialEditorUi();
    buildPerformanceUi();
    buildCameraUi();
    buildTransformUi();
    buildLightUi();
    buildViewUi();
    buildPbrDebugUi();
    result.iblBakeRequested = buildEnvironmentUi();
    return result;
  }

private:
  DefaultDebugUIBindings bindings;

  bool buildMaterialEditorUi() {
    bool materialChanged = false;
    auto &settings = bindings.settings;
    auto &materials = bindings.sceneModel.mutableMaterials();
    if (materials.empty()) {
      return false;
    }

    settings.selectedMaterialIndex =
        std::clamp(settings.selectedMaterialIndex, 0,
                   static_cast<int>(materials.size()) - 1);

    ImGui::Begin("Materials");
    for (int index = 0; index < static_cast<int>(materials.size()); ++index) {
      const bool selected = settings.selectedMaterialIndex == index;
      const char *label = materials[index].name.empty()
                              ? "<unnamed>"
                              : materials[index].name.c_str();
      if (ImGui::Selectable(label, selected)) {
        settings.selectedMaterialIndex = index;
      }
    }
    ImGui::End();

    auto &material =
        materials[static_cast<size_t>(settings.selectedMaterialIndex)];
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
    auto &settings = bindings.settings;
    ImGui::Begin("Light");
    float azimuthDegrees = glm::degrees(settings.lightAzimuthRadians);
    float elevationDegrees = glm::degrees(settings.lightElevationRadians);
    if (ImGui::SliderFloat("Azimuth", &azimuthDegrees, -180.0f, 180.0f)) {
      settings.lightAzimuthRadians = glm::radians(azimuthDegrees);
    }
    if (ImGui::SliderFloat("Elevation", &elevationDegrees, -89.0f, 89.0f)) {
      settings.lightElevationRadians = glm::radians(elevationDegrees);
    }
    ImGui::SliderFloat("Intensity", &settings.lightIntensity, 0.0f, 20.0f);
    ImGui::ColorEdit3("Color", &settings.lightColor.x);
    ImGui::SeparatorText("Tonemap");

    int tonemapOperatorIndex = static_cast<int>(settings.tonemapOperator);
    ImGui::Combo("Operator", &tonemapOperatorIndex,
                 "None\0Reinhard\0ACES\0Filmic\0");
    settings.tonemapOperator =
        static_cast<TonemapOperator>(tonemapOperatorIndex);

    ImGui::Checkbox("Auto Exposure", &settings.autoExposureEnabled);
    if (settings.autoExposureEnabled) {
      ImGui::SliderFloat("Auto Key", &settings.autoExposureKey, 0.1f, 2.5f);
    } else {
      ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 4.0f);
    }
    ImGui::SliderFloat("White Point", &settings.whitePoint, 0.5f, 16.0f);
    ImGui::SliderFloat("Gamma", &settings.gamma, 1.0f, 3.0f);

    const glm::vec3 lightDirectionWorld =
        bindings.callbacks.currentLightDirectionWorld();
    ImGui::Text("Direction: %.2f %.2f %.2f", lightDirectionWorld.x,
                lightDirectionWorld.y, lightDirectionWorld.z);
    ImGui::End();
  }

  void buildViewUi() {
    auto &settings = bindings.settings;
    ImGui::Begin("View");
    int output = static_cast<int>(settings.presentedOutput);
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

    settings.presentedOutput = static_cast<PresentedOutput>(output);
    ImGui::End();
  }

  void buildTransformUi() {
    auto &settings = bindings.settings;
    ImGui::Begin("Transform");
    ImGui::DragFloat3("Position", &settings.modelPosition.x, 0.01f);
    ImGui::SliderFloat3("Rotation", &settings.modelRotationDegrees.x, -180.0f,
                        180.0f);
    ImGui::DragFloat3("Scale", &settings.modelScale.x, 0.1f, 0.01f, 200.0f);
    ImGui::Separator();
    ImGui::Checkbox("Smooth glTF Normals", &settings.smoothGltfNormalsEnabled);
    if (ImGui::Button("Reload Model")) {
      bindings.callbacks.reloadSceneModel();
    }
    ImGui::End();
  }

  void buildCameraUi() {
    auto &settings = bindings.settings;
    DefaultDebugCameraController cameraController =
        DefaultDebugCameraController::create(settings);
    ImGui::Begin("Camera");
    ImGui::TextUnformatted("Move: WASD + Q/E");
    ImGui::TextUnformatted("Look: Hold RMB and drag");
    ImGui::SliderFloat("Move Speed", &settings.cameraMoveSpeed, 0.5f, 10.0f);
    ImGui::SliderFloat("Look Sensitivity", &settings.cameraLookSensitivity,
                       0.001f, 0.01f);
    ImGui::SliderFloat("Far Clip", &settings.cameraFarPlane, 10.0f, 500.0f,
                       "%.1f");
    if (ImGui::Button("Reset Camera")) {
      cameraController.reset();
    }
    ImGui::Text("Position: %.2f %.2f %.2f", settings.cameraPosition.x,
                settings.cameraPosition.y, settings.cameraPosition.z);
    ImGui::End();
  }

  void buildPerformanceUi() {
    const auto &performanceStats = bindings.performanceStats;
    ImGui::Begin("Performance");
    ImGui::Text("FPS: %.1f", performanceStats.fps);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Text("Frame Time: %.2f ms", performanceStats.frameTimeMs);
    ImGui::End();
  }

  void buildPbrDebugUi() {
    auto &settings = bindings.settings;
    ImGui::Begin("PBR Debug");
    int pbrDebugMode = static_cast<int>(settings.pbrDebugView);
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
    settings.pbrDebugView = static_cast<PbrDebugView>(pbrDebugMode);
    ImGui::End();
  }

  bool buildEnvironmentUi() {
    auto &settings = bindings.settings;
    ImGui::Begin("Environment");
    ImGui::Checkbox("Enable IBL", &settings.iblEnabled);
    ImGui::Checkbox("Show Skybox", &settings.skyboxVisible);
    ImGui::SliderFloat("Env Intensity", &settings.environmentIntensity, 0.0f,
                       4.0f);
    ImGui::SliderFloat("Skybox Weight", &settings.environmentBackgroundWeight,
                       0.0f, 4.0f);
    ImGui::SliderFloat("Diffuse IBL", &settings.environmentDiffuseWeight, 0.0f,
                       4.0f);
    ImGui::SliderFloat("Specular IBL", &settings.environmentSpecularWeight,
                       0.0f, 4.0f);
    ImGui::SliderFloat("Dielectric Specular", &settings.dielectricSpecularScale,
                       0.5f, 3.0f);
    ImGui::SliderAngle("Env Rotation", &settings.environmentRotationRadians,
                       -180.0f, 180.0f);
    ImGui::End();

    ImGui::Begin("Procedural Sky");
    ImGui::TextUnformatted("Changes here do not rebuild automatically.");
    ImGui::TextUnformatted("Use the button below to regenerate the IBL.");
    ImGui::Separator();
    if (!settings.iblBakeSettings.environmentHdrPath.empty()) {
      ImGui::TextWrapped("Using HDRI environment: %s",
                         settings.iblBakeSettings.environmentHdrPath.c_str());
      ImGui::TextUnformatted(
          "Procedural sky controls are ignored while an HDRI is active.");
    } else {
      ImGui::Checkbox("Sync Sun To Light", &settings.syncSkySunToLight);

      if (settings.syncSkySunToLight) {
        bindings.callbacks.syncProceduralSkySunWithLight();
        ImGui::Text(
            "Sun Azimuth: %.1f deg",
            glm::degrees(settings.iblBakeSettings.sky.sunAzimuthRadians));
        ImGui::Text(
            "Sun Elevation: %.1f deg",
            glm::degrees(settings.iblBakeSettings.sky.sunElevationRadians));
      } else {
        float sunAzimuthDegrees =
            glm::degrees(settings.iblBakeSettings.sky.sunAzimuthRadians);
        float sunElevationDegrees =
            glm::degrees(settings.iblBakeSettings.sky.sunElevationRadians);
        if (ImGui::SliderFloat("Sun Azimuth", &sunAzimuthDegrees, -180.0f,
                               180.0f)) {
          settings.iblBakeSettings.sky.sunAzimuthRadians =
              glm::radians(sunAzimuthDegrees);
        }
        if (ImGui::SliderFloat("Sun Elevation", &sunElevationDegrees, -89.0f,
                               89.0f)) {
          settings.iblBakeSettings.sky.sunElevationRadians =
              glm::radians(sunElevationDegrees);
        }
      }

      ImGui::ColorEdit3("Zenith", &settings.iblBakeSettings.sky.zenithColor.x);
      ImGui::ColorEdit3("Horizon",
                        &settings.iblBakeSettings.sky.horizonColor.x);
      ImGui::ColorEdit3("Ground", &settings.iblBakeSettings.sky.groundColor.x);
      ImGui::ColorEdit3("Sun Color", &settings.iblBakeSettings.sky.sunColor.x);
      ImGui::SliderFloat("Sun Intensity",
                         &settings.iblBakeSettings.sky.sunIntensity, 0.0f,
                         80.0f);
      ImGui::SliderFloat("Sun Radius",
                         &settings.iblBakeSettings.sky.sunAngularRadius, 0.005f,
                         0.15f);
      ImGui::SliderFloat("Sun Glow", &settings.iblBakeSettings.sky.sunGlow,
                         0.0f, 8.0f);
      ImGui::SliderFloat("Horizon Glow",
                         &settings.iblBakeSettings.sky.horizonGlow, 0.0f, 1.0f);
    }

    const bool rebuildRequested = ImGui::Button("Rebuild IBL");
    ImGui::End();
    return rebuildRequested;
  }
};
