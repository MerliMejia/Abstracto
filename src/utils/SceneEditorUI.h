#pragma once

#include "DebugUIState.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

class SceneEditorUI {
public:
  explicit SceneEditorUI(DefaultDebugUIBindings bindings)
      : bindings(std::move(bindings)) {}

  bool build() {
    buildSceneOutlinerUi();
    buildObjectInspectorUi();
    buildLightsUi();
    return buildMaterialEditorUi();
  }

private:
  DefaultDebugUIBindings bindings;

  static const char *lightTypeLabel(SceneLightType type) {
    switch (type) {
    case SceneLightType::Directional:
      return "Directional";
    case SceneLightType::Point:
      return "Point";
    case SceneLightType::Spot:
      return "Spot";
    }
    return "Unknown";
  }

  static glm::vec3 directionFromAngles(float azimuthRadians,
                                       float elevationRadians) {
    const float cosElevation = std::cos(elevationRadians);
    return glm::normalize(glm::vec3(cosElevation * std::cos(azimuthRadians),
                                    cosElevation * std::sin(azimuthRadians),
                                    std::sin(elevationRadians)));
  }

  static void anglesFromDirection(const glm::vec3 &direction,
                                  float &azimuthRadians,
                                  float &elevationRadians) {
    const glm::vec3 normalizedDirection = glm::normalize(
        glm::length(direction) > 1e-6f ? direction
                                       : glm::vec3(0.0f, -1.0f, -1.0f));
    azimuthRadians = std::atan2(normalizedDirection.y, normalizedDirection.x);
    elevationRadians =
        std::asin(glm::clamp(normalizedDirection.z, -1.0f, 1.0f));
  }

  void buildSceneOutlinerUi() {
    auto &settings = bindings.settings;
    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    const std::string assetLabel =
        asset == nullptr
            ? "Scene Model"
            : std::filesystem::path(asset->path()).filename().string();
    const bool modelSelected = settings.selectedLightIndex < 0;
    const auto &lights = settings.sceneLights.lights();

    ImGui::Begin("Scene Outliner");
    ImGui::SeparatorText("Objects");
    if (ImGui::Selectable(assetLabel.c_str(), modelSelected)) {
      settings.selectedLightIndex = -1;
    }
    if (asset != nullptr) {
      ImGui::Text("Submeshes: %d", static_cast<int>(asset->submeshes().size()));
      ImGui::Text("Materials: %d",
                  static_cast<int>(bindings.sceneModel.materials().size()));
    }

    ImGui::SeparatorText("Lights");
    if (lights.empty()) {
      ImGui::TextUnformatted("No lights in the scene.");
    } else {
      for (int index = 0; index < static_cast<int>(lights.size()); ++index) {
        const SceneLight &light = lights[static_cast<size_t>(index)];
        std::string label =
            light.name + "##outliner_light_" + std::to_string(index);
        if (ImGui::Selectable(label.c_str(),
                              settings.selectedLightIndex == index)) {
          settings.selectedLightIndex = index;
        }
      }
    }
    ImGui::End();
  }

  void buildObjectInspectorUi() {
    auto &settings = bindings.settings;
    const bool modelSelected = settings.selectedLightIndex < 0;

    ImGui::Begin("Object Inspector");
    if (!modelSelected) {
      ImGui::TextUnformatted(
          "A light is selected. Edit its properties in the Lights window.");
      ImGui::End();
      return;
    }

    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    if (asset != nullptr) {
      const std::string assetPath = asset->path();
      ImGui::Text("Object: %s",
                  std::filesystem::path(assetPath).filename().string().c_str());
      if (!assetPath.empty()) {
        ImGui::TextWrapped("Source: %s", assetPath.c_str());
      }
    } else {
      ImGui::TextUnformatted("Object: Scene Model");
    }

    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &settings.modelPosition.x, 0.01f);
    ImGui::SliderFloat3("Rotation", &settings.modelRotationDegrees.x, -180.0f,
                        180.0f);
    ImGui::DragFloat3("Scale", &settings.modelScale.x, 0.1f, 0.01f, 200.0f);
    if (ImGui::Button("Reload Model")) {
      bindings.callbacks.reloadSceneModel();
    }
    ImGui::End();
  }

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

  void buildLightsUi() {
    auto &settings = bindings.settings;
    auto &lights = settings.sceneLights.lights();
    ImGui::Begin("Lights");
    if (ImGui::Button("Add Directional")) {
      settings.sceneLights.addDirectional();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Point")) {
      settings.sceneLights.addPoint();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Spot")) {
      settings.sceneLights.addSpot();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
    }
    if (ImGui::Button("Reset Showcase Lights")) {
      settings.sceneLights = SceneLightSet::showcaseLights();
      settings.selectedLightIndex = 0;
    }

    if (lights.empty()) {
      settings.selectedLightIndex = -1;
      ImGui::TextUnformatted("No lights in the scene.");
      ImGui::End();
      return;
    }

    if (settings.selectedLightIndex >= static_cast<int>(lights.size())) {
      settings.selectedLightIndex = static_cast<int>(lights.size()) - 1;
    }

    ImGui::SeparatorText("Scene Lights");
    for (int index = 0; index < static_cast<int>(lights.size()); ++index) {
      const SceneLight &light = lights[static_cast<size_t>(index)];
      std::string label = light.name + "##light_" + std::to_string(index);
      if (ImGui::Selectable(label.c_str(),
                            settings.selectedLightIndex == index)) {
        settings.selectedLightIndex = index;
      }
    }

    if (settings.selectedLightIndex < 0) {
      ImGui::SeparatorText("Selected Light");
      ImGui::TextUnformatted("Select a light from the Scene Outliner.");
      ImGui::End();
      return;
    }

    SceneLight &light =
        lights[static_cast<size_t>(settings.selectedLightIndex)];
    ImGui::SeparatorText("Selected Light");
    ImGui::Text("Type: %s", lightTypeLabel(light.type));
    ImGui::Checkbox("Enabled", &light.enabled);
    ImGui::ColorEdit3("Color", &light.color.x);
    ImGui::DragFloat("Power", &light.power, 0.1f, 0.0f, 10000.0f, "%.3f");
    light.power = std::max(light.power, 0.0f);
    ImGui::SliderFloat("Exposure", &light.exposure, -16.0f, 16.0f, "%.3f");
    if (light.type == SceneLightType::Directional ||
        light.type == SceneLightType::Spot) {
      ImGui::SeparatorText("Shadowing");
      ImGui::Checkbox("Casts Shadow", &light.castsShadow);
      ImGui::SliderFloat("Shadow Bias", &light.shadowBias, 0.0001f, 0.02f,
                         "%.4f");
      ImGui::SliderFloat("Shadow Normal Bias", &light.shadowNormalBias, 0.0f,
                         0.2f, "%.4f");
    } else {
      bool pointShadowDisabled = false;
      ImGui::SeparatorText("Shadowing");
      ImGui::BeginDisabled();
      ImGui::Checkbox("Casts Shadow", &pointShadowDisabled);
      ImGui::EndDisabled();
      light.castsShadow = false;
      ImGui::TextUnformatted("Point-light shadows are not implemented yet.");
    }

    if (light.type == SceneLightType::Directional) {
      ImGui::BeginDisabled();
      glm::vec3 directionalPosition(0.0f, 0.0f, 0.0f);
      ImGui::DragFloat3("Position", &directionalPosition.x, 0.05f);
      ImGui::EndDisabled();
      ImGui::TextUnformatted("Directional lights use direction only.");
    }

    if (light.type == SceneLightType::Directional ||
        light.type == SceneLightType::Spot) {
      float azimuthRadians = 0.0f;
      float elevationRadians = 0.0f;
      anglesFromDirection(light.direction, azimuthRadians, elevationRadians);
      float azimuthDegrees = glm::degrees(azimuthRadians);
      float elevationDegrees = glm::degrees(elevationRadians);
      if (ImGui::SliderFloat("Azimuth", &azimuthDegrees, -180.0f, 180.0f)) {
        azimuthRadians = glm::radians(azimuthDegrees);
        light.direction = directionFromAngles(azimuthRadians, elevationRadians);
      }
      if (ImGui::SliderFloat("Elevation", &elevationDegrees, -89.0f, 89.0f)) {
        elevationRadians = glm::radians(elevationDegrees);
        light.direction = directionFromAngles(azimuthRadians, elevationRadians);
      }
      ImGui::Text("Direction: %.2f %.2f %.2f", light.direction.x,
                  light.direction.y, light.direction.z);
    }

    if (light.type == SceneLightType::Point ||
        light.type == SceneLightType::Spot) {
      ImGui::SeparatorText("Transform");
      ImGui::DragFloat3("Position", &light.position.x, 0.05f);
    }

    if (light.type == SceneLightType::Point) {
      ImGui::DragFloat("Radius", &light.radius, 0.01f, 0.0f, 25.0f, "%.3f m");
      light.radius = std::max(light.radius, 0.0f);
    }

    if (light.type == SceneLightType::Spot) {
      ImGui::SliderFloat("Range", &light.range, 0.5f, 25.0f);
      light.range = std::max(light.range, 0.01f);
      float innerDegrees = glm::degrees(light.innerConeAngleRadians);
      float outerDegrees = glm::degrees(light.outerConeAngleRadians);
      if (ImGui::SliderFloat("Inner Cone", &innerDegrees, 1.0f, 85.0f)) {
        light.innerConeAngleRadians = glm::radians(innerDegrees);
      }
      if (ImGui::SliderFloat("Outer Cone", &outerDegrees, 1.0f, 89.0f)) {
        light.outerConeAngleRadians = glm::radians(outerDegrees);
      }
      light.outerConeAngleRadians =
          std::max(light.outerConeAngleRadians, light.innerConeAngleRadians);
    }

    if (ImGui::Button("Remove Selected Light")) {
      settings.sceneLights.remove(
          static_cast<size_t>(settings.selectedLightIndex));
      if (settings.sceneLights.empty()) {
        settings.selectedLightIndex = -1;
      } else {
        settings.selectedLightIndex =
            std::clamp(settings.selectedLightIndex, 0,
                       static_cast<int>(settings.sceneLights.size()) - 1);
      }
    }

    const glm::vec3 primaryDirection =
        bindings.callbacks.currentPrimaryDirectionalLightWorld();
    ImGui::SeparatorText("Primary Directional");
    ImGui::Text("Direction: %.2f %.2f %.2f", primaryDirection.x,
                primaryDirection.y, primaryDirection.z);
    ImGui::End();
  }
};
