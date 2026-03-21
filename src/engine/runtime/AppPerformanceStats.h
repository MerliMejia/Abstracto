#pragma once

#include "engine/assets/RenderableModel.h"
#include "engine/editor/DebugUIState.h"

class AppPerformanceStats {
public:
  static DefaultDebugUIPerformanceStats
  build(float fps, float frameTimeMs, const DefaultDebugUISettings &settings,
        uint32_t materialCount, uint32_t vertexCount, uint32_t triangleCount,
        const std::vector<RenderItem> &renderItems,
        const RenderPass *geometryPass, const RenderPass *pbrPass,
        const RenderPass *tonemapPass, const RenderPass *debugPresentPass,
        uint32_t activeShadowPassCount) {
    DefaultDebugUIPerformanceStats stats{
        .fps = fps,
        .frameTimeMs = frameTimeMs,
        .objectCount = static_cast<uint32_t>(settings.sceneObjects.size()),
        .lightCount = static_cast<uint32_t>(settings.sceneLights.size()),
        .materialCount = materialCount,
        .vertexCount = vertexCount,
        .triangleCount = triangleCount,
    };

    uint32_t postProcessDrawCallCount = 0;
    for (const auto &renderItem : renderItems) {
      if (renderItem.targetPass == geometryPass) {
        ++stats.sceneDrawCallCount;
        continue;
      }
      if (renderItem.targetPass == pbrPass ||
          renderItem.targetPass == tonemapPass ||
          renderItem.targetPass == debugPresentPass) {
        ++postProcessDrawCallCount;
      }
    }

    stats.preparedDrawCallCount = static_cast<uint32_t>(renderItems.size());
    stats.shadowDrawCallCount =
        stats.sceneDrawCallCount * activeShadowPassCount;
    stats.drawCallCount = stats.sceneDrawCallCount + stats.shadowDrawCallCount +
                          postProcessDrawCallCount;
    return stats;
  }
};
