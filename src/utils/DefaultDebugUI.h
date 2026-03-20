#pragma once

#include "DebugUIState.h"
#include "RenderSettingsUI.h"
#include "SceneEditorUI.h"
#include <utility>

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
    SceneEditorUI sceneEditorUi(bindings);
    RenderSettingsUI renderSettingsUi(bindings);
    result.materialChanged = sceneEditorUi.build();
    buildPerformanceUi();
    buildSessionUi(result);
    buildCameraUi();
    result.iblBakeRequested = renderSettingsUi.build();
    return result;
  }

private:
  DefaultDebugUIBindings bindings;

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

  void buildSessionUi(DefaultDebugUIResult &result) {
    ImGui::Begin("Session");
    if (ImGui::Button("Save Current")) {
      result.saveSessionRequested = true;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Reload From Disk")) {
      result.reloadSessionRequested = true;
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Reset To Defaults")) {
      result.resetSessionRequested = true;
    }
    ImGui::End();
  }
};
