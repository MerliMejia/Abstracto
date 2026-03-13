#pragma once

#include <algorithm>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "../backend/DeviceContext.h"

class Sampler {
public:
  struct Config {
    vk::Filter magFilter = vk::Filter::eLinear;
    vk::Filter minFilter = vk::Filter::eLinear;
    vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear;
    vk::SamplerAddressMode addressModeU = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeV = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeW = vk::SamplerAddressMode::eRepeat;
    bool anisotropyEnable = true;
    float maxAnisotropy = 0.0f;
    float minLod = 0.0f;
    float maxLod = VK_LOD_CLAMP_NONE;
  };

  void create(DeviceContext &deviceContext) {
    create(deviceContext, Config{});
  }

  void create(DeviceContext &deviceContext, const Config &config) {
    vk::PhysicalDeviceProperties properties =
        deviceContext.physicalDeviceHandle().getProperties();
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = config.magFilter,
        .minFilter = config.minFilter,
        .mipmapMode = config.mipmapMode,
        .addressModeU = config.addressModeU,
        .addressModeV = config.addressModeV,
        .addressModeW = config.addressModeW,
        .mipLodBias = 0.0f,
        .anisotropyEnable = config.anisotropyEnable ? vk::True : vk::False,
        .maxAnisotropy =
            config.anisotropyEnable
                ? (config.maxAnisotropy > 0.0f
                       ? std::min(config.maxAnisotropy,
                                  properties.limits.maxSamplerAnisotropy)
                       : properties.limits.maxSamplerAnisotropy)
                : 1.0f,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways,
        .minLod = config.minLod,
        .maxLod = config.maxLod};
    sampler = vk::raii::Sampler(deviceContext.deviceHandle(), samplerInfo);
  }

  vk::raii::Sampler &handle() { return sampler; }
  const vk::raii::Sampler &handle() const { return sampler; }

private:
  vk::raii::Sampler sampler = nullptr;
};
