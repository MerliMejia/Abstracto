#pragma once

#include "../renderable/SceneLightSet.h"
#include "../utils/DebugUIState.h"
#include <functional>
#include <string>
#include <vector>

struct SceneObjectOverride {
  std::string name;
  SceneTransform transform{};
  bool overrideTransform = false;
  bool visible = true;
  bool overrideVisibility = false;
};

struct SceneDefinition {
  std::string modelPath = "assets/models/night.glb";
  SceneLightSet sceneLights = SceneLightSet::showcaseLights();
  std::vector<SceneObjectOverride> objectOverrides;
  std::function<void(DefaultDebugUISettings &)> configureSettings;

  static SceneDefinition fromModel(const std::string &modelPathValue) {
    SceneDefinition definition;
    definition.modelPath = modelPathValue;
    return definition;
  }
};
