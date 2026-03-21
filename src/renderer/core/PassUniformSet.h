#pragma once

#include "renderer/resources/RenderUtils.h"
#include <cstring>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

template <typename TUniform> class PassUniformSet {
public:
  void initialize(DeviceContext &deviceContext,
                  const vk::raii::DescriptorSetLayout &descriptorSetLayout,
                  uint32_t framesInFlight, uint32_t binding = 0) {
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBuffersMapped.clear();
    descriptorSets.clear();
    frameCount = framesInFlight;

    for (uint32_t i = 0; i < frameCount; ++i) {
      vk::raii::Buffer buffer = nullptr;
      vk::raii::DeviceMemory bufferMemory = nullptr;
      RenderUtils::createBuffer(deviceContext, sizeof(TUniform),
                                vk::BufferUsageFlagBits::eUniformBuffer,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent,
                                buffer, bufferMemory);
      uniformBuffers.emplace_back(std::move(buffer));
      uniformBuffersMemory.emplace_back(std::move(bufferMemory));
      uniformBuffersMapped.emplace_back(
          uniformBuffersMemory.back().mapMemory(0, sizeof(TUniform)));
    }

    createDescriptorPool(deviceContext);
    allocateDescriptorSets(deviceContext, descriptorSetLayout);
    writeDescriptorSets(deviceContext, binding);
  }

  void write(uint32_t frameIndex, const TUniform &value) {
    std::memcpy(uniformBuffersMapped.at(frameIndex), &value, sizeof(TUniform));
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
  std::vector<vk::raii::Buffer> uniformBuffers;
  std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
  std::vector<void *> uniformBuffersMapped;
  std::vector<vk::raii::DescriptorSet> descriptorSets;
  uint32_t frameCount = 0;

  void createDescriptorPool(DeviceContext &deviceContext) {
    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eUniformBuffer,
                                    frameCount};
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

  void writeDescriptorSets(DeviceContext &deviceContext, uint32_t binding) {
    for (uint32_t i = 0; i < frameCount; ++i) {
      vk::DescriptorBufferInfo bufferInfo{.buffer = uniformBuffers[i],
                                          .offset = 0,
                                          .range = sizeof(TUniform)};
      vk::WriteDescriptorSet descriptorWrite{
          .dstSet = descriptorSets[i],
          .dstBinding = binding,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo = &bufferInfo};
      deviceContext.deviceHandle().updateDescriptorSets({descriptorWrite}, {});
    }
  }
};
