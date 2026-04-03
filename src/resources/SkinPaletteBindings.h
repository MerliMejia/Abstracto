#pragma once

#include "core/PassUniformSet.h"
#include "resources/DescriptorBindings.h"
#include <array>

constexpr uint32_t MAX_SKIN_JOINTS = 128;

struct SkinPaletteUniformData {
  std::array<glm::mat4, MAX_SKIN_JOINTS> joints{};
};

class SkinPaletteBindings : public FrameDescriptorBindings {
public:
  void create(DeviceContext &deviceContext,
              const vk::raii::DescriptorSetLayout &descriptorSetLayout,
              uint32_t framesInFlight) {
    uniformSet.initialize(deviceContext, descriptorSetLayout, framesInFlight);
    SkinPaletteUniformData identityPalette{};
    for (auto &joint : identityPalette.joints) {
      joint = glm::mat4(1.0f);
    }
    for (uint32_t frameIndex = 0; frameIndex < framesInFlight; ++frameIndex) {
      uniformSet.write(frameIndex, identityPalette);
    }
  }

  void write(uint32_t frameIndex, const SkinPaletteUniformData &palette) {
    uniformSet.write(frameIndex, palette);
  }

  vk::raii::DescriptorSet &descriptorSet(uint32_t frameIndex) override {
    return uniformSet.descriptorSet(frameIndex);
  }

private:
  PassUniformSet<SkinPaletteUniformData> uniformSet;
};
