#pragma once

#include "engine/scene/SceneLightSet.h"
#include "renderer/passes/DebugOverlayPass.h"
#include "renderer/passes/PbrPass.h"
#include <algorithm>
#include <vector>

class RendererSceneAdapters {
public:
  static std::vector<PbrLightInput>
  buildPbrLightInputs(const SceneLightSet &sceneLights) {
    std::vector<PbrLightInput> inputs;
    inputs.reserve(sceneLights.size());

    for (size_t sceneLightIndex = 0; sceneLightIndex < sceneLights.lights().size();
         ++sceneLightIndex) {
      const auto &light = sceneLights.lights()[sceneLightIndex];
      inputs.push_back(PbrLightInput{
          .sourceIndex = static_cast<uint32_t>(sceneLightIndex),
          .type = toPbrLightType(light.type),
          .enabled = light.enabled,
          .position = light.position,
          .range = light.range,
          .direction = light.direction,
          .radiance = light.radianceScale(),
          .color = light.color,
          .radius = light.radius,
          .innerConeAngleRadians = light.innerConeAngleRadians,
          .outerConeAngleRadians = light.outerConeAngleRadians,
      });
    }

    return inputs;
  }

  static std::vector<DebugOverlayMarker>
  buildDebugLightMarkers(const SceneLightSet &sceneLights,
                         const glm::vec3 &directionalAnchor,
                         float markerScale) {
    std::vector<DebugOverlayMarker> markers;
    markers.reserve(sceneLights.size());

    for (const auto &light : sceneLights.lights()) {
      if (!light.enabled) {
        continue;
      }

      switch (light.type) {
      case SceneLightType::Directional: {
        const glm::vec3 direction = safeDirection(
            light.direction, glm::vec3(-0.55f, -0.25f, -1.0f));
        markers.push_back(DebugOverlayMarker{
            .type = DebugOverlayMarkerType::Directional,
            .position = directionalAnchor - direction * markerScale * 1.5f,
            .direction = direction,
            .color = glm::vec4(light.color, 1.0f),
        });
        break;
      }
      case SceneLightType::Point:
        markers.push_back(DebugOverlayMarker{
            .type = DebugOverlayMarkerType::Point,
            .position = light.position,
            .direction = glm::vec3(0.0f, 0.0f, 1.0f),
            .color = glm::vec4(light.color, 1.0f),
        });
        break;
      case SceneLightType::Spot:
        markers.push_back(DebugOverlayMarker{
            .type = DebugOverlayMarkerType::Spot,
            .position = light.position,
            .direction = safeDirection(light.direction,
                                       glm::vec3(0.0f, 0.0f, 1.0f)),
            .color = glm::vec4(light.color, 1.0f),
        });
        break;
      }
    }

    return markers;
  }

private:
  static PbrLightType toPbrLightType(SceneLightType type) {
    switch (type) {
    case SceneLightType::Directional:
      return PbrLightType::Directional;
    case SceneLightType::Point:
      return PbrLightType::Point;
    case SceneLightType::Spot:
      return PbrLightType::Spot;
    }
    return PbrLightType::Directional;
  }

  static glm::vec3 safeDirection(const glm::vec3 &direction,
                                 const glm::vec3 &fallback) {
    const float lengthSquared = glm::dot(direction, direction);
    return glm::normalize(lengthSquared > 1e-6f ? direction : fallback);
  }
};
