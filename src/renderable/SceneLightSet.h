#pragma once

#include "LightTypes.h"
#include <stdexcept>
#include <vector>

class SceneLightSet {
public:
  std::vector<SceneLight> &lights() { return sceneLights; }
  const std::vector<SceneLight> &lights() const { return sceneLights; }

  SceneLight &addDirectional(const std::string &name = "Directional Light",
                             glm::vec3 direction = glm::vec3(-0.4f, -0.3f,
                                                             -1.0f),
                             glm::vec3 color = glm::vec3(1.0f, 0.95f, 0.9f),
                             float power = 3.0f, float exposure = 0.0f) {
    sceneLights.push_back(
        SceneLight::directional(name, direction, color, power, exposure));
    return sceneLights.back();
  }

  SceneLight &addPoint(const std::string &name = "Point Light",
                       glm::vec3 position = glm::vec3(1.5f, -1.5f, 1.5f),
                       glm::vec3 color = glm::vec3(0.45f, 0.7f, 1.0f),
                       float power = 18.0f, float exposure = 0.0f,
                       float radius = 0.64f, float range = 25.0f) {
    sceneLights.push_back(SceneLight::point(name, position, color, power,
                                            exposure, radius, range));
    return sceneLights.back();
  }

  SceneLight &addSpot(const std::string &name = "Spot Light",
                      glm::vec3 position = glm::vec3(-2.2f, 0.8f, 2.5f),
                      glm::vec3 direction = glm::vec3(0.65f, -0.2f, -0.75f),
                      glm::vec3 color = glm::vec3(1.0f, 0.78f, 0.58f),
                      float power = 28.0f, float exposure = 0.0f,
                      float range = 10.0f,
                      float innerConeAngleRadians = glm::radians(16.0f),
                      float outerConeAngleRadians = glm::radians(27.0f)) {
    sceneLights.push_back(
        SceneLight::spot(name, position, direction, color, power, exposure,
                         range,
                         innerConeAngleRadians, outerConeAngleRadians));
    return sceneLights.back();
  }

  void remove(size_t index) {
    if (index >= sceneLights.size()) {
      throw std::runtime_error("scene light index out of range");
    }
    sceneLights.erase(sceneLights.begin() + static_cast<long>(index));
  }

  void clear() { sceneLights.clear(); }

  bool empty() const { return sceneLights.empty(); }
  size_t size() const { return sceneLights.size(); }

  int firstDirectionalLightIndex() const {
    for (size_t index = 0; index < sceneLights.size(); ++index) {
      if (sceneLights[index].type == SceneLightType::Directional) {
        return static_cast<int>(index);
      }
    }
    return -1;
  }

  static SceneLightSet showcaseLights() {
    SceneLightSet lights;
    lights.addDirectional("Key Sun", glm::vec3(-0.55f, -0.25f, -1.0f),
                          glm::vec3(1.0f, 0.94f, 0.88f), 3.5f, 0.0f);
    lights.addPoint("Cool Fill", glm::vec3(1.9f, -2.0f, 1.2f),
                    glm::vec3(0.45f, 0.7f, 1.0f), 24.0f, 0.0f, 0.64f, 25.0f);
    lights.addSpot("Warm Rim", glm::vec3(-2.4f, 1.1f, 2.7f),
                   glm::vec3(0.75f, -0.15f, -0.65f),
                   glm::vec3(1.0f, 0.72f, 0.5f), 34.0f, 0.0f, 11.0f,
                   glm::radians(14.0f), glm::radians(24.0f));
    return lights;
  }

private:
  std::vector<SceneLight> sceneLights;
};
