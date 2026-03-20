#pragma once

#include "../passes/ShadowPass.h"
#include "../renderable/RenderableModel.h"
#include "../utils/DebugUIState.h"
#include "SceneDefinition.h"
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string_view>
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

  static std::vector<glm::mat4>
  sceneObjectMatrices(const DefaultDebugUISettings &settings) {
    std::vector<glm::mat4> matrices;
    matrices.reserve(settings.sceneObjects.size());
    for (const auto &object : settings.sceneObjects) {
      matrices.push_back(sceneTransformMatrix(object.transform));
    }
    return matrices;
  }

  static void syncSceneObjectsWithModel(DefaultDebugUISettings &settings,
                                        const RenderableModel &sceneModel) {
    const ModelAsset *asset = sceneModel.modelAsset();
    if (asset == nullptr) {
      ensureSceneObjects(settings);
      return;
    }

    const auto &submeshes = asset->submeshes();
    if (submeshes.empty()) {
      ensureSceneObjects(settings);
      return;
    }

    if (settings.sceneObjects.size() == submeshes.size()) {
      for (size_t index = 0; index < submeshes.size(); ++index) {
        if (settings.sceneObjects[index].name.empty()) {
          settings.sceneObjects[index].name = submeshes[index].name;
        }
      }
      ensureSceneObjects(settings);
      return;
    }

    const glm::mat4 legacyRootMatrix =
        settings.sceneObjects.size() == 1
            ? sceneTransformMatrix(settings.sceneObjects.front().transform)
            : glm::mat4(1.0f);

    std::vector<SceneObject> sceneObjects;
    sceneObjects.reserve(submeshes.size());
    for (const auto &submesh : submeshes) {
      sceneObjects.push_back(SceneObject{
          .name = submesh.name.empty() ? "Scene Object" : submesh.name,
          .transform =
              sceneTransformFromMatrix(legacyRootMatrix * submesh.transform),
      });
    }

    settings.sceneObjects = std::move(sceneObjects);
    ensureSceneObjects(settings);
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

  template <size_t SpotShadowPassCount>
  static void rebuildRenderItems(
      std::vector<RenderItem> &renderItems, RenderableModel &sceneModel,
      const DefaultDebugUISettings &settings, const RenderPass *geometryPass,
      const RenderPass *directionalShadowPass,
      const std::array<ShadowPass *, SpotShadowPassCount> &spotShadowPasses,
      Mesh &fullscreenQuad, const RenderPass *pbrPass,
      const RenderPass *tonemapPass, const RenderPass *debugPresentPass) {
    const std::vector<glm::mat4> objectMatrices = sceneObjectMatrices(settings);
    renderItems.clear();

    auto geometryItems =
        sceneModel.buildRenderItems(geometryPass, objectMatrices);
    appendVisibleItems(renderItems, geometryItems, settings);

    if (directionalShadowPass != nullptr) {
      auto shadowItems =
          sceneModel.buildRenderItems(directionalShadowPass, objectMatrices);
      appendVisibleItems(renderItems, shadowItems, settings);
    }

    for (ShadowPass *spotShadowPass : spotShadowPasses) {
      if (spotShadowPass == nullptr) {
        continue;
      }
      auto shadowItems =
          sceneModel.buildRenderItems(spotShadowPass, objectMatrices);
      appendVisibleItems(renderItems, shadowItems, settings);
    }

    if (pbrPass != nullptr) {
      renderItems.push_back(RenderItem{.mesh = &fullscreenQuad,
                                       .descriptorBindings = nullptr,
                                       .targetPass = pbrPass});
    }
    if (tonemapPass != nullptr) {
      renderItems.push_back(RenderItem{.mesh = &fullscreenQuad,
                                       .descriptorBindings = nullptr,
                                       .targetPass = tonemapPass});
    }
    if (debugPresentPass != nullptr) {
      renderItems.push_back(RenderItem{.mesh = &fullscreenQuad,
                                       .descriptorBindings = nullptr,
                                       .targetPass = debugPresentPass});
    }
  }

private:
  static void appendVisibleItems(std::vector<RenderItem> &renderItems,
                                 const std::vector<RenderItem> &sourceItems,
                                 const DefaultDebugUISettings &settings) {
    if (sourceItems.size() != settings.sceneObjects.size()) {
      renderItems.insert(renderItems.end(), sourceItems.begin(),
                         sourceItems.end());
      return;
    }

    for (size_t index = 0; index < sourceItems.size(); ++index) {
      if (!settings.sceneObjects[index].visible) {
        continue;
      }
      renderItems.push_back(sourceItems[index]);
    }
  }
};
