#pragma once

#include "AnimationEditorUI.h"
#include "DebugUIState.h"
#include "RenderSettingsUI.h"
#include "SceneEditorUI.h"
#include <utility>

#include <imgui_internal.h>

class DefaultDebugUI {
public:
  explicit DefaultDebugUI(DefaultDebugUIBindings bindings)
      : bindings(std::move(bindings)) {}

  static DefaultDebugUI
  create(RenderableModel &sceneModel, std::vector<RenderableModel> &sceneModels,
         DefaultDebugUISettings &settings,
         DefaultDebugUICallbacks callbacks,
         DefaultDebugUIPerformanceStats performanceStats = {},
         ImGuiID dockspaceId = 0) {
    return DefaultDebugUI(DefaultDebugUIBindings{
        .sceneModel = sceneModel,
        .sceneModels = sceneModels,
        .settings = settings,
        .callbacks = std::move(callbacks),
        .performanceStats = performanceStats,
        .dockspaceId = dockspaceId,
    });
  }

  DefaultDebugUIResult build() {
    applyDefaultDockLayout();

    DefaultDebugUIResult result;
    SceneEditorUI sceneEditorUi(bindings);
    AnimationEditorUI animationEditorUi(bindings);
    RenderSettingsUI renderSettingsUi(bindings);
    result.materialChanged = sceneEditorUi.build();
    animationEditorUi.build();
    result.iblBakeRequested = renderSettingsUi.buildWorldPanel();
    buildToolsPanel(result);
    return result;
  }

private:
  DefaultDebugUIBindings bindings;

  void applyDefaultDockLayout() {
    static ImGuiID lastDockspaceId = 0;
    if (bindings.dockspaceId == 0 || bindings.dockspaceId == lastDockspaceId) {
      return;
    }

    ImGui::DockBuilderRemoveNode(bindings.dockspaceId);
    ImGui::DockBuilderAddNode(bindings.dockspaceId,
                              ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(bindings.dockspaceId,
                                  ImGui::GetMainViewport()->WorkSize);

    ImGuiID dockMain = bindings.dockspaceId;
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down,
                                                     0.24f, nullptr, &dockMain);
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left,
                                                   0.20f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right,
                                                    0.28f, nullptr, &dockMain);

    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Animations", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Animation Inspector", dockRight);
    ImGui::DockBuilderDockWindow("World", dockRight);
    ImGui::DockBuilderDockWindow("Tools", dockBottom);
    ImGui::DockBuilderFinish(bindings.dockspaceId);
    lastDockspaceId = bindings.dockspaceId;
  }

  void buildToolsPanel(DefaultDebugUIResult &result) {
    ImGui::Begin("Tools");
    if (ImGui::BeginTabBar("ToolsTabs")) {
      if (ImGui::BeginTabItem("Performance")) {
        buildPerformanceUi();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Camera")) {
        buildCameraUi();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Session")) {
        buildSessionUi(result);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::End();
  }

  void buildCameraUi() {
    auto &settings = bindings.settings;
    DefaultDebugCameraController cameraController =
        DefaultDebugCameraController::create(settings);
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
  }

  void buildPerformanceUi() {
    const auto &performanceStats = bindings.performanceStats;
    if (ImGui::BeginTable("PerformanceStats", 4,
                          ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("FPS: %.1f", performanceStats.fps);

      ImGui::TableNextColumn();
      ImGui::Text("Frame Time: %.2f ms", performanceStats.frameTimeMs);

      ImGui::TableNextColumn();
      ImGui::Text("Objects: %u", performanceStats.objectCount);

      ImGui::TableNextColumn();
      ImGui::Text("Lights: %u", performanceStats.lightCount);

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Materials: %u", performanceStats.materialCount);

      ImGui::TableNextColumn();
      ImGui::Text("Vertices: %u", performanceStats.vertexCount);

      ImGui::TableNextColumn();
      ImGui::Text("Triangles: %u", performanceStats.triangleCount);

      ImGui::TableNextColumn();
      ImGui::Text("Draw Calls: %u", performanceStats.drawCallCount);

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Prepared Draws: %u",
                  performanceStats.preparedDrawCallCount);

      ImGui::TableNextColumn();
      ImGui::Text("Scene Draws: %u", performanceStats.sceneDrawCallCount);

      ImGui::TableNextColumn();
      ImGui::Text("Shadow Draws: %u", performanceStats.shadowDrawCallCount);

      ImGui::EndTable();
    }
  }

  void buildSessionUi(DefaultDebugUIResult &result) {
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
  }
};
