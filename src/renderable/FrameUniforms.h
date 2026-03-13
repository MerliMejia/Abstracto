#pragma once

#include "RenderUtils.h"
#include <cstring>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 modelNormal;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

class FrameUniforms {
public:
  void create(DeviceContext &deviceContext, uint32_t framesInFlight) {
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBuffersMapped.clear();

    for (uint32_t i = 0; i < framesInFlight; i++) {
      vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
      vk::raii::Buffer buffer({});
      vk::raii::DeviceMemory bufferMemory({});
      RenderUtils::createBuffer(deviceContext, bufferSize,
                                vk::BufferUsageFlagBits::eUniformBuffer,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent,
                                buffer, bufferMemory);
      uniformBuffers.emplace_back(std::move(buffer));
      uniformBuffersMemory.emplace_back(std::move(bufferMemory));
      uniformBuffersMapped.emplace_back(
          uniformBuffersMemory.back().mapMemory(0, bufferSize));
    }
  }

  void write(uint32_t frameIndex, const UniformBufferObject &ubo) {
    std::memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
  }

  vk::raii::Buffer &buffer(uint32_t frameIndex) {
    return uniformBuffers[frameIndex];
  }

  const vk::raii::Buffer &buffer(uint32_t frameIndex) const {
    return uniformBuffers[frameIndex];
  }

private:
  std::vector<vk::raii::Buffer> uniformBuffers;
  std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
  std::vector<void *> uniformBuffersMapped;
};
