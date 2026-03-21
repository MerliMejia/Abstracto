#pragma once

#include "SampledImageResource.h"
#include "../backend/DeviceContext.h"
#include <stdexcept>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct PassImageBinding {
  uint32_t binding = 0;
  SampledImageResource resource;
};

class PassImageSet {
public:
  void initialize(DeviceContext &deviceContext,
                  const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                  uint32_t framesInFlight,
                  std::vector<PassImageBinding> bindings) {
    frameCount = framesInFlight;
    imageBindings = std::move(bindings);
    validateBindings();
    createDescriptorPool(deviceContext);
    allocateDescriptorSets(deviceContext, descriptorSetLayout);
    writeDescriptorSets(deviceContext);
  }

  void update(DeviceContext &deviceContext,
              std::vector<PassImageBinding> bindings) {
    imageBindings = std::move(bindings);
    validateBindings();
    if (descriptorSets.empty()) {
      throw std::runtime_error(
          "PassImageSet must be initialized before update");
    }
    writeDescriptorSets(deviceContext);
  }

  void bind(vk::raii::CommandBuffer &commandBuffer,
            const vk::raii::PipelineLayout &pipelineLayout,
            uint32_t frameIndex, uint32_t setIndex = 0) {
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     pipelineLayout, setIndex,
                                     *descriptorSets.at(frameIndex), nullptr);
  }

  vk::raii::DescriptorSet &descriptorSet(uint32_t frameIndex) {
    return descriptorSets.at(frameIndex);
  }

  const vk::raii::DescriptorSet &descriptorSet(uint32_t frameIndex) const {
    return descriptorSets.at(frameIndex);
  }

  uint32_t framesInFlight() const { return frameCount; }

private:
  vk::raii::DescriptorPool descriptorPool = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;
  std::vector<PassImageBinding> imageBindings;
  uint32_t frameCount = 0;

  void validateBindings() const {
    if (imageBindings.empty()) {
      throw std::runtime_error(
          "PassImageSet requires at least one image binding");
    }
  }

  void createDescriptorPool(DeviceContext &deviceContext) {
    vk::DescriptorPoolSize poolSize{
        vk::DescriptorType::eCombinedImageSampler,
        static_cast<uint32_t>(imageBindings.size()) * frameCount};
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = frameCount,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize};
    descriptorPool =
        vk::raii::DescriptorPool(deviceContext.deviceHandle(), poolInfo);
  }

  void allocateDescriptorSets(
      DeviceContext &deviceContext,
      const vk::raii::DescriptorSetLayout &descriptorSetLayout) {
    std::vector<vk::DescriptorSetLayout> layouts(frameCount, descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()};
    descriptorSets =
        deviceContext.deviceHandle().allocateDescriptorSets(allocInfo);
  }

  void writeDescriptorSets(DeviceContext &deviceContext) {
    for (uint32_t i = 0; i < frameCount; ++i) {
      std::vector<vk::DescriptorImageInfo> imageInfos;
      std::vector<vk::WriteDescriptorSet> descriptorWrites;
      imageInfos.reserve(imageBindings.size());
      descriptorWrites.reserve(imageBindings.size());

      for (const auto &binding : imageBindings) {
        imageInfos.push_back(vk::DescriptorImageInfo{
            .sampler = binding.resource.sampler,
            .imageView = binding.resource.imageView,
            .imageLayout = binding.resource.imageLayout});
        descriptorWrites.push_back(vk::WriteDescriptorSet{
            .dstSet = descriptorSets[i],
            .dstBinding = binding.binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &imageInfos.back()});
      }

      deviceContext.deviceHandle().updateDescriptorSets(descriptorWrites, {});
    }
  }
};
