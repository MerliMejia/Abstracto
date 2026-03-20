#pragma once

#include "../passes/PbrPass.h"
#include "../passes/TonemapPass.h"
#include "../renderable/ImageBasedLightingTypes.h"
#include "../renderable/RenderableModel.h"
#include "../renderable/SceneLightSet.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
  DirectionalShadow = 8,
  SpotShadow0 = 9,
  SpotShadow1 = 10,
  SpotShadow2 = 11,
};

struct SceneTransform {
  glm::vec3 position = {0.0f, 0.0f, 0.0f};
  glm::vec3 rotationDegrees = {0.0f, 0.0f, 0.0f};
  glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

struct SceneObject {
  std::string name = "Scene Model";
  SceneTransform transform{};
  bool visible = true;
};

struct DefaultDebugUISettings {
  PresentedOutput presentedOutput = PresentedOutput::PbrPass;
  PbrDebugView pbrDebugView = PbrDebugView::Final;
  int selectedMaterialIndex = 0;
  int selectedObjectIndex = 0;
  int selectedLightIndex = -1;

  std::vector<SceneObject> sceneObjects{SceneObject{}};

  SceneLightSet sceneLights = SceneLightSet::showcaseLights();
  bool lightMarkersVisible = true;
  float lightMarkerScale = 0.35f;
  bool shadowsEnabled = true;
  float directionalShadowExtent = 12.0f;
  float directionalShadowNearPlane = 0.1f;
  float directionalShadowFarPlane = 24.0f;

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
  float dielectricSpecularScale = 1.0f;
  float environmentRotationRadians = 0.0f;
  bool iblEnabled = false;
  bool skyboxVisible = false;
  bool syncSkySunToLight = true;
  ImageBasedLightingBakeSettings iblBakeSettings{};

  glm::vec3 cameraPosition = {0.0f, 1.4f, 4.5f};
  float cameraYawRadians = glm::radians(180.0f);
  float cameraPitchRadians = glm::radians(-12.0f);
  float cameraMoveSpeed = 2.5f;
  float cameraLookSensitivity = 0.0035f;
  float cameraFarPlane = 100.0f;
  bool cameraLookActive = false;
  double cameraLastCursorX = 0.0;
  double cameraLastCursorY = 0.0;
};

struct DefaultDebugUICallbacks {
  std::function<void()> syncProceduralSkySunWithLight;
  std::function<glm::vec3()> currentPrimaryDirectionalLightWorld;
};

class DefaultDebugCameraController {
public:
  explicit DefaultDebugCameraController(DefaultDebugUISettings &settings)
      : settings(settings) {}

  static DefaultDebugCameraController create(DefaultDebugUISettings &settings) {
    return DefaultDebugCameraController(settings);
  }

  void reset() {
    settings.cameraPosition = {0.0f, 1.4f, 4.5f};
    settings.cameraYawRadians = glm::radians(180.0f);
    settings.cameraPitchRadians = glm::radians(-12.0f);
    settings.cameraFarPlane = 100.0f;
    settings.cameraLookActive = false;
  }

  static glm::vec3 forwardFromAngles(float yawRadians, float pitchRadians) {
    const float cosPitch = std::cos(pitchRadians);
    return glm::normalize(glm::vec3(std::sin(yawRadians) * cosPitch,
                                    std::sin(pitchRadians),
                                    -std::cos(yawRadians) * cosPitch));
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

        settings.cameraYawRadians +=
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
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
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
  bool saveSessionRequested = false;
  bool reloadSessionRequested = false;
  bool resetSessionRequested = false;
};

struct DefaultDebugUIPerformanceStats {
  float fps = 0.0f;
  float frameTimeMs = 0.0f;
  uint32_t objectCount = 0;
  uint32_t lightCount = 0;
  uint32_t materialCount = 0;
  uint32_t vertexCount = 0;
  uint32_t triangleCount = 0;
  uint32_t drawCallCount = 0;
  uint32_t sceneDrawCallCount = 0;
  uint32_t shadowDrawCallCount = 0;
};

struct DefaultDebugUIBindings {
  RenderableModel &sceneModel;
  DefaultDebugUISettings &settings;
  DefaultDebugUICallbacks callbacks;
  DefaultDebugUIPerformanceStats performanceStats;
  ImGuiID dockspaceId = 0;
};

inline void ensureSceneObjects(DefaultDebugUISettings &settings) {
  if (settings.sceneObjects.empty()) {
    settings.sceneObjects.push_back(SceneObject{});
  }
  settings.selectedObjectIndex =
      std::clamp(settings.selectedObjectIndex, 0,
                 static_cast<int>(settings.sceneObjects.size()) - 1);
}
