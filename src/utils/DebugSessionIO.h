#pragma once

#include "DefaultDebugUI.h"
#include <filesystem>
#include <fstream>
#include <json.hpp>

namespace DebugSessionIO {

using json = nlohmann::json;

inline json vec3ToJson(const glm::vec3 &value) {
  return json::array({value.x, value.y, value.z});
}

inline glm::vec3 vec3FromJson(const json &value,
                              const glm::vec3 &fallback = glm::vec3(0.0f)) {
  if (!value.is_array() || value.size() != 3) {
    return fallback;
  }
  return glm::vec3(value[0].get<float>(), value[1].get<float>(),
                   value[2].get<float>());
}

inline json sceneLightToJson(const SceneLight &light) {
  return {
      {"name", light.name},
      {"type", static_cast<uint32_t>(light.type)},
      {"enabled", light.enabled},
      {"color", vec3ToJson(light.color)},
      {"power", light.power},
      {"exposure", light.exposure},
      {"position", vec3ToJson(light.position)},
      {"radius", light.radius},
      {"range", light.range},
      {"direction", vec3ToJson(light.direction)},
      {"innerConeAngleRadians", light.innerConeAngleRadians},
      {"outerConeAngleRadians", light.outerConeAngleRadians},
      {"castsShadow", light.castsShadow},
      {"shadowBias", light.shadowBias},
      {"shadowNormalBias", light.shadowNormalBias},
  };
}

inline SceneLight sceneLightFromJson(const json &value) {
  SceneLight light;
  light.name = value.value("name", light.name);
  light.type = static_cast<SceneLightType>(
      value.value("type", static_cast<uint32_t>(light.type)));
  light.enabled = value.value("enabled", light.enabled);
  light.color = vec3FromJson(value.value("color", json::array()), light.color);
  light.power = std::max(
      value.value("power", value.value("intensity", light.power)), 0.0f);
  light.exposure = value.value("exposure", light.exposure);
  light.position =
      vec3FromJson(value.value("position", json::array()), light.position);
  light.radius = std::max(value.value("radius", light.radius), 0.0f);
  light.range = std::max(value.value("range", light.range), 0.01f);
  light.direction =
      vec3FromJson(value.value("direction", json::array()), light.direction);
  light.innerConeAngleRadians =
      value.value("innerConeAngleRadians", light.innerConeAngleRadians);
  light.outerConeAngleRadians = std::max(
      value.value("outerConeAngleRadians", light.outerConeAngleRadians),
      light.innerConeAngleRadians);
  const bool defaultCastsShadow =
      light.type == SceneLightType::Directional ||
      light.type == SceneLightType::Spot;
  light.castsShadow = value.value("castsShadow", defaultCastsShadow);
  light.shadowBias =
      std::max(value.value("shadowBias", light.shadowBias), 0.0f);
  light.shadowNormalBias =
      std::max(value.value("shadowNormalBias", light.shadowNormalBias), 0.0f);
  light.normalizeDirection();
  return light;
}

inline json proceduralSkyToJson(const ProceduralSkySettings &sky) {
  return {
      {"zenithColor", vec3ToJson(sky.zenithColor)},
      {"horizonColor", vec3ToJson(sky.horizonColor)},
      {"groundColor", vec3ToJson(sky.groundColor)},
      {"sunColor", vec3ToJson(sky.sunColor)},
      {"sunIntensity", sky.sunIntensity},
      {"sunAngularRadius", sky.sunAngularRadius},
      {"sunGlow", sky.sunGlow},
      {"horizonGlow", sky.horizonGlow},
      {"skyExponent", sky.skyExponent},
      {"groundFalloff", sky.groundFalloff},
      {"sunAzimuthRadians", sky.sunAzimuthRadians},
      {"sunElevationRadians", sky.sunElevationRadians},
  };
}

inline ProceduralSkySettings proceduralSkyFromJson(const json &value) {
  ProceduralSkySettings sky;
  sky.zenithColor =
      vec3FromJson(value.value("zenithColor", json::array()), sky.zenithColor);
  sky.horizonColor = vec3FromJson(value.value("horizonColor", json::array()),
                                  sky.horizonColor);
  sky.groundColor =
      vec3FromJson(value.value("groundColor", json::array()), sky.groundColor);
  sky.sunColor =
      vec3FromJson(value.value("sunColor", json::array()), sky.sunColor);
  sky.sunIntensity = value.value("sunIntensity", sky.sunIntensity);
  sky.sunAngularRadius = value.value("sunAngularRadius", sky.sunAngularRadius);
  sky.sunGlow = value.value("sunGlow", sky.sunGlow);
  sky.horizonGlow = value.value("horizonGlow", sky.horizonGlow);
  sky.skyExponent = value.value("skyExponent", sky.skyExponent);
  sky.groundFalloff = value.value("groundFalloff", sky.groundFalloff);
  sky.sunAzimuthRadians =
      value.value("sunAzimuthRadians", sky.sunAzimuthRadians);
  sky.sunElevationRadians =
      value.value("sunElevationRadians", sky.sunElevationRadians);
  return sky;
}

inline json
iblBakeSettingsToJson(const ImageBasedLightingBakeSettings &settings) {
  return {
      {"environmentResolution", settings.environmentResolution},
      {"irradianceResolution", settings.irradianceResolution},
      {"prefilteredResolution", settings.prefilteredResolution},
      {"brdfResolution", settings.brdfResolution},
      {"irradianceSamples", settings.irradianceSamples},
      {"prefilteredSamples", settings.prefilteredSamples},
      {"brdfSamples", settings.brdfSamples},
      {"environmentHdrPath", settings.environmentHdrPath},
      {"sky", proceduralSkyToJson(settings.sky)},
  };
}

inline ImageBasedLightingBakeSettings
iblBakeSettingsFromJson(const json &value) {
  ImageBasedLightingBakeSettings settings;
  settings.environmentResolution =
      value.value("environmentResolution", settings.environmentResolution);
  settings.irradianceResolution =
      value.value("irradianceResolution", settings.irradianceResolution);
  settings.prefilteredResolution =
      value.value("prefilteredResolution", settings.prefilteredResolution);
  settings.brdfResolution =
      value.value("brdfResolution", settings.brdfResolution);
  settings.irradianceSamples =
      value.value("irradianceSamples", settings.irradianceSamples);
  settings.prefilteredSamples =
      value.value("prefilteredSamples", settings.prefilteredSamples);
  settings.brdfSamples = value.value("brdfSamples", settings.brdfSamples);
  settings.environmentHdrPath =
      value.value("environmentHdrPath", settings.environmentHdrPath);
  if (value.contains("sky") && value["sky"].is_object()) {
    settings.sky = proceduralSkyFromJson(value["sky"]);
  }
  return settings;
}

inline json settingsToJson(const DefaultDebugUISettings &settings) {
  json sceneLights = json::array();
  for (const auto &light : settings.sceneLights.lights()) {
    sceneLights.push_back(sceneLightToJson(light));
  }

  return {
      {"presentedOutput", static_cast<uint32_t>(settings.presentedOutput)},
      {"pbrDebugView", static_cast<uint32_t>(settings.pbrDebugView)},
      {"selectedMaterialIndex", settings.selectedMaterialIndex},
      {"selectedLightIndex", settings.selectedLightIndex},
      {"sceneLights", sceneLights},
      {"lightMarkersVisible", settings.lightMarkersVisible},
      {"lightMarkerScale", settings.lightMarkerScale},
      {"shadowsEnabled", settings.shadowsEnabled},
      {"directionalShadowExtent", settings.directionalShadowExtent},
      {"directionalShadowNearPlane", settings.directionalShadowNearPlane},
      {"directionalShadowFarPlane", settings.directionalShadowFarPlane},
      {"exposure", settings.exposure},
      {"autoExposureKey", settings.autoExposureKey},
      {"whitePoint", settings.whitePoint},
      {"gamma", settings.gamma},
      {"autoExposureEnabled", settings.autoExposureEnabled},
      {"tonemapOperator", static_cast<uint32_t>(settings.tonemapOperator)},
      {"environmentIntensity", settings.environmentIntensity},
      {"environmentBackgroundWeight", settings.environmentBackgroundWeight},
      {"environmentDiffuseWeight", settings.environmentDiffuseWeight},
      {"environmentSpecularWeight", settings.environmentSpecularWeight},
      {"dielectricSpecularScale", settings.dielectricSpecularScale},
      {"environmentRotationRadians", settings.environmentRotationRadians},
      {"iblEnabled", settings.iblEnabled},
      {"skyboxVisible", settings.skyboxVisible},
      {"syncSkySunToLight", settings.syncSkySunToLight},
      {"iblBakeSettings", iblBakeSettingsToJson(settings.iblBakeSettings)},
      {"modelPosition", vec3ToJson(settings.modelPosition)},
      {"modelRotationDegrees", vec3ToJson(settings.modelRotationDegrees)},
      {"modelScale", vec3ToJson(settings.modelScale)},
      {"cameraPosition", vec3ToJson(settings.cameraPosition)},
      {"cameraYawRadians", settings.cameraYawRadians},
      {"cameraPitchRadians", settings.cameraPitchRadians},
      {"cameraMoveSpeed", settings.cameraMoveSpeed},
      {"cameraLookSensitivity", settings.cameraLookSensitivity},
      {"cameraFarPlane", settings.cameraFarPlane},
  };
}

inline DefaultDebugUISettings settingsFromJson(const json &value) {
  DefaultDebugUISettings settings;
  settings.presentedOutput = static_cast<PresentedOutput>(value.value(
      "presentedOutput", static_cast<uint32_t>(settings.presentedOutput)));
  settings.pbrDebugView = static_cast<PbrDebugView>(value.value(
      "pbrDebugView", static_cast<uint32_t>(settings.pbrDebugView)));
  settings.selectedMaterialIndex =
      value.value("selectedMaterialIndex", settings.selectedMaterialIndex);
  settings.selectedLightIndex =
      value.value("selectedLightIndex", settings.selectedLightIndex);

  settings.sceneLights.clear();
  if (value.contains("sceneLights") && value["sceneLights"].is_array()) {
    for (const auto &lightValue : value["sceneLights"]) {
      settings.sceneLights.lights().push_back(sceneLightFromJson(lightValue));
    }
  }
  if (settings.sceneLights.empty()) {
    settings.sceneLights = SceneLightSet::showcaseLights();
  }

  settings.lightMarkersVisible =
      value.value("lightMarkersVisible", settings.lightMarkersVisible);
  settings.lightMarkerScale = std::max(
      value.value("lightMarkerScale", settings.lightMarkerScale), 0.01f);
  settings.shadowsEnabled =
      value.value("shadowsEnabled", settings.shadowsEnabled);
  settings.directionalShadowExtent = std::max(
      value.value("directionalShadowExtent", settings.directionalShadowExtent),
      0.5f);
  settings.directionalShadowNearPlane =
      std::max(value.value("directionalShadowNearPlane",
                           settings.directionalShadowNearPlane),
               0.01f);
  settings.directionalShadowFarPlane =
      std::max(value.value("directionalShadowFarPlane",
                           settings.directionalShadowFarPlane),
               settings.directionalShadowNearPlane + 0.5f);
  settings.exposure = value.value("exposure", settings.exposure);
  settings.autoExposureKey =
      value.value("autoExposureKey", settings.autoExposureKey);
  settings.whitePoint = value.value("whitePoint", settings.whitePoint);
  settings.gamma = value.value("gamma", settings.gamma);
  settings.autoExposureEnabled =
      value.value("autoExposureEnabled", settings.autoExposureEnabled);
  settings.tonemapOperator = static_cast<TonemapOperator>(value.value(
      "tonemapOperator", static_cast<uint32_t>(settings.tonemapOperator)));
  settings.environmentIntensity =
      value.value("environmentIntensity", settings.environmentIntensity);
  settings.environmentBackgroundWeight = value.value(
      "environmentBackgroundWeight", settings.environmentBackgroundWeight);
  settings.environmentDiffuseWeight = value.value(
      "environmentDiffuseWeight", settings.environmentDiffuseWeight);
  settings.environmentSpecularWeight = value.value(
      "environmentSpecularWeight", settings.environmentSpecularWeight);
  settings.dielectricSpecularScale =
      value.value("dielectricSpecularScale", settings.dielectricSpecularScale);
  settings.environmentRotationRadians = value.value(
      "environmentRotationRadians", settings.environmentRotationRadians);
  settings.iblEnabled = value.value("iblEnabled", settings.iblEnabled);
  settings.skyboxVisible = value.value("skyboxVisible", settings.skyboxVisible);
  settings.syncSkySunToLight =
      value.value("syncSkySunToLight", settings.syncSkySunToLight);
  if (value.contains("iblBakeSettings") &&
      value["iblBakeSettings"].is_object()) {
    settings.iblBakeSettings =
        iblBakeSettingsFromJson(value["iblBakeSettings"]);
  }

  settings.modelPosition = vec3FromJson(
      value.value("modelPosition", json::array()), settings.modelPosition);
  settings.modelRotationDegrees =
      vec3FromJson(value.value("modelRotationDegrees", json::array()),
                   settings.modelRotationDegrees);
  settings.modelScale = vec3FromJson(value.value("modelScale", json::array()),
                                     settings.modelScale);
  settings.cameraPosition = vec3FromJson(
      value.value("cameraPosition", json::array()), settings.cameraPosition);
  settings.cameraYawRadians =
      value.value("cameraYawRadians", settings.cameraYawRadians);
  settings.cameraPitchRadians =
      value.value("cameraPitchRadians", settings.cameraPitchRadians);
  settings.cameraMoveSpeed =
      value.value("cameraMoveSpeed", settings.cameraMoveSpeed);
  settings.cameraLookSensitivity =
      value.value("cameraLookSensitivity", settings.cameraLookSensitivity);
  settings.cameraFarPlane =
      value.value("cameraFarPlane", settings.cameraFarPlane);
  settings.cameraLookActive = false;
  settings.cameraLastCursorX = 0.0;
  settings.cameraLastCursorY = 0.0;
  return settings;
}

inline bool saveDebugSession(const std::filesystem::path &path,
                             const DefaultDebugUISettings &settings) {
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
  }

  std::ofstream output(path);
  if (!output.is_open()) {
    return false;
  }

  output << settingsToJson(settings).dump(2);
  return output.good();
}

inline bool loadDebugSession(const std::filesystem::path &path,
                             DefaultDebugUISettings &settings) {
  if (!std::filesystem::exists(path)) {
    return false;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    return false;
  }

  json parsed;
  input >> parsed;
  settings = settingsFromJson(parsed);
  return true;
}

} // namespace DebugSessionIO
