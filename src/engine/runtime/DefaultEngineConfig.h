#pragma once

#include "engine/editor/DebugUIState.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

inline constexpr uint32_t DEFAULT_ENGINE_WIDTH = 1280;
inline constexpr uint32_t DEFAULT_ENGINE_HEIGHT = 720;
inline constexpr uint32_t DEFAULT_ENGINE_MAX_FRAMES_IN_FLIGHT = 2;
inline constexpr float DEFAULT_ENGINE_CAMERA_NEAR_PLANE = 0.1f;
inline constexpr uint32_t DEFAULT_ENGINE_SHADOW_MAP_RESOLUTION = 1024;
inline constexpr uint32_t DEFAULT_ENGINE_MAX_SPOT_SHADOW_PASSES = 3;

class ModelAsset;
struct SceneAssetInstance;

struct DefaultEngineConfig {
  uint32_t width = DEFAULT_ENGINE_WIDTH;
  uint32_t height = DEFAULT_ENGINE_HEIGHT;
  std::string windowTitle = "Default Example";
  bool enableDebugSessionPersistence = true;
  bool restoreSessionOnStartup = true;
  std::string contentPath = "assets";
  std::string rendererAssetPath = "resources";
  std::filesystem::path debugSessionPath{};
  std::string defaultEnvironmentHdrPath;
  std::optional<bool> skyboxVisible;
  std::function<void(const SceneAssetInstance &, const ModelAsset &)>
      onSceneAssetLoaded;
  std::function<void(DefaultDebugUISettings &)> configureSettings;
};

inline std::filesystem::path
resolvedDebugSessionPath(const DefaultEngineConfig &config) {
  if (!config.debugSessionPath.empty()) {
    return config.debugSessionPath;
  }
  return std::filesystem::path(config.contentPath) / "debug" /
         "last_session.json";
}

inline std::string
resolvedDefaultEnvironmentHdrPath(const DefaultEngineConfig &config) {
  if (!config.defaultEnvironmentHdrPath.empty()) {
    return config.defaultEnvironmentHdrPath;
  }
  return config.contentPath + "/textures/dikhololo_night_4k.hdr";
}
