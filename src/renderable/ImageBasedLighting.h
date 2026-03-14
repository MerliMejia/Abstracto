#pragma once

#include "CubemapTexture.h"
#include "FloatTexture2D.h"
#include "ImageBasedLightingBaker.h"
#include "ImageBasedLightingTypes.h"
#include "Sampler.h"
#include <algorithm>

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
