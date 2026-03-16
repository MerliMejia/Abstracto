#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

enum class SceneLightType : uint32_t {
  Directional = 0,
  Point = 1,
  Spot = 2,
};

struct SceneLight {
  std::string name = "Light";
  SceneLightType type = SceneLightType::Directional;
  bool enabled = true;
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float power = 1.0f;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  float range = 8.0f;
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  float exposure = 0.0f;
  float radius = 0.25f;
  float innerConeAngleRadians = glm::radians(18.0f);
  float outerConeAngleRadians = glm::radians(28.0f);
  bool castsShadow = false;
  float shadowBias = 0.0015f;
  float shadowNormalBias = 0.02f;

  static SceneLight directional(const std::string &name = "Directional Light",
                                glm::vec3 direction = glm::vec3(0.0f, -1.0f,
                                                                -1.0f),
                                glm::vec3 color = glm::vec3(1.0f),
                                float power = 3.0f, float exposure = 0.0f) {
    SceneLight light;
    light.name = name;
    light.type = SceneLightType::Directional;
    light.direction =
        normalizedOrFallback(direction, glm::vec3(0.0f, -1.0f, -1.0f));
    light.color = color;
    light.power = std::max(power, 0.0f);
    light.exposure = exposure;
    light.castsShadow = true;
    return light;
  }

  static SceneLight point(const std::string &name = "Point Light",
                          glm::vec3 position = glm::vec3(0.0f),
                          glm::vec3 color = glm::vec3(1.0f),
                          float power = 18.0f, float exposure = 0.0f,
                          float radius = 0.64f, float range = 25.0f) {
    SceneLight light;
    light.name = name;
    light.type = SceneLightType::Point;
    light.position = position;
    light.color = color;
    light.power = std::max(power, 0.0f);
    light.exposure = exposure;
    light.radius = std::max(radius, 0.0f);
    light.range = std::max(range, 0.01f);
    return light;
  }

  static SceneLight spot(const std::string &name = "Spot Light",
                         glm::vec3 position = glm::vec3(0.0f),
                         glm::vec3 direction = glm::vec3(0.0f, -1.0f, -1.0f),
                         glm::vec3 color = glm::vec3(1.0f), float power = 18.0f,
                         float exposure = 0.0f, float range = 10.0f,
                         float innerConeAngleRadians = glm::radians(18.0f),
                         float outerConeAngleRadians = glm::radians(28.0f)) {
    SceneLight light;
    light.name = name;
    light.type = SceneLightType::Spot;
    light.position = position;
    light.direction =
        normalizedOrFallback(direction, glm::vec3(0.0f, -1.0f, -1.0f));
    light.color = color;
    light.power = std::max(power, 0.0f);
    light.exposure = exposure;
    light.range = std::max(range, 0.01f);
    light.innerConeAngleRadians =
        std::clamp(innerConeAngleRadians, 0.0f, glm::radians(89.0f));
    light.outerConeAngleRadians =
        std::max(light.innerConeAngleRadians,
                 std::clamp(outerConeAngleRadians, 0.0f, glm::radians(89.0f)));
    light.castsShadow = true;
    return light;
  }

  void normalizeDirection() {
    direction = normalizedOrFallback(direction, glm::vec3(0.0f, -1.0f, -1.0f));
  }

  float radianceScale() const {
    return std::max(power, 0.0f) * std::exp2(exposure);
  }

private:
  static glm::vec3 normalizedOrFallback(const glm::vec3 &value,
                                        const glm::vec3 &fallback) {
    const float lengthSquared = glm::dot(value, value);
    if (lengthSquared <= 1e-6f) {
      return glm::normalize(fallback);
    }
    return glm::normalize(value);
  }
};
