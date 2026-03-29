#pragma once

#include "renderer/passes/PbrPass.h"
#include "renderer/passes/ShadowPass.h"
#include "renderer/resources/FrameGeometryUniforms.h"
#include "engine/editor/DebugUIState.h"
#include "engine/scene/AppSceneController.h"
#include <algorithm>
#include <array>
#include <glm/gtc/matrix_transform.hpp>

class ShadowSystem {
public:
  template <size_t SpotShadowPassCount>
  static uint32_t activeShadowPassCount(
      const DefaultDebugUISettings &settings, const PbrPass *pbrPass,
      const ShadowPass *directionalShadowPass,
      const std::array<ShadowPass *, SpotShadowPassCount> &spotShadowPasses) {
    if (!settings.shadowsEnabled || pbrPass == nullptr) {
      return 0;
    }

    bool directionalAssigned = false;
    uint32_t spotShadowSlot = 0;
    const auto &lights = settings.sceneLights.lights();
    for (size_t sceneLightIndex = 0; sceneLightIndex < lights.size();
         ++sceneLightIndex) {
      const auto &light = lights[sceneLightIndex];
      if (!light.enabled || !light.castsShadow) {
        continue;
      }

      const auto uniformLightIndex =
          pbrPass->uniformLightIndexForSource(sceneLightIndex);
      if (!uniformLightIndex.has_value()) {
        continue;
      }

      if (light.type == SceneLightType::Directional && !directionalAssigned &&
          directionalShadowPass != nullptr) {
        directionalAssigned = true;
        continue;
      }

      if (light.type == SceneLightType::Spot &&
          spotShadowSlot < SpotShadowPassCount &&
          spotShadowPasses[spotShadowSlot] != nullptr) {
        ++spotShadowSlot;
      }
    }

    return (directionalAssigned ? 1u : 0u) + spotShadowSlot;
  }

  template <size_t SpotShadowPassCount>
  static void configure(
      const DefaultDebugUISettings &settings, const glm::vec3 &sceneAnchor,
      PbrPass *pbrPass, ShadowPass *directionalShadowPass,
      const std::array<ShadowPass *, SpotShadowPassCount> &spotShadowPasses) {
    if (pbrPass == nullptr) {
      return;
    }

    pbrPass->clearLightShadows();

    if (directionalShadowPass != nullptr) {
      directionalShadowPass->setEnabled(false);
      directionalShadowPass->setLightViewProj(glm::mat4(1.0f));
    }
    for (ShadowPass *spotShadowPass : spotShadowPasses) {
      if (spotShadowPass == nullptr) {
        continue;
      }
      spotShadowPass->setEnabled(false);
      spotShadowPass->setLightViewProj(glm::mat4(1.0f));
    }

    if (!settings.shadowsEnabled) {
      return;
    }

    bool directionalAssigned = false;
    uint32_t spotShadowSlot = 0;
    const auto &lights = settings.sceneLights.lights();
    for (size_t sceneLightIndex = 0; sceneLightIndex < lights.size();
         ++sceneLightIndex) {
      const auto &light = lights[sceneLightIndex];
      if (!light.enabled || !light.castsShadow) {
        continue;
      }

      const auto uniformLightIndex =
          pbrPass->uniformLightIndexForSource(sceneLightIndex);
      if (!uniformLightIndex.has_value()) {
        continue;
      }

      if (light.type == SceneLightType::Directional && !directionalAssigned &&
          directionalShadowPass != nullptr) {
        const glm::mat4 shadowMatrix =
            buildDirectionalShadowMatrix(settings, sceneAnchor, light);
        directionalShadowPass->setEnabled(true);
        directionalShadowPass->setLightViewProj(shadowMatrix);
        pbrPass->setLightShadow(
            *uniformLightIndex, 0, shadowMatrix, light.shadowBias,
            light.shadowNormalBias,
            static_cast<float>(directionalShadowPass->resolution()));
        directionalAssigned = true;
        continue;
      }

      if (light.type == SceneLightType::Spot &&
          spotShadowSlot < SpotShadowPassCount &&
          spotShadowPasses[spotShadowSlot] != nullptr) {
        const glm::mat4 shadowMatrix = buildSpotShadowMatrix(light);
        spotShadowPasses[spotShadowSlot]->setEnabled(true);
        spotShadowPasses[spotShadowSlot]->setLightViewProj(shadowMatrix);
        pbrPass->setLightShadow(
            *uniformLightIndex, spotShadowSlot + 1, shadowMatrix,
            light.shadowBias, light.shadowNormalBias,
            static_cast<float>(spotShadowPasses[spotShadowSlot]->resolution()));
        ++spotShadowSlot;
      }
    }
  }

private:
  static glm::vec3 shadowUpVector(const glm::vec3 &direction) {
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(direction, worldUp)) > 0.98f) {
      return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return worldUp;
  }

  static glm::mat4 buildDirectionalShadowMatrix(
      const DefaultDebugUISettings &settings, const glm::vec3 &sceneAnchor,
      const SceneLight &light) {
    const glm::vec3 direction = glm::normalize(light.direction);
    const float shadowExtent =
        std::max(settings.directionalShadowExtent, 0.5f);
    const float nearPlane =
        std::max(settings.directionalShadowNearPlane, 0.01f);
    const float farPlane =
        std::max(settings.directionalShadowFarPlane, nearPlane + 0.5f);
    const glm::vec3 eye = sceneAnchor - direction * (farPlane * 0.5f);
    const glm::mat4 view = glm::lookAt(eye, sceneAnchor, shadowUpVector(direction));
    glm::mat4 proj = glm::ortho(-shadowExtent, shadowExtent, -shadowExtent,
                                shadowExtent, nearPlane, farPlane);
    proj[1][1] *= -1.0f;
    return proj * view;
  }

  static glm::mat4 buildSpotShadowMatrix(const SceneLight &light) {
    const glm::vec3 direction = glm::normalize(light.direction);
    const float nearPlane = 0.05f;
    const float farPlane = std::max(light.range, nearPlane + 0.05f);
    const float fovRadians =
        std::clamp(light.outerConeAngleRadians * 2.0f, glm::radians(1.0f),
                   glm::radians(179.0f));
    const glm::mat4 view = glm::lookAt(
        light.position, light.position + direction, shadowUpVector(direction));
    glm::mat4 proj = glm::perspective(fovRadians, 1.0f, nearPlane, farPlane);
    proj[1][1] *= -1.0f;
    return proj * view;
  }
};
