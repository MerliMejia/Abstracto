#pragma once

#include "resources/CubemapTexture.h"
#include "resources/FloatTexture2D.h"
#include "ImageBasedLightingBaker.h"
#include "ImageBasedLightingTypes.h"
#include "resources/Sampler.h"
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

class ImageBasedLighting {
public:
  void create(DeviceContext &deviceContext, CommandContext &commandContext,
              const ImageBasedLightingBakeSettings &settings) {
    bakeSettings = settings;

    const BakedImageBasedLightingData bakedData =
        ImageBasedLightingBaker::bakeOrLoadCache(settings);

    environmentMap.create(deviceContext, commandContext, bakedData.environment);
    irradianceMap.create(deviceContext, commandContext, bakedData.irradiance);
    prefilteredMap.create(deviceContext, commandContext, bakedData.prefiltered);
    brdfLut.create(deviceContext, commandContext, settings.brdfResolution,
                   settings.brdfResolution, bakedData.brdfLut);
    createSamplers(deviceContext);
  }

  void createFallback(DeviceContext &deviceContext,
                      CommandContext &commandContext,
                      const ImageBasedLightingBakeSettings &settings) {
    bakeSettings = settings;

    const std::vector<CubemapMipLevelData> blackCubemap =
        solidCubemap({0.0f, 0.0f, 0.0f, 1.0f});
    environmentMap.create(deviceContext, commandContext, blackCubemap);
    irradianceMap.create(deviceContext, commandContext, blackCubemap);
    prefilteredMap.create(deviceContext, commandContext, blackCubemap);
    brdfLut.create(deviceContext, commandContext, 1, 1,
                   std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f});
    createSamplers(deviceContext);
  }

  void rebuild(DeviceContext &deviceContext, CommandContext &commandContext,
               const ImageBasedLightingBakeSettings &settings) {
    create(deviceContext, commandContext, settings);
  }

  float maxPrefilterMipLevel() const {
    return static_cast<float>(std::max(prefilteredMap.mipLevelCount(), 1u) -
                              1u);
  }

  SampledImageResource environmentResource() const {
    return environmentMap.sampledResource(environmentSampler.handle());
  }

  SampledImageResource irradianceResource() const {
    return irradianceMap.sampledResource(irradianceSampler.handle());
  }

  SampledImageResource prefilteredResource() const {
    return prefilteredMap.sampledResource(prefilteredSampler.handle());
  }

  SampledImageResource brdfResource() const {
    return brdfLut.sampledResource(brdfSampler.handle());
  }

  const ImageBasedLightingBakeSettings &settings() const {
    return bakeSettings;
  }

private:
  static std::vector<CubemapMipLevelData>
  solidCubemap(const std::array<float, 4> &rgba) {
    CubemapMipLevelData mip{.width = 1, .height = 1};
    for (auto &face : mip.faces) {
      face.assign(rgba.begin(), rgba.end());
    }
    return {std::move(mip)};
  }

  void createSamplers(DeviceContext &deviceContext) {
    const Sampler::Config cubeSamplerConfig{
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
    };
    environmentSampler.create(deviceContext, cubeSamplerConfig);
    irradianceSampler.create(deviceContext, cubeSamplerConfig);
    prefilteredSampler.create(
        deviceContext,
        Sampler::Config{
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod = static_cast<float>(
                std::max(prefilteredMap.mipLevelCount(), 1u) - 1u),
        });
    brdfSampler.create(deviceContext,
                       Sampler::Config{
                           .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                           .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                           .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                           .anisotropyEnable = false,
                           .maxLod = 0.0f,
                       });
  }

  CubemapTexture environmentMap;
  CubemapTexture irradianceMap;
  CubemapTexture prefilteredMap;
  FloatTexture2D brdfLut;

  Sampler environmentSampler;
  Sampler irradianceSampler;
  Sampler prefilteredSampler;
  Sampler brdfSampler;

  ImageBasedLightingBakeSettings bakeSettings;
};
