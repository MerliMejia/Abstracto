#pragma once

#include "RenderUtils.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

struct RenderInstanceData {
  glm::vec4 model0{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec4 model1{0.0f, 1.0f, 0.0f, 0.0f};
  glm::vec4 model2{0.0f, 0.0f, 1.0f, 0.0f};
  glm::vec4 model3{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 modelNormal0{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec4 modelNormal1{0.0f, 1.0f, 0.0f, 0.0f};
  glm::vec4 modelNormal2{0.0f, 0.0f, 1.0f, 0.0f};
  glm::vec4 modelNormal3{0.0f, 0.0f, 0.0f, 1.0f};

  static vk::VertexInputBindingDescription
  getBindingDescription(uint32_t binding = 1) {
    return {binding, sizeof(RenderInstanceData), vk::VertexInputRate::eInstance};
  }

  static std::array<vk::VertexInputAttributeDescription, 8>
  getAttributeDescriptions(uint32_t binding = 1,
                           uint32_t firstLocation = 7) {
    return {
        vk::VertexInputAttributeDescription(
            firstLocation + 0, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, model0)),
        vk::VertexInputAttributeDescription(
            firstLocation + 1, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, model1)),
        vk::VertexInputAttributeDescription(
            firstLocation + 2, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, model2)),
        vk::VertexInputAttributeDescription(
            firstLocation + 3, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, model3)),
        vk::VertexInputAttributeDescription(
            firstLocation + 4, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, modelNormal0)),
        vk::VertexInputAttributeDescription(
            firstLocation + 5, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, modelNormal1)),
        vk::VertexInputAttributeDescription(
            firstLocation + 6, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, modelNormal2)),
        vk::VertexInputAttributeDescription(
            firstLocation + 7, binding, vk::Format::eR32G32B32A32Sfloat,
            offsetof(RenderInstanceData, modelNormal3)),
    };
  }
};

class FrameInstanceBuffer {
public:
  ~FrameInstanceBuffer() { releaseBuffers(); }

  void write(DeviceContext &deviceContext, uint32_t framesInFlight,
             const std::vector<RenderInstanceData> &instances) {
    if (instances.empty()) {
      throw std::runtime_error("cannot upload an empty instance buffer");
    }

    ensureCapacity(deviceContext, framesInFlight, instances.size());

    const size_t bufferSize = sizeof(RenderInstanceData) * instances.size();
    for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
      std::memcpy(mappedBuffers[frameIndex], instances.data(), bufferSize);
    }
    instanceCountValue = static_cast<uint32_t>(instances.size());
  }

  vk::raii::Buffer &buffer(uint32_t frameIndex) {
    return buffers.at(frameIndex);
  }

  const vk::raii::Buffer &buffer(uint32_t frameIndex) const {
    return buffers.at(frameIndex);
  }

  uint32_t instanceCount() const { return instanceCountValue; }

private:
  std::vector<vk::raii::Buffer> buffers;
  std::vector<vk::raii::DeviceMemory> bufferMemory;
  std::vector<void *> mappedBuffers;
  uint32_t frameCount = 0;
  size_t capacity = 0;
  uint32_t instanceCountValue = 0;

  void ensureCapacity(DeviceContext &deviceContext, uint32_t framesInFlight,
                      size_t requiredInstanceCount) {
    if (framesInFlight == 0) {
      throw std::runtime_error("instance buffers require frames in flight");
    }
    if (requiredInstanceCount == 0) {
      throw std::runtime_error("instance buffers require at least one instance");
    }
    if (frameCount == framesInFlight && capacity >= requiredInstanceCount) {
      return;
    }

    releaseBuffers();
    frameCount = framesInFlight;
    capacity = requiredInstanceCount;
    buffers.reserve(frameCount);
    bufferMemory.reserve(frameCount);
    mappedBuffers.reserve(frameCount);

    const vk::DeviceSize bufferSize =
        sizeof(RenderInstanceData) * requiredInstanceCount;
    for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
      vk::raii::Buffer buffer = nullptr;
      vk::raii::DeviceMemory memory = nullptr;
      RenderUtils::createBuffer(deviceContext, bufferSize,
                                vk::BufferUsageFlagBits::eVertexBuffer,
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent,
                                buffer, memory);
      mappedBuffers.push_back(memory.mapMemory(0, bufferSize));
      buffers.emplace_back(std::move(buffer));
      bufferMemory.emplace_back(std::move(memory));
    }
  }

  void releaseBuffers() {
    for (size_t index = 0; index < bufferMemory.size() &&
                           index < mappedBuffers.size();
         ++index) {
      if (bufferMemory[index] != nullptr && mappedBuffers[index] != nullptr) {
        bufferMemory[index].unmapMemory();
      }
    }
    buffers.clear();
    bufferMemory.clear();
    mappedBuffers.clear();
    frameCount = 0;
    capacity = 0;
    instanceCountValue = 0;
  }
};
