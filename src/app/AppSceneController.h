#pragma once

#include "../passes/ShadowPass.h"
#include "../renderable/RenderableModel.h"
#include "../utils/DebugUIState.h"
#include <array>
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
    for (const auto &object : settings.sceneObjects) {
      anchor += object.transform.position;
    }
    return anchor / static_cast<float>(settings.sceneObjects.size());
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

  template <size_t SpotShadowPassCount>
  static void rebuildRenderItems(
      std::vector<RenderItem> &renderItems, RenderableModel &sceneModel,
      const DefaultDebugUISettings &settings, const RenderPass *geometryPass,
      const RenderPass *directionalShadowPass,
      const std::array<ShadowPass *, SpotShadowPassCount> &spotShadowPasses,
      Mesh &fullscreenQuad, const RenderPass *pbrPass,
      const RenderPass *tonemapPass, const RenderPass *debugPresentPass) {
    const std::vector<glm::mat4> objectMatrices = sceneObjectMatrices(settings);
    renderItems = sceneModel.buildRenderItems(geometryPass, objectMatrices);

    if (directionalShadowPass != nullptr) {
      auto shadowItems =
          sceneModel.buildRenderItems(directionalShadowPass, objectMatrices);
      renderItems.insert(renderItems.end(), shadowItems.begin(),
                         shadowItems.end());
    }

    for (ShadowPass *spotShadowPass : spotShadowPasses) {
      if (spotShadowPass == nullptr) {
        continue;
      }
      auto shadowItems =
          sceneModel.buildRenderItems(spotShadowPass, objectMatrices);
      renderItems.insert(renderItems.end(), shadowItems.begin(),
                         shadowItems.end());
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
};
