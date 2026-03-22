#include "engine/runtime/DefaultEngineApp.h"
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

void printLoadedAssetInfo(const SceneAssetInstance &sceneAsset,
                          const ModelAsset &asset) {
  std::cout << "Loaded asset: "
            << (sceneAsset.name.empty() ? sceneAsset.assetPath
                                        : sceneAsset.name)
            << "\n";
  std::cout << "  Path: " << asset.path() << "\n";
  std::cout << "  Vertices: " << asset.mesh().vertexCount() << "\n";
  std::cout << "  Indices: " << asset.mesh().getIndices().size() << "\n";
  std::cout << "  Submeshes: " << asset.submeshes().size() << "\n";
  std::cout << "  Materials: " << asset.materials().size() << "\n";

  if (const auto *skeleton = asset.skeletonAsset(); skeleton != nullptr) {
    std::cout << "  Skeleton Nodes: " << skeleton->nodes.size() << "\n";
    std::cout << "  Skins: " << skeleton->skins.size() << "\n";
    std::cout << "  Animations: " << skeleton->animations.size() << "\n";

    for (size_t skinIndex = 0; skinIndex < skeleton->skins.size();
         ++skinIndex) {
      const auto &skin = skeleton->skins[skinIndex];
      std::cout << "    Skin[" << skinIndex << "] " << skin.name
                << " joints=" << skin.jointNodeIndices.size()
                << " inverseBindMatrices=" << skin.inverseBindMatrices.size()
                << "\n";
    }

    for (size_t animationIndex = 0;
         animationIndex < skeleton->animations.size(); ++animationIndex) {
      const auto &animation = skeleton->animations[animationIndex];
      std::cout << "    Animation[" << animationIndex << "] " << animation.name
                << " duration=" << animation.durationSeconds
                << " tracks=" << animation.tracks.size() << "\n";
    }
  } else {
    std::cout << "  Skeleton Nodes: 0\n";
    std::cout << "  Skins: 0\n";
    std::cout << "  Animations: 0\n";
  }
}

} // namespace

int main() {
  try {
    DefaultEngineConfig engineConfig{};
    engineConfig.windowTitle = "Robot Scene";
    engineConfig.debugSessionPath = "assets/debug/skeletal_animation.json";
    engineConfig.defaultEnvironmentHdrPath = "assets/textures/nature_sky.hdr";
    engineConfig.skyboxVisible = false;
    engineConfig.onSceneAssetLoaded = printLoadedAssetInfo;

    SceneDefinition scene = SceneDefinition::empty();

    scene.assets.push_back(
        {.name = "Robot", .assetPath = "assets/models/robot.glb"});

    DefaultEngineApp::create(engineConfig, scene).run();
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
