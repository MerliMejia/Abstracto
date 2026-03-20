#include "app/DefaultEngineApp.h"
#include <cstdlib>
#include <exception>
#include <iostream>

int main() {
  try {
    DefaultEngineConfig engineConfig{};
    engineConfig.windowTitle = "Tree Scene";
    engineConfig.debugSessionPath = "assets/debug/tree_scene.json";
    engineConfig.defaultEnvironmentHdrPath = "assets/textures/nature_sky.hdr";

    SceneDefinition scene = SceneDefinition::empty();

    scene.assets.push_back(
        {.name = "Tree", .assetPath = "assets/models/tree.glb"});

    DefaultEngineApp::create(engineConfig, scene).run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
