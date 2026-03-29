#pragma once

#include "engine/editor/DebugUIState.h"
#include "SceneDefinition.h"
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

class AppSceneController {
public:
  static glm::mat4 sceneTransformMatrix(const SceneTransform &transform) {
    glm::mat4 matrix(1.0f);
    matrix = glm::translate(matrix, transform.position);
    matrix = glm::rotate(matrix, glm::radians(transform.rotationDegrees.x),
                         glm::vec3(1.0f, 0.0f, 0.0f));
    matrix = glm::rotate(matrix, glm::radians(transform.rotationDegrees.y),
                         glm::vec3(0.0f, 1.0f, 0.0f));
    matrix = glm::rotate(matrix, glm::radians(transform.rotationDegrees.z),
                         glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(matrix, transform.scale);
  }

  static SceneTransform sceneTransformFromMatrix(const glm::mat4 &matrix) {
    SceneTransform transform;
    transform.position = glm::vec3(matrix[3]);

    glm::mat3 rotationMatrix(matrix);
    transform.scale.x = glm::length(rotationMatrix[0]);
    transform.scale.y = glm::length(rotationMatrix[1]);
    transform.scale.z = glm::length(rotationMatrix[2]);

    if (transform.scale.x > 1e-6f) {
      rotationMatrix[0] /= transform.scale.x;
    }
    if (transform.scale.y > 1e-6f) {
      rotationMatrix[1] /= transform.scale.y;
    }
    if (transform.scale.z > 1e-6f) {
      rotationMatrix[2] /= transform.scale.z;
    }

    transform.rotationDegrees =
        glm::degrees(glm::eulerAngles(glm::quat_cast(rotationMatrix)));
    return transform;
  }

  static glm::vec3 sceneObjectsAnchor(const DefaultDebugUISettings &settings) {
    if (settings.sceneObjects.empty()) {
      return glm::vec3(0.0f);
    }

    glm::vec3 anchor(0.0f);
    uint32_t visibleObjectCount = 0;
    for (const auto &object : settings.sceneObjects) {
      if (!object.visible) {
        continue;
      }
      anchor += object.transform.position;
      ++visibleObjectCount;
    }

    if (visibleObjectCount == 0) {
      return glm::vec3(0.0f);
    }
    return anchor / static_cast<float>(visibleObjectCount);
  }

  static void syncSceneObjectsWithAssets(
      DefaultDebugUISettings &settings,
      const std::vector<SceneAssetInstance> &sceneAssets) {
    if (settings.sceneObjects.size() == sceneAssets.size()) {
      for (size_t index = 0; index < sceneAssets.size(); ++index) {
        if (settings.sceneObjects[index].name.empty()) {
          settings.sceneObjects[index].name =
              sceneAssetName(sceneAssets[index], index);
        }
      }
      clampSceneObjectSelection(settings);
      return;
    }

    settings.sceneObjects.clear();
    settings.sceneObjects.reserve(sceneAssets.size());
    for (size_t index = 0; index < sceneAssets.size(); ++index) {
      const auto &sceneAsset = sceneAssets[index];
      settings.sceneObjects.push_back(SceneObject{
          .name = sceneAssetName(sceneAsset, index),
          .transform = sceneAsset.transform,
          .visible = sceneAsset.visible,
      });
    }

    clampSceneObjectSelection(settings);
    settings.selectedLightIndex = -1;
  }

  static void applyObjectOverrides(
      DefaultDebugUISettings &settings,
      const std::vector<SceneObjectOverride> &objectOverrides) {
    for (const auto &objectOverride : objectOverrides) {
      auto objectIt = std::find_if(
          settings.sceneObjects.begin(), settings.sceneObjects.end(),
          [&objectOverride](const SceneObject &object) {
            return object.name == objectOverride.name;
          });
      if (objectIt == settings.sceneObjects.end()) {
        continue;
      }
      if (objectOverride.overrideTransform) {
        objectIt->transform = objectOverride.transform;
      }
      if (objectOverride.overrideVisibility) {
        objectIt->visible = objectOverride.visible;
      }
    }
  }

private:
  static std::string sceneAssetName(const SceneAssetInstance &sceneAsset,
                                    size_t index) {
    if (!sceneAsset.name.empty()) {
      return sceneAsset.name;
    }
    if (!sceneAsset.assetPath.empty()) {
      const std::string stem =
          std::filesystem::path(sceneAsset.assetPath).stem().string();
      if (!stem.empty()) {
        return stem;
      }
    }
    return "Scene Asset " + std::to_string(index);
  }
};
