#pragma once

#include "engine/assets/RenderableModel.h"
#include "engine/editor/DebugUIState.h"
#include "engine/scene/AppSceneController.h"
#include "renderer/passes/ShadowPass.h"
#include <array>
#include <vector>

class SceneRenderItemBuilder {
public:
  template <size_t SpotShadowPassCount>
  static void rebuild(std::vector<RenderItem> &renderItems,
                      std::vector<RenderableModel> &sceneAssetModels,
                      const DefaultDebugUISettings &settings,
                      const RenderPass *geometryPass,
                      const RenderPass *directionalShadowPass,
                      const std::array<ShadowPass *, SpotShadowPassCount>
                          &spotShadowPasses,
                      Mesh &fullscreenQuad, const RenderPass *pbrPass,
                      const RenderPass *tonemapPass,
                      const RenderPass *debugPresentPass) {
    renderItems.clear();

    const size_t sceneAssetCount =
        std::min(sceneAssetModels.size(), settings.sceneObjects.size());
    for (size_t index = 0; index < sceneAssetCount; ++index) {
      if (!settings.sceneObjects[index].visible ||
          sceneAssetModels[index].modelAsset() == nullptr) {
        continue;
      }

      const std::vector<glm::mat4> objectMatrices = {
          AppSceneController::sceneTransformMatrix(
              settings.sceneObjects[index].transform)};
      const int selectedBoneIndex =
          geometryPass != nullptr && settings.showBoneWeights &&
                  static_cast<int>(index) == settings.selectedObjectIndex
              ? settings.selectedBoneIndex
              : -1;

      appendItems(renderItems, sceneAssetModels[index].buildRenderItems(
                                   geometryPass, objectMatrices,
                                   selectedBoneIndex));

      if (directionalShadowPass != nullptr) {
        appendItems(renderItems, sceneAssetModels[index].buildRenderItems(
                                     directionalShadowPass, objectMatrices));
      }

      for (ShadowPass *spotShadowPass : spotShadowPasses) {
        if (spotShadowPass == nullptr) {
          continue;
        }
        appendItems(renderItems, sceneAssetModels[index].buildRenderItems(
                                     spotShadowPass, objectMatrices));
      }
    }

    appendFullscreenItem(renderItems, fullscreenQuad, pbrPass);
    appendFullscreenItem(renderItems, fullscreenQuad, tonemapPass);
    appendFullscreenItem(renderItems, fullscreenQuad, debugPresentPass);
  }

private:
  static void appendItems(std::vector<RenderItem> &renderItems,
                          const std::vector<RenderItem> &sourceItems) {
    renderItems.insert(renderItems.end(), sourceItems.begin(),
                       sourceItems.end());
  }

  static void appendFullscreenItem(std::vector<RenderItem> &renderItems,
                                   Mesh &fullscreenQuad,
                                   const RenderPass *targetPass) {
    if (targetPass == nullptr) {
      return;
    }

    renderItems.push_back(RenderItem{.mesh = &fullscreenQuad,
                                     .descriptorBindings = nullptr,
                                     .targetPass = targetPass});
  }
};
