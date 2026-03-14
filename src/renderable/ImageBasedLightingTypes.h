#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

struct ProceduralSkySettings {
  glm::vec3 zenithColor{0.16f, 0.32f, 0.72f};
  glm::vec3 horizonColor{0.85f, 0.72f, 0.58f};
  glm::vec3 groundColor{0.10f, 0.09f, 0.08f};
  glm::vec3 sunColor{1.0f, 0.95f, 0.82f};
  float sunIntensity = 24.0f;
  float sunAngularRadius = 0.045f;
  float sunGlow = 2.5f;
  float horizonGlow = 0.28f;
  float skyExponent = 0.45f;
  float groundFalloff = 1.6f;
  float sunAzimuthRadians = glm::radians(35.0f);
  float sunElevationRadians = glm::radians(32.0f);
};

struct ImageBasedLightingBakeSettings {
  uint32_t environmentResolution = 1024;
  uint32_t irradianceResolution = 32;
  uint32_t prefilteredResolution = 256;
  uint32_t brdfResolution = 256;
  uint32_t irradianceSamples = 1024;
  uint32_t prefilteredSamples = 64;
  uint32_t brdfSamples = 128;
  std::string environmentHdrPath;
  ProceduralSkySettings sky;
};

struct CubemapMipLevelData {
  uint32_t width = 0;
  uint32_t height = 0;
  std::array<std::vector<float>, 6> faces;
};

struct BakedImageBasedLightingData {
  std::vector<CubemapMipLevelData> environment;
  std::vector<CubemapMipLevelData> irradiance;
  std::vector<CubemapMipLevelData> prefiltered;
  std::vector<float> brdfLut;
};
