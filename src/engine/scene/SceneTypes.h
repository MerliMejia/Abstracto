#pragma once

#include <glm/glm.hpp>
#include <string>

struct SceneTransform {
  glm::vec3 position = {0.0f, 0.0f, 0.0f};
  glm::vec3 rotationDegrees = {0.0f, 0.0f, 0.0f};
  glm::vec3 scale = {1.0f, 1.0f, 1.0f};
};

struct SceneObject {
  std::string name = "Scene Model";
  SceneTransform transform{};
  bool visible = true;
};
