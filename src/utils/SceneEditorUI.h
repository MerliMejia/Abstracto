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
    buildHierarchyPanel();
    return buildInspectorPanel();
  }

private:
  DefaultDebugUIBindings bindings;

  void selectObject(int index) {
    auto &settings = bindings.settings;
    settings.selectedObjectIndex = index;
    settings.selectedLightIndex = -1;

    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    if (asset == nullptr) {
      return;
    }

    const auto &submeshes = asset->submeshes();
    if (index < 0 || index >= static_cast<int>(submeshes.size())) {
      return;
    }

    const int materialIndex =
        submeshes[static_cast<size_t>(index)].materialIndex;
    if (materialIndex >= 0 &&
        materialIndex <
            static_cast<int>(bindings.sceneModel.materials().size())) {
      settings.selectedMaterialIndex = materialIndex;
    }
  }

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

  void buildHierarchyPanel() {
    auto &settings = bindings.settings;
    ensureSceneObjects(settings);
    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    const auto &lights = settings.sceneLights.lights();

    ImGui::Begin("Hierarchy");
    if (ImGui::Button("+ Directional")) {
      settings.sceneLights.addDirectional();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Point")) {
      settings.sceneLights.addPoint();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Spot")) {
      settings.sceneLights.addSpot();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
    }
    if (ImGui::Button("Reset Lights")) {
      settings.sceneLights = SceneLightSet::showcaseLights();
      settings.selectedLightIndex = settings.sceneLights.empty() ? -1 : 0;
    }

    ImGui::SeparatorText("Objects");
    for (int index = 0; index < static_cast<int>(settings.sceneObjects.size());
         ++index) {
      const SceneObject &object =
          settings.sceneObjects[static_cast<size_t>(index)];
      std::string label = object.name.empty()
                              ? "Scene Object " + std::to_string(index)
                              : object.name;
      if (asset != nullptr && index == 0) {
        label +=
            "##" + std::filesystem::path(asset->path()).filename().string();
      }
      const bool selected = settings.selectedLightIndex < 0 &&
                            settings.selectedObjectIndex == index;
      if (ImGui::Selectable(label.c_str(), selected)) {
        selectObject(index);
      }
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

  bool buildInspectorPanel() {
    auto &settings = bindings.settings;
    ensureSceneObjects(settings);

    ImGui::Begin("Inspector");
    const bool objectSelected = settings.selectedLightIndex < 0;
    bool materialChanged = false;
    if (objectSelected) {
      materialChanged = buildObjectInspector();
    } else {
      buildLightInspector();
    }
    ImGui::End();
    return materialChanged;
  }

  bool buildObjectInspector() {
    auto &settings = bindings.settings;
    SceneObject &object =
        settings
            .sceneObjects[static_cast<size_t>(settings.selectedObjectIndex)];
    const ModelAsset *asset = bindings.sceneModel.modelAsset();

    ImGui::Text("Object: %s",
                object.name.empty() ? "<unnamed>" : object.name.c_str());
    if (asset != nullptr) {
      const std::string assetPath = asset->path();
      ImGui::Text("Asset: %s",
                  std::filesystem::path(assetPath).filename().string().c_str());
      if (!assetPath.empty()) {
        ImGui::TextWrapped("Source: %s", assetPath.c_str());
      }
    } else {
      ImGui::TextUnformatted("Asset: Scene Model");
    }

    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &object.transform.position.x, 0.01f);
    ImGui::SliderFloat3("Rotation", &object.transform.rotationDegrees.x,
                        -180.0f, 180.0f);
    ImGui::DragFloat3("Scale", &object.transform.scale.x, 0.1f, 0.01f, 200.0f);

    auto &materials = bindings.sceneModel.mutableMaterials();
    if (materials.empty()) {
      return false;
    }

    settings.selectedMaterialIndex =
        std::clamp(settings.selectedMaterialIndex, 0,
                   static_cast<int>(materials.size()) - 1);

    ImGui::SeparatorText("Materials");
    if (ImGui::BeginListBox("Material Slots")) {
      for (int index = 0; index < static_cast<int>(materials.size()); ++index) {
        const bool selected = settings.selectedMaterialIndex == index;
        const char *label = materials[index].name.empty()
                                ? "<unnamed>"
                                : materials[index].name.c_str();
        if (ImGui::Selectable(label, selected)) {
          settings.selectedMaterialIndex = index;
        }
      }
      ImGui::EndListBox();
    }

    bool materialChanged = false;
    auto &material =
        materials[static_cast<size_t>(settings.selectedMaterialIndex)];
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
    return materialChanged;
  }

  void buildLightInspector() {
    auto &settings = bindings.settings;
    auto &lights = settings.sceneLights.lights();
    if (lights.empty()) {
      settings.selectedLightIndex = -1;
      ImGui::TextUnformatted("No lights in the scene.");
      return;
    }

    settings.selectedLightIndex = std::clamp(
        settings.selectedLightIndex, 0, static_cast<int>(lights.size()) - 1);
    SceneLight &light =
        lights[static_cast<size_t>(settings.selectedLightIndex)];

    ImGui::Text("Light: %s", light.name.c_str());
    ImGui::Text("Type: %s", lightTypeLabel(light.type));
    ImGui::Checkbox("Enabled", &light.enabled);
    ImGui::ColorEdit3("Color", &light.color.x);
    ImGui::DragFloat("Power", &light.power, 0.1f, 0.0f, 10000.0f, "%.3f");
    light.power = std::max(light.power, 0.0f);
    ImGui::SliderFloat("Exposure", &light.exposure, -16.0f, 16.0f, "%.3f");

    ImGui::SeparatorText("Shadowing");
    if (light.type == SceneLightType::Directional ||
        light.type == SceneLightType::Spot) {
      ImGui::Checkbox("Casts Shadow", &light.castsShadow);
      ImGui::SliderFloat("Shadow Bias", &light.shadowBias, 0.0001f, 0.02f,
                         "%.4f");
      ImGui::SliderFloat("Shadow Normal Bias", &light.shadowNormalBias, 0.0f,
                         0.2f, "%.4f");
    } else {
      bool pointShadowDisabled = false;
      ImGui::BeginDisabled();
      ImGui::Checkbox("Casts Shadow", &pointShadowDisabled);
      ImGui::EndDisabled();
      light.castsShadow = false;
      ImGui::TextUnformatted("Point-light shadows are not implemented yet.");
    }

    if (light.type == SceneLightType::Directional) {
      ImGui::TextUnformatted("Directional lights use direction only.");
    }

    if (light.type == SceneLightType::Directional ||
        light.type == SceneLightType::Spot) {
      ImGui::SeparatorText("Direction");
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
      settings.selectedLightIndex =
          settings.sceneLights.empty()
              ? -1
              : std::clamp(settings.selectedLightIndex, 0,
                           static_cast<int>(settings.sceneLights.size()) - 1);
    }

    const glm::vec3 primaryDirection =
        bindings.callbacks.currentPrimaryDirectionalLightWorld();
    ImGui::SeparatorText("Primary Directional");
    ImGui::Text("Direction: %.2f %.2f %.2f", primaryDirection.x,
                primaryDirection.y, primaryDirection.z);
  }
};
