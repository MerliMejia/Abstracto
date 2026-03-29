#pragma once

#include "engine/scene/SceneLightSet.h"
#include "engine/scene/SceneTypes.h"
#include "engine/editor/DebugUIState.h"
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

struct SceneAssetInstance {
  std::string assetPath;
  std::string name;
  SceneTransform transform{};
  bool visible = true;
};

struct SceneDefinition {
  std::vector<SceneAssetInstance> assets;
  std::string modelPath = "assets/models/night.glb";
  SceneLightSet sceneLights = SceneLightSet::showcaseLights();
  std::vector<SceneObjectOverride> objectOverrides;
  std::function<void(DefaultDebugUISettings &)> configureSettings;

  static SceneDefinition fromModel(const std::string &modelPathValue) {
    SceneDefinition definition;
    definition.assets.push_back(SceneAssetInstance{
        .assetPath = modelPathValue,
    });
    definition.modelPath = modelPathValue;
    return definition;
  }

  static SceneDefinition empty() {
    SceneDefinition definition;
    definition.assets.clear();
    definition.modelPath.clear();
    definition.sceneLights.clear();
    return definition;
  }
};
