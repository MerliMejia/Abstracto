#pragma once

#include "DebugUIState.h"
#include <algorithm>
#include <filesystem>
#include <unordered_set>
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

  const SkeletonAssetData *currentSkeleton() const {
    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    return asset == nullptr ? nullptr : asset->skeletonAsset();
  }

  std::unordered_set<int> currentDeformingBoneSet() const {
    std::unordered_set<int> deformingBones;
    const ModelAsset *asset = bindings.sceneModel.modelAsset();
    const SkeletonAssetData *skeleton = currentSkeleton();
    if (asset == nullptr || skeleton == nullptr) {
      return deformingBones;
    }

    for (const auto &submesh : asset->submeshes()) {
      if (submesh.skinIndex < 0 ||
          static_cast<size_t>(submesh.skinIndex) >= skeleton->skins.size()) {
        continue;
      }

      const SkinData &skin =
          skeleton->skins[static_cast<size_t>(submesh.skinIndex)];
      for (const int jointNodeIndex : skin.jointNodeIndices) {
        deformingBones.insert(jointNodeIndex);
      }
    }

    return deformingBones;
  }

  void clampBoneSelection(DefaultDebugUISettings &settings) const {
    const SkeletonAssetData *skeleton = currentSkeleton();
    if (skeleton == nullptr || skeleton->nodes.empty()) {
      settings.selectedBoneIndex = -1;
      return;
    }

    settings.selectedBoneIndex =
        std::clamp(settings.selectedBoneIndex, -1,
                   static_cast<int>(skeleton->nodes.size()) - 1);
  }

  void selectObject(int index) {
    auto &settings = bindings.settings;
    settings.selectedObjectIndex = index;
    settings.selectedLightIndex = -1;
    settings.selectedBoneIndex = -1;

    if (bindings.sceneModel.modelAsset() == nullptr) {
      return;
    }
    if (!bindings.sceneModel.materials().empty()) {
      settings.selectedMaterialIndex = 0;
    }
  }

  void selectBone(int index) {
    auto &settings = bindings.settings;
    settings.selectedLightIndex = -1;
    settings.selectedBoneIndex = index;
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
    clampSceneObjectSelection(settings);
    clampBoneSelection(settings);
    const auto &lights = settings.sceneLights.lights();

    ImGui::Begin("Hierarchy");
    if (ImGui::Button("+ Directional")) {
      settings.sceneLights.addDirectional();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
      settings.selectedBoneIndex = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Point")) {
      settings.sceneLights.addPoint();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
      settings.selectedBoneIndex = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Spot")) {
      settings.sceneLights.addSpot();
      settings.selectedLightIndex =
          static_cast<int>(settings.sceneLights.size()) - 1;
      settings.selectedBoneIndex = -1;
    }
    if (ImGui::Button("Reset Lights")) {
      settings.sceneLights = SceneLightSet::showcaseLights();
      settings.selectedLightIndex = settings.sceneLights.empty() ? -1 : 0;
      settings.selectedBoneIndex = -1;
    }

    ImGui::SeparatorText("Objects");
    if (settings.sceneObjects.empty()) {
      ImGui::TextUnformatted("No objects in the scene.");
    } else {
      for (int index = 0; index < static_cast<int>(settings.sceneObjects.size());
           ++index) {
        buildObjectHierarchyNode(index);
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
          settings.selectedBoneIndex = -1;
        }
      }
    }
    ImGui::End();
  }

  bool buildInspectorPanel() {
    auto &settings = bindings.settings;
    clampSceneObjectSelection(settings);
    clampBoneSelection(settings);

    ImGui::Begin("Inspector");
    bool materialChanged = false;
    if (settings.selectedLightIndex >= 0) {
      buildLightInspector();
    } else if (settings.selectedBoneIndex >= 0) {
      buildBoneInspector();
    } else if (!settings.sceneObjects.empty()) {
      materialChanged = buildObjectInspector();
    } else {
      ImGui::TextUnformatted("Nothing selected.");
    }
    ImGui::End();
    return materialChanged;
  }

  void buildObjectHierarchyNode(int index) {
    auto &settings = bindings.settings;
    const SceneObject &object = settings.sceneObjects[static_cast<size_t>(index)];
    const bool isSelectedObject =
        settings.selectedLightIndex < 0 && settings.selectedObjectIndex == index;
    const bool showSkeletonChild =
        isSelectedObject && currentSkeleton() != nullptr &&
        !currentSkeleton()->nodes.empty();

    const std::string label =
        (object.name.empty() ? "Scene Object " + std::to_string(index)
                             : object.name) +
        "##scene_object_" + std::to_string(index);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!showSkeletonChild) {
      flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (isSelectedObject && settings.selectedBoneIndex < 0) {
      flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (isSelectedObject) {
      flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
      selectObject(index);
    }

    if (open) {
      if (showSkeletonChild) {
        buildSkeletonHierarchyNode();
      }
      ImGui::TreePop();
    }
  }

  bool buildObjectInspector() {
    auto &settings = bindings.settings;
    if (settings.sceneObjects.empty()) {
      return false;
    }
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
      if (const auto *skeleton = asset->skeletonAsset(); skeleton != nullptr) {
        ImGui::SeparatorText("Skeleton");
        ImGui::Text("Nodes: %zu", skeleton->nodes.size());
        ImGui::Text("Skins: %zu", skeleton->skins.size());
        ImGui::Text("Animations: %zu", skeleton->animations.size());
      }
    } else {
      ImGui::TextUnformatted("Asset: Scene Model");
    }

    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &object.transform.position.x, 0.01f);
    ImGui::SliderFloat3("Rotation", &object.transform.rotationDegrees.x,
                        -180.0f, 180.0f);
    ImGui::DragFloat3("Scale", &object.transform.scale.x, 0.1f, 0.01f, 200.0f);

    if (asset == nullptr) {
      return false;
    }

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

  void buildSkeletonHierarchyNode() {
    auto &settings = bindings.settings;
    const SkeletonAssetData *skeleton = currentSkeleton();
    if (skeleton == nullptr || skeleton->nodes.empty()) {
      return;
    }
    const std::unordered_set<int> deformingBones = currentDeformingBoneSet();
    if (deformingBones.empty()) {
      return;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (settings.selectedBoneIndex >= 0) {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool open = ImGui::TreeNodeEx("Skeleton##object_skeleton", flags);
    if (ImGui::IsItemClicked()) {
      settings.selectedBoneIndex = -1;
      settings.selectedLightIndex = -1;
    }
    if (!open) {
      return;
    }

    std::vector<bool> visited(skeleton->nodes.size(), false);
    for (const int jointNodeIndex : deformingBones) {
      if (jointNodeIndex < 0 ||
          static_cast<size_t>(jointNodeIndex) >= skeleton->nodes.size()) {
        continue;
      }

      const SkeletonNode &jointNode =
          skeleton->nodes[static_cast<size_t>(jointNodeIndex)];
      if (jointNode.parentIndex >= 0 &&
          deformingBones.contains(jointNode.parentIndex)) {
        continue;
      }
      buildBoneTreeNode(*skeleton, settings, visited, deformingBones,
                        jointNodeIndex);
    }

    for (size_t nodeIndex = 0; nodeIndex < skeleton->nodes.size(); ++nodeIndex) {
      if (deformingBones.contains(static_cast<int>(nodeIndex)) &&
          !visited[nodeIndex]) {
        buildBoneTreeNode(*skeleton, settings, visited, deformingBones,
                          static_cast<int>(nodeIndex));
      }
    }

    ImGui::TreePop();
  }

  void buildBoneTreeNode(const SkeletonAssetData &skeleton,
                         DefaultDebugUISettings &settings,
                         std::vector<bool> &visited,
                         const std::unordered_set<int> &deformingBones,
                         int nodeIndex) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= skeleton.nodes.size()) {
      return;
    }
    if (!deformingBones.contains(nodeIndex)) {
      return;
    }

    visited[static_cast<size_t>(nodeIndex)] = true;
    const SkeletonNode &node = skeleton.nodes[static_cast<size_t>(nodeIndex)];
    const bool selected = settings.selectedBoneIndex == nodeIndex;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.childIndices.empty()) {
      flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (selected) {
      flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string label =
        (node.name.empty() ? "Bone " + std::to_string(nodeIndex) : node.name) +
        "##bone_" + std::to_string(nodeIndex);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
      selectBone(nodeIndex);
    }

    if (open) {
      for (const int childIndex : node.childIndices) {
        buildBoneTreeNode(skeleton, settings, visited, deformingBones,
                          childIndex);
      }
      ImGui::TreePop();
    }
  }

  void buildBoneInspector() {
    auto &settings = bindings.settings;
    const SkeletonAssetData *skeleton = currentSkeleton();
    SkeletonPose *pose = bindings.sceneModel.mutableSkeletonPose();
    if (skeleton == nullptr || skeleton->nodes.empty() ||
        pose == nullptr ||
        settings.selectedBoneIndex < 0 ||
        static_cast<size_t>(settings.selectedBoneIndex) >= skeleton->nodes.size()) {
      settings.selectedBoneIndex = -1;
      ImGui::TextUnformatted("No bone selected.");
      return;
    }

    const SkeletonNode &bone =
        skeleton->nodes[static_cast<size_t>(settings.selectedBoneIndex)];
    ImGui::Text("Bone: %s",
                bone.name.empty() ? "<unnamed>" : bone.name.c_str());
    ImGui::Text("Node Index: %d", settings.selectedBoneIndex);
    ImGui::Text("Parent Index: %d", bone.parentIndex);
    ImGui::Text("Child Count: %zu", bone.childIndices.size());

    int referencedSkinCount = 0;
    int jointReferenceCount = 0;
    for (const auto &skin : skeleton->skins) {
      bool skinContainsBone = false;
      for (const int jointNodeIndex : skin.jointNodeIndices) {
        if (jointNodeIndex != settings.selectedBoneIndex) {
          continue;
        }
        skinContainsBone = true;
        ++jointReferenceCount;
      }
      if (skinContainsBone) {
        ++referencedSkinCount;
      }
    }

    ImGui::SeparatorText("Bindings");
    ImGui::Text("Referenced By Skins: %d", referencedSkinCount);
    ImGui::Text("Joint References: %d", jointReferenceCount);

    ImGui::SeparatorText("Local Bind Transform");
    ImGui::Text("Translation: %.3f %.3f %.3f",
                bone.localBindTransform.translation.x,
                bone.localBindTransform.translation.y,
                bone.localBindTransform.translation.z);
    const glm::vec3 rotationDegrees =
        glm::degrees(glm::eulerAngles(bone.localBindTransform.rotation));
    ImGui::Text("Rotation (Euler): %.3f %.3f %.3f", rotationDegrees.x,
                rotationDegrees.y, rotationDegrees.z);
    ImGui::Text("Rotation (Quat): %.3f %.3f %.3f %.3f",
                bone.localBindTransform.rotation.x,
                bone.localBindTransform.rotation.y,
                bone.localBindTransform.rotation.z,
                bone.localBindTransform.rotation.w);
    ImGui::Text("Scale: %.3f %.3f %.3f", bone.localBindTransform.scale.x,
                bone.localBindTransform.scale.y, bone.localBindTransform.scale.z);

    NodeTransform &poseTransform =
        pose->localTransform(static_cast<size_t>(settings.selectedBoneIndex));
    glm::vec3 poseRotationDegrees =
        glm::degrees(glm::eulerAngles(poseTransform.rotation));
    bool poseChanged = false;

    ImGui::SeparatorText("Current Pose");
    ImGui::Text("Translation: %.3f %.3f %.3f", poseTransform.translation.x,
                poseTransform.translation.y, poseTransform.translation.z);
    poseChanged |= ImGui::SliderFloat3("Local Rotation",
                                       &poseRotationDegrees.x, -180.0f, 180.0f);
    ImGui::Text("Scale: %.3f %.3f %.3f", poseTransform.scale.x,
                poseTransform.scale.y, poseTransform.scale.z);

    if (poseChanged) {
      poseTransform.rotation =
          glm::normalize(glm::quat(glm::radians(poseRotationDegrees)));
      pose->recomputeWorldTransforms(*skeleton);
    }

    if (ImGui::Button("Reset Selected Bone")) {
      poseTransform = bone.localBindTransform;
      pose->recomputeWorldTransforms(*skeleton);
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Reset Skeleton Pose")) {
      bindings.sceneModel.resetSkeletonPose();
    }

    const glm::mat4 &worldTransform =
        pose->worldTransform(static_cast<size_t>(settings.selectedBoneIndex));
    ImGui::SeparatorText("World Pose");
    ImGui::Text("Position: %.3f %.3f %.3f", worldTransform[3].x,
                worldTransform[3].y, worldTransform[3].z);

    if (!bone.childIndices.empty()) {
      ImGui::SeparatorText("Children");
      for (const int childIndex : bone.childIndices) {
        if (childIndex < 0 ||
            static_cast<size_t>(childIndex) >= skeleton->nodes.size()) {
          continue;
        }
        const SkeletonNode &child =
            skeleton->nodes[static_cast<size_t>(childIndex)];
        ImGui::BulletText("%s [%d]",
                          child.name.empty() ? "<unnamed>" : child.name.c_str(),
                          childIndex);
      }
    }

    ImGui::SeparatorText("Animation");
    int animatedTrackCount = 0;
    for (const auto &animation : skeleton->animations) {
      for (const auto &track : animation.tracks) {
        if (track.targetNodeIndex == settings.selectedBoneIndex) {
          ++animatedTrackCount;
        }
      }
    }
    ImGui::Text("Animated Tracks: %d", animatedTrackCount);
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
