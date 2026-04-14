#pragma once

#include "../backend/DeviceContext.h"
#include "../backend/SwapchainContext.h"
#include "resources/DescriptorBindings.h"
#include "resources/InstanceBuffer.h"
#include "resources/Mesh.h"
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class RenderPass;

struct RenderItem {
  Mesh *mesh = nullptr;
  FrameDescriptorBindings *descriptorBindings = nullptr;
  FrameDescriptorBindings *secondaryDescriptorBindings = nullptr;
  const RenderPass *targetPass = nullptr;
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  std::shared_ptr<FrameInstanceBuffer> instanceBuffer;
  uint32_t instanceCount = 1;
  glm::mat4 modelMatrix{1.0f};
  glm::mat4 modelNormalMatrix{1.0f};
  int boneWeightJointIndex = -1;
  int boneWeightDebugEnabled = 0;
  int skinningEnabled = 0;
};

struct RenderPassContext {
  vk::raii::CommandBuffer &commandBuffer;
  SwapchainContext &swapchainContext;
  uint32_t frameIndex = 0;
  uint32_t imageIndex = 0;
};

class RenderPass {
public:
  virtual ~RenderPass() = default;

  virtual void initialize(DeviceContext &deviceContext,
                          SwapchainContext &swapchainContext) = 0;
  virtual void recreate(DeviceContext &deviceContext,
                        SwapchainContext &swapchainContext) = 0;
  virtual void record(const RenderPassContext &context,
                      const std::vector<RenderItem> &renderItems) = 0;
  virtual vk::raii::DescriptorSetLayout *descriptorSetLayout() {
    return nullptr;
  }
  virtual vk::raii::DescriptorSetLayout *descriptorSetLayout(uint32_t setIndex) {
    return setIndex == 0 ? descriptorSetLayout() : nullptr;
  }
};
