#pragma once

#include "DebugUIState.h"
#include <algorithm>
#include <string>
#include <utility>

class AnimationEditorUI {
public:
  explicit AnimationEditorUI(DefaultDebugUIBindings bindings)
      : bindings(std::move(bindings)) {}

  void build() {
    buildAnimationHierarchyPanel();
    buildAnimationInspectorPanel();
  }

private:
  DefaultDebugUIBindings bindings;

  void clampAnimationSelection() const {
    auto &settings = bindings.settings;
    const size_t objectCount =
        std::min(bindings.sceneModels.size(), settings.sceneObjects.size());
    if (objectCount == 0) {
      settings.selectedAnimationObjectIndex = -1;
      settings.selectedAnimationIndex = -1;
      return;
    }

    settings.selectedAnimationObjectIndex =
        std::clamp(settings.selectedAnimationObjectIndex, -1,
                   static_cast<int>(objectCount) - 1);

    if (settings.selectedAnimationObjectIndex < 0) {
      settings.selectedAnimationIndex = -1;
      return;
    }

    const RenderableModel &model = bindings.sceneModels[static_cast<size_t>(
        settings.selectedAnimationObjectIndex)];
    const ModelAsset *asset = model.modelAsset();
    const SkeletonAssetData *skeleton =
        asset == nullptr ? nullptr : asset->skeletonAsset();
    if (skeleton == nullptr || skeleton->animations.empty()) {
      settings.selectedAnimationIndex = -1;
      return;
    }

    settings.selectedAnimationIndex =
        std::clamp(settings.selectedAnimationIndex, -1,
                   static_cast<int>(skeleton->animations.size()) - 1);
  }

  void selectAnimation(int objectIndex, int animationIndex) {
    auto &settings = bindings.settings;
    settings.selectedAnimationObjectIndex = objectIndex;
    settings.selectedAnimationIndex = animationIndex;
    settings.selectedObjectIndex = objectIndex;
    settings.selectedLightIndex = -1;
    settings.selectedBoneIndex = -1;

    if (objectIndex < 0 ||
        static_cast<size_t>(objectIndex) >= bindings.sceneModels.size()) {
      return;
    }
    bindings.sceneModels[static_cast<size_t>(objectIndex)].selectSourceAnimation(
        animationIndex);
  }

  void buildAnimationHierarchyPanel() {
    auto &settings = bindings.settings;
    clampAnimationSelection();

    ImGui::Begin("Animations");
    const size_t objectCount =
        std::min(bindings.sceneModels.size(), settings.sceneObjects.size());
    bool hasAnimatedObjects = false;

    for (size_t objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
      const RenderableModel &model = bindings.sceneModels[objectIndex];
      const ModelAsset *asset = model.modelAsset();
      const SkeletonAssetData *skeleton =
          asset == nullptr ? nullptr : asset->skeletonAsset();
      if (skeleton == nullptr || skeleton->animations.empty()) {
        continue;
      }

      hasAnimatedObjects = true;
      const SceneObject &object = settings.sceneObjects[objectIndex];
      const bool selectedObject =
          settings.selectedAnimationObjectIndex == static_cast<int>(objectIndex);
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                 ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                 ImGuiTreeNodeFlags_SpanAvailWidth;
      if (selectedObject) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
      }

      const std::string objectLabel =
          (object.name.empty() ? "Scene Object " + std::to_string(objectIndex)
                               : object.name) +
          "##animation_object_" + std::to_string(objectIndex);
      const bool open = ImGui::TreeNodeEx(objectLabel.c_str(), flags);
      if (ImGui::IsItemClicked()) {
        settings.selectedAnimationObjectIndex = static_cast<int>(objectIndex);
        settings.selectedAnimationIndex = -1;
      }

      if (open) {
        for (int animationIndex = 0;
             animationIndex < static_cast<int>(skeleton->animations.size());
             ++animationIndex) {
          const auto &animation =
              skeleton->animations[static_cast<size_t>(animationIndex)];
          const std::string animationLabel =
              (animation.name.empty()
                   ? "Animation " + std::to_string(animationIndex)
                   : animation.name) +
              "##animation_item_" + std::to_string(objectIndex) + "_" +
              std::to_string(animationIndex);
          if (ImGui::Selectable(
                  animationLabel.c_str(),
                  settings.selectedAnimationObjectIndex ==
                          static_cast<int>(objectIndex) &&
                      settings.selectedAnimationIndex == animationIndex)) {
            selectAnimation(static_cast<int>(objectIndex), animationIndex);
          }
        }
        ImGui::TreePop();
      }
    }

    if (!hasAnimatedObjects) {
      ImGui::TextUnformatted("No animated objects in the scene.");
    }
    ImGui::End();
  }

  void buildAnimationInspectorPanel() {
    auto &settings = bindings.settings;
    clampAnimationSelection();

    ImGui::Begin("Animation Inspector");
    if (settings.selectedAnimationObjectIndex < 0 ||
        settings.selectedAnimationIndex < 0 ||
        static_cast<size_t>(settings.selectedAnimationObjectIndex) >=
            bindings.sceneModels.size()) {
      ImGui::TextUnformatted("No animation selected.");
      ImGui::End();
      return;
    }

    RenderableModel &model = bindings.sceneModels[static_cast<size_t>(
        settings.selectedAnimationObjectIndex)];
    const ModelAsset *asset = model.modelAsset();
    const SkeletonAssetData *skeleton =
        asset == nullptr ? nullptr : asset->skeletonAsset();
    if (skeleton == nullptr ||
        static_cast<size_t>(settings.selectedAnimationIndex) >=
            skeleton->animations.size()) {
      ImGui::TextUnformatted("No animation selected.");
      ImGui::End();
      return;
    }

    const SceneObject &object = bindings.settings.sceneObjects[static_cast<size_t>(
        settings.selectedAnimationObjectIndex)];
    const auto &animation =
        skeleton->animations[static_cast<size_t>(settings.selectedAnimationIndex)];
    AnimationPlaybackState *playback = model.mutableAnimationPlayback();
    if (playback == nullptr) {
      ImGui::TextUnformatted("Selected object does not support animation playback.");
      ImGui::End();
      return;
    }
    if (playback->selectedSourceAnimationIndex != settings.selectedAnimationIndex) {
      model.selectSourceAnimation(settings.selectedAnimationIndex);
    }

    ImGui::Text("Object: %s",
                object.name.empty() ? "<unnamed>" : object.name.c_str());
    ImGui::Text("Animation: %s",
                animation.name.empty() ? "<unnamed>" : animation.name.c_str());
    ImGui::Text("Duration: %.3f s", animation.durationSeconds);
    ImGui::Text("Tracks: %zu", animation.tracks.size());
    ImGui::Text("Current Time: %.3f s", playback->currentTimeSeconds);

    if (ImGui::Button("Play")) {
      model.playSelectedAnimation();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Pause")) {
      model.pauseAnimationPlayback();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Reset")) {
      model.resetSelectedAnimation();
    }

    ImGui::Checkbox("Loop", &playback->loop);
    ImGui::SliderFloat("Playback Speed", &playback->speed, 0.0f, 3.0f,
                       "%.2f");
    ImGui::Text("State: %s", playback->playing ? "Playing" : "Paused");
    ImGui::End();
  }
};
